/*
 * VMX (NoVmp, ledgers U1300-U1399; design and requirement mapping: docs/vmx.md).
 *
 * Intel SDM 325462-092 Vol3C chapters 26-33 (VMX) and Vol3D Appendices A (capability reporting),
 * B (VMCS field encodings) and C (basic exit reasons), compared with SDM 093 where change document
 * 252046-085 lists changes. Opt-in UC_X86_FEATURE_VMX (default off).
 *
 * U1301: the capability set of the model (docs/vmx.md "Capability set", our choice) and the VMX
 * capability MSRs 480H-492H (Appendix A; Vol4 Table 2-2 pp. 2-46 to 2-48); the VMCS field table
 * (Appendix B) that IA32_VMX_VMCS_ENUM is computed from.
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U1301): whole file */
#include "cpu.h"
#include "exec/exec-all.h"
#include "uc_priv.h"
#include <unicorn/unicorn.h>

/*
 * ---- the capability set (docs/vmx.md, our choice) ----
 * Each control register is described by its allowed 0-settings (bits that must be 1) and its
 * allowed 1-settings (bits that may be 1), Vol3D A.3-A.5 pp. A-3 to A-6.
 */
#define VMX_REVISION_ID         1u                          /* A.1: bits 30:0, our choice */
#define VMX_REGION_SIZE         4096u                       /* A.1: bits 44:32 */

#define VMX_PIN_DEFAULT1        0x00000016u                 /* A.3.1: bits 1, 2, 4 */
#define VMX_PIN_ALLOWED1        0x000000ffu
#define VMX_PIN_TRUE0           0x00000016u

#define VMX_PROC_DEFAULT1       0x0401e172u                 /* A.3.2: 1, 4-6, 8, 13-16, 26 */
#define VMX_PROC_ALLOWED1       0xfffbfffeu                 /* all but 0 and 18 */
#define VMX_PROC_TRUE0          0x04006172u                 /* CR3-load / -store exiting free */

#define VMX_PROC2_ALLOWED1      0x0e517bffu                 /* A.3.3, docs/vmx.md */

#define VMX_EXIT_DEFAULT1       0x00036dffu                 /* A.4.1: 0-8, 10, 11, 13, 14, 16, 17 */
#define VMX_EXIT_ALLOWED1       0x007fffffu                 /* + 9, 15, 18-22 */
#define VMX_EXIT_TRUE0          0x00036dfbu                 /* save debug controls free */

#define VMX_ENTRY_DEFAULT1      0x000011ffu                 /* A.5: 0-8, 12 */
#define VMX_ENTRY_ALLOWED1      0x0000d3ffu                 /* + 9, 14, 15 */
#define VMX_ENTRY_TRUE0         0x000011fbu                 /* load debug controls free */

/*
 * A.6: bits 4:0 preemption-timer rate 5, bit 5 (store LMA), bit 6 (HLT), bits 24:16 = 4 CR3-target
 * values, bits 27:25 = 0 (512 MSRs), bit 29 (VMWRITE to any field), bit 30 (zero-length injection)
 */
#define VMX_MISC_VALUE          0x60040065ull
#define VMX_CR0_FIXED0          0x80000021ull               /* A.7: PE, NE, PG */
#define VMX_CR0_FIXED1          0xffffffffull
#define VMX_CR4_FIXED0          0x00002000ull               /* A.8: VMXE */
/* A.10: 0, 6, 7, 8, 14, 16, 17, 20, 21, 22, 25, 26, 32, 40-43 */
#define VMX_EPT_VPID_CAP        0x00000f01067341c1ull
#define VMX_VMFUNC_ALLOWED1     0x1ull                      /* A.11: EPTP switching */

/*
 * ---- VMCS fields (Vol3D Appendix B, pp. B-1 to B-13) ----
 * Every architecturally defined field, with the condition under which it exists (the footnotes of
 * Tables B-1 to B-15: "This field exists only on processors that support the 1-setting of ...").
 * A field exists if c1 (or c2) names a control whose 1-setting the capability set allows, or if
 * c1 is VMXF_ALWAYS. The table index is also the field's slot in the VMCS region (vmx_slot).
 */
