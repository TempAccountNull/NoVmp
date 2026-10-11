/*
 * VMX (NoVmp, ledgers U1300-U1399; design and requirement mapping: docs/vmx.md).
 *
 * Intel SDM 325462-092 Vol3C chapters 26-33 (VMX) and Vol3D Appendices A (capability reporting),
 * B (VMCS field encodings) and C (basic exit reasons), compared with SDM 093 where change document
 * 252046-085 lists changes. Opt-in UC_X86_FEATURE_VMX (default off).
 *
* U1303: VM entries and exits, VMLAUNCH / VMRESUME / VMFUNC, the VMX-preemption timer, unconditional
 * exits, the non-root paths of the VMX instructions.
 * U1303: VM entries and exits (chapters 29, 30), VMLAUNCH / VMRESUME / VMFUNC, the VMX-preemption
 * timer, the unconditional VM exits (28.1.2) and the non-root paths of the VMX instructions.
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

void x86_store_dr(CPUX86State *env, int reg, target_ulong t0);    /* bpt_helper.c */

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
void x86_vmx_check_insn_exit(CPUX86State *env, uint32_t op, uint32_t info, uint64_t qual,
                             uintptr_t ra);

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
        x86_vmx_check_insn_exit(env, op, info, qual, ra);   /* U1303: non-root: VM exit 18 */
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
            x86_vmx_check_insn_exit(env, op, info, qual, ra);   /* U1303: VM exit 27 */
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
    x86_vmx_check_insn_exit(env, op, info, qual, ra);       /* U1303: VM exits 26, 19, 21, 22 */
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
 * U1303: VMREAD / VMWRITE in VMX non-root operation (28.1.3 p. 28-5, 33.3): a VM exit unless "VMCS
 * shadowing" is 1, the encoding register has no bit set in 63:15 (31:15 outside 64-bit mode) and the
 * VMREAD / VMWRITE bitmap bit of encoding bits 14:0 is 0; otherwise the access goes to the VMCS
 * referenced by the VMCS link pointer
 */
