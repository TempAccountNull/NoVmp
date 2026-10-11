/*
 * VMX (NoVmp, ledgers U1300-U1399; design and requirement mapping: docs/vmx.md).
 *
 * Intel SDM 325462-092 Vol3C chapters 26-33 (VMX) and Vol3D Appendices A (capability reporting),
 * B (VMCS field encodings) and C (basic exit reasons), compared with SDM 093 where change document
 * 252046-085 lists changes. Opt-in UC_X86_FEATURE_VMX (default off).
 *
 * U1302: VMX operation (VMXON / VMXOFF), the VMCS lifecycle (VMCLEAR / VMPTRLD / VMPTRST), VMREAD /
 * VMWRITE, VMfail, INVEPT / INVVPID in VMX root operation, VMCALL in VMX root operation.
 * U1301: the capability set of the model (docs/vmx.md "Capability set", our choice) and the VMX
 * capability MSRs 480H-492H (Appendix A; Vol4 Table 2-2 pp. 2-46 to 2-48); the VMCS field table
 * (Appendix B) that IA32_VMX_VMCS_ENUM is computed from.
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U1301): whole file */
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
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

/*
 * ---- U1302: VMX operation, VMCS lifecycle and access (Vol3C 26.7, 27.1-27.2, 27.11, 33) ----
 */

static bool x86_vmx_canonical(CPUX86State *env, uint64_t a);

/* VM-instruction error numbers (Vol3C 33.4 Table 33-1, pp. 33-30 / 33-31) */
#define VMXERR_VMCALL_ROOT          1
#define VMXERR_VMCLEAR_ADDR         2
#define VMXERR_VMCLEAR_VMXON        3
#define VMXERR_VMLAUNCH_NONCLEAR    4
#define VMXERR_VMRESUME_NONLAUNCHED 5
#define VMXERR_ENTRY_CTLS           7
#define VMXERR_ENTRY_HOST           8
#define VMXERR_VMPTRLD_ADDR         9
#define VMXERR_VMPTRLD_VMXON        10
#define VMXERR_VMPTRLD_REV          11
#define VMXERR_FIELD                12
#define VMXERR_VMXON_ROOT           15
#define VMXERR_ENTRY_MOVSS          26
#define VMXERR_INVEPT_INVVPID       28

#define VMCS_VM_INSTRUCTION_ERROR   0x4400
#define VMX_NO_VMCS                 (~0ull)
#define VMCS_LAUNCH_OFFSET          8       /* our VMCS region layout (docs/vmx.md) */

/*
 * MAXPHYADDR for VMX pointers: CPUID.80000008H:EAX[7:0], reduced by IA32_TME_ACTIVATE[39:36] when
 * IA32_TME_ACTIVATE[0] = 1 (Vol3C 27.1 footnote 2 p. 27-1; 33.3 VMCLEAR footnote 2 p. 33-11)
 */
static int vmx_maxphyaddr(CPUX86State *env)
{
    int w = env_archcpu(env)->phys_bits;

    if (env->tme_activate & 1) {
        w -= (env->tme_activate >> 36) & 0xf;
    }
    return w;
}

/* a physical address within MAXPHYADDR (bit 48 of IA32_VMX_BASIC is 0) */
static bool vmx_paddr_ok(CPUX86State *env, uint64_t a)
{
    return !(a >> vmx_maxphyaddr(env));
}

/* a VMXON / VMCS pointer: 4-KByte aligned, within MAXPHYADDR (27.1 p. 27-1, 27.11.5 p. 27-34) */
static bool vmx_ptr_ok(CPUX86State *env, uint64_t a)
{
    return !(a & 0xfff) && vmx_paddr_ok(env, a);
}

static uint32_t vmx_ldl(CPUX86State *env, uint64_t pa)
{
    return x86_ldl_phys(env_cpu(env), pa);
}

static uint64_t vmx_ldq(CPUX86State *env, uint64_t pa)
{
    return x86_ldq_phys(env_cpu(env), pa);
}

static void vmx_stl(CPUX86State *env, uint64_t pa, uint32_t v)
{
    x86_stl_phys(env_cpu(env), pa, v);
}

static void vmx_stq(CPUX86State *env, uint64_t pa, uint64_t v)
{
    x86_stq_phys(env_cpu(env), pa, v);
}