enum {
    VMXF_ALWAYS = 0,
    VMXF_NEVER,                 /* condition outside the model (FRED until the FRED-VMX stage) */
    VMXF_PIN,                   /* bit b of the pin-based controls */
    VMXF_PROC,                  /* primary processor-based */
    VMXF_PROC2,                 /* secondary processor-based */
    VMXF_PROC3,                 /* tertiary processor-based */
    VMXF_EXIT,                  /* primary VM-exit */
    VMXF_EXIT2,                 /* secondary VM-exit */
    VMXF_ENTRY,                 /* VM-entry */
    VMXF_VMFUNC,                /* VM-function controls */
};

typedef struct VmxFieldDef {
    uint16_t enc;               /* encoding of the full access type (bit 0 = 0) */
    uint8_t c1, b1, c2, b2;     /* existence: (c1, bit b1) or (c2, bit b2) */
} VmxFieldDef;

#define F(e)              { e, VMXF_ALWAYS, 0, VMXF_NEVER, 0 }
#define FC(e, c, b)       { e, c, b, VMXF_NEVER, 0 }
#define FC2(e, c, b, d, x) { e, c, b, d, x }

static const VmxFieldDef vmx_fields[] = {
    /* B.1.1 16-bit control fields (Table B-1) */
    FC(0x0000, VMXF_PROC2, 5),          /* VPID */
    FC(0x0002, VMXF_PIN, 7),            /* posted-interrupt notification vector */
    FC(0x0004, VMXF_PROC2, 18),         /* EPTP index */
    FC(0x0006, VMXF_PROC3, 1),          /* HLAT prefix size */
    FC(0x0008, VMXF_PROC3, 4),          /* last PID-pointer index */
    FC(0x000a, VMXF_PROC3, 8),          /* virtual timer vector */
    /* B.1.2 16-bit guest-state fields (Table B-2) */
    F(0x0800), F(0x0802), F(0x0804), F(0x0806), F(0x0808), F(0x080a), F(0x080c), F(0x080e),
    FC(0x0810, VMXF_PROC2, 9),          /* guest interrupt status */
    FC(0x0812, VMXF_PROC2, 17),         /* PML index */
    FC2(0x0814, VMXF_EXIT, 27, VMXF_ENTRY, 19),     /* guest UINV */
    /* B.1.3 16-bit host-state fields (Table B-3) */
    F(0x0c00), F(0x0c02), F(0x0c04), F(0x0c06), F(0x0c08), F(0x0c0a), F(0x0c0c),
    /* B.2.1 64-bit control fields (Table B-4) */
    F(0x2000), F(0x2002),               /* I/O bitmaps A, B */
    FC(0x2004, VMXF_PROC, 28),          /* MSR bitmaps */
    F(0x2006), F(0x2008), F(0x200a),    /* VM-exit MSR-store / -load, VM-entry MSR-load */
    F(0x200c),                          /* executive-VMCS pointer */
    FC(0x200e, VMXF_PROC2, 17),         /* PML address */
    F(0x2010),                          /* TSC offset */
    FC(0x2012, VMXF_PROC, 21),          /* virtual-APIC address */
    FC(0x2014, VMXF_PROC2, 0),          /* APIC-access address */
    FC(0x2016, VMXF_PIN, 7),            /* posted-interrupt descriptor address */
    FC(0x2018, VMXF_PROC2, 13),         /* VM-function controls */
    FC(0x201a, VMXF_PROC2, 1),          /* EPTP */
    FC(0x201c, VMXF_PROC2, 9), FC(0x201e, VMXF_PROC2, 9),          /* EOI-exit bitmaps 0-3 */
    FC(0x2020, VMXF_PROC2, 9), FC(0x2022, VMXF_PROC2, 9),
    FC(0x2024, VMXF_VMFUNC, 0),         /* EPTP-list address */
    FC(0x2026, VMXF_PROC2, 14),         /* VMREAD-bitmap address */
    FC(0x2028, VMXF_PROC2, 14),         /* VMWRITE-bitmap address */
    FC(0x202a, VMXF_PROC2, 18),         /* virtualization-exception information address */
    FC(0x202c, VMXF_PROC2, 20),         /* XSS-exiting bitmap */
    FC(0x202e, VMXF_PROC2, 15),         /* ENCLS-exiting bitmap */
    FC(0x2030, VMXF_PROC2, 23),         /* sub-page-permission-table pointer */
    FC(0x2032, VMXF_PROC2, 25),         /* TSC multiplier */
    FC(0x2034, VMXF_PROC, 17),          /* tertiary processor-based controls */
    FC(0x2038, VMXF_PROC2, 21),         /* low PASID directory address */
    FC(0x203a, VMXF_PROC2, 21),         /* high PASID directory address */
    FC(0x203c, VMXF_PROC3, 5),          /* SEAM shared EPT pointer */
    FC(0x203e, VMXF_PROC2, 27),         /* PCONFIG-exiting bitmap */
    FC(0x2040, VMXF_PROC3, 1),          /* HLATP */
    FC(0x2042, VMXF_PROC3, 4),          /* PID-pointer table address */
    FC(0x2044, VMXF_EXIT, 31),          /* secondary VM-exit controls */
    FC(0x204a, VMXF_PROC3, 7),          /* IA32_SPEC_CTRL mask */
    FC(0x204c, VMXF_PROC3, 7),          /* IA32_SPEC_CTRL shadow */
    FC(0x204e, VMXF_PROC3, 8),          /* guest deadline shadow */
    FC(0x2052, VMXF_NEVER, 0),          /* injected-event data (FRED) */
    /* B.2.2 64-bit read-only data fields (Table B-5) */
    FC(0x2400, VMXF_PROC2, 1),          /* guest-physical address */
    FC(0x2402, VMXF_PROC3, 6),          /* MSR data */
    FC(0x2404, VMXF_NEVER, 0),          /* original-event data (FRED) */
    /* B.2.3 64-bit guest-state fields (Table B-6) */
    F(0x2800),                          /* VMCS link pointer */
    F(0x2802),                          /* IA32_DEBUGCTL */
    FC2(0x2804, VMXF_ENTRY, 14, VMXF_EXIT, 18),     /* IA32_PAT */
    FC2(0x2806, VMXF_ENTRY, 15, VMXF_EXIT, 20),     /* IA32_EFER */
    FC(0x2808, VMXF_ENTRY, 13),         /* IA32_PERF_GLOBAL_CTRL */
    FC(0x280a, VMXF_PROC2, 1), FC(0x280c, VMXF_PROC2, 1),          /* PDPTE0-3 */
    FC(0x280e, VMXF_PROC2, 1), FC(0x2810, VMXF_PROC2, 1),
    FC2(0x2812, VMXF_ENTRY, 16, VMXF_EXIT, 23),     /* IA32_BNDCFGS */
    FC2(0x2814, VMXF_ENTRY, 18, VMXF_EXIT, 25),     /* IA32_RTIT_CTL */
    FC2(0x2816, VMXF_ENTRY, 21, VMXF_EXIT, 26),     /* IA32_LBR_CTL */
    FC(0x2818, VMXF_ENTRY, 22),         /* IA32_PKRS */
    FC2(0x281a, VMXF_ENTRY, 23, VMXF_EXIT2, 0),     /* IA32_FRED_CONFIG */
    FC2(0x281c, VMXF_ENTRY, 23, VMXF_EXIT2, 0),     /* IA32_FRED_RSP1-3 */
    FC2(0x281e, VMXF_ENTRY, 23, VMXF_EXIT2, 0),
    FC2(0x2820, VMXF_ENTRY, 23, VMXF_EXIT2, 0),
    FC2(0x2822, VMXF_ENTRY, 23, VMXF_EXIT2, 0),     /* IA32_FRED_STKLVLS */
    FC2(0x2824, VMXF_ENTRY, 23, VMXF_EXIT2, 0),     /* IA32_FRED_SSP1-3 */
    FC2(0x2826, VMXF_ENTRY, 23, VMXF_EXIT2, 0),
    FC2(0x2828, VMXF_ENTRY, 23, VMXF_EXIT2, 0),
    FC(0x282e, VMXF_ENTRY, 24),         /* IA32_SPEC_CTRL */
    FC(0x2830, VMXF_PROC3, 8),          /* guest deadline */
    /* B.2.4 64-bit host-state fields (Table B-7) */
    FC(0x2c00, VMXF_EXIT, 19),          /* IA32_PAT */
    FC(0x2c02, VMXF_EXIT, 21),          /* IA32_EFER */
    FC(0x2c04, VMXF_EXIT, 12),          /* IA32_PERF_GLOBAL_CTRL */
    FC(0x2c06, VMXF_EXIT, 29),          /* IA32_PKRS */
    FC(0x2c08, VMXF_EXIT2, 1), FC(0x2c0a, VMXF_EXIT2, 1),          /* FRED_CONFIG, RSP1 */
    FC(0x2c0c, VMXF_EXIT2, 1), FC(0x2c0e, VMXF_EXIT2, 1),          /* RSP2, RSP3 */
    FC(0x2c10, VMXF_EXIT2, 1), FC(0x2c12, VMXF_EXIT2, 1),          /* STKLVLS, SSP1 */
    FC(0x2c14, VMXF_EXIT2, 1), FC(0x2c16, VMXF_EXIT2, 1),          /* SSP2, SSP3 */
    FC(0x2c1a, VMXF_EXIT2, 2),          /* IA32_SPEC_CTRL */
    /* B.3.1 32-bit control fields (Table B-8) */
    F(0x4000), F(0x4002), F(0x4004), F(0x4006), F(0x4008), F(0x400a), F(0x400c), F(0x400e),
    F(0x4010), F(0x4012), F(0x4014), F(0x4016), F(0x4018), F(0x401a),
    FC(0x401c, VMXF_PROC, 21),          /* TPR threshold */
    FC(0x401e, VMXF_PROC, 31),          /* secondary processor-based controls */
    FC(0x4020, VMXF_PROC2, 10),         /* PLE_Gap */
    FC(0x4022, VMXF_PROC2, 10),         /* PLE_Window */
    FC(0x4024, VMXF_PROC2, 31),         /* instruction-timeout control */
    FC(0x4026, VMXF_PROC3, 5),          /* SEAM-guest KeyID */
    /* B.3.2 32-bit read-only data fields (Table B-9) */
    F(0x4400), F(0x4402), F(0x4404), F(0x4406), F(0x4408), F(0x440a), F(0x440c), F(0x440e),
    /* B.3.3 32-bit guest-state fields (Table B-10) */
    F(0x4800), F(0x4802), F(0x4804), F(0x4806), F(0x4808), F(0x480a), F(0x480c), F(0x480e),
    F(0x4810), F(0x4812), F(0x4814), F(0x4816), F(0x4818), F(0x481a), F(0x481c), F(0x481e),
    F(0x4820), F(0x4822), F(0x4824), F(0x4826), F(0x4828), F(0x482a),
    FC(0x482e, VMXF_PIN, 6),            /* VMX-preemption timer value */
    /* B.3.4 32-bit host-state field (Table B-11) */
    F(0x4c00),
    /* B.4.1 natural-width control fields (Table B-12) */
    F(0x6000), F(0x6002), F(0x6004), F(0x6006), F(0x6008), F(0x600a), F(0x600c), F(0x600e),
    /* B.4.2 natural-width read-only data fields (Table B-13) */
    F(0x6400), F(0x6402), F(0x6404), F(0x6406), F(0x6408), F(0x640a),
    /* B.4.3 natural-width guest-state fields (Table B-14) */
    F(0x6800), F(0x6802), F(0x6804), F(0x6806), F(0x6808), F(0x680a), F(0x680c), F(0x680e),
    F(0x6810), F(0x6812), F(0x6814), F(0x6816), F(0x6818), F(0x681a), F(0x681c), F(0x681e),
    F(0x6820), F(0x6822), F(0x6824), F(0x6826),
    FC(0x6828, VMXF_ENTRY, 20), FC(0x682a, VMXF_ENTRY, 20), FC(0x682c, VMXF_ENTRY, 20),  /* CET */
    /* B.4.4 natural-width host-state fields (Table B-15) */
    F(0x6c00), F(0x6c02), F(0x6c04), F(0x6c06), F(0x6c08), F(0x6c0a), F(0x6c0c), F(0x6c0e),
    F(0x6c10), F(0x6c12), F(0x6c14), F(0x6c16),
    FC(0x6c18, VMXF_EXIT, 28), FC(0x6c1a, VMXF_EXIT, 28), FC(0x6c1c, VMXF_EXIT, 28),  /* CET */
};