static void vmx_shadow_check(CPUX86State *env, bool write, uint32_t info, target_ulong qual,
                             uintptr_t ra)
{
    uint64_t enc, bmp;

    if (!env->vmx.nonroot) {
        return;
    }
    enc = vmx_reg(env, VMX_INFO_REG2(info));
    bmp = write ? env->vmx.vmwrite_bmp : env->vmx.vmread_bmp;
    if (!(env->vmx.proc2 & (1u << 14)) || (enc >> 15) ||
        ((vmx_ldl(env, bmp | ((enc & 0x7fff) >> 3)) >> (enc & 7)) & 1)) {
        x86_vmx_insn_exit(env, write ? 25 : 23, VMX_INFO_ISREG(info) ? 0 : qual, info, true, ra);
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
    vmx_shadow_check(env, false, info, qual, ra);      /* U1303: VM exit 23 or the shadow VMCS */
    if (vmx_cpl(env)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    ptr = env->vmx.nonroot ? env->vmx.link_ptr : env->vmx.vmcs_ptr;
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
    vmx_shadow_check(env, true, info, qual, ra);       /* U1303: VM exit 25 or the shadow VMCS */
    if (vmx_cpl(env)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    ptr = env->vmx.nonroot ? env->vmx.link_ptr : env->vmx.vmcs_ptr;
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
    if (env->vmx.nonroot) {                            /* U1303: VM exits 50 / 53 */
        x86_vmx_insn_exit(env, vpid ? 53 : 50, qual, info, true, ra);
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

/*
 * ---- U1303: VM entries, VM exits, VMLAUNCH / VMRESUME / VMCALL / VMFUNC, preemption timer ----
 * Vol3C chapters 29 (VM entries) and 30 (VM exits), 28.1.2 (unconditional exits), 28.5.1 (timer),
 * 28.5.7 (VM functions). The controls of the current VM entry are cached in env->vmx (the
 * processor may keep VMCS data on the processor, 27.11.1); every VMCS field written by a VM exit
 * goes to the VMCS region.
 */

/* VMCS encodings used below (Appendix B) */
#define VMCS_VPID               0x0000
#define VMCS_PI_NV              0x0002
#define VMCS_GUEST_ES_SEL       0x0800      /* +2 * n: ES CS SS DS FS GS LDTR TR */
#define VMCS_GUEST_INTR_STATUS  0x0810
#define VMCS_HOST_ES_SEL        0x0c00      /* +2 * n: ES CS SS DS FS GS TR */
#define VMCS_IO_BITMAP_A        0x2000
#define VMCS_IO_BITMAP_B        0x2002
#define VMCS_MSR_BITMAP         0x2004
#define VMCS_EXIT_MSR_STORE     0x2006
#define VMCS_EXIT_MSR_LOAD      0x2008
#define VMCS_ENTRY_MSR_LOAD     0x200a
#define VMCS_TSC_OFFSET         0x2010
#define VMCS_VAPIC_ADDR         0x2012
#define VMCS_APIC_ACCESS_ADDR   0x2014
#define VMCS_PI_DESC            0x2016
#define VMCS_VMFUNC_CTLS        0x2018
#define VMCS_EPTP               0x201a
#define VMCS_EOI_EXIT0          0x201c
#define VMCS_EPTP_LIST          0x2024
#define VMCS_VMREAD_BITMAP      0x2026
#define VMCS_VMWRITE_BITMAP     0x2028
#define VMCS_XSS_EXIT_BITMAP    0x202c
#define VMCS_TSC_MULTIPLIER     0x2032
#define VMCS_PROC3              0x2034
#define VMCS_PCONFIG_BITMAP     0x203e
#define VMCS_GPA                0x2400
#define VMCS_LINK_PTR           0x2800
#define VMCS_GUEST_DEBUGCTL     0x2802
#define VMCS_GUEST_PAT          0x2804
#define VMCS_GUEST_EFER         0x2806
#define VMCS_GUEST_PDPTE0       0x280a
#define VMCS_HOST_PAT           0x2c00
#define VMCS_HOST_EFER          0x2c02
#define VMCS_PIN                0x4000
#define VMCS_PROC               0x4002
#define VMCS_EXC_BITMAP         0x4004
#define VMCS_PFEC_MASK          0x4006
#define VMCS_PFEC_MATCH         0x4008
#define VMCS_CR3_COUNT          0x400a
#define VMCS_EXIT_CTLS          0x400c
#define VMCS_EXIT_MSR_STORE_CNT 0x400e
#define VMCS_EXIT_MSR_LOAD_CNT  0x4010
#define VMCS_ENTRY_CTLS         0x4012
#define VMCS_ENTRY_MSR_LOAD_CNT 0x4014
#define VMCS_ENTRY_INTR_INFO    0x4016
#define VMCS_ENTRY_EXC_ERR      0x4018
#define VMCS_ENTRY_INSN_LEN     0x401a
#define VMCS_TPR_THRESHOLD      0x401c
#define VMCS_PROC2              0x401e
#define VMCS_EXIT_REASON        0x4402
#define VMCS_EXIT_INTR_INFO     0x4404
#define VMCS_EXIT_INTR_ERR      0x4406
#define VMCS_IDTV_INFO          0x4408
#define VMCS_IDTV_ERR           0x440a
#define VMCS_EXIT_INSN_LEN      0x440c
#define VMCS_EXIT_INSN_INFO     0x440e
#define VMCS_GUEST_ES_LIMIT     0x4800      /* +2 * n: ES CS SS DS FS GS LDTR TR GDTR IDTR */
#define VMCS_GUEST_ES_AR        0x4814      /* +2 * n: ES CS SS DS FS GS LDTR TR */
#define VMCS_GUEST_INTERRUPTIBILITY 0x4824
#define VMCS_GUEST_ACTIVITY     0x4826
#define VMCS_GUEST_SYSENTER_CS  0x482a
#define VMCS_PREEMPT_TIMER      0x482e
#define VMCS_HOST_SYSENTER_CS   0x4c00
#define VMCS_CR0_MASK           0x6000
#define VMCS_CR4_MASK           0x6002
#define VMCS_CR0_SHADOW         0x6004
#define VMCS_CR4_SHADOW         0x6006
#define VMCS_CR3_TARGET0        0x6008
#define VMCS_EXIT_QUAL          0x6400
#define VMCS_GUEST_LINEAR       0x640a
#define VMCS_GUEST_CR0          0x6800
#define VMCS_GUEST_CR3          0x6802
#define VMCS_GUEST_CR4          0x6804
#define VMCS_GUEST_ES_BASE      0x6806      /* +2 * n: ES CS SS DS FS GS LDTR TR GDTR IDTR */
#define VMCS_GUEST_DR7          0x681a
#define VMCS_GUEST_RSP          0x681c
#define VMCS_GUEST_RIP          0x681e
#define VMCS_GUEST_RFLAGS       0x6820
#define VMCS_GUEST_PENDING_DBG  0x6822
#define VMCS_GUEST_SYSENTER_ESP 0x6824
#define VMCS_GUEST_SYSENTER_EIP 0x6826
#define VMCS_HOST_CR0           0x6c00
#define VMCS_HOST_CR3           0x6c02
#define VMCS_HOST_CR4           0x6c04
#define VMCS_HOST_FS_BASE       0x6c06
#define VMCS_HOST_GS_BASE       0x6c08
#define VMCS_HOST_TR_BASE       0x6c0a
#define VMCS_HOST_GDTR_BASE     0x6c0c
#define VMCS_HOST_IDTR_BASE     0x6c0e
#define VMCS_HOST_SYSENTER_ESP  0x6c10
#define VMCS_HOST_SYSENTER_EIP  0x6c12
#define VMCS_HOST_RSP           0x6c14
#define VMCS_HOST_RIP           0x6c16

/* controls (Tables 27-5 to 27-8, 27-14, 27-17) */
#define PIN_EXT_INTR            (1u << 0)
#define PIN_NMI_EXIT            (1u << 3)
#define PIN_VIRTUAL_NMI         (1u << 5)
#define PIN_PREEMPT_TIMER       (1u << 6)
#define PIN_POSTED_INTR         (1u << 7)
#define PROC_INTR_WINDOW        (1u << 2)
#define PROC_TSC_OFFSET         (1u << 3)
#define PROC_HLT                (1u << 7)
#define PROC_INVLPG             (1u << 9)
#define PROC_MWAIT              (1u << 10)
#define PROC_RDPMC              (1u << 11)
#define PROC_RDTSC              (1u << 12)
#define PROC_CR3_LOAD           (1u << 15)
#define PROC_CR3_STORE          (1u << 16)
#define PROC_TERTIARY           (1u << 17)
#define PROC_CR8_LOAD           (1u << 19)
#define PROC_CR8_STORE          (1u << 20)
#define PROC_TPR_SHADOW         (1u << 21)
#define PROC_NMI_WINDOW         (1u << 22)
#define PROC_MOV_DR             (1u << 23)
#define PROC_UNCOND_IO          (1u << 24)
#define PROC_IO_BITMAPS         (1u << 25)
#define PROC_MTF                (1u << 27)
#define PROC_MSR_BITMAPS        (1u << 28)
#define PROC_MONITOR            (1u << 29)
#define PROC_PAUSE              (1u << 30)
#define PROC_SECONDARY          (1u << 31)
#define PROC2_APIC_ACCESS       (1u << 0)
#define PROC2_EPT               (1u << 1)
#define PROC2_DESC_TABLE        (1u << 2)
#define PROC2_RDTSCP            (1u << 3)
#define PROC2_X2APIC            (1u << 4)
#define PROC2_VPID              (1u << 5)
#define PROC2_WBINVD            (1u << 6)
#define PROC2_UNRESTRICTED      (1u << 7)
#define PROC2_APIC_REG          (1u << 8)
#define PROC2_VID               (1u << 9)
#define PROC2_RDRAND            (1u << 11)
#define PROC2_INVPCID           (1u << 12)
#define PROC2_VMFUNC            (1u << 13)
#define PROC2_SHADOW_VMCS       (1u << 14)
#define PROC2_RDSEED            (1u << 16)
#define PROC2_XSAVES            (1u << 20)
#define PROC2_MBEC              (1u << 22)
#define PROC2_TSC_SCALING       (1u << 25)
#define PROC2_USER_WAIT         (1u << 26)
#define PROC2_PCONFIG           (1u << 27)
#define EXIT_SAVE_DEBUG         (1u << 2)
#define EXIT_HOST_64            (1u << 9)
#define EXIT_ACK_INTR           (1u << 15)
#define EXIT_SAVE_PAT           (1u << 18)
#define EXIT_LOAD_PAT           (1u << 19)
#define EXIT_SAVE_EFER          (1u << 20)
#define EXIT_LOAD_EFER          (1u << 21)
#define EXIT_SAVE_TIMER         (1u << 22)
#define ENTRY_LOAD_DEBUG        (1u << 2)
#define ENTRY_IA32E             (1u << 9)
#define ENTRY_SMM               (1u << 10)
#define ENTRY_DEACT_DUAL        (1u << 11)
#define ENTRY_LOAD_PAT          (1u << 14)
#define ENTRY_LOAD_EFER         (1u << 15)

/* basic exit reasons (Appendix C) */
#define EXIT_REASON_EXCEPTION_NMI   0
#define EXIT_REASON_EXT_INTR        1
#define EXIT_REASON_TRIPLE_FAULT    2
#define EXIT_REASON_INTR_WINDOW     7
#define EXIT_REASON_NMI_WINDOW      8
#define EXIT_REASON_TASK_SWITCH     9
#define EXIT_REASON_CPUID           10
#define EXIT_REASON_GETSEC          11
#define EXIT_REASON_HLT             12
#define EXIT_REASON_INVD            13
#define EXIT_REASON_VMCALL          18
#define EXIT_REASON_VMCLEAR         19
#define EXIT_REASON_VMLAUNCH        20
#define EXIT_REASON_VMPTRLD         21
#define EXIT_REASON_VMPTRST         22
#define EXIT_REASON_VMREAD          23
#define EXIT_REASON_VMRESUME        24
#define EXIT_REASON_VMWRITE         25
#define EXIT_REASON_VMXOFF          26
#define EXIT_REASON_VMXON           27
#define EXIT_REASON_INVALID_GUEST   33
#define EXIT_REASON_MSR_LOADING     34
#define EXIT_REASON_MTF             37
#define EXIT_REASON_INVEPT          50
#define EXIT_REASON_PREEMPT_TIMER   52
#define EXIT_REASON_INVVPID         53
#define EXIT_REASON_XSETBV          55
#define EXIT_REASON_VMFUNC          59
#define EXIT_REASON_ENTRY_FAIL      (1u << 31)

#define MSR_IA32_DEBUGCTL_VMX        0x1d9   /* IA32_DEBUGCTL */
#define MSR_IA32_SMM_MONITOR_CTL_NV  0x9b    /* written only in SMM (29.4, 30.6) */
#define MSR_IA32_SMBASE_NV           0x9e    /* read only in SMM (30.4) */

/* SGX as the CPU reports it (29.3.1.5: the enclave-interruption bit needs CPUID.07H:EBX.SGX[2]) */
static bool x86_vmx_sgx(CPUX86State *env)
{
    return vmx_feat(env, FEAT_7_0_EBX, 1u << 2, 7, 0, 1);
}

/* the description of a VM exit (30.2); fields not marked valid are left unmodified (undefined) */
typedef struct VmxExit {
    uint32_t reason;
    uint64_t qual;
    uint64_t gla, gpa;
    uint32_t intr_info, intr_err;
    uint32_t insn_len, insn_info;
    bool gla_valid, gpa_valid, len_valid, info_valid;
    bool rf_zero;               /* RFLAGS.RF saved as 0 (instruction exits, 30.3.3) */
} VmxExit;

static void vmx_exit_init(VmxExit *e, uint32_t reason, uint64_t qual)
{
    memset(e, 0, sizeof(*e));
    e->reason = reason;
    e->qual = qual;
    e->intr_info = 0;           /* exiting-event identification: invalid */
}

static bool vmx_long_mode(CPUX86State *env)
{
    return (env->hflags & HF_LMA_MASK) != 0;
}

/* the linear-address width N (CPUID.80000008H:EAX[15:8]) */
static int vmx_la_width(CPUX86State *env)
{
    return (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57) ? 57 : 48;
}

/* bits 63:N of a value are identical (29.3.1.4: RIP and SSP; no canonicality of bit N-1) */
static bool vmx_upper_same(CPUX86State *env, uint64_t v)
{
    int n = vmx_la_width(env);
    uint64_t top = v >> n;

    return top == 0 || top == (~0ull >> n);
}

/* each of bits 63:N set to bit N-1 (30.5.1 / 30.5.2: loaded host addresses) */
static uint64_t vmx_sext(CPUX86State *env, uint64_t v)
{
    int n = vmx_la_width(env);

    return (uint64_t)((int64_t)(v << (64 - n)) >> (64 - n));
}

/* QEMU segment flags <-> VMCS access rights (Table 27-2: bits 23:8 of the descriptor's high dword) */
static uint32_t vmx_flags_to_ar(const SegmentCache *sc, bool unusable)
{
    uint32_t ar = (sc->flags >> 8) & 0xf0ff;

    return unusable ? (ar | (1u << 16)) : ar;
}

static uint32_t vmx_ar_to_flags(uint32_t ar)
{
    uint32_t f = (ar & 0xf0ff) << 8;

    if (ar & (1u << 16)) {
        f &= ~DESC_P_MASK;          /* the model's "unusable": P = 0 (a null-loaded segment) */
    }
    return f;
}

/* a segment register is unusable when it was loaded with a null selector: P = 0 in the cache */
static bool vmx_seg_unusable(CPUX86State *env, int seg)
{
    return !(env->segs[seg].flags & DESC_P_MASK);
}

/* WRMSR / RDMSR for the VMX MSR lists (misc_helper.c, no hooks, no #GP): false = would #GP */
static bool vmx_wrmsr(CPUX86State *env, uint32_t msr, uint64_t v)
{
    return x86_msr_vmx_access(env, msr, &v, true);
}

static bool vmx_rdmsr(CPUX86State *env, uint32_t msr, uint64_t *v)
{
    return x86_msr_vmx_access(env, msr, v, false);
}

/* the valid memory types of a PAT value (29.2.2 / 29.3.1.1: 0, 1, 4, 5, 6, 7 in each byte) */
static bool vmx_pat_ok(uint64_t v)
{
    int i;

    for (i = 0; i < 8; i++) {
        uint8_t t = v >> (8 * i);
        if (t == 2 || t == 3 || t > 7) {
            return false;
        }
    }
    return true;
}

/* CR3 bits reserved for the Intel 64 architecture: 63:MAXPHYADDR (29.2.2, 29.3.1.1) */
static bool vmx_cr3_ok(CPUX86State *env, uint64_t cr3)
{
    return !(cr3 >> vmx_maxphyaddr(env));
}

/* ---- cached controls ---- */
static void vmx_load_controls(CPUX86State *env)
{
    X86VmxState *v = &env->vmx;
    int i;

    v->pin = vmcs_get(env, VMCS_PIN);
    v->proc = vmcs_get(env, VMCS_PROC);
    v->proc2 = (v->proc & PROC_SECONDARY) ? vmcs_get(env, VMCS_PROC2) : 0;
    v->proc3 = (v->proc & PROC_TERTIARY) ? vmcs_get(env, VMCS_PROC3) : 0;
    v->exit_ctls = vmcs_get(env, VMCS_EXIT_CTLS);
    v->entry_ctls = vmcs_get(env, VMCS_ENTRY_CTLS);
    v->exc_bitmap = vmcs_get(env, VMCS_EXC_BITMAP);
    v->pfec_mask = vmcs_get(env, VMCS_PFEC_MASK);
    v->pfec_match = vmcs_get(env, VMCS_PFEC_MATCH);
    v->cr3_count = vmcs_get(env, VMCS_CR3_COUNT);
    for (i = 0; i < 4; i++) {
        v->cr3_target[i] = vmcs_get(env, VMCS_CR3_TARGET0 + 2 * i);
    }
    v->cr0_mask = vmcs_get(env, VMCS_CR0_MASK);
    v->cr4_mask = vmcs_get(env, VMCS_CR4_MASK);
    v->cr0_shadow = vmcs_get(env, VMCS_CR0_SHADOW);
    v->cr4_shadow = vmcs_get(env, VMCS_CR4_SHADOW);
    v->io_a = vmcs_get(env, VMCS_IO_BITMAP_A);
    v->io_b = vmcs_get(env, VMCS_IO_BITMAP_B);
    v->msr_bitmap = vmcs_get(env, VMCS_MSR_BITMAP);
    v->tsc_offset = vmcs_get(env, VMCS_TSC_OFFSET);
    v->tsc_mult = vmcs_get(env, VMCS_TSC_MULTIPLIER);
    v->vapic = vmcs_get(env, VMCS_VAPIC_ADDR);
    v->apic_access = vmcs_get(env, VMCS_APIC_ACCESS_ADDR);
    v->tpr_threshold = vmcs_get(env, VMCS_TPR_THRESHOLD);
    v->eptp = vmcs_get(env, VMCS_EPTP);
    v->vpid = vmcs_get(env, VMCS_VPID);
    v->vmfunc = (v->proc2 & PROC2_VMFUNC) ? vmcs_get(env, VMCS_VMFUNC_CTLS) : 0;
    v->eptp_list = vmcs_get(env, VMCS_EPTP_LIST);
    v->vmread_bmp = vmcs_get(env, VMCS_VMREAD_BITMAP);
    v->vmwrite_bmp = vmcs_get(env, VMCS_VMWRITE_BITMAP);
    v->xss_bmp = vmcs_get(env, VMCS_XSS_EXIT_BITMAP);
    v->pconfig_bmp = vmcs_get(env, VMCS_PCONFIG_BITMAP);
    v->link_ptr = vmcs_get(env, VMCS_LINK_PTR);
    v->pi_nv = vmcs_get(env, VMCS_PI_NV);
    v->pi_desc = vmcs_get(env, VMCS_PI_DESC);
    for (i = 0; i < 4; i++) {
        v->eoi_exit[i] = vmcs_get(env, VMCS_EOI_EXIT0 + 2 * i);
    }
}

/* ---- 29.2.1 checks on the VMX controls: false = VMfailValid(7) ---- */

/* a control word against its capability: bits of allowed0 must be 1, bits outside allowed1 0 */
static bool vmx_ctl_ok(uint64_t v, uint64_t allowed0, uint64_t allowed1)
{
    return (v & allowed0) == allowed0 && !(v & ~allowed1);
}

/* a 4-KByte aligned physical address within MAXPHYADDR */
static bool vmx_page_ok(CPUX86State *env, uint64_t a)
{
    return !(a & 0xfff) && vmx_paddr_ok(env, a);
}

/* an MSR area: 16-byte aligned, the address and its last byte within MAXPHYADDR (29.2.1.2) */
static bool vmx_msr_area_ok(CPUX86State *env, uint64_t a, uint32_t count)
{
    if (!count) {
        return true;
    }
    return !(a & 0xf) && vmx_paddr_ok(env, a) &&
           vmx_paddr_ok(env, a + (uint64_t)count * 16 - 1) &&
           a + (uint64_t)count * 16 - 1 >= a;
}

static bool vmx_check_controls(CPUX86State *env)
{
    X86VmxState *v = &env->vmx;
    uint32_t pin = vmcs_get(env, VMCS_PIN), proc = vmcs_get(env, VMCS_PROC);
    uint32_t proc2 = (proc & PROC_SECONDARY) ? vmcs_get(env, VMCS_PROC2) : 0;
    uint64_t proc3 = (proc & PROC_TERTIARY) ? vmcs_get(env, VMCS_PROC3) : 0;
    uint32_t exit = vmcs_get(env, VMCS_EXIT_CTLS), entry = vmcs_get(env, VMCS_ENTRY_CTLS);
    uint32_t info, type, vec, len;

    /* 29.2.1.1 VM-execution control fields (with the TRUE capability MSRs, A.3) */
    if (!vmx_ctl_ok(pin, VMX_PIN_TRUE0, VMX_PIN_ALLOWED1) ||
        !vmx_ctl_ok(proc, VMX_PROC_TRUE0, VMX_PROC_ALLOWED1) ||
        !vmx_ctl_ok(proc2, 0, VMX_PROC2_ALLOWED1) ||
        !vmx_ctl_ok(proc3, 0, vmx_proc3_allowed1(env))) {
        return false;
    }
    if (vmcs_get(env, VMCS_CR3_COUNT) > 4) {
        return false;
    }
    if ((proc & PROC_IO_BITMAPS) &&
        (!vmx_page_ok(env, vmcs_get(env, VMCS_IO_BITMAP_A)) ||
         !vmx_page_ok(env, vmcs_get(env, VMCS_IO_BITMAP_B)))) {
        return false;
    }
    if ((proc & PROC_MSR_BITMAPS) && !vmx_page_ok(env, vmcs_get(env, VMCS_MSR_BITMAP))) {
        return false;
    }
    if ((proc & PROC_TPR_SHADOW) && !vmx_page_ok(env, vmcs_get(env, VMCS_VAPIC_ADDR))) {
        return false;
    }
    if ((proc2 & PROC2_APIC_ACCESS) && !vmx_page_ok(env, vmcs_get(env, VMCS_APIC_ACCESS_ADDR))) {
        return false;
    }
    if ((proc2 & PROC2_VMFUNC) && (vmcs_get(env, VMCS_VMFUNC_CTLS) & 1) &&
        !vmx_page_ok(env, vmcs_get(env, VMCS_EPTP_LIST))) {
        return false;
    }
    if ((proc2 & PROC2_SHADOW_VMCS) &&
        (!vmx_page_ok(env, vmcs_get(env, VMCS_VMREAD_BITMAP)) ||
         !vmx_page_ok(env, vmcs_get(env, VMCS_VMWRITE_BITMAP)))) {
        return false;
    }
    if ((proc & PROC_TPR_SHADOW) && !(proc2 & PROC2_VID) &&
        (vmcs_get(env, VMCS_TPR_THRESHOLD) >> 4)) {
        return false;
    }
    if ((proc & PROC_TPR_SHADOW) && !(proc2 & (PROC2_APIC_ACCESS | PROC2_VID)) &&
        (vmcs_get(env, VMCS_TPR_THRESHOLD) & 0xf) >
        ((vmx_ldl(env, vmcs_get(env, VMCS_VAPIC_ADDR) + 0x80) >> 4) & 0xf)) {
        return false;
    }
    if (!(pin & PIN_NMI_EXIT) && (pin & PIN_VIRTUAL_NMI)) {
        return false;
    }
    if (!(pin & PIN_VIRTUAL_NMI) && (proc & PROC_NMI_WINDOW)) {
        return false;
    }
    if (!(proc & PROC_TPR_SHADOW) && (proc2 & (PROC2_X2APIC | PROC2_APIC_REG | PROC2_VID))) {
        return false;
    }
    if ((proc2 & PROC2_X2APIC) && (proc2 & PROC2_APIC_ACCESS)) {
        return false;
    }
    if ((proc2 & PROC2_VID) && !(pin & PIN_EXT_INTR)) {
        return false;
    }
    if (pin & PIN_POSTED_INTR) {
        uint64_t pi = vmcs_get(env, VMCS_PI_DESC);

        if (!(proc2 & PROC2_VID) || !(exit & EXIT_ACK_INTR) ||
            (vmcs_get(env, VMCS_PI_NV) >> 8) || (pi & 0x3f) || !vmx_paddr_ok(env, pi)) {
            return false;
        }
    }
    if ((proc2 & PROC2_VPID) && vmcs_get(env, VMCS_VPID) == 0) {
        return false;
    }
    if ((proc2 & PROC2_EPT) && !x86_vmx_eptp_ok(env, vmcs_get(env, VMCS_EPTP))) {
        return false;
    }
    if (!(proc2 & PROC2_EPT) && (proc2 & (PROC2_UNRESTRICTED | PROC2_MBEC))) {
        return false;
    }
    if (proc2 & PROC2_VMFUNC) {
        uint64_t f = vmcs_get(env, VMCS_VMFUNC_CTLS);

        if (f & ~VMX_VMFUNC_ALLOWED1) {
            return false;
        }
        if ((f & 1) && !(proc2 & PROC2_EPT)) {
            return false;
        }
    }
    if ((proc2 & PROC2_TSC_SCALING) && vmcs_get(env, VMCS_TSC_MULTIPLIER) == 0) {
        return false;
    }
    /* 29.2.1.2 VM-exit control fields */
    if (!vmx_ctl_ok(exit, VMX_EXIT_TRUE0, VMX_EXIT_ALLOWED1)) {
        return false;
    }
    if (!(pin & PIN_PREEMPT_TIMER) && (exit & EXIT_SAVE_TIMER)) {
        return false;
    }
    if (!vmx_msr_area_ok(env, vmcs_get(env, VMCS_EXIT_MSR_STORE),
                         vmcs_get(env, VMCS_EXIT_MSR_STORE_CNT)) ||
        !vmx_msr_area_ok(env, vmcs_get(env, VMCS_EXIT_MSR_LOAD),
                         vmcs_get(env, VMCS_EXIT_MSR_LOAD_CNT))) {
        return false;
    }
    /* 29.2.1.3 VM-entry control fields */
    if (!vmx_ctl_ok(entry, VMX_ENTRY_TRUE0, VMX_ENTRY_ALLOWED1)) {
        return false;
    }
    info = vmcs_get(env, VMCS_ENTRY_INTR_INFO);
    if (info >> 31) {
        type = (info >> 8) & 7;
        vec = info & 0xff;
        if (type == 1) {
            return false;
        }
        /* type 7: the 1-setting of "monitor trap flag" is supported; no FRED: vector 0 only */
        if ((type == 2 && vec != 2) || (type == 3 && vec > 31) || (type == 7 && vec != 0)) {
            return false;
        }
        /* deliver-error-code bit: IA32_VMX_BASIC[56] = 1, so only the must-be-0 rules apply */
        if (info & (1u << 11)) {
            if (type != 3) {
                return false;
            }
            if ((proc2 & PROC2_UNRESTRICTED) &&
                !(vmcs_get(env, VMCS_GUEST_CR0) & CR0_PE_MASK)) {
                return false;
            }
            if (vmcs_get(env, VMCS_ENTRY_EXC_ERR) >> 16) {
                return false;
            }
        }
        /* bit 13 (nested exception): IA32_VMX_BASIC[58] = 0; reserved 30:14 and 12 */
        if (info & 0x7fffd000u) {
            return false;
        }
        len = vmcs_get(env, VMCS_ENTRY_INSN_LEN);
        if ((type == 4 || type == 5 || type == 6) && len > 15) {
            return false;               /* 0 allowed: IA32_VMX_MISC[30] = 1 */
        }
    }
    if (!vmx_msr_area_ok(env, vmcs_get(env, VMCS_ENTRY_MSR_LOAD),
                         vmcs_get(env, VMCS_ENTRY_MSR_LOAD_CNT))) {
        return false;
    }
    /* outside SMM: "entry to SMM" and "deactivate dual-monitor treatment" 0 (also reserved) */
    if (entry & (ENTRY_SMM | ENTRY_DEACT_DUAL)) {
        return false;
    }
    (void)v;
    return true;
}

/* ---- 29.2.2-29.2.4 checks on the host-state area: false = VMfailValid(8) ---- */
static bool vmx_check_host(CPUX86State *env)
{
    uint32_t exit = vmcs_get(env, VMCS_EXIT_CTLS), entry = vmcs_get(env, VMCS_ENTRY_CTLS);
    uint64_t cr0 = vmcs_get(env, VMCS_HOST_CR0), cr4 = vmcs_get(env, VMCS_HOST_CR4);
    bool host64 = (exit & EXIT_HOST_64) != 0;
    int i;

    /* 29.2.2: CR0 / CR4 fixed bits (CR0.NW / CD are never checked: FIXED1 allows them) */
    if (!vmx_cr0_ok(env, cr0 & 0xffffffffull, false) || (cr0 >> 32) || !vmx_cr4_ok(env, cr4)) {
        return false;
    }
    if ((cr4 & CR4_CET_MASK) && !(cr0 & CR0_WP_MASK)) {
        return false;
    }
    if (!vmx_cr3_ok(env, vmcs_get(env, VMCS_HOST_CR3))) {
        return false;
    }
    if (!x86_vmx_canonical(env, vmcs_get(env, VMCS_HOST_SYSENTER_ESP)) ||
        !x86_vmx_canonical(env, vmcs_get(env, VMCS_HOST_SYSENTER_EIP))) {
        return false;
    }
    if ((exit & EXIT_LOAD_PAT) && !vmx_pat_ok(vmcs_get(env, VMCS_HOST_PAT))) {
        return false;
    }
    if (exit & EXIT_LOAD_EFER) {
        uint64_t efer = vmcs_get(env, VMCS_HOST_EFER);

        if (!x86_efer_value_ok(env, efer) ||
            !!(efer & MSR_EFER_LMA) != host64 || !!(efer & MSR_EFER_LME) != host64) {
            return false;
        }
    }
    /* 29.2.3: selectors (RPL and TI 0; CS and TR not null; SS not null outside 64-bit hosts) */
    for (i = 0; i < 7; i++) {
        if (vmcs_get(env, VMCS_HOST_ES_SEL + 2 * i) & 7) {
            return false;
        }
    }
    if (vmcs_get(env, VMCS_HOST_ES_SEL + 2 * R_CS) == 0 ||
        vmcs_get(env, VMCS_HOST_ES_SEL + 12) == 0) {               /* TR */
        return false;
    }
    if (!host64 && vmcs_get(env, VMCS_HOST_ES_SEL + 2 * R_SS) == 0) {
        return false;
    }
    if (!x86_vmx_canonical(env, vmcs_get(env, VMCS_HOST_FS_BASE)) ||
        !x86_vmx_canonical(env, vmcs_get(env, VMCS_HOST_GS_BASE)) ||
        !x86_vmx_canonical(env, vmcs_get(env, VMCS_HOST_GDTR_BASE)) ||
        !x86_vmx_canonical(env, vmcs_get(env, VMCS_HOST_IDTR_BASE)) ||
        !x86_vmx_canonical(env, vmcs_get(env, VMCS_HOST_TR_BASE))) {
        return false;
    }
    /* 29.2.4: address-space size */
    if (!vmx_long_mode(env)) {
        if ((entry & ENTRY_IA32E) || host64) {
            return false;
        }
    } else if (!host64) {
        return false;
    }
    if (!host64) {
        if ((entry & ENTRY_IA32E) || (cr4 & CR4_PCIDE_MASK) || (cr4 & CR4_FRED_MASK) ||
            (vmcs_get(env, VMCS_HOST_RIP) >> 32)) {
            return false;
        }
    } else {
        if (!(cr4 & CR4_PAE_MASK) || !x86_vmx_canonical(env, vmcs_get(env, VMCS_HOST_RIP))) {
            return false;
        }
    }
    return true;
}

/*
 * ---- 29.3.1 checks on the guest-state area: 0 = pass, else the exit qualification + 1 of the
 * VM-entry failure (29.8: 0 in most cases, 2 PDPTEs, 3 NMI injection with STI blocking, 4 VMCS
 * link pointer) ----
 */
#define VMX_GUEST_OK    (-1)

static bool vmx_seg_limit_g_ok(uint32_t limit, uint32_t ar)
{
    if ((limit & 0xfff) != 0xfff && (ar & (1u << 15))) {
        return false;
    }
    if ((limit >> 20) && !(ar & (1u << 15))) {
        return false;
    }
    return true;
}

/* PDPTE checks of MOV to CR3 with PAE paging (Vol3A 5.4.1): present entries, reserved bits */
static bool vmx_pdpte_ok(CPUX86State *env, uint64_t e)
{
    uint64_t rsvd = 0x1e6ull | ~((1ull << env_archcpu(env)->phys_bits) - 1);

    return !(e & 1) || !(e & rsvd);
}

static int vmx_check_guest(CPUX86State *env, uint32_t proc2)
{
    uint32_t entry = vmcs_get(env, VMCS_ENTRY_CTLS), pin = vmcs_get(env, VMCS_PIN);
    bool ug = (proc2 & PROC2_UNRESTRICTED) != 0, ia32e = (entry & ENTRY_IA32E) != 0;
    uint64_t cr0 = vmcs_get(env, VMCS_GUEST_CR0), cr4 = vmcs_get(env, VMCS_GUEST_CR4);
    uint64_t rflags = vmcs_get(env, VMCS_GUEST_RFLAGS), rip = vmcs_get(env, VMCS_GUEST_RIP);
    uint32_t intr = vmcs_get(env, VMCS_ENTRY_INTR_INFO);
    uint32_t itype = (intr >> 8) & 7, ivec = intr & 0xff;
    bool inject = intr >> 31;
    uint32_t act = vmcs_get(env, VMCS_GUEST_ACTIVITY);
    uint32_t irs = vmcs_get(env, VMCS_GUEST_INTERRUPTIBILITY);
    uint64_t pend = vmcs_get(env, VMCS_GUEST_PENDING_DBG);
    uint32_t sel[8], lim[8], ar[8];
    uint64_t base[8];
    bool v86 = (rflags & VM_MASK) != 0;
    int i;

    for (i = 0; i < 8; i++) {
        sel[i] = vmcs_get(env, VMCS_GUEST_ES_SEL + 2 * i);
        lim[i] = vmcs_get(env, VMCS_GUEST_ES_LIMIT + 2 * i);
        ar[i] = vmcs_get(env, VMCS_GUEST_ES_AR + 2 * i);
        base[i] = vmcs_get(env, VMCS_GUEST_ES_BASE + 2 * i);
    }
#define AR_UNUSABLE(a) (((a) >> 16) & 1)
#define AR_TYPE(a) ((a) & 0xf)
#define AR_S(a) (((a) >> 4) & 1)
#define AR_DPL(a) (((a) >> 5) & 3)
#define AR_P(a) (((a) >> 7) & 1)
#define AR_L(a) (((a) >> 13) & 1)
#define AR_DB(a) (((a) >> 14) & 1)

    /* 29.3.1.1 control registers, debug registers, MSRs */
    if (cr0 >> 32) {
        return 0;
    }
    if (!vmx_cr0_ok(env, cr0, ug)) {
        return 0;
    }
    if ((cr0 & CR0_PG_MASK) && !(cr0 & CR0_PE_MASK)) {
        return 0;
    }
    if (!vmx_cr4_ok(env, cr4)) {
        return 0;
    }
    if ((cr4 & CR4_CET_MASK) && !(cr0 & CR0_WP_MASK)) {
        return 0;
    }
    if ((entry & ENTRY_LOAD_DEBUG) &&
        !x86_msr_value_ok(env, MSR_IA32_DEBUGCTL_VMX, vmcs_get(env, VMCS_GUEST_DEBUGCTL))) {
        return 0;
    }
    if (ia32e && (!(cr0 & CR0_PG_MASK) || !(cr4 & CR4_PAE_MASK))) {
        return 0;
    }
    if (!ia32e && ((cr4 & CR4_PCIDE_MASK) || (cr4 & CR4_FRED_MASK))) {
        return 0;
    }
    if (!vmx_cr3_ok(env, vmcs_get(env, VMCS_GUEST_CR3))) {
        return 0;
    }
    /* SDM 093 29.3.1.1: the bits reserved in DR7 (the model's DR7: bits 63:32, no Intel PT) */
    if ((entry & ENTRY_LOAD_DEBUG) && (vmcs_get(env, VMCS_GUEST_DR7) >> 32)) {
        return 0;
    }
    if (!x86_vmx_canonical(env, vmcs_get(env, VMCS_GUEST_SYSENTER_ESP)) ||
        !x86_vmx_canonical(env, vmcs_get(env, VMCS_GUEST_SYSENTER_EIP))) {
        return 0;
    }
    if ((entry & ENTRY_LOAD_PAT) && !vmx_pat_ok(vmcs_get(env, VMCS_GUEST_PAT))) {
        return 0;
    }
    if (entry & ENTRY_LOAD_EFER) {
        uint64_t efer = vmcs_get(env, VMCS_GUEST_EFER);

        if (!x86_efer_value_ok(env, efer) || !!(efer & MSR_EFER_LMA) != ia32e) {
            return 0;
        }
        if ((cr0 & CR0_PG_MASK) && !!(efer & MSR_EFER_LMA) != !!(efer & MSR_EFER_LME)) {
            return 0;
        }
    }

    /* 29.3.1.2 segment registers (ES CS SS DS FS GS LDTR TR = 0..7) */
    if (sel[7] & 4) {
        return 0;
    }
    if (!AR_UNUSABLE(ar[6]) && (sel[6] & 4)) {
        return 0;
    }
    if (!v86 && !ug && (sel[R_SS] & 3) != (sel[R_CS] & 3)) {
        return 0;
    }
    if (v86) {
        for (i = 0; i < 6; i++) {
            if (base[i] != (uint64_t)sel[i] << 4 || lim[i] != 0xffff || ar[i] != 0xf3) {
                return 0;
            }
        }
    }
    if (!x86_vmx_canonical(env, base[7]) || !x86_vmx_canonical(env, base[R_FS]) ||
        !x86_vmx_canonical(env, base[R_GS])) {
        return 0;
    }
    if (!AR_UNUSABLE(ar[6]) && !x86_vmx_canonical(env, base[6])) {
        return 0;
    }
    if (base[R_CS] >> 32) {
        return 0;
    }
    if ((!AR_UNUSABLE(ar[R_SS]) && (base[R_SS] >> 32)) ||
        (!AR_UNUSABLE(ar[R_DS]) && (base[R_DS] >> 32)) ||
        (!AR_UNUSABLE(ar[R_ES]) && (base[R_ES] >> 32))) {
        return 0;
    }
    if (!v86) {
        for (i = 0; i < 6; i++) {
            uint32_t a = ar[i];
            bool cs = i == R_CS;

            if (!cs && AR_UNUSABLE(a)) {
                continue;
            }
            /* Type */
            if (cs) {
                uint32_t t = AR_TYPE(a);
                if (!(t == 9 || t == 11 || t == 13 || t == 15 || (ug && t == 3))) {
                    return 0;
                }
            } else if (i == R_SS) {
                if (AR_TYPE(a) != 3 && AR_TYPE(a) != 7) {
                    return 0;
                }
            } else {
                if (!(AR_TYPE(a) & 1) || ((AR_TYPE(a) & 8) && !(AR_TYPE(a) & 2))) {
                    return 0;
                }
            }
            if (!AR_S(a) || !AR_P(a) || (a & 0xf00) || (a & 0xfffe0000u)) {
                return 0;
            }
            if (!vmx_seg_limit_g_ok(lim[i], a)) {
                return 0;
            }
        }
        /* DPL */
        if (AR_TYPE(ar[R_CS]) == 3 && AR_DPL(ar[R_CS]) != 0) {
            return 0;
        }
        if ((AR_TYPE(ar[R_CS]) == 9 || AR_TYPE(ar[R_CS]) == 11) &&
            AR_DPL(ar[R_CS]) != AR_DPL(ar[R_SS])) {
            return 0;
        }
        if ((AR_TYPE(ar[R_CS]) == 13 || AR_TYPE(ar[R_CS]) == 15) &&
            AR_DPL(ar[R_CS]) > AR_DPL(ar[R_SS])) {
            return 0;
        }
        if (!ug && AR_DPL(ar[R_SS]) != (sel[R_SS] & 3)) {
            return 0;
        }
        if ((AR_TYPE(ar[R_CS]) == 3 || !(cr0 & CR0_PE_MASK)) && AR_DPL(ar[R_SS]) != 0) {
            return 0;
        }
        for (i = 0; i < 6; i++) {
            if (i == R_CS || i == R_SS || ug || AR_UNUSABLE(ar[i])) {
                continue;
            }
            if (AR_TYPE(ar[i]) <= 11 && AR_DPL(ar[i]) < (sel[i] & 3)) {
                return 0;
            }
        }
        /* CS.D/B with IA-32e mode and CS.L */
        if (ia32e && AR_L(ar[R_CS]) && AR_DB(ar[R_CS])) {
            return 0;
        }
    }
    /* TR */
    if (ia32e ? AR_TYPE(ar[7]) != 11 : (AR_TYPE(ar[7]) != 3 && AR_TYPE(ar[7]) != 11)) {
        return 0;
    }
    if (AR_S(ar[7]) || !AR_P(ar[7]) || (ar[7] & 0xf00) || AR_UNUSABLE(ar[7]) ||
        (ar[7] & 0xfffe0000u) || !vmx_seg_limit_g_ok(lim[7], ar[7])) {
        return 0;
    }
    /* LDTR */
    if (!AR_UNUSABLE(ar[6])) {
        if (AR_TYPE(ar[6]) != 2 || AR_S(ar[6]) || !AR_P(ar[6]) || (ar[6] & 0xf00) ||
            (ar[6] & 0xfffe0000u) || !vmx_seg_limit_g_ok(lim[6], ar[6])) {
            return 0;
        }
    }

    /* 29.3.1.3 GDTR / IDTR */
    if (!x86_vmx_canonical(env, vmcs_get(env, VMCS_GUEST_ES_BASE + 16)) ||
        !x86_vmx_canonical(env, vmcs_get(env, VMCS_GUEST_ES_BASE + 18)) ||
        (vmcs_get(env, VMCS_GUEST_ES_LIMIT + 16) >> 16) ||
        (vmcs_get(env, VMCS_GUEST_ES_LIMIT + 18) >> 16)) {
        return 0;
    }

    /* 29.3.1.4 RIP, RFLAGS */
    if (!(ia32e && AR_L(ar[R_CS]))) {
        if (rip >> 32) {
            return 0;
        }
    } else if (!vmx_upper_same(env, rip)) {
        return 0;
    }
    if ((rflags >> 22) || (rflags & ((1u << 15) | (1u << 5) | (1u << 3))) || !(rflags & 2)) {
        return 0;
    }
    if (v86 && (ia32e || !(cr0 & CR0_PE_MASK))) {
        return 0;
    }
    if (inject && itype == 0 && !(rflags & IF_MASK)) {
        return 0;
    }

    /* 29.3.1.5 non-register state: activity (IA32_VMX_MISC: active and HLT) */
    if (act > 1) {
        return 0;
    }
    if (act == 1 && AR_DPL(ar[R_SS]) != 0) {
        return 0;
    }
    if (act != 0 && (irs & 3)) {
        return 0;
    }
    if (inject && act == 1) {
        if (!(itype == 0 || itype == 2 || (itype == 3 && (ivec == 1 || ivec == 18)) ||
              (itype == 7 && ivec == 0))) {
            return 0;
        }
    }
    /* interruptibility state */
    if (irs >> 5) {
        return 0;
    }
    if ((irs & 3) == 3) {
        return 0;
    }
    if ((irs & 1) && !(rflags & IF_MASK)) {
        return 0;
    }
    if (inject && (itype == 0 || itype == 2) && (irs & 3)) {
        return (itype == 2 && (irs & 1)) ? 3 + 1 : 0;   /* qualification 3: NMI with STI blocking */
    }
    if (irs & 4) {
        return 0;                       /* blocking by SMI: the processor is never in SMM here */
    }
    if ((pin & PIN_VIRTUAL_NMI) && inject && itype == 2 && (irs & 8)) {
        return 0;
    }
    if ((irs & 16) && ((irs & 2) || !x86_vmx_sgx(env))) {
        return 0;
    }
    /* pending debug exceptions */
    if (pend & ~(0xfull | (1ull << 11) | (1ull << 12) | (1ull << 14) | (1ull << 16))) {
        return 0;
    }
    if ((irs & 3) || act == 1) {
        bool tf = (rflags & TF_MASK) != 0;
        bool btf = (vmcs_get(env, VMCS_GUEST_DEBUGCTL) & 2) != 0;
        bool bs = (pend >> 14) & 1;

        if (tf && !btf && !bs) {
            return 0;
        }
        if ((!tf || btf) && bs) {
            return 0;
        }
    }
    if (pend & (1ull << 16)) {
        if ((pend & ~((1ull << 16) | (1ull << 12))) != 0 || !(pend & (1ull << 12)) ||
            !(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_RTM) || (irs & 2)) {
            return 0;
        }
    }
    /* VMCS link pointer: qualification 4 */
    {
        uint64_t link = vmcs_get(env, VMCS_LINK_PTR);

        if (link != ~0ull) {
            uint32_t rev;

            if ((link & 0xfff) || !vmx_paddr_ok(env, link) || link == env->vmx.vmcs_ptr) {
                return 4 + 1;
            }
            rev = vmx_ldl(env, link);
            if ((rev & 0x7fffffffu) != VMX_REVISION_ID ||
                (rev >> 31) != ((proc2 & PROC2_SHADOW_VMCS) ? 1u : 0u)) {
                return 4 + 1;
            }
        }
    }
    /* 29.3.1.6 PDPTEs of a PAE-paging guest: qualification 2 */
    if ((cr0 & CR0_PG_MASK) && (cr4 & CR4_PAE_MASK) && !ia32e) {
        for (i = 0; i < 4; i++) {
            uint64_t e = (proc2 & PROC2_EPT)
                ? vmcs_get(env, VMCS_GUEST_PDPTE0 + 2 * i)
                : vmx_ldq(env, (vmcs_get(env, VMCS_GUEST_CR3) & ~0x1full) + 8 * i);

            if (!vmx_pdpte_ok(env, e)) {
                return 2 + 1;
            }
        }
    }
#undef AR_UNUSABLE
#undef AR_TYPE
#undef AR_S
#undef AR_DPL
#undef AR_P
#undef AR_L
#undef AR_DB
    return VMX_GUEST_OK;
}

/* ---- loading processor state (29.3.2 guest, 30.5 host) ---- */

/* CR0 / CR4 / EFER / CR3 in this order; the TLB is flushed by the caller */
static void vmx_set_cr(CPUX86State *env, uint64_t cr0, uint64_t cr3, uint64_t cr4, uint64_t efer)
{
    cpu_x86_update_cr4(env, cr4);
    cpu_x86_update_cr0(env, (uint32_t)cr0);
    cpu_load_efer(env, efer);
    cpu_x86_update_cr3(env, cr3);
}

static void vmx_set_seg(CPUX86State *env, SegmentCache *sc, uint32_t sel, uint64_t base,
                        uint32_t limit, uint32_t flags)
{
    sc->selector = sel;
    sc->base = base;
    sc->limit = limit;
    sc->flags = flags;
}

/* CR0 bits never modified by VM entries / exits (29.3.2.1, 30.5.1): ET, 15:6, 17, 28:19, NW, CD */
#define VMX_CR0_KEEP  (0x10ull | 0xffc0ull | 0x20000ull | 0x1ff80000ull | 0x60000000ull)

static void vmx_load_guest(CPUX86State *env)
{
    uint32_t entry = env->vmx.entry_ctls;
    bool ia32e = (entry & ENTRY_IA32E) != 0;
    uint64_t cr0 = (vmcs_get(env, VMCS_GUEST_CR0) & ~VMX_CR0_KEEP) | (env->cr[0] & VMX_CR0_KEEP);
    uint64_t efer = env->efer;
    uint32_t irs = vmcs_get(env, VMCS_GUEST_INTERRUPTIBILITY);
    int i;

    /* 29.3.2.1 */
    if (entry & ENTRY_LOAD_EFER) {
        efer = vmcs_get(env, VMCS_GUEST_EFER);
    } else {
        efer = ia32e ? (efer | MSR_EFER_LMA) : (efer & ~MSR_EFER_LMA);
        if (cr0 & CR0_PG_MASK) {
            efer = ia32e ? (efer | MSR_EFER_LME) : (efer & ~MSR_EFER_LME);
        }
    }
    vmx_set_cr(env, cr0, vmcs_get(env, VMCS_GUEST_CR3), vmcs_get(env, VMCS_GUEST_CR4), efer);
    if (entry & ENTRY_LOAD_DEBUG) {
        uint64_t dr7 = vmcs_get(env, VMCS_GUEST_DR7);

        x86_store_dr(env, 7, (dr7 & ~0xd000ull) | 0x400);
        env->msr_debugctl = vmcs_get(env, VMCS_GUEST_DEBUGCTL);
    }
    env->sysenter_cs = vmcs_get(env, VMCS_GUEST_SYSENTER_CS);
    env->sysenter_esp = vmcs_get(env, VMCS_GUEST_SYSENTER_ESP);
    env->sysenter_eip = vmcs_get(env, VMCS_GUEST_SYSENTER_EIP);
    if (entry & ENTRY_LOAD_PAT) {
        env->pat = vmcs_get(env, VMCS_GUEST_PAT);
    }
    /* 29.3.2.2 segment and descriptor-table registers */
    for (i = 0; i < 6; i++) {
        uint32_t ar = vmcs_get(env, VMCS_GUEST_ES_AR + 2 * i);
        uint64_t base = vmcs_get(env, VMCS_GUEST_ES_BASE + 2 * i);
        uint32_t limit = vmcs_get(env, VMCS_GUEST_ES_LIMIT + 2 * i);

        if ((ar >> 16) & 1) {
            /* unusable: base, limit, rest of the AR undefined except SS.DPL, SS.B, SS base 3:0,
               the FS / GS bases, bits 63:32 of the SS / DS / ES bases (our choice: base kept with
               those rules, limit 0) */
            if (i == R_SS) {
                base &= ~0xfull;
            }
            if (i == R_SS || i == R_DS || i == R_ES) {
                base = (uint32_t)base;
            }
            if (i == R_SS) {
                ar |= 1u << 14;
            }
            if (i == R_CS) {
                limit = vmcs_get(env, VMCS_GUEST_ES_LIMIT + 2 * R_CS);
            } else {
                limit = 0;
            }
        }
        vmx_set_seg(env, &env->segs[i], vmcs_get(env, VMCS_GUEST_ES_SEL + 2 * i), base, limit,
                    vmx_ar_to_flags(ar));
    }
    vmx_set_seg(env, &env->ldt, vmcs_get(env, VMCS_GUEST_ES_SEL + 12),
                vmcs_get(env, VMCS_GUEST_ES_BASE + 12), vmcs_get(env, VMCS_GUEST_ES_LIMIT + 12),
                vmx_ar_to_flags(vmcs_get(env, VMCS_GUEST_ES_AR + 12)));
    vmx_set_seg(env, &env->tr, vmcs_get(env, VMCS_GUEST_ES_SEL + 14),
                vmcs_get(env, VMCS_GUEST_ES_BASE + 14), vmcs_get(env, VMCS_GUEST_ES_LIMIT + 14),
                vmx_ar_to_flags(vmcs_get(env, VMCS_GUEST_ES_AR + 14)));
    env->gdt.base = vmcs_get(env, VMCS_GUEST_ES_BASE + 16);
    env->gdt.limit = vmcs_get(env, VMCS_GUEST_ES_LIMIT + 16);
    env->idt.base = vmcs_get(env, VMCS_GUEST_ES_BASE + 18);
    env->idt.limit = vmcs_get(env, VMCS_GUEST_ES_LIMIT + 18);
    /* 29.3.2.3 RIP, RSP, RFLAGS */
    env->regs[R_ESP] = vmcs_get(env, VMCS_GUEST_RSP);
    env->eip = vmcs_get(env, VMCS_GUEST_RIP);
    cpu_load_eflags(env, (uint32_t)vmcs_get(env, VMCS_GUEST_RFLAGS), ~0);
    x86_update_hflags(env);
    /* 29.7.1 interruptibility (the injecting case is handled by vmx_inject) */
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    if (irs & 3) {
        env->hflags |= HF_INHIBIT_IRQ_MASK;
        env->fred_sti = irs & 1;
    }
    if (env->vmx.pin & PIN_VIRTUAL_NMI) {
        env->vmx.vnmi_blocked = (irs >> 3) & 1;
    } else if (irs & 8) {
        env->hflags2 |= HF2_NMI_MASK;
    } else {
        env->hflags2 &= ~HF2_NMI_MASK;
    }
}

/* 29.4 / 30.6: load the MSRs of an MSR-load area; 0 = success, else the failing entry (1-based) */
static uint32_t vmx_load_msr_list(CPUX86State *env, uint64_t area, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; i++) {
        uint64_t idx = vmx_ldq(env, area + 16 * (uint64_t)i);
        uint64_t data = vmx_ldq(env, area + 16 * (uint64_t)i + 8);
        uint32_t msr = (uint32_t)idx;

        if ((idx >> 32) || msr == MSR_FSBASE || msr == MSR_GSBASE || (msr >> 8) == 8 ||
            msr == MSR_IA32_SMM_MONITOR_CTL_NV || !vmx_wrmsr(env, msr, data)) {
            return i + 1;
        }
    }
    return 0;
}

/* 30.4: store the MSRs of the VM-exit MSR-store area; false = VMX abort 1 */
static bool vmx_store_msr_list(CPUX86State *env, uint64_t area, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; i++) {
        uint64_t idx = vmx_ldq(env, area + 16 * (uint64_t)i), v;
        uint32_t msr = (uint32_t)idx;

        if ((idx >> 32) || (msr >> 8) == 8 || msr == MSR_IA32_SMBASE_NV || !vmx_rdmsr(env, msr, &v)) {
            return false;
        }
        vmx_stq(env, area + 16 * (uint64_t)i + 8, v);
    }
    return true;
}

/*
 * 30.7 VMX abort: the abort indicator at offset 4 of the current VMCS, then the VMX-abort shutdown
 * state (only RESET leaves it): the model stops the emulation with UC_ERR_EXCEPTION and keeps the
 * vCPU halted (docs/vmx.md)
 */
static G_NORETURN void vmx_abort(CPUX86State *env, uint32_t indicator)
{
    CPUState *cs = env_cpu(env);

    vmx_stl(env, env->vmx.vmcs_ptr + 4, indicator);
    env->vmx.abort = indicator;
    env->vmx.nonroot = 0;
    env->hflags &= ~HF_GUEST_MASK;
    cs->halted = 1;
    if (env->uc->invalid_error == UC_ERR_OK) {
        env->uc->invalid_error = UC_ERR_EXCEPTION;
    }
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

/* 30.5 loading host state (also for VM-entry failures, 29.8) */
static void vmx_load_host(CPUX86State *env)
{
    uint32_t exit = env->vmx.exit_ctls;
    bool host64 = (exit & EXIT_HOST_64) != 0;
    uint64_t cr0, cr3, cr4, efer;
    uint32_t cs_flags, data_flags;
    int i, mpa = vmx_maxphyaddr(env);

    /* 30.5.1 */
    cr0 = (vmcs_get(env, VMCS_HOST_CR0) & ~VMX_CR0_KEEP & 0xffffffffull) |
          (env->cr[0] & VMX_CR0_KEEP);
    cr3 = (vmcs_get(env, VMCS_HOST_CR3) & ((1ull << mpa) - 1)) | (env->cr[3] & ~((1ull << mpa) - 1));
    cr4 = vmcs_get(env, VMCS_HOST_CR4);
    if (host64) {
        cr4 |= CR4_PAE_MASK;
    } else {
        cr4 &= ~(uint64_t)(CR4_PCIDE_MASK | CR4_FRED_MASK);
    }
    if (exit & EXIT_LOAD_EFER) {
        efer = vmcs_get(env, VMCS_HOST_EFER);
    } else {
        efer = host64 ? (env->efer | MSR_EFER_LMA | MSR_EFER_LME)
                      : (env->efer & ~(uint64_t)(MSR_EFER_LMA | MSR_EFER_LME));
    }
    vmx_set_cr(env, cr0, cr3, cr4, efer);
    x86_store_dr(env, 7, 0x400);
    env->msr_debugctl = 0;
    env->sysenter_cs = vmcs_get(env, VMCS_HOST_SYSENTER_CS);
    env->sysenter_esp = vmx_sext(env, vmcs_get(env, VMCS_HOST_SYSENTER_ESP));
    env->sysenter_eip = vmx_sext(env, vmcs_get(env, VMCS_HOST_SYSENTER_EIP));
    if (exit & EXIT_LOAD_PAT) {
        env->pat = vmcs_get(env, VMCS_HOST_PAT);
    }
    /* 30.5.2 segments: CS type 11 S=1 DPL 0 P L=host64 D=!host64 G; data type 3 S=1 P D G */
    cs_flags = DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK | DESC_G_MASK |
               (host64 ? DESC_L_MASK : DESC_B_MASK);
    data_flags = DESC_P_MASK | DESC_S_MASK | DESC_W_MASK | DESC_A_MASK | DESC_G_MASK | DESC_B_MASK;
    for (i = 0; i < 6; i++) {
        uint32_t sel = vmcs_get(env, VMCS_HOST_ES_SEL + 2 * i);
        uint64_t base = 0;

        if (i == R_FS) {
            base = vmx_sext(env, vmcs_get(env, VMCS_HOST_FS_BASE));
        } else if (i == R_GS) {
            base = vmx_sext(env, vmcs_get(env, VMCS_HOST_GS_BASE));
        }
        if (i == R_CS) {
            vmx_set_seg(env, &env->segs[i], sel, 0, 0xffffffff, cs_flags);
        } else if (sel == 0) {
            /* unusable (our choice for the undefined parts: limit 0; SS keeps D = 1) */
            vmx_set_seg(env, &env->segs[i], 0, base, 0,
                        i == R_SS ? DESC_B_MASK : 0);
        } else {
            vmx_set_seg(env, &env->segs[i], sel, base, 0xffffffff, data_flags);
        }
    }
    vmx_set_seg(env, &env->tr, vmcs_get(env, VMCS_HOST_ES_SEL + 12),
                vmx_sext(env, vmcs_get(env, VMCS_HOST_TR_BASE)), 0x67,
                DESC_P_MASK | (11u << DESC_TYPE_SHIFT));
    vmx_set_seg(env, &env->ldt, 0, 0, 0, 0);
    env->gdt.base = vmx_sext(env, vmcs_get(env, VMCS_HOST_GDTR_BASE));
    env->gdt.limit = 0xffff;
    env->idt.base = vmx_sext(env, vmcs_get(env, VMCS_HOST_IDTR_BASE));
    env->idt.limit = 0xffff;
    /* 30.5.3 */
    env->eip = vmcs_get(env, VMCS_HOST_RIP);
    env->regs[R_ESP] = vmcs_get(env, VMCS_HOST_RSP);
    cpu_load_eflags(env, 0x2, ~0);
    env->hflags &= ~(HF_GUEST_MASK | HF_INHIBIT_IRQ_MASK);
    x86_update_hflags(env);
    /* 30.5.4 PDPTEs of a PAE-paging host: VMX abort 2 if MOV to CR3 would #GP */
    if ((cr4 & CR4_PAE_MASK) && !host64 && (cr0 & CR0_PG_MASK)) {
        for (i = 0; i < 4; i++) {
            if (!vmx_pdpte_ok(env, vmx_ldq(env, (cr3 & ~0x1full) + 8 * i))) {
                vmx_abort(env, 2);
            }
        }
    }
    /* 30.5.5: active, no STI / MOV-SS blocking, no pending debug exceptions; the TLB */
    env->vmx.nonroot = 0;
    env->vmx.timer_on = 0;
    env->vmx.mtf_pending = 0;
    env->vmx.delivering = 0;
    env_cpu(env)->halted = 0;
    tlb_flush(env_cpu(env));
}

/* ---- the VMX-preemption timer (28.5.1, 29.7.4): counts down 1 per change of TSC bit 5 ---- */
#define VMX_TIMER_RATE  5

static uint64_t vmx_tsc(CPUX86State *env)
{
    return cpu_get_tsc(env) + env->tsc_adjust;
}

static uint32_t vmx_timer_value(CPUX86State *env)
{
    uint64_t now;

    if (!env->vmx.timer_on) {
        return 0;
    }
    now = vmx_tsc(env) >> VMX_TIMER_RATE;
    return now >= env->vmx.timer_deadline ? 0 : (uint32_t)(env->vmx.timer_deadline - now);
}

/* ---- 30.2-30.3 saving guest state and recording the exit ---- */
static void vmx_save_guest(CPUX86State *env, const VmxExit *e)
{
    uint32_t exit = env->vmx.exit_ctls;
    uint32_t irs = 0, act = 0;
    uint64_t rflags = cpu_compute_eflags(env);
    int i;

    vmcs_put(env, VMCS_GUEST_CR0, env->cr[0]);
    vmcs_put(env, VMCS_GUEST_CR3, env->cr[3]);
    vmcs_put(env, VMCS_GUEST_CR4, env->cr[4]);
    if (exit & EXIT_SAVE_DEBUG) {
        vmcs_put(env, VMCS_GUEST_DR7, env->dr[7]);
        vmcs_put(env, VMCS_GUEST_DEBUGCTL, env->msr_debugctl);
    }
    if (exit & EXIT_SAVE_PAT) {
        vmcs_put(env, VMCS_GUEST_PAT, env->pat);
    }
    if (exit & EXIT_SAVE_EFER) {
        vmcs_put(env, VMCS_GUEST_EFER, env->efer);
    }
    vmcs_put(env, VMCS_GUEST_SYSENTER_CS, env->sysenter_cs);
    vmcs_put(env, VMCS_GUEST_SYSENTER_ESP, env->sysenter_esp);
    vmcs_put(env, VMCS_GUEST_SYSENTER_EIP, env->sysenter_eip);
    /* 30.3.2 segments: the unusable bit from the model's "P = 0"; bits 31:17, 11:8 cleared */
    for (i = 0; i < 8; i++) {
        const SegmentCache *sc = i < 6 ? &env->segs[i] : (i == 6 ? &env->ldt : &env->tr);
        bool unusable = i == 7 ? false : !(sc->flags & DESC_P_MASK);
        uint64_t base = sc->base;

        if (unusable && (i == R_SS || i == R_DS || i == R_ES)) {
            base = (uint32_t)base;
        }
        vmcs_put(env, VMCS_GUEST_ES_SEL + 2 * i, sc->selector);
        vmcs_put(env, VMCS_GUEST_ES_BASE + 2 * i, base);
        vmcs_put(env, VMCS_GUEST_ES_LIMIT + 2 * i, sc->limit);
        vmcs_put(env, VMCS_GUEST_ES_AR + 2 * i, vmx_flags_to_ar(sc, unusable));
    }
    vmcs_put(env, VMCS_GUEST_ES_BASE + 16, env->gdt.base);
    vmcs_put(env, VMCS_GUEST_ES_LIMIT + 16, env->gdt.limit);
    vmcs_put(env, VMCS_GUEST_ES_BASE + 18, env->idt.base);
    vmcs_put(env, VMCS_GUEST_ES_LIMIT + 18, env->idt.limit);
    /* 30.3.3 */
    vmcs_put(env, VMCS_GUEST_RIP, env->eip);
    vmcs_put(env, VMCS_GUEST_RSP, env->regs[R_ESP]);
    if (e->rf_zero) {
        rflags &= ~(uint64_t)RF_MASK;
    }
    vmcs_put(env, VMCS_GUEST_RFLAGS, rflags);
    /* 30.3.4 non-register state */
    if (env_cpu(env)->halted) {
        act = 1;
    }
    vmcs_put(env, VMCS_GUEST_ACTIVITY, act);
    if (env->hflags & HF_INHIBIT_IRQ_MASK) {
        irs |= env->fred_sti ? 1 : 2;
    }
    if (env->vmx.pin & PIN_VIRTUAL_NMI) {
        irs |= env->vmx.vnmi_blocked ? 8 : 0;
    } else if (env->hflags2 & HF2_NMI_MASK) {
        irs |= 8;
    }
    vmcs_put(env, VMCS_GUEST_INTERRUPTIBILITY, irs);
    vmcs_put(env, VMCS_GUEST_PENDING_DBG, 0);
    if (exit & EXIT_SAVE_TIMER) {
        vmcs_put(env, VMCS_PREEMPT_TIMER, vmx_timer_value(env));
    }
}

/*
 * The VM exit itself (30.1-30.6). The caller has put the logical processor's state at the point of
 * the exit (RIP of the instruction, of the interrupted boundary, ...).
 */
static G_NORETURN void vmx_vmexit(CPUX86State *env, VmxExit *e)
{
    CPUState *cs = env_cpu(env);
    uint32_t cnt, bad;

    /* 30.2: exit information; the valid bit of the injected-event identification; IA-32e guest */
    vmcs_put(env, VMCS_EXIT_REASON, e->reason);
    vmcs_put(env, VMCS_EXIT_QUAL, e->qual);
    if (e->gla_valid) {
        vmcs_put(env, VMCS_GUEST_LINEAR, e->gla);
    }
    if (e->gpa_valid) {
        vmcs_put(env, VMCS_GPA, e->gpa);
    }
    vmcs_put(env, VMCS_EXIT_INTR_INFO, e->intr_info);
    if (e->intr_info & (1u << 11)) {
        vmcs_put(env, VMCS_EXIT_INTR_ERR, e->intr_err);
    }
    if (env->vmx.delivering) {
        vmcs_put(env, VMCS_IDTV_INFO, env->vmx.idtv_info);
        if (env->vmx.idtv_info & (1u << 11)) {
            vmcs_put(env, VMCS_IDTV_ERR, env->vmx.idtv_err);
        }
    } else {
        vmcs_put(env, VMCS_IDTV_INFO, 0);
    }
    if (e->len_valid) {
        vmcs_put(env, VMCS_EXIT_INSN_LEN, e->insn_len);
    }
    if (e->info_valid) {
        vmcs_put(env, VMCS_EXIT_INSN_INFO, e->insn_info);
    }
    vmcs_put(env, VMCS_ENTRY_INTR_INFO, vmcs_get(env, VMCS_ENTRY_INTR_INFO) & ~(1u << 31));
    vmcs_put(env, VMCS_ENTRY_CTLS, (vmcs_get(env, VMCS_ENTRY_CTLS) & ~ENTRY_IA32E) |
                                   (vmx_long_mode(env) ? ENTRY_IA32E : 0));
    /* 30.3 */
    vmx_save_guest(env, e);
    /* 30.4 */
    cnt = vmcs_get(env, VMCS_EXIT_MSR_STORE_CNT);
    if (!vmx_store_msr_list(env, vmcs_get(env, VMCS_EXIT_MSR_STORE), cnt)) {
        vmx_abort(env, 1);
    }
    /* 30.5: IA-32e mode with "host address-space size" 0 is a VMX abort (6) */
    if (vmx_long_mode(env) && !(env->vmx.exit_ctls & EXIT_HOST_64)) {
        vmx_abort(env, 6);
    }
    vmx_load_host(env);
    /* 30.6 */
    cnt = vmcs_get(env, VMCS_EXIT_MSR_LOAD_CNT);
    bad = vmx_load_msr_list(env, vmcs_get(env, VMCS_EXIT_MSR_LOAD), cnt);
    if (bad) {
        vmx_abort(env, 4);
    }
    cs->exception_index = -1;
    env->old_exception = -1;
    cpu_loop_exit(cs);
}

/*
 * A fault-like VM exit for an instruction (28.1): the state is restored to the instruction's start
 * (its length from the translator, insn_start word 2), RFLAGS.RF saved as 0 (30.3.3)
 */
G_NORETURN void x86_vmx_insn_exit(CPUX86State *env, uint32_t reason, uint64_t qual,
                                  uint32_t info, bool info_valid, uintptr_t ra)
{
    VmxExit e;

    if (ra) {
        cpu_restore_state(env_cpu(env), ra, true);
    }
    vmx_exit_init(&e, reason, qual);
    e.len_valid = true;
    e.insn_len = env->vmx.insn_len;
    e.info_valid = info_valid;
    e.insn_info = info;
    e.rf_zero = true;
    vmx_vmexit(env, &e);
}

/* ---- 29.8 VM-entry failure after the guest-state checks ---- */
static G_NORETURN void vmx_entry_fail(CPUX86State *env, uint32_t reason, uint64_t qual)
{
    CPUState *cs = env_cpu(env);
    uint32_t bad;

    vmcs_put(env, VMCS_EXIT_REASON, reason | EXIT_REASON_ENTRY_FAIL);
    vmcs_put(env, VMCS_EXIT_QUAL, qual);
    vmx_load_host(env);
    bad = vmx_load_msr_list(env, vmcs_get(env, VMCS_EXIT_MSR_LOAD),
                            vmcs_get(env, VMCS_EXIT_MSR_LOAD_CNT));
    if (bad) {
        vmx_abort(env, 4);
    }
    cs->exception_index = -1;
    cpu_loop_exit(cs);
}

/*
 * ---- 29.6 event injection ----
 * The event is delivered in the guest context as the model delivers events of that kind
 * (UC_CTL_X86_EVENT_DELIVERY: through the guest IDT, or to UC_HOOK_INTR); a pending MTF VM exit
 * (type 7, vector 0) is made pending. Returns only if nothing is delivered.
 */
static void vmx_inject(CPUX86State *env, uint32_t info)
{
    CPUState *cs = env_cpu(env);
    uint32_t type = (info >> 8) & 7, vec = info & 0xff;
    uint32_t len = vmcs_get(env, VMCS_ENTRY_INSN_LEN);
    uint32_t ec = vmcs_get(env, VMCS_ENTRY_EXC_ERR);

    if (type == 7) {
        env->vmx.mtf_pending = 1;       /* 29.6.2 */
        return;
    }
    /* 29.7.1: no blocking by STI or MOV SS after an injecting VM entry; NMI blocking */
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    if (type == 2) {
        if (env->vmx.pin & PIN_VIRTUAL_NMI) {
            env->vmx.vnmi_blocked = 1;
        } else {
            env->hflags2 |= HF2_NMI_MASK;
        }
    }
    /* the original event if its delivery causes a VM exit (29.6.1.2, 30.2.4) */
    env->vmx.delivering = 1;
    env->vmx.idtv_info = info & ~(1u << 12);
    env->vmx.idtv_err = ec;
    env->vmx.idtv_len = len;
    env->vmx.inject_type = type;
    /* 29.6.1.1: the error code (bit 11) and the RIP pushed (+ length for software events) */
    env->vmx.inj_active = 1;
    env->vmx.inj_has_ec = (info >> 11) & 1;
    env->vmx.inj_ec = ec;
    env->vmx.inj_push_rip = env->eip + ((type == 4 || type == 5 || type == 6) ? len : 0);
    env->old_exception = -1;
    if (type == 0 || type == 2) {
        /* an external interrupt / NMI: delivered as the local APIC delivers one (docs/apic.md) */
        x86_apic_deliver_event(env, vec);
        cs->exception_index = -1;
        cpu_loop_exit(cs);
    }
    cs->exception_index = vec;
    env->error_code = env->vmx.inj_has_ec ? (int)ec : 0;
    /* INT n / INT3 / INTO: the DPL check of software events (29.6.1.1); INT1 (type 5): none */
    env->exception_is_int = (type == 4 || type == 6) ? 1 : 0;
    env->exception_next_eip = env->vmx.inj_push_rip;
    x86_vmx_record_event(env, vec, env->exception_is_int, env->error_code);
    cpu_loop_exit(cs);
}

/*
 * ---- VMLAUNCH / VMRESUME (Vol3C 33.3 pp. 33-14 to 33-16, chapter 29) ----
 * next_len: the instruction's length (a VMfail continues with the next instruction).
 */
void helper_vmlaunch(CPUX86State *env, uint32_t launch, uint32_t next_len)
{
    uintptr_t ra = GETPC();
    uint32_t launch_state, info, bad, proc2;
    int q;

    if (!env->vmx.on || vmx_bad_mode(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (env->vmx.nonroot) {
        x86_vmx_insn_exit(env, launch ? EXIT_REASON_VMLAUNCH : EXIT_REASON_VMRESUME, 0, 0, false,
                          ra);
    }
    if (vmx_cpl(env)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    env->eip += next_len;               /* a VMfail passes to the next instruction */
    /* 29.1 basic checks */
    if (env->vmx.vmcs_ptr == VMX_NO_VMCS || (vmx_ldl(env, env->vmx.vmcs_ptr) >> 31)) {
        vmx_fail_invalid(env);
        return;
    }
    if ((env->hflags & HF_INHIBIT_IRQ_MASK) && !env->fred_sti) {
        vmx_fail_valid(env, VMXERR_ENTRY_MOVSS);
        return;
    }
    launch_state = vmx_ldl(env, env->vmx.vmcs_ptr + VMCS_LAUNCH_OFFSET);
    if (launch && launch_state != 0) {
        vmx_fail_valid(env, VMXERR_VMLAUNCH_NONCLEAR);
        return;
    }
    if (!launch && launch_state != 1) {
        vmx_fail_valid(env, VMXERR_VMRESUME_NONLAUNCHED);
        return;
    }
    /* 29.2 controls and host state */
    if (!vmx_check_controls(env)) {
        vmx_fail_valid(env, VMXERR_ENTRY_CTLS);
        return;
    }
    if (!vmx_check_host(env)) {
        vmx_fail_valid(env, VMXERR_ENTRY_HOST);
        return;
    }
    vmx_load_controls(env);
    /* 29.3 guest state (checked before anything is loaded: an order 29.3 allows) */
    proc2 = env->vmx.proc2;
    q = vmx_check_guest(env, proc2);
    if (q != VMX_GUEST_OK) {
        vmx_entry_fail(env, EXIT_REASON_INVALID_GUEST, q ? q - 1 : 0);
    }
    vmx_load_guest(env);
    env->vmx.nonroot = 1;
    env->hflags |= HF_GUEST_MASK;
    env->vmx.delivering = 0;
    env->vmx.mtf_pending = 0;
    tlb_flush(env_cpu(env));
    /* 29.4 MSR loading */
    bad = vmx_load_msr_list(env, vmcs_get(env, VMCS_ENTRY_MSR_LOAD),
                            vmcs_get(env, VMCS_ENTRY_MSR_LOAD_CNT));
    if (bad) {
        env->vmx.nonroot = 0;
        env->hflags &= ~HF_GUEST_MASK;
        vmx_entry_fail(env, EXIT_REASON_MSR_LOADING, bad);
    }
    /* step 5: launch state */
    if (launch) {
        vmx_stl(env, env->vmx.vmcs_ptr + VMCS_LAUNCH_OFFSET, 1);
    }
    /* 29.7.2 activity state (HLT unless injecting) */
    info = vmcs_get(env, VMCS_ENTRY_INTR_INFO);
    if (!(info >> 31) || ((info >> 8) & 7) == 7) {
        env_cpu(env)->halted = vmcs_get(env, VMCS_GUEST_ACTIVITY) == 1;
    }
    /* 29.7.4 the VMX-preemption timer */
    if (env->vmx.pin & PIN_PREEMPT_TIMER) {
        env->vmx.timer_on = 1;
        env->vmx.timer_deadline = (vmx_tsc(env) >> VMX_TIMER_RATE) +
                                  (uint32_t)vmcs_get(env, VMCS_PREEMPT_TIMER);
    }
    /* the monitor trap flag needs one instruction per TB (translate.c): retranslate */
    if (env->vmx.proc & PROC_MTF) {
        tb_flush(env_cpu(env));
    }
    /* 28.5.2: an injected vectored event with "monitor trap flag" makes an MTF VM exit pending */
    if ((info >> 31) && ((info >> 8) & 7) != 7 && (env->vmx.proc & PROC_MTF)) {
        env->vmx.mtf_pending = 1;
    }
    /* 29.6 event injection */
    if (info >> 31) {
        vmx_inject(env, info);
    }
    /* no injection: events due right after VM entry are evaluated at the first boundary */
    env_cpu(env)->exception_index = -1;
    cpu_loop_exit(env_cpu(env));
}

/*
 * The VMX events of an instruction boundary in VMX non-root operation (28.2, 28.5.1, 28.5.2, 29.7):
 * called by the translator at the start of every TB of non-root code (a boundary; each TB is one
 * instruction while the monitor trap flag is set). Priority (28.2 / 29.7): pending MTF VM exit (higher
 * than debug traps, lower than SMI / INIT), VMX-preemption timer (above NMI window), NMI window,
 * interrupt window (below NMI, above external interrupts).
 */
void helper_vmx_boundary(CPUX86State *env)
{
    VmxExit e;
    CPUState *cs = env_cpu(env);

    if (!env->vmx.nonroot) {
        return;
    }
    if (env->vmx.mtf_pending) {
        env->vmx.mtf_pending = 0;
        vmx_exit_init(&e, EXIT_REASON_MTF, 0);
        vmx_vmexit(env, &e);
    }
    if (env->vmx.timer_on && vmx_timer_value(env) == 0) {
        vmx_exit_init(&e, EXIT_REASON_PREEMPT_TIMER, 0);
        env->vmx.timer_on = 1;
        vmx_vmexit(env, &e);
    }
    if ((env->vmx.proc & PROC_NMI_WINDOW) && !env->vmx.vnmi_blocked &&
        !(env->hflags & HF_INHIBIT_IRQ_MASK)) {
        vmx_exit_init(&e, EXIT_REASON_NMI_WINDOW, 0);
        vmx_vmexit(env, &e);
    }
    if ((env->vmx.proc & PROC_INTR_WINDOW) && (env->eflags & IF_MASK) &&
        !(env->hflags & HF_INHIBIT_IRQ_MASK)) {
        vmx_exit_init(&e, EXIT_REASON_INTR_WINDOW, 0);
        vmx_vmexit(env, &e);
    }
    /* the monitor trap flag: an MTF VM exit is pending after this instruction (28.5.2) */
    if (env->vmx.proc & PROC_MTF) {
        env->vmx.mtf_pending = 1;
    }
    (void)cs;
}

/* VMCALL / VMXON / ... in VMX non-root operation: unconditional VM exits (28.1.2) */
void x86_vmx_check_insn_exit(CPUX86State *env, uint32_t op, uint32_t info, uint64_t qual,
                             uintptr_t ra)
{
    static const uint8_t reason[] = {
        [VMX_OP_VMXON] = EXIT_REASON_VMXON, [VMX_OP_VMXOFF] = EXIT_REASON_VMXOFF,
        [VMX_OP_VMCLEAR] = EXIT_REASON_VMCLEAR, [VMX_OP_VMPTRLD] = EXIT_REASON_VMPTRLD,
        [VMX_OP_VMPTRST] = EXIT_REASON_VMPTRST, [VMX_OP_VMCALL] = EXIT_REASON_VMCALL,
    };
    bool mem = op == VMX_OP_VMXON || op == VMX_OP_VMCLEAR || op == VMX_OP_VMPTRLD ||
               op == VMX_OP_VMPTRST;

    if (env->vmx.nonroot) {
        x86_vmx_insn_exit(env, reason[op], mem ? qual : 0, info, mem, ra);
    }
}

/*
 * VMFUNC (Vol3C 33.3 p. 33-13, 28.5.7): #UD outside VMX non-root operation, with "enable VM
 * functions" 0 or EAX >= 64; a function not enabled: VM exit 59; EPTP switching (function 0).
 */
void helper_vmfunc(CPUX86State *env)
{
    uintptr_t ra = GETPC();
    uint32_t eax = (uint32_t)env->regs[R_EAX];
    uint32_t ecx = (uint32_t)env->regs[R_ECX];
    uint64_t eptp;

    if (!env->vmx.nonroot || !(env->vmx.proc2 & PROC2_VMFUNC) || eax >= 64) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (!((env->vmx.vmfunc >> eax) & 1)) {
        x86_vmx_insn_exit(env, EXIT_REASON_VMFUNC, 0, 0, false, ra);
    }
    /* 28.5.7.3 EPTP switching */
    if (ecx >= 512) {
        x86_vmx_insn_exit(env, EXIT_REASON_VMFUNC, 0, 0, false, ra);
    }
    eptp = vmx_ldq(env, env->vmx.eptp_list + 8 * (uint64_t)ecx);
    if (!x86_vmx_eptp_ok(env, eptp) || ((eptp ^ env->vmx.eptp) & 0x38)) {
        x86_vmx_insn_exit(env, EXIT_REASON_VMFUNC, 0, 0, false, ra);
    }
    env->vmx.eptp = eptp;
    vmcs_put(env, VMCS_EPTP, eptp);
    tlb_flush(env_cpu(env));            /* new EPT paging structures; no flags modified */
}

/* an instruction that exits unconditionally in VMX non-root operation (CPUID, GETSEC, INVD, XSETBV) */
void x86_vmx_uncond_exit(CPUX86State *env, uint32_t reason, uintptr_t ra)
{
    if (env->vmx.nonroot) {
        x86_vmx_insn_exit(env, reason, 0, 0, false, ra);
    }
}

void helper_vmx_uncond_exit(CPUX86State *env, uint32_t reason)
{
    x86_vmx_uncond_exit(env, reason, GETPC());
}

/*
 * 28.2 triple fault in VMX non-root operation: a VM exit (2) instead of shutdown; the saved RIP and
 * RFLAGS are those the double fault would have pushed (30.3.3), i.e. the current state
 */
G_NORETURN void x86_vmx_triple_fault(CPUX86State *env)
{
    VmxExit e;

    vmx_exit_init(&e, EXIT_REASON_TRIPLE_FAULT, 0);
    env->vmx.delivering = 0;
    vmx_vmexit(env, &e);
}

#endif /* __Use_Original_Qemu (U1301) */