/* the width mask of a field (encoding bits 14:13: 0 16-bit, 1 64-bit, 2 32-bit, 3 natural) */
static uint64_t vmx_width_mask(uint32_t enc)
{
    switch ((enc >> 13) & 3) {
    case 0:
        return 0xffffull;
    case 2:
        return 0xffffffffull;
    default:
        return ~0ull;
    }
}

/* read / write a field of the VMCS at 'ptr' by its full encoding (internal: the field exists) */
static uint64_t vmcs_read(CPUX86State *env, uint64_t ptr, uint32_t enc)
{
    int high, slot = x86_vmx_field_slot(env, enc, &high);

    g_assert(slot >= 0 && !high);
    return vmx_ldq(env, ptr + slot) & vmx_width_mask(enc);
}

static void vmcs_write(CPUX86State *env, uint64_t ptr, uint32_t enc, uint64_t v)
{
    int high, slot = x86_vmx_field_slot(env, enc, &high);

    g_assert(slot >= 0 && !high);
    vmx_stq(env, ptr + slot, v & vmx_width_mask(enc));
}

/* the current VMCS (fields of the VMCS the processor operates on) */
static uint64_t vmcs_get(CPUX86State *env, uint32_t enc)
{
    return vmcs_read(env, env->vmx.vmcs_ptr, enc);
}

static void vmcs_put(CPUX86State *env, uint32_t enc, uint64_t v)
{
    vmcs_write(env, env->vmx.vmcs_ptr, enc, v);
}

/*
 * VMsucceed / VMfailInvalid / VMfailValid (Vol3C 33.2 p. 33-2): CF, PF, AF, ZF, SF, OF; the
 * translator continues with CC_OP_EFLAGS
 */
static void vmx_succeed(CPUX86State *env)
{
    CC_SRC = 0;
}

static void vmx_fail_invalid(CPUX86State *env)
{
    CC_SRC = CC_C;
}

static void vmx_fail_valid(CPUX86State *env, uint32_t err)
{
    CC_SRC = CC_Z;
    vmcs_put(env, VMCS_VM_INSTRUCTION_ERROR, err);
}

static void vmx_fail(CPUX86State *env, uint32_t err)
{
    if (env->vmx.vmcs_ptr != VMX_NO_VMCS) {
        vmx_fail_valid(env, err);
    } else {
        vmx_fail_invalid(env);
    }
}

static int vmx_cpl(CPUX86State *env)
{
    return env->hflags & HF_CPL_MASK;
}

/* "(CR0.PE = 0) or (RFLAGS.VM = 1) or (IA32_EFER.LMA = 1 and CS.L = 0)": #UD (33.3) */
static bool vmx_bad_mode(CPUX86State *env)
{
    return !(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK) ||
           ((env->hflags & HF_LMA_MASK) && !(env->hflags & HF_CS64_MASK));
}

/* CR0 and CR4 values supported in VMX operation (26.8 pp. 26-3 / 26-4, A.7 / A.8 p. A-7) */
static bool vmx_cr0_ok(CPUX86State *env, uint64_t cr0, bool unrestricted)
{
    uint64_t fixed0 = VMX_CR0_FIXED0;

    if (unrestricted) {
        fixed0 &= ~(uint64_t)(CR0_PE_MASK | CR0_PG_MASK);
    }
    return (cr0 & fixed0) == fixed0 && !(cr0 & ~VMX_CR0_FIXED1);
}

static bool vmx_cr4_ok(CPUX86State *env, uint64_t cr4)
{
    return (cr4 & VMX_CR4_FIXED0) == VMX_CR4_FIXED0 && !(cr4 & cr4_reserved_bits(env));
}

/*
 * MOV to CR0 / CR4, CLTS and LMSW in VMX operation: a value outside the fixed bits is #GP(0)
 * (26.8 p. 26-3); in VMX non-root operation with "unrestricted guest" CR0.PE / CR0.PG may be 0
 * (26.8 notes, p. 26-4). Called by helper_write_crN.
 */
bool x86_vmx_cr_write_ok(CPUX86State *env, int reg, uint64_t v)
{
    if (!env->vmx.on) {
        return true;
    }
    if (reg == 0) {
        return vmx_cr0_ok(env, v, false);
    }
    return vmx_cr4_ok(env, v);
}

/* the register named by bits 6:3 / 31:28 of a VM-exit instruction-information value */
#define VMX_INFO_REG1(i)    (((i) >> 3) & 15)
#define VMX_INFO_REG2(i)    (((i) >> 28) & 15)
#define VMX_INFO_ISREG(i)   (((i) >> 10) & 1)