#undef F
#undef FC
#undef FC2

/* the VMCS region holds the data slots from byte 16 (docs/vmx.md "VMCS storage") */
typedef char vmx_fields_fit[16 + 8 * (sizeof(vmx_fields) / sizeof(vmx_fields[0])) <=
                            VMX_REGION_SIZE ? 1 : -1];

/* a CPUID feature as the CPU reports it (the model, hidden by a strict UC_CTL_X86_CPUID profile) */
static bool vmx_feat(CPUX86State *env, FeatureWord w, uint32_t bit, uint32_t leaf, uint32_t sub,
                     int reg)
{
    return (env->features[w] & bit) && (x86_cpuid_profile_mask(env, leaf, sub, reg) & bit);
}

/*
 * The allowed 1-settings of the tertiary controls (A.3.4): LOADIWKEY exiting (0) with Key Locker,
 * enable MSR-list instructions (6) with RDMSRLIST / WRMSRLIST, enable PBNDKB (9) with PBNDKB - the
 * controls of instructions the CPU model has (docs/vmx.md, our choice)
 */
static uint64_t vmx_proc3_allowed1(CPUX86State *env)
{
    uint64_t v = 0;

    if (vmx_feat(env, FEAT_7_0_ECX, CPUID_7_0_ECX_KeyLocker, 7, 0, 2)) {
        v |= 1ull << 0;
    }
    if (vmx_feat(env, FEAT_7_1_EAX, CPUID_7_1_EAX_MSRLIST, 7, 1, 0)) {
        v |= 1ull << 6;
    }
    if (vmx_feat(env, FEAT_7_1_EBX, CPUID_7_1_EBX_PBNDKB, 7, 1, 1)) {
        v |= 1ull << 9;
    }
    return v;
}

/* the allowed 1-settings of a control register of the capability set */
static uint64_t x86_vmx_allowed1(CPUX86State *env, int reg)
{
    switch (reg) {
    case VMXF_PIN:
        return VMX_PIN_ALLOWED1;
    case VMXF_PROC:
        return VMX_PROC_ALLOWED1;
    case VMXF_PROC2:
        return VMX_PROC2_ALLOWED1;
    case VMXF_PROC3:
        return vmx_proc3_allowed1(env);
    case VMXF_EXIT:
        return VMX_EXIT_ALLOWED1;
    case VMXF_EXIT2:
        return 0;                       /* EXIT_CTLS[63] = 0: no secondary VM-exit controls */
    case VMXF_ENTRY:
        return VMX_ENTRY_ALLOWED1;
    case VMXF_VMFUNC:
        return VMX_VMFUNC_ALLOWED1;
    default:
        return 0;
    }
}

static bool vmx_cond(CPUX86State *env, int c, int b)
{
    if (c == VMXF_ALWAYS) {
        return true;
    }
    if (c == VMXF_NEVER) {
        return false;
    }
    return (x86_vmx_allowed1(env, c) >> b) & 1;
}

/* the table index of a full-access encoding, or -1 */
static int vmx_field_index(uint32_t enc)
{
    int i;

    for (i = 0; i < (int)(sizeof(vmx_fields) / sizeof(vmx_fields[0])); i++) {
        if (vmx_fields[i].enc == enc) {
            return i;
        }
    }
    return -1;
}