/* VMX operand size: 64 bits in 64-bit mode, else 32 bits (33.3: CS.D ignored) */
static bool vmx_op64(CPUX86State *env)
{
    return (env->hflags & HF_CS64_MASK) != 0;
}

static uint64_t vmx_reg(CPUX86State *env, int r)
{
    return vmx_op64(env) ? env->regs[r] : (uint32_t)env->regs[r];
}

/*
 * VMXON / VMXOFF / VMCLEAR / VMPTRLD / VMPTRST / VMCALL (Vol3C 33.3). op: VMX_OP_*; a0: the linear
 * address of the m64 operand; info / qual: the VM-exit instruction information / exit
 * qualification of the instruction (for VM exits in VMX non-root operation, U1303).
 */
void helper_vmx_op(CPUX86State *env, uint32_t op, target_ulong a0, uint32_t info,
                   target_ulong qual)
{
    uintptr_t ra = GETPC();
    uint64_t addr;
    uint32_t rev;

    if (op == VMX_OP_VMCALL) {
        /* VMCALL p. 33-9: the order of its checks differs */
        if (!env->vmx.on) {
            raise_exception_ra(env, EXCP06_ILLOP, ra);
        }
        if ((env->eflags & VM_MASK) ||
            ((env->hflags & HF_LMA_MASK) && !(env->hflags & HF_CS64_MASK))) {
            raise_exception_ra(env, EXCP06_ILLOP, ra);
        }
        if (vmx_cpl(env)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        /* no dual-monitor treatment of SMIs and SMM (IA32_VMX_BASIC[49] = 0) */
        vmx_fail(env, VMXERR_VMCALL_ROOT);
        return;
    }
    if (op == VMX_OP_VMXON) {
        /* VMXON pp. 33-28 / 33-29 */
        if (vmx_bad_mode(env) || !(env->cr[4] & CR4_VMXE_MASK)) {
            raise_exception_ra(env, EXCP06_ILLOP, ra);
        }
        if (env->vmx.on) {
            if (vmx_cpl(env)) {
                raise_exception_ra(env, EXCP0D_GPF, ra);
            }
            vmx_fail(env, VMXERR_VMXON_ROOT);
            return;
        }
        /* the model is never in SMX operation (no GETSEC[SENTER]): bit 2 enables VMXON */
        if (vmx_cpl(env) || env->a20_mask != -1 ||
            !vmx_cr0_ok(env, env->cr[0], false) || !vmx_cr4_ok(env, env->cr[4]) ||
            !(env->msr_ia32_feature_control & FEATURE_CONTROL_LOCKED) ||
            !(env->msr_ia32_feature_control & FEATURE_CONTROL_VMXON_ENABLED_OUTSIDE_SMX)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        addr = cpu_ldq_data_ra(env, a0, ra);
        if (!vmx_ptr_ok(env, addr)) {
            vmx_fail_invalid(env);
            return;
        }
        rev = vmx_ldl(env, addr);
        if ((rev & 0x7fffffffu) != VMX_REVISION_ID || (rev >> 31)) {
            vmx_fail_invalid(env);
            return;
        }
        /*
         * enter VMX operation with no current VMCS; INIT / A20M blocking: the model has neither
         * INIT nor A20M (a20_mask is all ones); there is no address-range monitoring state to
         * clear (MONITOR stores nothing); no Intel PT
         */
        env->vmx.on = 1;
        env->vmx.vmxon_ptr = addr;
        env->vmx.vmcs_ptr = VMX_NO_VMCS;
        vmx_succeed(env);
        return;
    }

    /* VMXOFF, VMCLEAR, VMPTRLD, VMPTRST */
    if (!env->vmx.on || vmx_bad_mode(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (vmx_cpl(env)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    switch (op) {
    case VMX_OP_VMXOFF:
        /* pp. 33-26 / 33-27: no dual-monitor treatment, so no VMfail 23 */
        env->vmx.on = 0;
        env->vmx.vmcs_ptr = VMX_NO_VMCS;
        vmx_succeed(env);
        break;
    case VMX_OP_VMCLEAR:
        /* pp. 33-11 / 33-12 */
        addr = cpu_ldq_data_ra(env, a0, ra);
        if (!vmx_ptr_ok(env, addr)) {
            vmx_fail(env, VMXERR_VMCLEAR_ADDR);
        } else if (addr == env->vmx.vmxon_ptr) {
            vmx_fail(env, VMXERR_VMCLEAR_VMXON);
        } else {
            vmx_stl(env, addr + VMCS_LAUNCH_OFFSET, 0);     /* launch state "clear" */
            if (addr == env->vmx.vmcs_ptr) {
                env->vmx.vmcs_ptr = VMX_NO_VMCS;
            }
            vmx_succeed(env);
        }
        break;
    case VMX_OP_VMPTRLD:
        /* pp. 33-17 / 33-18 */
        addr = cpu_ldq_data_ra(env, a0, ra);
        if (!vmx_ptr_ok(env, addr)) {
            vmx_fail(env, VMXERR_VMPTRLD_ADDR);
        } else if (addr == env->vmx.vmxon_ptr) {
            vmx_fail(env, VMXERR_VMPTRLD_VMXON);
        } else {
            rev = vmx_ldl(env, addr);
            /* rev[31] = 1 is accepted: the 1-setting of "VMCS shadowing" is supported */
            if ((rev & 0x7fffffffu) != VMX_REVISION_ID) {
                vmx_fail(env, VMXERR_VMPTRLD_REV);
            } else {
                env->vmx.vmcs_ptr = addr;
                vmx_succeed(env);
            }
        }
        break;
    case VMX_OP_VMPTRST:
        /* pp. 33-19 / 33-20 */
        cpu_stq_data_ra(env, a0, env->vmx.vmcs_ptr, ra);
        vmx_succeed(env);
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * VMREAD / VMWRITE (Vol3C 27.11.2 pp. 27-32 / 27-33; 33.3 pp. 33-21 to 33-25). The encoding is in
 * the register named by info[31:28]; the other operand is the register info[6:3] (info[10] = 1) or
 * memory at a0. Operand size: 64 bits in 64-bit mode, 32 bits outside IA-32e mode.
 */
void helper_vmread(CPUX86State *env, target_ulong a0, uint32_t info, target_ulong qual)
{
    uintptr_t ra = GETPC();
    uint64_t enc, ptr, v;
    int slot, high;

    if (!env->vmx.on || vmx_bad_mode(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (vmx_cpl(env)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    ptr = env->vmx.vmcs_ptr;
    if (ptr == VMX_NO_VMCS) {
        vmx_fail_invalid(env);
        return;
    }
    enc = vmx_reg(env, VMX_INFO_REG2(info));
    slot = x86_vmx_field_slot(env, enc, &high);
    if (slot < 0) {
        vmx_fail_valid(env, VMXERR_FIELD);
        return;
    }
    v = vmx_ldq(env, ptr + slot) & vmx_width_mask((uint32_t)enc);
    if (high) {
        v >>= 32;                               /* high access type: bits 63:32 in 31:0 */
    } else if (!vmx_op64(env)) {
        v = (uint32_t)v;                        /* outside IA-32e mode: bits 31:0 */
    }
    if (VMX_INFO_ISREG(info)) {
        env->regs[VMX_INFO_REG1(info)] = vmx_op64(env) ? v : (uint32_t)v;
    } else if (vmx_op64(env)) {
        cpu_stq_data_ra(env, a0, v, ra);
    } else {
        cpu_stl_data_ra(env, a0, (uint32_t)v, ra);
    }
    vmx_succeed(env);
}

void helper_vmwrite(CPUX86State *env, target_ulong a0, uint32_t info, target_ulong qual)
{
    uintptr_t ra = GETPC();
    uint64_t enc, ptr, v, old;
    int slot, high;

    if (!env->vmx.on || vmx_bad_mode(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (vmx_cpl(env)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    ptr = env->vmx.vmcs_ptr;
    if (ptr == VMX_NO_VMCS) {
        vmx_fail_invalid(env);
        return;
    }
    /* "faults resulting from accessing a memory source operand occur ... before determining if
       the destination VMCS field is supported" (33.3 VMWRITE, p. 33-24) */
    if (VMX_INFO_ISREG(info)) {
        v = vmx_reg(env, VMX_INFO_REG1(info));
    } else if (vmx_op64(env)) {
        v = cpu_ldq_data_ra(env, a0, ra);
    } else {
        v = cpu_ldl_data_ra(env, a0, ra);
    }
    enc = vmx_reg(env, VMX_INFO_REG2(info));
    slot = x86_vmx_field_slot(env, enc, &high);
    if (slot < 0) {
        vmx_fail_valid(env, VMXERR_FIELD);
        return;
    }
    /* IA32_VMX_MISC[29] = 1: the VM-exit information fields are writable (no error 13) */
    if (high) {
        old = vmx_ldq(env, ptr + slot);
        v = (old & 0xffffffffull) | ((uint64_t)(uint32_t)v << 32);
    } else if (!vmx_op64(env)) {
        v = (uint32_t)v;                        /* full access outside IA-32e mode: 63:32 := 0 */
    }
    vmx_stq(env, ptr + slot, v & vmx_width_mask((uint32_t)enc));
    vmx_succeed(env);
}

/* the linear address a0 + n of an operand (32-bit wrap-around outside 64-bit mode) */
static target_ulong vmx_addr_add(CPUX86State *env, target_ulong a0, int n)
{
    return vmx_op64(env) ? a0 + n : (uint32_t)(a0 + n);
}

/*
 * INVEPT (vpid = 0) / INVVPID (vpid = 1) (Vol3C 33.3 pp. 33-3 to 33-8; 31.4.3 p. 31-22). The
 * type is in the register info[31:28], the 128-bit descriptor at a0. The model keeps no
 * translation across an invalidation it is asked for: every type flushes the whole TLB (an
 * invalidation of more than the specified mappings is allowed, 33.3 / 31.4.3).
 */
void helper_invept(CPUX86State *env, target_ulong a0, uint32_t info, target_ulong qual,
                   uint32_t vpid)
{
    uintptr_t ra = GETPC();
    uint64_t type, lo, hi;

    if (!env->vmx.on || vmx_bad_mode(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (vmx_cpl(env)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    type = vmx_reg(env, VMX_INFO_REG2(info));
    if (!vpid) {
        /* A.10: bit 25 single-context (type 1), bit 26 all-context (type 2) */
        if (type != 1 && type != 2) {
            vmx_fail(env, VMXERR_INVEPT_INVVPID);
            return;
        }
        lo = cpu_ldq_data_ra(env, a0, ra);
        (void)cpu_ldq_data_ra(env, vmx_addr_add(env, a0, 8), ra);
        if (type == 1 && !x86_vmx_eptp_ok(env, lo)) {
            vmx_fail(env, VMXERR_INVEPT_INVVPID);
            return;
        }
    } else {
        /* A.10: bits 40-43, types 0-3 */
        if (type > 3) {
            vmx_fail(env, VMXERR_INVEPT_INVVPID);
            return;
        }
        lo = cpu_ldq_data_ra(env, a0, ra);
        hi = cpu_ldq_data_ra(env, vmx_addr_add(env, a0, 8), ra);
        if (lo >> 16) {
            vmx_fail(env, VMXERR_INVEPT_INVVPID);
            return;
        }
        if ((type == 0 || type == 1 || type == 3) && !(lo & 0xffff)) {
            vmx_fail(env, VMXERR_INVEPT_INVVPID);
            return;
        }
        if (type == 0 && !x86_vmx_canonical(env, hi)) {
            vmx_fail(env, VMXERR_INVEPT_INVVPID);
            return;
        }
    }
    tlb_flush(env_cpu(env));
    vmx_succeed(env);
}

/*
 * The EPTP checks of VM entry (Vol3C 29.2.1.1 p. 29-4; Table 27-9 p. 27-17): memory type UC (0) or
 * WB (6) (A.10 bits 8, 14), page-walk length 4 or 5 (bits 6, 7), A/D enable allowed (bit 21),
 * bit 7 (supervisor shadow-stack control) 0 (A.10 bit 23 = 0), bits 11:8 reserved, the address
 * within MAXPHYADDR
 */
bool x86_vmx_eptp_ok(CPUX86State *env, uint64_t eptp)
{
    uint64_t mt = eptp & 7, wl = (eptp >> 3) & 7;

    if (mt != 0 && mt != 6) {
        return false;
    }
    if (wl != 3 && wl != 4) {
        return false;
    }
    if (eptp & 0xf80) {
        return false;
    }
    return vmx_paddr_ok(env, eptp & ~0xfffull);
}

/* canonical for the CPU's linear-address width (CPUID.80000008H:EAX[15:8]; Vol3A 4.5) */
static bool x86_vmx_canonical(CPUX86State *env, uint64_t a)
{
    int la = (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57) ? 57 : 48;

    return (uint64_t)((int64_t)(a << (64 - la)) >> (64 - la)) == a;
}

#endif /* __Use_Original_Qemu (U1301) */