/*
 * The slot of a field in the VMCS region (byte offset), or -1 if the encoding names no field the
 * processor supports (Vol3C 27.11.2: bits 31:15 and 12 reserved; bit 0 (high access) only for
 * 64-bit fields, width 1). *high = 1 for the high access type of a 64-bit field.
 */
int x86_vmx_field_slot(CPUX86State *env, uint64_t enc, int *high)
{
    int i;

    if (enc >> 15 || (enc & 0x1000)) {
        return -1;
    }
    *high = enc & 1;
    if (*high && ((enc >> 13) & 3) != 1) {
        return -1;
    }
    i = vmx_field_index((uint32_t)enc & ~1u);
    if (i < 0 || !(vmx_cond(env, vmx_fields[i].c1, vmx_fields[i].b1) ||
                   vmx_cond(env, vmx_fields[i].c2, vmx_fields[i].b2))) {
        return -1;
    }
    return 16 + 8 * i;
}

/* A.9: bits 9:1 = the highest index value of any supported field */
static uint64_t vmx_vmcs_enum(CPUX86State *env)
{
    int i, hi;
    uint64_t idx = 0;

    for (i = 0; i < (int)(sizeof(vmx_fields) / sizeof(vmx_fields[0])); i++) {
        if (x86_vmx_field_slot(env, vmx_fields[i].enc, &hi) >= 0 &&
            ((vmx_fields[i].enc >> 1) & 0x1ff) > idx) {
            idx = (vmx_fields[i].enc >> 1) & 0x1ff;
        }
    }
    return idx << 1;
}

/*
 * Vol4 Table 2-2 pp. 2-46 to 2-48: which of 480H-493H exist ("If CPUID.01H:ECX[5] = 1", TRUE_*
 * with IA32_VMX_BASIC[55], 48BH with PROCBASED_CTLS[63], 48CH with PROCBASED_CTLS2[33] or [37],
 * 491H with PROCBASED_CTLS2[45], 492H with PROCBASED_CTLS[49], 493H with EXIT_CTLS[63])
 */
bool x86_vmx_msr_exists(CPUX86State *env, uint32_t msr)
{
    if (!x86_vmx_cpuid(env)) {
        return false;
    }
    if ((msr >= MSR_IA32_VMX_BASIC && msr <= MSR_IA32_VMX_VMCS_ENUM) ||
        (msr >= MSR_IA32_VMX_TRUE_PINBASED_CTLS && msr <= MSR_IA32_VMX_TRUE_ENTRY_CTLS)) {
        return true;                    /* TRUE_*: IA32_VMX_BASIC[55] = 1 */
    }
    switch (msr) {
    case MSR_IA32_VMX_PROCBASED_CTLS2:
        return (VMX_PROC_ALLOWED1 >> 31) & 1;
    case MSR_IA32_VMX_EPT_VPID_CAP:
        return ((VMX_PROC_ALLOWED1 >> 31) & 1) && (VMX_PROC2_ALLOWED1 & ((1u << 1) | (1u << 5)));
    case MSR_IA32_VMX_VMFUNC:
        return ((VMX_PROC_ALLOWED1 >> 31) & 1) && ((VMX_PROC2_ALLOWED1 >> 13) & 1);
    case MSR_IA32_VMX_PROCBASED_CTLS3_NV:
        return (VMX_PROC_ALLOWED1 >> 17) & 1;
    case MSR_IA32_VMX_EXIT_CTLS2_NV:
        return x86_vmx_allowed1(env, VMXF_EXIT) >> 31;
    default:
        return false;
    }
}

/* the value of a VMX capability MSR (Appendix A); false if it does not exist */
bool x86_vmx_msr_read(CPUX86State *env, uint32_t msr, uint64_t *val)
{
    if (!x86_vmx_msr_exists(env, msr)) {
        return false;
    }
    switch (msr) {
    case MSR_IA32_VMX_BASIC:
        /* A.1: revision, region size, WB (6), INS/OUTS information, TRUE_*, injection rule 56 */
        *val = VMX_REVISION_ID | ((uint64_t)VMX_REGION_SIZE << 32) | (6ull << 50) |
               (1ull << 54) | (1ull << 55) | (1ull << 56);
        break;
    case MSR_IA32_VMX_PINBASED_CTLS:
        *val = ((uint64_t)VMX_PIN_ALLOWED1 << 32) | VMX_PIN_DEFAULT1;
        break;
    case MSR_IA32_VMX_TRUE_PINBASED_CTLS:
        *val = ((uint64_t)VMX_PIN_ALLOWED1 << 32) | VMX_PIN_TRUE0;
        break;
    case MSR_IA32_VMX_PROCBASED_CTLS:
        *val = ((uint64_t)VMX_PROC_ALLOWED1 << 32) | VMX_PROC_DEFAULT1;
        break;
    case MSR_IA32_VMX_TRUE_PROCBASED_CTLS:
        *val = ((uint64_t)VMX_PROC_ALLOWED1 << 32) | VMX_PROC_TRUE0;
        break;
    case MSR_IA32_VMX_EXIT_CTLS:
        *val = ((uint64_t)VMX_EXIT_ALLOWED1 << 32) | VMX_EXIT_DEFAULT1;
        break;
    case MSR_IA32_VMX_TRUE_EXIT_CTLS:
        *val = ((uint64_t)VMX_EXIT_ALLOWED1 << 32) | VMX_EXIT_TRUE0;
        break;
    case MSR_IA32_VMX_ENTRY_CTLS:
        *val = ((uint64_t)VMX_ENTRY_ALLOWED1 << 32) | VMX_ENTRY_DEFAULT1;
        break;
    case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
        *val = ((uint64_t)VMX_ENTRY_ALLOWED1 << 32) | VMX_ENTRY_TRUE0;
        break;
    case MSR_IA32_VMX_MISC:
        *val = VMX_MISC_VALUE;
        break;
    case MSR_IA32_VMX_CR0_FIXED0:
        *val = VMX_CR0_FIXED0;
        break;
    case MSR_IA32_VMX_CR0_FIXED1:
        *val = VMX_CR0_FIXED1;
        break;
    case MSR_IA32_VMX_CR4_FIXED0:
        *val = VMX_CR4_FIXED0;
        break;
    case MSR_IA32_VMX_CR4_FIXED1:
        *val = ~cr4_reserved_bits(env);       /* every CR4 bit the CPU model supports (A.8) */
        break;
    case MSR_IA32_VMX_VMCS_ENUM:
        *val = vmx_vmcs_enum(env);
        break;
    case MSR_IA32_VMX_PROCBASED_CTLS2:
        *val = (uint64_t)VMX_PROC2_ALLOWED1 << 32;  /* A.3.3: allowed 0-settings always 0 */
        break;
    case MSR_IA32_VMX_EPT_VPID_CAP:
        *val = VMX_EPT_VPID_CAP;
        break;
    case MSR_IA32_VMX_VMFUNC:
        *val = VMX_VMFUNC_ALLOWED1;
        break;
    case MSR_IA32_VMX_PROCBASED_CTLS3_NV:
        *val = vmx_proc3_allowed1(env);
        break;
    default:
        *val = 0;                       /* EXIT_CTLS2: not reachable (does not exist) */
        break;
    }
    return true;
}

#endif /* __Use_Original_Qemu (U1301) */
