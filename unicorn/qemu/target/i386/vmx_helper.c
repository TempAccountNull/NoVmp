/*
 * VMX (NoVmp, ledgers U1300-U1399; design and requirement mapping: docs/vmx.md).
 *
 * Intel SDM 325462-092 Vol3C chapters 26-33 (VMX) and Vol3D Appendices A (capability reporting),
 * B (VMCS field encodings) and C (basic exit reasons), compared with SDM 093 where change document
 * 252046-085 lists changes. Opt-in UC_X86_FEATURE_VMX (default off).
 *
 * U1304: the conditional VM exits of instructions (28.1.3) and the changed instruction behaviour in
 * VMX non-root operation (28.3): CR / DR accesses with guest/host masks and read shadows, I/O and
 * MSR bitmaps, TSC offsetting and scaling, the enable controls (#UD) of RDTSCP / INVPCID / XSAVES /
 * user wait / PCONFIG / MSR lists.
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
#include "qemu/host-utils.h"
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

#define VMX_PROC2_ALLOWED1      0x0e557bffu                 /* A.3.3, docs/vmx.md (U1324: + 18) */

#define VMX_EXIT_DEFAULT1       0x00036dffu                 /* A.4.1: 0-8, 10, 11, 13, 14, 16, 17 */
#define VMX_EXIT_ALLOWED1       0x007fefffu                 /* + 9, 15, 18-22 (U1317: not 12) */
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
    VMXF_FRED,                  /* U1309: "exists only on processors that support FRED transitions" */
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
    FC(0x2052, VMXF_FRED, 0),           /* injected-event data (FRED; U1309) */
    /* B.2.2 64-bit read-only data fields (Table B-5) */
    FC(0x2400, VMXF_PROC2, 1),          /* guest-physical address */
    FC(0x2402, VMXF_PROC3, 6),          /* MSR data */
    FC(0x2404, VMXF_FRED, 0),           /* original-event data (FRED; U1309) */
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
#if __Use_Original_Qemu != 1 /* ours (U1331) */
    /*
     * U1331: "SEAM guest-physical address width" (5) with UC_X86_FEATURE_SEAM: the bit enumerates
     * SEAM and its instructions (343754-003 ch. 9 p. 9-1; Vol3C 35.1.3 p. 35-2, Table 27-8 p. 27-14).
     * The fields SEAM-guest KeyID (4026H) and SEAM shared EPT pointer (203CH) exist with it (Vol3D
     * Appendix B footnotes); the control and the fields are ignored outside SEAM (Vol3C 27.6.26
     * p. 27-22) - Stage A never enters SEAM (docs/tdx.md).
     */
    if (x86_seam_enumerated(env)) {
        v |= 1ull << 5;
    }
#endif /* __Use_Original_Qemu (U1331) */
    return v;
}

/* the allowed 1-settings of a control register of the capability set */
/*
 * U1309: with FRED transitions (CPUID.(07H,1):EAX.FRED as reported) the capability set adds "load FRED"
 * (VM-entry bit 23), "activate secondary controls" (VM-exit bit 31) with the secondary VM-exit controls
 * "save FRED" (bit 0) and "load FRED" (bit 1), and VMX nested-exception support (IA32_VMX_BASIC[58]:
 * "Any processor that supports FRED transitions will provide VMX nested-exception support", Vol3D A.1)
 */
static bool vmx_fred(CPUX86State *env)
{
    return x86_fred_cpuid(env);
}

/*
 * U1313: "enable ENCLS exiting" (secondary bit 15, Vol3C 27.6.2 Table 27-7, 27.6.16) is supported when
 * the CPU reports SGX (CPUID.07H.00H:EBX.SGX[2]); without it the ENCLS-exiting bitmap field does not
 * exist (Vol3D Appendix B footnote 13)
 */
static bool x86_vmx_sgx(CPUX86State *env);

static uint32_t vmx_proc2_allowed1(CPUX86State *env)
{
    return VMX_PROC2_ALLOWED1 | (x86_vmx_sgx(env) ? 1u << 15 : 0);
}

static uint32_t vmx_exit_allowed1(CPUX86State *env)
{
    return VMX_EXIT_ALLOWED1 | (vmx_fred(env) ? 1u << 31 : 0);
}

static uint32_t vmx_entry_allowed1(CPUX86State *env)
{
#if __Use_Original_Qemu != 1 /* ours (U1331) */
    /*
     * U1331: "allow SEAM-guest telemetry" (VM-entry bit 25, Vol3C Table 27-15 p. 27-25): "Any processor
     * that supports SEAM also supports the SEAM-specific VMCS fields and controls" (Vol3C 35.1.3
     * p. 35-2; 343754-003 1.1.3 p. 1-3); it acts only on VM entry to SEAM non-root operation
     */
    if (x86_seam_enumerated(env)) {
        return VMX_ENTRY_ALLOWED1 | (vmx_fred(env) ? 1u << 23 : 0) | (1u << 25);
    }
#endif /* __Use_Original_Qemu (U1331) */
    return VMX_ENTRY_ALLOWED1 | (vmx_fred(env) ? 1u << 23 : 0);
}

static uint64_t x86_vmx_allowed1(CPUX86State *env, int reg)
{
    switch (reg) {
    case VMXF_PIN:
        return VMX_PIN_ALLOWED1;
    case VMXF_PROC:
        return VMX_PROC_ALLOWED1;
    case VMXF_PROC2:
        return vmx_proc2_allowed1(env);
    case VMXF_PROC3:
        return vmx_proc3_allowed1(env);
    case VMXF_EXIT:
        return vmx_exit_allowed1(env);
    case VMXF_EXIT2:
        return vmx_fred(env) ? 3 : 0;   /* save FRED, load FRED (U1309); else no EXIT_CTLS2 */
    case VMXF_ENTRY:
        return vmx_entry_allowed1(env);
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
    if (c == VMXF_FRED) {
        return vmx_fred(env);
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
               (1ull << 54) | (1ull << 55) | (1ull << 56) |
               (vmx_fred(env) ? 1ull << 58 : 0);           /* U1309: nested-exception support */
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
        *val = ((uint64_t)vmx_exit_allowed1(env) << 32) | VMX_EXIT_DEFAULT1;
        break;
    case MSR_IA32_VMX_TRUE_EXIT_CTLS:
        *val = ((uint64_t)vmx_exit_allowed1(env) << 32) | VMX_EXIT_TRUE0;
        break;
    case MSR_IA32_VMX_ENTRY_CTLS:
        *val = ((uint64_t)vmx_entry_allowed1(env) << 32) | VMX_ENTRY_DEFAULT1;
        break;
    case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
        *val = ((uint64_t)vmx_entry_allowed1(env) << 32) | VMX_ENTRY_TRUE0;
        break;
    case MSR_IA32_VMX_EXIT_CTLS2_NV:
        *val = x86_vmx_allowed1(env, VMXF_EXIT2);    /* A.4.2: the allowed 1-settings (U1309) */
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
        *val = (uint64_t)vmx_proc2_allowed1(env) << 32;     /* A.3.3: allowed 0-settings 0 */
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
        /*
         * U1304: "unrestricted guest" in VMX non-root operation: PE / PG are not checked against
         * the fixed bits, but PE = 0 with PG = 1, or PG = 1 with CR4.PAE = 0 and IA32_EFER.LME = 1,
         * is still #GP (28.3 MOV to CR0, pp. 28-9 / 28-10)
         */
        if (env->vmx.nonroot && (env->vmx.proc2 & (1u << 7) /* unrestricted guest */)) {
            if ((v & CR0_PG_MASK) && (!(v & CR0_PE_MASK) ||
                (!(env->cr[4] & CR4_PAE_MASK) && (env->efer & MSR_EFER_LME)))) {
                return false;
            }
            return vmx_cr0_ok(env, v, true);
        }
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
#define VMCS_EPTP_INDEX         0x0004      /* U1324 */
#define VMCS_VE_INFO            0x202a      /* U1324: virtualization-exception information address */
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
#define PROC2_ENCLS             (1u << 15)  /* U1313 */
#define PROC2_EPT_VE            (1u << 18)  /* U1324: EPT-violation #VE */
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
    bool block_nmi;             /* U1305: a VM exit caused by an NMI blocks NMIs (30.5) */
    bool rf_one;                /* U1306: RFLAGS.RF saved as 1 (EPT exits outside delivery, 30.3.3) */
    bool enc;                   /* U1314: interruptibility bit 4 "enclave interruption" (30.3.4) */
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

/*
 * CR3 bits reserved for the Intel 64 architecture: 63:MAXPHYADDR (29.2.2 p. 29-7, 29.3.1.1 p. 29-9:
 * "bits reserved in CR3 are 0"; 'mpa' = the MAXPHYADDR of the check). U1319: with LAM enumerated
 * (CPUID.(07H,1):EAX.LAM[26]) bits 62:61 - LAM_U48 / LAM_U57 - are not reserved (Vol3A 2.5 Figure 2-7
 * p. 2-15, Figure 5-11 note 2 p. 5-32)
 */
static bool vmx_cr3_ok(CPUX86State *env, uint64_t cr3, int mpa)
{
    uint64_t rsvd = ~0ull << mpa;

    if (x86_lam_cpuid(env)) {
        rsvd &= ~(CR3_LAM_U57_MASK | CR3_LAM_U48_MASK);
    }
    return !(cr3 & rsvd);
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
    v->exit2_ctls = (v->exit_ctls & (1u << 31)) ? vmcs_get(env, 0x2044) : 0;     /* U1309 */
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
    v->encls_bmp = (v->proc2 & PROC2_ENCLS) ? vmcs_get(env, 0x202e) : 0;   /* U1313 */
    v->link_ptr = vmcs_get(env, VMCS_LINK_PTR);
    v->pi_nv = vmcs_get(env, VMCS_PI_NV);
    v->pi_desc = vmcs_get(env, VMCS_PI_DESC);
    for (i = 0; i < 4; i++) {
        v->eoi_exit[i] = vmcs_get(env, VMCS_EOI_EXIT0 + 2 * i);
    }
    v->ve_info = (v->proc2 & PROC2_EPT_VE) ? vmcs_get(env, VMCS_VE_INFO) : 0; /* U1324 */
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
        !vmx_ctl_ok(proc2, 0, vmx_proc2_allowed1(env)) ||
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
    /* U1324: 29.2.1.1 - the virtualization-exception information address (202AH) */
    if ((proc2 & PROC2_EPT_VE) && !vmx_page_ok(env, vmcs_get(env, VMCS_VE_INFO))) {
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
    /* 29.2.1.2 VM-exit control fields (U1309: the secondary VM-exit controls, A.4.2) */
    if (!vmx_ctl_ok(exit, VMX_EXIT_TRUE0, vmx_exit_allowed1(env))) {
        return false;
    }
    if ((exit & (1u << 31)) && (vmcs_get(env, 0x2044) & ~x86_vmx_allowed1(env, VMXF_EXIT2))) {
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
    if (!vmx_ctl_ok(entry, VMX_ENTRY_TRUE0, vmx_entry_allowed1(env))) {
        return false;
    }
    info = vmcs_get(env, VMCS_ENTRY_INTR_INFO);
    if (info >> 31) {
        type = (info >> 8) & 7;
        vec = info & 0xff;
        if (type == 1) {
            return false;
        }
        /* type 7: vector 0 (pending MTF VM exit); U1309: 1 / 2 (SYSCALL / SYSENTER) when FRED
           transitions would be enabled after VM entry (guest CR4.FRED) */
        if ((type == 2 && vec != 2) || (type == 3 && vec > 31) ||
            (type == 7 && vec != 0 &&
             !((vec == 1 || vec == 2) && vmx_fred(env) &&
               (vmcs_get(env, VMCS_GUEST_CR4) & CR4_FRED_MASK)))) {
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
        /* bit 13 (nested exception): only for hardware exceptions with IA32_VMX_BASIC[58] = 1
           (FRED, U1309); reserved 30:14 and 12 */
        if ((info & 0x7fffc000u) || (info & (1u << 12)) ||
            ((info & (1u << 13)) && (type != 3 || !vmx_fred(env)))) {
            return false;
        }
        len = vmcs_get(env, VMCS_ENTRY_INSN_LEN);
        /* U1322: also other event (7) with vector 1 (SYSCALL) / 2 (SYSENTER) (29.2.1.3 p. 29-6) */
        if ((type == 4 || type == 5 || type == 6 || (type == 7 && (vec == 1 || vec == 2))) &&
            len > 15) {
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
/*
 * U1309: the FRED MSR fields at 'base' (CONFIG, RSP1-3, STKLVLS, SSP1-3; 29.2.2 / 29.3.1.1):
 * IA32_FRED_CONFIG bits 2, 5:4 and 11 zero and the value CPU canonical; RSP1-3 CPU canonical with bits
 * 5:0 zero; SSP1-3 CPU canonical with bits 2:0 zero
 */
static bool vmx_fred_fields_ok(CPUX86State *env, uint32_t base)
{
    uint64_t cfg = vmcs_get(env, base);
    int i;

    if ((cfg & 0x834) || !x86_vmx_canonical(env, cfg)) {
        return false;
    }
    for (i = 1; i <= 3; i++) {
        uint64_t rsp = vmcs_get(env, base + 2 * i), ssp = vmcs_get(env, base + 8 + 2 * i);

        if ((rsp & 0x3f) || !x86_vmx_canonical(env, rsp) || (ssp & 7) ||
            !x86_vmx_canonical(env, ssp)) {
            return false;
        }
    }
    return true;
}

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
    if (!vmx_cr3_ok(env, vmcs_get(env, VMCS_HOST_CR3), vmx_maxphyaddr(env))) {
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
    /* U1309: "load FRED" VM-exit control: the host FRED fields (29.2.2) */
    if ((exit & (1u << 31)) && (vmcs_get(env, 0x2044) & 2) && !vmx_fred_fields_ok(env, 0x2c08)) {
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
/*
 * A PDPTE as MOV to CR3 checks it (29.3.1.6 pp. 29-15 / 29-16, 30.5.4; Vol3A Table 5-8 p. 5-15): reserved
 * bits 2:1, 8:5, 63:'mpa'. U1328: 'mpa' is the paging MAXPHYADDR - reduced by
 * IA32_TME_ACTIVATE[39:36] (vmx_maxphyaddr) for a guest without EPT and for the host, the full width
 * for guest-physical addresses under EPT (Vol3A 5.1.4 p. 5-6)
 */
static bool vmx_pdpte_ok(CPUX86State *env, uint64_t e, int mpa)
{
    uint64_t rsvd = 0x1e6ull | ~((1ull << mpa) - 1);

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
    /* U1319: the TME reduction of MAXPHYADDR applies to the guest CR3 only with "enable EPT" 0 */
    if (!vmx_cr3_ok(env, vmcs_get(env, VMCS_GUEST_CR3),
                    (proc2 & PROC2_EPT) ? env_archcpu(env)->phys_bits : vmx_maxphyaddr(env))) {
        return 0;
    }
    /* U1309: "load FRED" VM-entry control: the guest FRED fields (29.3.1.1) */
    if ((entry & (1u << 23)) && !vmx_fred_fields_ok(env, 0x281a)) {
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
        /* U1309: a guest that will use FRED transitions (CR4.FRED): SS.DPL 0 (with CS.L = 1) or 3
           (with RFLAGS.IOPL = 0 and no blocking by STI) (29.3.1.2) */
        if (cr4 & CR4_FRED_MASK) {
            int dpl = AR_DPL(ar[R_SS]);

            if ((dpl != 0 && dpl != 3) || (dpl == 0 && !AR_L(ar[R_CS])) ||
                (dpl == 3 && ((vmcs_get(env, VMCS_GUEST_RFLAGS) & IOPL_MASK) ||
                              (vmcs_get(env, VMCS_GUEST_INTERRUPTIBILITY) & 1)))) {
                return 0;
            }
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

            if (!vmx_pdpte_ok(env, e, (proc2 & PROC2_EPT) ? env_archcpu(env)->phys_bits
                                                          : vmx_maxphyaddr(env))) {
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
    /* U1309: "load FRED" VM-entry control (29.3.2.1) */
    if (entry & (1u << 23)) {
        env->fred_config = vmcs_get(env, 0x281a);
        for (i = 1; i <= 3; i++) {
            env->fred_rsp[i] = vmcs_get(env, 0x281a + 2 * i);
            env->fred_ssp[i] = vmcs_get(env, 0x2822 + 2 * i);
        }
        env->fred_stklvls = vmcs_get(env, 0x2822);
    }
    /* U1306: PAE paging with EPT: the PDPTE registers from the guest PDPTE fields (29.3.2.4) */
    env->vmx.pdpte_valid = 0;
    if ((env->vmx.proc2 & PROC2_EPT) && (env->cr[0] & CR0_PG_MASK) &&
        (env->cr[4] & CR4_PAE_MASK) && !(env->efer & MSR_EFER_LMA)) {
        for (i = 0; i < 4; i++) {
            env->vmx.pdpte[i] = vmcs_get(env, VMCS_GUEST_PDPTE0 + 2 * i);
        }
        env->vmx.pdpte_valid = 1;
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
            if (!vmx_pdpte_ok(env, vmx_ldq(env, (cr3 & ~0x1full) + 8 * i), mpa)) {   /* U1328 */
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
    /* U1309: "load FRED" VM-exit control (30.5.2) */
    if (exit & (1u << 31) && (env->vmx.exit2_ctls & 2)) {
        env->fred_config = vmcs_get(env, 0x2c08);
        for (i = 1; i <= 3; i++) {
            env->fred_rsp[i] = vmcs_get(env, 0x2c08 + 2 * i);
            env->fred_ssp[i] = vmcs_get(env, 0x2c10 + 2 * i);
        }
        env->fred_stklvls = vmcs_get(env, 0x2c10);
    }
    env->vmx.mwait_wait = 0;            /* U1304 */
    env->vmx.hlt_wake = 0;              /* U1305 */
    env->vmx.iret_nmi = 0;
    env->vmx.pdbg = 0;                  /* U1310 */
    env->vmx.dbg_trap = 0;              /* U1321 */
    env->vmx.trap_pending = 0;          /* U1307 */
    env->vmx.apic_write_pending = 0;
    env->vmx.apic_va_valid = 0;
    env->vmx.vintr_pending = 0;
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
    if (e->rf_one) {
        rflags |= RF_MASK;
    }
    vmcs_put(env, VMCS_GUEST_RFLAGS, rflags);
    /* 30.3.4 non-register state */
    if ((env_cpu(env)->halted && !env->vmx.mwait_wait) || env->vmx.hlt_wake) {
        act = 1;            /* U1304: waiting after MWAIT is the active state (30.1 footnote 3);
                               U1305: HLT ended by the event causing the VM exit */
    }
    vmcs_put(env, VMCS_GUEST_ACTIVITY, act);
    if (env->hflags & HF_INHIBIT_IRQ_MASK) {
        irs |= env->fred_sti ? 1 : 2;
    }
    if (e->enc) {
        irs |= 16;                      /* U1314: enclave interruption */
    }
    if (env->vmx.pin & PIN_VIRTUAL_NMI) {
        irs |= env->vmx.vnmi_blocked ? 8 : 0;
    } else if (env->hflags2 & HF2_NMI_MASK) {
        irs |= 8;
    }
    vmcs_put(env, VMCS_GUEST_INTERRUPTIBILITY, irs);
    /*
     * U1320: 30.3.4 (pp. 30-27 / 30-28) - the field is saved as 0 except for the VM exits that keep
     * the debug exceptions pending at the exit; of those the model has basic reason "TPR below
     * threshold" (43) and "monitor trap flag" (37), which save the bits of the debug exceptions still
     * pending - those made pending by VM entry and not yet delivered (29.7.3: B3-B0, enabled
     * breakpoint, BS, RTM)
     */
    {
        uint32_t basic = e->reason & 0xffff;

        vmcs_put(env, VMCS_GUEST_PENDING_DBG,
                 (basic == 37 || basic == 43) ?
                 ((env->vmx.pdbg & 0x1500full) | env->vmx.dbg_trap) : 0);   /* U1321: + trap */
    }
    if (exit & EXIT_SAVE_TIMER) {
        vmcs_put(env, VMCS_PREEMPT_TIMER, vmx_timer_value(env));
    }
    /* U1309: "save FRED" VM-exit control (30.3.4 / 30.4) */
    if (env->vmx.exit2_ctls & 1) {
        vmcs_put(env, 0x281a, env->fred_config);
        for (i = 1; i <= 3; i++) {
            vmcs_put(env, 0x281a + 2 * i, env->fred_rsp[i]);
            vmcs_put(env, 0x2822 + 2 * i, env->fred_ssp[i]);
        }
        vmcs_put(env, 0x2822, env->fred_stklvls);
    }
    /* U1307: the guest interrupt status (30.3.4) */
    if (env->vmx.proc2 & PROC2_VID) {
        vmcs_put(env, VMCS_GUEST_INTR_STATUS, env->vmx.rvi | ((uint32_t)env->vmx.svi << 8));
    }
    /* U1306: EPT with PAE paging: the PDPTE registers in use (30.3.4) */
    if ((env->vmx.proc2 & PROC2_EPT) && env->vmx.pdpte_valid && (env->cr[0] & CR0_PG_MASK) &&
        (env->cr[4] & CR4_PAE_MASK) && !(env->efer & MSR_EFER_LMA)) {
        for (i = 0; i < 4; i++) {
            vmcs_put(env, VMCS_GUEST_PDPTE0 + 2 * i, env->vmx.pdpte[i]);
        }
    }
}

/*
 * U1314: Intel SGX and VM exits (SDM 092 Vol3C 30.1 p. 30-3, 30.2.1 pp. 30-4 to 30-11, 30.2.5
 * pp. 30-16 / 30-17, 30.3.3, 30.3.4; Vol3D 42.6 p. 42-4). A VM exit in enclave mode: the AEX first
 * (sgx_model.c; an exception VM exit gives the SSA its vector / error code - a #PF its address -
 * every other VM exit is an exit without exception information), then the VM exit saves the state
 * the AEX left (RIP = AEP, RFLAGS.RF = 0), sets exit-reason bit 27 and interruptibility bit 4, clears
 * the VM-exit instruction length / information and the I/O RCX / RSI / RDI / RIP fields, and bits
 * 11:0 of the #PF and APIC-access qualifications, of the EPT-violation guest-linear address and of
 * the EPT guest-physical address. A VM exit due to or incident to the delivery of an event incident
 * to enclave mode (the AEX before its delivery, or an event injected / made pending by VM entry
 * with interruptibility bit 4: env->sgx_enc_event) sets bit 27 and bit 4 only. Returns true for a
 * VM exit in enclave mode.
 */
static bool vmx_sgx_exit(CPUX86State *env, VmxExit *e)
{
    uint32_t basic = e->reason & 0xffff, type = (e->intr_info >> 8) & 7, vec = e->intr_info & 0xff;
    bool exc = basic == EXIT_REASON_EXCEPTION_NMI && (e->intr_info >> 31) && type == 3;

    e->enc = env->sgx_enc_event != 0;
    env->sgx_enc_event = 0;
    if (!env->sgx_enclave_mode) {
        if (e->enc) {
            e->reason |= 1u << 27;
        }
        return false;
    }
    if (exc) {
        uint64_t cr2 = env->cr[2];

        if (vec == EXCP0E_PAGE) {
            env->cr[2] = e->qual;       /* the faulting address for EXINFO.MADDR (CR2 stays) */
        }
        x86_sgx_aex(env, (int)vec, 0, 0, (e->intr_info & (1u << 11)) ? (int)e->intr_err : 0,
                    env->eip);
        env->cr[2] = cr2;
    } else {
        x86_sgx_aex(env, -1, 0, 1, 0, env->eip);
    }
    env->sgx_enc_event = 0;
    e->enc = true;
    e->reason |= 1u << 27;
    e->rf_zero = true;
    e->rf_one = false;
    e->len_valid = true;
    e->insn_len = 0;
    e->info_valid = true;
    e->insn_info = 0;
    if ((exc && vec == EXCP0E_PAGE) || basic == 44) {   /* APIC access */
        e->qual &= ~0xfffull;
    }
    if (basic == 48) {
        e->gla &= ~0xfffull;
    }
    e->gpa &= ~0xfffull;
    return true;
}

/*
 * The VM exit itself (30.1-30.6). The caller has put the logical processor's state at the point of
 * the exit (RIP of the instruction, of the interrupted boundary, ...).
 */
static G_NORETURN void vmx_vmexit(CPUX86State *env, VmxExit *e)
{
    CPUState *cs = env_cpu(env);
    uint32_t cnt, bad;
    bool enclave = vmx_sgx_exit(env, e);    /* U1314 */

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
    if (env->vmx.delivering && x86_fred_enabled(env)) {
        vmcs_put(env, 0x2404, env->vmx.idtv_data);     /* U1309: original-event data (30.2.4) */
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
    } else if (env->vmx.delivering && ((env->vmx.idtv_info >> 8) & 7) >= 4 &&
               ((env->vmx.idtv_info >> 8) & 7) <= 6) {
        /* U1305: during the delivery of a software interrupt / exception: its length (30.2.5) */
        vmcs_put(env, VMCS_EXIT_INSN_LEN, env->vmx.idtv_len);
    }
    if (e->info_valid) {
        vmcs_put(env, VMCS_EXIT_INSN_INFO, e->insn_info);
    }
    if (enclave) {
        for (cnt = 0; cnt < 4; cnt++) {
            vmcs_put(env, 0x6402 + 2 * cnt, 0);     /* U1314: I/O RCX, RSI, RDI, RIP cleared */
        }
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
    if (e->block_nmi) {
        env->hflags2 |= HF2_NMI_MASK;   /* U1305: after the guest state was saved (30.5) */
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
static G_NORETURN void vmx_insn_exit_e(CPUX86State *env, VmxExit *e, uintptr_t ra)
{
    if (ra) {
        cpu_restore_state(env_cpu(env), ra, true);
    }
    e->len_valid = true;
    e->insn_len = env->vmx.insn_len;
    e->rf_zero = true;
    vmx_vmexit(env, e);
}

G_NORETURN void x86_vmx_insn_exit(CPUX86State *env, uint32_t reason, uint64_t qual,
                                  uint32_t info, bool info_valid, uintptr_t ra)
{
    VmxExit e;

    vmx_exit_init(&e, reason, qual);
    e.info_valid = info_valid;
    e.insn_info = info;
    vmx_insn_exit_e(env, &e, ra);
}

static void vmx_apic_traps(CPUX86State *env);         /* U1307 */
static void vmx_vintr_deliver(CPUX86State *env);
static void vmx_ppr_virt(CPUX86State *env);
static void vmx_eval_vintr(CPUX86State *env);
static bool vmx_posted_intr(CPUX86State *env, int vec);
static uint32_t vapic_rd(CPUX86State *env, uint32_t off);

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

    if (type == 7 && vec == 0) {
        env->vmx.mtf_pending = 1;       /* 29.6.2 */
        return;
    }
    /* U1314: guest interruptibility bit 4 - the event is incident to enclave mode (29.6.1.1, 30.2.1) */
    env->sgx_enc_event = (vmcs_get(env, VMCS_GUEST_INTERRUPTIBILITY) >> 4) & 1;
    /* U1309: FRED event delivery of the injected event (29.6.1.1): its event data, STI blocking
       from the guest interruptibility state, the nested-exception bit */
    env->vmx.inj_data = vmx_fred(env) ? vmcs_get(env, 0x2052) : 0;
    env->vmx.inj_sti = vmcs_get(env, VMCS_GUEST_INTERRUPTIBILITY) & 1;
    env->fred_nested = type == 3 && ((info >> 13) & 1);
    env->fred_kind = type == 6 ? 6 : 3;     /* INT3 / INTO: FRED software exception (type 6) */
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
    /*
     * U1329: a fault during the delivery of an injected hardware exception is handled as if the
     * exception had occurred normally (29.6.1 p. 29-21: a contributory or page-fault-class nested
     * exception may become #DF; footnote 1: #VE is page-fault class); other event types are benign here
     */
    env->old_exception = type == 3 ? (int)vec : -1;
    if (type == 7) {
        /* U1309: SYSCALL (1) / SYSENTER (2) with FRED event delivery: RIP + length pushed */
        env->vmx.idtv_info = info & ~(1u << 12);
        x86_fred_inject_other(env, vec, env->eip + len, (int)len);
        env->sgx_enc_event = 0;         /* U1314 */
        env->vmx.delivering = 0;
        env->vmx.inj_active = 0;
        cs->exception_index = -1;
        cpu_loop_exit(cs);
    }
    if (type == 5 && x86_fred_enabled(env)) {
        env->fred_kind = 5;             /* U1309: FRED privileged software exception */
        env->fred_insn_len = len;
        env->eip += len;                /* x86_fred_event: the event "occurs" at RIP (RIP + len pushed) */
    }
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
        env->vmx.mwait_wait = 0;        /* U1304 */
        env->vmx.hlt_wake = 0;          /* U1305 */
    }
    /*
     * U1310: 29.7.3 - valid pending debug exceptions (BS or enabled breakpoint) are delivered after a VM
     * entry that injects nothing (or a pending MTF VM exit), unless blocked by MOV SS (then "held
     * pending or lost as would normally be the case": the model loses them) and outside shutdown /
     * wait-for-SIPI; an injecting VM entry leaves none (the MOV-SS software-event cases are not modelled)
     */
    env->vmx.pdbg = 0;
    {
        uint64_t pend = vmcs_get(env, VMCS_GUEST_PENDING_DBG);
        uint32_t act = vmcs_get(env, VMCS_GUEST_ACTIVITY);

        if ((!(info >> 31) || ((info >> 8) & 7) == 7) && (pend & ((1u << 14) | (1u << 12))) &&
            !(vmcs_get(env, VMCS_GUEST_INTERRUPTIBILITY) & 2) && act != 2 && act != 3) {
            env->vmx.pdbg = pend | (1ull << 63);    /* bit 63: marks "pending" when B3-B0 / BS = 0 */
            if (vmcs_get(env, VMCS_GUEST_INTERRUPTIBILITY) & 16) {
                env->vmx.pdbg |= 1ull << 62;            /* U1314: incident to enclave mode */
            }
        }
    }
    /* 29.7.4 the VMX-preemption timer */
    if (env->vmx.pin & PIN_PREEMPT_TIMER) {
        env->vmx.timer_on = 1;
        env->vmx.timer_deadline = (vmx_tsc(env) >> VMX_TIMER_RATE) +
                                  (uint32_t)vmcs_get(env, VMCS_PREEMPT_TIMER);
    }
    /* the monitor trap flag needs one instruction per TB (translate.c): retranslate; U1307: so
       does APIC-access virtualization (APIC-write emulation after each instruction) */
    if ((env->vmx.proc & PROC_MTF) || (env->vmx.proc2 & PROC2_APIC_ACCESS)) {
        tb_flush(env_cpu(env));
    }
    /* U1307: 29.3.3 VM entry clears address-range monitoring; APIC virtualization state */
    env->vmx.monitor_armed = 0;
    env->vmx.trap_pending = 0;
    env->vmx.apic_write_pending = 0;
    env->vmx.apic_va_valid = 0;
    env->vmx.vintr_pending = 0;
    if (env->vmx.proc2 & PROC2_VID) {
        /* 29.3.2.5: RVI / SVI from the guest interrupt status, PPR virtualization, evaluation */
        uint32_t gis = vmcs_get(env, VMCS_GUEST_INTR_STATUS);

        env->vmx.rvi = gis & 0xff;
        env->vmx.svi = (gis >> 8) & 0xff;
        vmx_ppr_virt(env);
        vmx_eval_vintr(env);
    } else if ((env->vmx.proc & PROC_TPR_SHADOW) && (env->vmx.proc2 & PROC2_APIC_ACCESS) &&
               (env->vmx.tpr_threshold & 0xf) > ((vapic_rd(env, 0x80) >> 4) & 0xf)) {
        /* 29.7.7: a VM exit right after VM entry (after any injection), before other events */
        env->vmx.trap_pending = 1;
        env->vmx.trap_reason = 43;
        env->vmx.trap_qual = 0;
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
static void vmx_boundary_exits(CPUX86State *env);     /* U1305 */
static bool vmx_intr_window(CPUX86State *env);

/*
 * U1321: a single-step #DB trap at the end of an instruction in VMX non-root operation (bpt_helper.c
 * helper_single_step, before DR6 changes). A pending MTF VM exit has priority over debug traps (Vol3C
 * 28.5.2 p. 28-18, 28.2): the MTF VM exit comes first, RIP after the instruction, and the
 * pending-debug-exceptions field gets BS (30.3.4: RFLAGS.TF = 1 with IA32_DEBUGCTL.BTF = 0 - the model
 * has no BTF); the #DB is not delivered (VM entry with that field delivers it, 29.7.3).
 */
void x86_vmx_single_step(CPUX86State *env)
{
    VmxExit e;

    if (!env->vmx.nonroot) {
        return;
    }
    /*
     * U1325: the APIC-write emulation and the trap-like VM exits of the instruction (TPR below
     * threshold, virtualized EOI, APIC write; 32.1.2 p. 32-2, 32.4.3.2 p. 32-11) belong to the
     * instruction and come before the boundary events - a TPR-below-threshold VM exit saves BS too
     * (30.3.4); then the MTF VM exit (28.5.2)
     */
    env->vmx.dbg_trap = 1ull << 14;
    vmx_apic_traps(env);
    if (env->vmx.mtf_pending) {
        env->vmx.mtf_pending = 0;
        vmx_exit_init(&e, EXIT_REASON_MTF, 0);
        vmx_vmexit(env, &e);
    }
    env->vmx.dbg_trap = 0;
}

void helper_vmx_boundary(CPUX86State *env)
{
    VmxExit e;

    if (!env->vmx.nonroot) {
        return;
    }
    vmx_boundary_exits(env);            /* pending MTF, timer, NMI window (U1305: shared) */
    if (vmx_intr_window(env)) {
        vmx_exit_init(&e, EXIT_REASON_INTR_WINDOW, 0);
        vmx_vmexit(env, &e);
    }
    vmx_vintr_deliver(env);             /* U1307: a recognized virtual interrupt (32.2.2) */
    /*
     * the monitor trap flag: an MTF VM exit is pending after this instruction (28.5.2); U1315: not
     * inside an enclave entered through an opt-out entry (Vol3D EENTER p. 41-93 / ERESUME p. 41-126:
     * "Suppress any MTF VM exits during execution of the enclave")
     */
    if ((env->vmx.proc & PROC_MTF) && !(env->sgx_enclave_mode && !env->sgx_dbgoptin)) {
        env->vmx.mtf_pending = 1;
    }
    env->vmx.hlt_wake = 0;
    env->vmx.iret_nmi = 0;
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
    /* U1324: the CPU supports "EPT-violation #VE": ECX[15:0] -> EPTP index (28.5.7.3) */
    vmcs_put(env, VMCS_EPTP_INDEX, ecx & 0xffff);
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

/*
 * ---- U1304: conditional VM exits of instructions (Vol3C 28.1.3 pp. 28-2 to 28-6) and the changed
 * behaviour of instructions in VMX non-root operation (28.3 pp. 28-8 to 28-15) ----
 * Priority (28.1.1): #UD, faults based on privilege level and the TSS I/O-permission #GP come
 * first (the translator / the instruction helper checks them before calling here); MOV DR exits
 * come before those #GP / CR4.DE #UD faults; every other fault comes after the VM exit. The exit
 * qualifications are Tables 30-3 to 30-5 (30.2.1), the instruction information Tables 30-8 to
 * 30-16 (30.2.5).
 */
#define EXIT_REASON_INVLPG          14
#define EXIT_REASON_CR_ACCESS       28
#define EXIT_REASON_DR_ACCESS       29
#define EXIT_REASON_IO              30
#define EXIT_REASON_MWAIT           36
#define EXIT_REASON_GDTR_IDTR       46
#define EXIT_REASON_LDTR_TR         47
#define EXIT_REASON_RDTSCP          51
#define EXIT_REASON_WBINVD          54
#define EXIT_REASON_RDRAND          57
#define EXIT_REASON_INVPCID         58
#define EXIT_REASON_RDSEED          61
#define EXIT_REASON_XSAVES          63
#define EXIT_REASON_XRSTORS         64
#define EXIT_REASON_PCONFIG         65
#define EXIT_REASON_UMWAIT          67
#define EXIT_REASON_TPAUSE          68
#define EXIT_REASON_LOADIWKEY       69
#define EXIT_REASON_RDMSRLIST       78
#define EXIT_REASON_WRMSRLIST       79
#define EXIT_REASON_URDMSR          80
#define EXIT_REASON_UWRMSR          81

#define PROC3_LOADIWKEY             (1ull << 0)
#define PROC3_MSRLIST               (1ull << 6)
#define VMCS_MSR_DATA               0x2402

static uint8_t vmx_ldub(CPUX86State *env, uint64_t pa)
{
    return x86_ldub_phys(env_cpu(env), pa);
}

/* bits 63:32 cleared outside 64-bit mode (linear addresses in exit qualifications, 30.2.1) */
static uint64_t vmx_la_trunc(CPUX86State *env, uint64_t la)
{
    return (env->hflags & HF_CS64_MASK) ? la : (uint32_t)la;
}

static G_NORETURN void vmx_ud(CPUX86State *env, uintptr_t ra)
{
    raise_exception_ra(env, EXCP06_ILLOP, ra);
}

/*
 * 28.3 RDTSC / RDTSCP / RDMSR 10H: with "use TSC offsetting" the TSC offset is added; with "use TSC
 * scaling" too, the TSC is first multiplied by the TSC multiplier and shifted right 48 bits
 */
uint64_t x86_vmx_guest_tsc(CPUX86State *env, uint64_t tsc)
{
    uint64_t lo, hi;

    if (!env->vmx.nonroot || !(env->vmx.proc & PROC_TSC_OFFSET)) {
        return tsc;
    }
    if (env->vmx.proc2 & PROC2_TSC_SCALING) {
        mulu64(&lo, &hi, tsc, env->vmx.tsc_mult);
        tsc = (hi << 16) | (lo >> 48);
    }
    return tsc + env->vmx.tsc_offset;
}

/* HLT, RDPMC, RDTSC, RDTSCP, MONITOR, PAUSE: an exit by one primary control (28.1.3) */
void x86_vmx_insn_check(CPUX86State *env, uint32_t reason, uintptr_t ra)
{
    uint32_t p = env->vmx.proc, bit;

    if (!env->vmx.nonroot) {
        return;
    }
    switch (reason) {
    case EXIT_REASON_HLT:       bit = PROC_HLT; break;
    case VMX_EXIT_RDPMC:        bit = PROC_RDPMC; break;
    case VMX_EXIT_RDTSC:        bit = PROC_RDTSC; break;
    case EXIT_REASON_RDTSCP:    bit = PROC_RDTSC; break;    /* "enable RDTSCP" checked before */
    case VMX_EXIT_MONITOR:      bit = PROC_MONITOR; break;
    case VMX_EXIT_PAUSE:        bit = PROC_PAUSE; break;    /* PAUSE-loop exiting: not allowed */
    default:                    return;
    }
    if (p & bit) {
        x86_vmx_insn_exit(env, reason, 0, 0, false, ra);
    }
}

/* RDTSCP with "enable RDTSCP" 0: #UD, before any other exception (28.3) */
void x86_vmx_rdtscp_check(CPUX86State *env, uintptr_t ra)
{
    if (env->vmx.nonroot && !(env->vmx.proc2 & PROC2_RDTSCP)) {
        vmx_ud(env, ra);
    }
}

/* MWAIT: the exit qualification says whether address-range monitoring is armed (30.2.1) */
void x86_vmx_mwait_check(CPUX86State *env, uintptr_t ra)
{
    if (env->vmx.nonroot && (env->vmx.proc & PROC_MWAIT)) {
        x86_vmx_insn_exit(env, EXIT_REASON_MWAIT, env->vmx.monitor_armed ? 1 : 0, 0, false, ra);
    }
}

/* the MSR bitmaps (27.6.9): read low 000H, read high 400H, write low 800H, write high C00H */
static bool vmx_msr_exits(CPUX86State *env, uint32_t msr, bool write)
{
    uint32_t off;

    if (!(env->vmx.proc & PROC_MSR_BITMAPS)) {
        return true;
    }
    if (msr <= 0x1fff) {
        off = 0;
    } else if (msr - 0xc0000000u <= 0x1fff) {
        off = 0x400;
    } else {
        return true;
    }
    off += write ? 0x800 : 0;
    return (vmx_ldub(env, env->vmx.msr_bitmap + off + (msr & 0x1fff) / 8) >> (msr & 7)) & 1;
}

/* RDMSR / WRMSR / WRMSRNS (31, 32: qualification 0 WRMSR, 1 WRMSRNS) and the immediate forms (84, 85) */
void x86_vmx_msr_check(CPUX86State *env, uint32_t msr, bool write, uint32_t reason,
                       uint64_t qual, uintptr_t ra)
{
    if (env->vmx.nonroot && vmx_msr_exits(env, msr, write)) {
        x86_vmx_insn_exit(env, reason, qual, 0, false, ra);
    }
}

/* URDMSR / UWRMSR (80, 81): after the user-MSR #GP checks (28.1.3) */
void x86_vmx_user_msr_check(CPUX86State *env, uint32_t msr, bool write, uintptr_t ra)
{
    bool ex;

    if (!env->vmx.nonroot) {
        return;
    }
    if (!(env->vmx.proc & PROC_MSR_BITMAPS) || (msr >= 0x2000 && msr <= 0x3fff)) {
        ex = true;
    } else {
        ex = msr <= 0x1fff && vmx_msr_exits(env, msr, write);
    }
    if (ex) {
        x86_vmx_insn_exit(env, write ? EXIT_REASON_UWRMSR : EXIT_REASON_URDMSR, 0, 0, false, ra);
    }
}

/*
 * RDMSRLIST / WRMSRLIST (78, 79): n < 0 at the start (without MSR bitmaps the instruction exits with
 * qualification 0, nothing read), else for table entry n (qualification n; WRMSRLIST saves the
 * data it read into the MSR-data field; RCX[n] stays set)
 */
void x86_vmx_msrlist_entry(CPUX86State *env, uint32_t msr, int n, bool write, uint64_t data,
                           uintptr_t ra)
{
    uint32_t reason = write ? EXIT_REASON_WRMSRLIST : EXIT_REASON_RDMSRLIST;

    if (!env->vmx.nonroot) {
        return;
    }
    if (n < 0) {
        if (!(env->vmx.proc & PROC_MSR_BITMAPS)) {
            x86_vmx_insn_exit(env, reason, 0, 0, false, ra);
        }
        return;
    }
    if (vmx_msr_exits(env, msr, write)) {
        if (write) {
            vmcs_put(env, VMCS_MSR_DATA, data);
        }
        x86_vmx_insn_exit(env, reason, (uint64_t)n, 0, false, ra);
    }
}

/* MOV from CR0 / CR4 (and SMSW): bits set in the guest/host mask come from the read shadow (28.3) */
uint64_t x86_vmx_cr_read(CPUX86State *env, int reg, uint64_t val)
{
    if (!env->vmx.nonroot) {
        return val;
    }
    if (reg == 0) {
        return (val & ~env->vmx.cr0_mask) | (env->vmx.cr0_shadow & env->vmx.cr0_mask);
    }
    if (reg == 4) {
        return (val & ~env->vmx.cr4_mask) | (env->vmx.cr4_shadow & env->vmx.cr4_mask);
    }
    if (reg == 8) {
        return x86_vmx_cr8_read(env, val);      /* U1307: "use TPR shadow" (32.3) */
    }
    return val;
}

/* MOV to CR0 / CR4, LMSW without a VM exit: bits set in the guest/host mask are not modified (28.3) */
uint64_t x86_vmx_cr_write(CPUX86State *env, int reg, uint64_t val)
{
    if (!env->vmx.nonroot) {
        return val;
    }
    if (reg == 0) {
        return (val & ~env->vmx.cr0_mask) | (env->cr[0] & env->vmx.cr0_mask);
    }
    if (reg == 4) {
        return (val & ~env->vmx.cr4_mask) | (env->cr[4] & env->vmx.cr4_mask);
    }
    return val;
}

/* LOADIWKEY (69): after the CR4.KL #UD and the CPL #GP; Table 30-16 (XMM register numbers) */
void x86_vmx_loadiwkey_check(CPUX86State *env, uint32_t src1, uint32_t src2, uintptr_t ra)
{
    if (env->vmx.nonroot && (env->vmx.proc3 & PROC3_LOADIWKEY)) {
        x86_vmx_insn_exit(env, EXIT_REASON_LOADIWKEY, 0, ((src1 & 15) << 3) | ((src2 & 15) << 28),
                          true, ra);
    }
}

/*
 * U1315: the monitor trap flag at enclave entry and exit (SDM 092 Vol3D EENTER p. 41-93, ERESUME
 * pp. 41-126 / 41-127, EEXIT p. 41-98, the AEX 40.4.1 p. 40-6, 43.2.2 / 43.2.3 pp. 43-1 to 43-3):
 * 'pend' - EEXIT, an AEX and an opt-in EENTER pend an MTF VM exit at their end when the control is
 * 1; otherwise (an opt-out EENTER / ERESUME, an opt-in ERESUME with TMP_NOTIFY = 0: AEX-Notify is
 * not enumerated) a pending MTF VM exit is cleared.
 */
void x86_vmx_sgx_mtf(CPUX86State *env, bool pend)
{
    if (!env->vmx.nonroot) {
        return;
    }
    if (!pend) {
        env->vmx.mtf_pending = 0;
    } else if (env->vmx.proc & PROC_MTF) {
        env->vmx.mtf_pending = 1;
    }
}

/*
 * U1313: ENCLS in VMX non-root operation (SDM 092 Vol3D ENCLS Operation p. 41-8, 42.5.1 p. 42-3;
 * Vol3C 28.1.3, 27.6.16): with "enable ENCLS exiting" the ENCLS-exiting bitmap bit EAX (EAX < 63) or
 * bit 63 (EAX >= 63) makes ENCLS exit (reason 60) - after the #UD and CPL checks, before the
 * IA32_FEATURE_CONTROL, leaf and CR0.PG #GP(0). EAX: RAX[63:32] are ignored.
 */
void x86_vmx_encls_check(CPUX86State *env, uintptr_t ra)
{
    uint32_t leaf = (uint32_t)env->regs[R_EAX];

    if (env->vmx.nonroot && (env->vmx.proc2 & PROC2_ENCLS) &&
        ((env->vmx.encls_bmp >> (leaf < 63 ? leaf : 63)) & 1)) {
        x86_vmx_insn_exit(env, 60, 0, 0, false, ra);
    }
}

/* PCONFIG: "enable PCONFIG" 0 is #UD before any other exception; the PCONFIG-exiting bitmap (65) */
void x86_vmx_pconfig_check(CPUX86State *env, uintptr_t ra)
{
    uint32_t leaf = (uint32_t)env->regs[R_EAX];

    if (!env->vmx.nonroot) {
        return;
    }
    if (!(env->vmx.proc2 & PROC2_PCONFIG)) {
        vmx_ud(env, ra);
    }
    if ((env->vmx.pconfig_bmp >> (leaf < 63 ? leaf : 63)) & 1) {
        x86_vmx_insn_exit(env, EXIT_REASON_PCONFIG, 0, 0, false, ra);
    }
}

/* PBNDKB: "enable PBNDKB" (tertiary bit 9) 0 is #UD before any other exception (28.3) */
void x86_vmx_pbndkb_check(CPUX86State *env, uintptr_t ra)
{
    if (env->vmx.nonroot && !(env->vmx.proc3 & (1ull << 9))) {
        vmx_ud(env, ra);
    }
}

/* the I/O bitmaps (27.6.4): A for ports 0000H-7FFFH, B for 8000H-FFFFH; a wrap-around always exits */
static bool vmx_io_exits(CPUX86State *env, uint32_t port, uint32_t size)
{
    uint32_t i, p;
    uint64_t bm;

    if (!(env->vmx.proc & PROC_IO_BITMAPS)) {
        return (env->vmx.proc & PROC_UNCOND_IO) != 0;
    }
    if (port + size - 1 > 0xffff) {
        return true;
    }
    for (i = 0; i < size; i++) {
        p = port + i;
        bm = p < 0x8000 ? env->vmx.io_a : env->vmx.io_b;
        if ((vmx_ldub(env, bm + (p & 0x7fff) / 8) >> (p & 7)) & 1) {
            return true;
        }
    }
    return false;
}

/*
 * The checks the translator emits for instructions without a helper of their own, or whose exit
 * needs operand information only the translator has (kind: VMXI_*, cpu.h)
 */
void helper_vmx_insn(CPUX86State *env, uint32_t kind, uint32_t info, target_ulong qual,
                     target_ulong val)
{
    uintptr_t ra = GETPC();
    X86VmxState *v = &env->vmx;
    VmxExit e;
    uint32_t reason = 0;
    uint64_t q = 0;
    bool ex = false, inf = false;
    int i;

    if (!v->nonroot) {
        return;
    }
    switch (kind) {
    case VMXI_CR: {
        int cr = info & 15;
        uint64_t src;

        q = info & 0xfff;
        switch ((info >> 4) & 3) {
        case 0:                                         /* MOV to CR */
            if (!(env->hflags & HF_CS64_MASK)) {
                val = (uint32_t)val;
            }
            if (cr == 0) {
                ex = ((val ^ v->cr0_shadow) & v->cr0_mask) != 0;
            } else if (cr == 4) {
                ex = ((val ^ v->cr4_shadow) & v->cr4_mask) != 0;
            } else if (cr == 3 && (v->proc & PROC_CR3_LOAD)) {
                ex = true;
                for (i = 0; i < (int)v->cr3_count && i < 4; i++) {
                    if (val == v->cr3_target[i]) {
                        ex = false;
                    }
                }
            } else if (cr == 8) {
                ex = (v->proc & PROC_CR8_LOAD) != 0;
            }
            break;
        case 1:                                         /* MOV from CR */
            ex = (cr == 3 && (v->proc & PROC_CR3_STORE)) || (cr == 8 && (v->proc & PROC_CR8_STORE));
            break;
        case 2:                                         /* CLTS */
            ex = (v->cr0_mask & v->cr0_shadow & CR0_TS_MASK) != 0;
            break;
        default:                                        /* LMSW */
            src = val & 0xffff;
            ex = ((v->cr0_mask & 1) && (src & 1) && !(v->cr0_shadow & 1)) ||
                 ((src ^ v->cr0_shadow) & v->cr0_mask & 0xe) != 0;
            q |= src << 16;
            break;
        }
        if (ex) {
            vmx_exit_init(&e, EXIT_REASON_CR_ACCESS, q);
            if (((info >> 4) & 3) == 3 && (info & 0x40)) {
                e.gla_valid = true;                     /* LMSW with a memory operand */
                e.gla = vmx_la_trunc(env, qual);
            }
            vmx_insn_exit_e(env, &e, ra);
        }
        return;
    }
    case VMXI_DR:
        ex = (v->proc & PROC_MOV_DR) != 0;
        reason = EXIT_REASON_DR_ACCESS;
        q = info;
        break;
    case VMXI_INVLPG:
        ex = (v->proc & PROC_INVLPG) != 0;
        reason = EXIT_REASON_INVLPG;
        q = vmx_la_trunc(env, val);
        break;
    case VMXI_INVPCID_UD:
        if (!(v->proc2 & PROC2_INVPCID)) {
            vmx_ud(env, ra);
        }
        return;
    case VMXI_INVPCID:
        ex = (v->proc & PROC_INVLPG) != 0;
        reason = EXIT_REASON_INVPCID;
        q = qual;
        inf = true;
        break;
    case VMXI_GDTR_IDTR:
    case VMXI_LDTR_TR:
        ex = (v->proc2 & PROC2_DESC_TABLE) != 0;
        reason = kind == VMXI_GDTR_IDTR ? EXIT_REASON_GDTR_IDTR : EXIT_REASON_LDTR_TR;
        q = qual;
        inf = true;
        break;
    case VMXI_RDRAND:
    case VMXI_RDSEED:
        ex = (v->proc2 & (kind == VMXI_RDRAND ? PROC2_RDRAND : PROC2_RDSEED)) != 0;
        reason = kind == VMXI_RDRAND ? EXIT_REASON_RDRAND : EXIT_REASON_RDSEED;
        inf = true;
        break;
    case VMXI_XSAVES:
    case VMXI_XRSTORS: {
        uint64_t rfbm = (uint32_t)env->regs[R_EAX] | ((uint64_t)(uint32_t)env->regs[R_EDX] << 32);

        if (!(v->proc2 & PROC2_XSAVES)) {
            vmx_ud(env, ra);
        }
        ex = (rfbm & env->xss & v->xss_bmp) != 0;
        reason = kind == VMXI_XSAVES ? EXIT_REASON_XSAVES : EXIT_REASON_XRSTORS;
        q = qual;
        inf = true;
        break;
    }
    case VMXI_WBINVD:
        ex = (v->proc2 & PROC2_WBINVD) != 0;
        reason = EXIT_REASON_WBINVD;
        q = qual;
        break;
    case VMXI_RDPID:
        if (!(v->proc2 & PROC2_RDTSCP)) {
            vmx_ud(env, ra);
        }
        return;
    case VMXI_UMONITOR:
    case VMXI_UMWAIT:
    case VMXI_TPAUSE:
        if (!(v->proc2 & PROC2_USER_WAIT)) {
            vmx_ud(env, ra);
        }
        if (kind == VMXI_UMONITOR) {
            return;
        }
        /* CR4.TSD at CPL > 0 is a fault based on privilege level: before the VM exit */
        if ((env->cr[4] & CR4_TSD_MASK) && (env->hflags & HF_CPL_MASK)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        ex = (v->proc & PROC_RDTSC) != 0;
        reason = kind == VMXI_UMWAIT ? EXIT_REASON_UMWAIT : EXIT_REASON_TPAUSE;
        inf = true;
        break;
    case VMXI_MSRLIST_UD:
        if (!(v->proc3 & PROC3_MSRLIST)) {
            vmx_ud(env, ra);
        }
        return;
    case VMXI_WRMSRNS:
        x86_vmx_msr_check(env, (uint32_t)env->regs[R_ECX], true, VMX_EXIT_WRMSR, 1, ra);
        return;
    case VMXI_IO: {
        uint32_t port = val & 0xffff;

        if (!vmx_io_exits(env, port, (qual & 7) + 1)) {
            return;
        }
        vmx_exit_init(&e, EXIT_REASON_IO, (qual & 0x7f) | ((uint64_t)port << 16));
        if (qual & 0x10) {
            /* INS / OUTS: Table 30-8; the guest-linear address if the segment is usable */
            int seg = (info >> 15) & 7, aw = (info >> 7) & 7;
            uint64_t off = env->regs[(qual & 8) ? R_EDI : R_ESI];

            off = aw == 0 ? (uint16_t)off : aw == 1 ? (uint32_t)off : off;
            if (seg < 6 && (env->segs[seg].flags & DESC_P_MASK)) {
                e.gla_valid = true;
                e.gla = vmx_la_trunc(env, env->segs[seg].base + off);
            }
            e.info_valid = true;
            e.insn_info = info;
        }
        vmx_insn_exit_e(env, &e, ra);
    }
    default:
        return;
    }
    if (ex) {
        x86_vmx_insn_exit(env, reason, q, info, inf, ra);
    }
}

/*
 * ---- U1305: events in VMX non-root operation (Vol3C 28.2 pp. 28-6 to 28-8, 28.4, 30.1-30.3) ----
 * Exceptions and the exception bitmap (with the #PF error-code mask / match), NMIs ("NMI exiting",
 * virtual NMIs, IRET), external interrupts ("external-interrupt exiting", "acknowledge interrupt on
 * exit"), task switches, the original-event information of every event delivered in VMX non-root
 * operation (30.2.4), the HLT activity state left by VMX events.
 */

/* hardware exceptions that deliver an error code (Vol3A Table 7-1) */
static bool vmx_exc_has_ec(int v)
{
    return v == EXCP08_DBLE || (v >= EXCP0A_TSS && v <= EXCP0E_PAGE) || v == EXCP11_ALGN ||
           v == EXCP15_CP;
}

/* the event information format (Tables 27-20 / 27-21) for an event of type 'type' */
static uint32_t vmx_event_info(CPUX86State *env, int vec, uint32_t type)
{
    uint32_t info = (vec & 0xff) | (type << 8) | (1u << 31);

    if (type == 3 && vmx_exc_has_ec(vec) && (env->cr[0] & CR0_PE_MASK)) {
        info |= 1u << 11;
    }
    return info;
}

/*
 * The delivery of an event through the IDT begins (do_interrupt_all): its original-event
 * information, used if a VM exit occurs during the delivery (30.2.4). An event injected by VM entry
 * was recorded by vmx_inject (29.6.1.2). The processor is active once it delivers an event (30.1).
 */
void x86_vmx_delivery_start(CPUX86State *env, int intno, int is_int, int error_code,
                            target_ulong next_eip, int is_hw)
{
    X86VmxState *v = &env->vmx;
    uint32_t type, len = 0;

    if (!v->nonroot) {
        return;
    }
    v->hlt_wake = 0;
    if (v->inj_active) {
        return;
    }
    if (is_hw) {
        type = intno == EXCP02_NMI ? 2 : 0;
    } else if (is_int) {
        type = env->fred_kind == 6 ? 6 : 4;         /* INT3 / INTO: software exception */
        len = (uint32_t)(next_eip - env->eip);
    } else if (env->fred_kind == 5) {
        type = 5;                                   /* INT1: privileged software exception */
        len = env->fred_insn_len;
    } else {
        type = 3;
    }
    v->delivering = 1;
    v->idtv_info = vmx_event_info(env, intno, type);
    /* U1309: an exception nested on FRED event delivery (Table 27-21 bit 13) */
    if (type == 3 && intno != EXCP08_DBLE && env->fred_nested && x86_fred_enabled(env)) {
        v->idtv_info |= 1u << 13;
    }
    v->idtv_err = (uint32_t)error_code;
    v->idtv_len = len;
}

/* debug-exception state before a #DB updated it (restored if the #DB causes a VM exit, 30.1) */
void x86_vmx_db_snapshot(CPUX86State *env)
{
    env->vmx.db_dr6 = env->dr[6];
    env->vmx.db_dr7 = env->dr[7];
    env->vmx.db_snap = 1;
}

/*
 * An exception in VMX non-root operation (raise_interrupt2, helper_icebp): a VM exit if its bit in
 * the exception bitmap says so (28.2; for #PF the bit is reversed when PFEC & PFEC_MASK !=
 * PFEC_MATCH). type: 3 hardware, 5 INT1, 6 INT3 / INTO (4, INT n, never exits by the bitmap); len:
 * the instruction length of types 5 / 6; back: bytes to step RIP back to the instruction (INT1).
 * Returns if there is no VM exit. A VM exit caused directly by #PF leaves CR2, by #DB leaves DR6 /
 * DR7 unmodified; the exit qualification is the #PF linear address / the #DB conditions (Table
 * 30-1); bit 12 of the exiting-event information is "NMI unblocking due to IRET" (30.2.3).
 */
void x86_vmx_exception(CPUX86State *env, int intno, int type, int error_code, int len, int back,
                       uintptr_t ra)
{
    X86VmxState *v = &env->vmx;
    bool db = v->db_snap;
    VmxExit e;
    bool ex;

    v->db_snap = 0;
    if (!v->nonroot || type == 4 || intno < 0 || intno > 31) {
        return;
    }
    ex = (v->exc_bitmap >> intno) & 1;
    if (intno == EXCP0E_PAGE && type == 3 && ((uint32_t)error_code & v->pfec_mask) != v->pfec_match) {
        ex = !ex;
    }
    if (!ex) {
        return;
    }
    if (ra) {
        cpu_restore_state(env_cpu(env), ra, true);
    }
    env->eip -= back;
    vmx_exit_init(&e, EXIT_REASON_EXCEPTION_NMI, 0);
    e.intr_info = vmx_event_info(env, intno, (uint32_t)type);
    e.intr_err = (uint32_t)error_code;
    if (type == 3 && intno == EXCP0E_PAGE) {
        e.qual = vmx_la_trunc(env, env->cr[2]);
        env->cr[2] = v->pf_cr2;
    } else if (type == 3 && intno == EXCP01_DB) {
        e.qual = env->fred_evdata & 0x600f;         /* B3-B0, BD, BS */
        if (db) {
            env->dr[6] = v->db_dr6;
            env->dr[7] = v->db_dr7;
        }
    }
    if (type == 5 || type == 6) {
        e.len_valid = true;
        e.insn_len = (uint32_t)len;
    }
    if (v->iret_nmi && !v->delivering && intno != EXCP08_DBLE) {
        e.intr_info |= 1u << 12;
    }
    /* U1309: a hardware exception encountered during FRED event delivery (30.2.2 bit 13) */
    if (type == 3 && intno != EXCP08_DBLE && env->fred_nested) {
        e.intr_info |= 1u << 13;
    }
    v->iret_nmi = 0;
    vmx_vmexit(env, &e);
}

/* IRET in VMX non-root operation (28.3): NMI or virtual-NMI unblocking at its start (also if it faults) */
void x86_vmx_iret_start(CPUX86State *env)
{
    X86VmxState *v = &env->vmx;

    if (!v->nonroot) {
        return;
    }
    v->iret_nmi = 0;
    if (!(v->pin & PIN_NMI_EXIT)) {
        if (env->hflags2 & HF2_NMI_MASK) {
            v->iret_nmi = 1;
            env->hflags2 &= ~HF2_NMI_MASK;
        }
    } else if (v->pin & PIN_VIRTUAL_NMI) {
        if (v->vnmi_blocked) {
            v->iret_nmi = 1;
            v->vnmi_blocked = 0;
        }
    }
}

/* the end of IRET: false if IRET must leave the blocking by NMI alone ("NMI exiting" = 1, 28.3) */
bool x86_vmx_iret_end(CPUX86State *env)
{
    env->vmx.iret_nmi = 0;
    return !(env->vmx.nonroot && (env->vmx.pin & PIN_NMI_EXIT));
}

/*
 * A task switch in VMX non-root operation (28.4.2): after the checks of the task gate and of the
 * new TSS descriptor, a VM exit (9) with the qualification of Table 30-2 (the TSS selector; source
 * 0 CALL, 1 IRET, 2 JMP, 3 task gate in the IDT); none of the task-switch state changes happen
 */
void x86_vmx_task_switch(CPUX86State *env, int tss_selector, int source, uintptr_t ra)
{
    VmxExit e;
    uint32_t src;

    if (!env->vmx.nonroot) {
        return;
    }
    /* QEMU's sources: 0 JMP, 1 IRET, 2 CALL (event delivery through a task gate calls with CALL) */
    if (source == 2) {
        src = env->vmx.delivering ? 3 : 0;
    } else {
        src = source == 1 ? 1 : 2;
    }
    if (ra) {
        cpu_restore_state(env_cpu(env), ra, true);
    }
    vmx_exit_init(&e, EXIT_REASON_TASK_SWITCH, (uint32_t)(tss_selector & 0xffff) | (src << 30));
    if (src != 3) {
        e.len_valid = true;
        e.insn_len = env->vmx.insn_len;
    }
    vmx_vmexit(env, &e);
}

/* the interrupt window (28.2): RFLAGS.IF = 1, no blocking by STI / MOV SS */
static bool vmx_intr_window(CPUX86State *env)
{
    return (env->vmx.proc & PROC_INTR_WINDOW) && (env->eflags & IF_MASK) &&
           !(env->hflags & HF_INHIBIT_IRQ_MASK);
}

static bool vmx_nmi_window(CPUX86State *env)
{
    return (env->vmx.proc & PROC_NMI_WINDOW) && !env->vmx.vnmi_blocked &&
           !(env->hflags & HF_INHIBIT_IRQ_MASK);
}

/* the VMX events of an instruction boundary above NMIs: pending MTF, timer, NMI window (28.2) */
static void vmx_boundary_exits(CPUX86State *env)
{
    VmxExit e;

    vmx_apic_traps(env);                /* U1307: APIC-write emulation, trap-like VM exits */
    if (env->vmx.mtf_pending) {
        env->vmx.mtf_pending = 0;
        vmx_exit_init(&e, EXIT_REASON_MTF, 0);
        vmx_vmexit(env, &e);
    }
    /*
     * U1310: debug exceptions left pending by VM entry (29.7.3): a #DB with the priority of a trap on
     * the previous instruction - after TPR-threshold and pending MTF VM exits, before the timer, NMIs,
     * interrupts and the windows; DR6 gets B3-B0 and BS as the model's #DB raise sets them; the #DB
     * exits by the exception bitmap like any other (the qualification B3-B0 / BS, DR6 unchanged)
     */
    if (env->vmx.pdbg) {
        uint64_t p = env->vmx.pdbg & 0x400f;
        bool enc = (env->vmx.pdbg >> 62) & 1;  /* U1314 */

        env->vmx.pdbg = 0;
        x86_vmx_db_snapshot(env);
        env->dr[6] = (env->dr[6] & ~0x400full) | p;
        env->fred_evdata_next = p;
        env->sgx_enc_event = enc;       /* U1314: made pending with interruptibility bit 4 */
        raise_exception(env, EXCP01_DB);
    }
    if (env->vmx.timer_on && vmx_timer_value(env) == 0) {
        vmx_exit_init(&e, EXIT_REASON_PREEMPT_TIMER, 0);
        vmx_vmexit(env, &e);
    }
    if (vmx_nmi_window(env)) {
        vmx_exit_init(&e, EXIT_REASON_NMI_WINDOW, 0);
        vmx_vmexit(env, &e);
    }
}

/*
 * x86_cpu_exec_interrupt in VMX non-root operation for the request 'req' (CPU_INTERRUPT_*): the VMX
 * events in the priority of 28.2 - pending MTF / timer / NMI window, an NMI ("NMI exiting": VM exit
 * 0 with NMI information; the exit blocks NMIs, 30.5), the interrupt window, an external interrupt
 * ("external-interrupt exiting": VM exit 1; with "acknowledge interrupt on exit" the local APIC is
 * acknowledged and the vector saved, else the interrupt stays pending). Returns if the request is
 * handled as outside VMX operation.
 */
bool x86_vmx_interrupt(CPUX86State *env, int req)
{
    CPUState *cs = env_cpu(env);
    X86VmxState *v = &env->vmx;
    VmxExit e;
    int vec;

    if (!v->nonroot || (req != CPU_INTERRUPT_NMI && req != CPU_INTERRUPT_HARD)) {
        return false;
    }
    vmx_boundary_exits(env);
    if (req == CPU_INTERRUPT_NMI) {
        if (v->pin & PIN_NMI_EXIT) {
            cs->interrupt_request &= ~CPU_INTERRUPT_NMI;
            vmx_exit_init(&e, EXIT_REASON_EXCEPTION_NMI, 0);
            e.intr_info = 2 | (2u << 8) | (1u << 31);
            e.block_nmi = true;
            vmx_vmexit(env, &e);
        }
        return false;
    }
    if (vmx_intr_window(env)) {
        vmx_exit_init(&e, EXIT_REASON_INTR_WINDOW, 0);
        vmx_vmexit(env, &e);
    }
    vmx_vintr_deliver(env);             /* U1307: above external interrupts (32.2.2) */
    if (v->pin & PIN_EXT_INTR) {
        vmx_exit_init(&e, EXIT_REASON_EXT_INTR, 0);
        if (v->exit_ctls & EXIT_ACK_INTR) {
            vec = x86_apic_acknowledge(env);
            if (vec < 0) {
                return true;                /* nothing deliverable any more */
            }
            /* U1307: the posted-interrupt notification vector (32.6): no VM exit */
            if ((v->pin & PIN_POSTED_INTR) && vmx_posted_intr(env, vec)) {
                vmx_vintr_deliver(env);
                return true;
            }
            e.intr_info = (uint32_t)vec | (1u << 31);
        }
        vmx_vmexit(env, &e);
    }
    return false;
}

/* the interrupt request filter (x86_cpu_pending_interrupt): "external-interrupt exiting" ignores IF */
bool x86_vmx_intr_unmasked(CPUX86State *env)
{
    return env->vmx.nonroot && (env->vmx.pin & PIN_EXT_INTR) &&
           !(env->hflags & HF_INHIBIT_IRQ_MASK);
}

/*
 * HLT in VMX non-root operation (after "HLT exiting" was checked): the VMX events that end the HLT
 * state at once (28.2: pending MTF - 28.5.2 MTF after HLT -, the timer, the NMI window, the interrupt
 * window) exit with the HLT activity state saved and RIP after the HLT (30.1, 30.3.4). Otherwise HLT
 * halts as outside VMX operation (the emulation run ends there).
 */
void x86_vmx_hlt(CPUX86State *env)
{
    VmxExit e;

    if (!env->vmx.nonroot) {
        return;
    }
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    env->vmx.hlt_wake = 1;
    vmx_boundary_exits(env);
    if (vmx_intr_window(env)) {
        vmx_exit_init(&e, EXIT_REASON_INTR_WINDOW, 0);
        vmx_vmexit(env, &e);
    }
    vmx_vintr_deliver(env);             /* U1307: virtual interrupts wake HLT (32.2.2) */
    env->vmx.hlt_wake = 0;
}

/*
 * ---- U1306: EPT (Vol3C 31.3 pp. 31-2 to 31-19; exit qualification Table 30-7) ----
 * Guest-physical addresses of VMX non-root operation with "enable EPT" are translated by a 4- or
 * 5-level EPT walk (EPTP bits 5:3), with 1-GByte / 2-MByte pages, execute-only entries (EPT_VPID_CAP
 * bit 0), mode-based execute control (bit 10 = execute for user-mode linear addresses) and EPT
 * accessed / dirty flags (EPTP bit 6). The guest paging-structure accesses of a linear-address
 * translation are reads (writes with EPT A/D flags, 31.3.3.2); the final access is the instruction's.
 */
#define EXIT_REASON_EPT_VIOLATION   48
#define EXIT_REASON_EPT_MISCONFIG   49
#define EPT_ADDR_MASK               0x000ffffffffff000ull

/*
 * Translates gpa for access_type (MMU_DATA_LOAD / STORE / INST_FETCH). kind: 0 the access of a
 * linear address itself (*prot narrowed by the EPT permissions), 1 a guest paging-structure entry,
 * 2 a PDPTE load by MOV to CR (a read without a guest-linear address, 31.3.3.2, Table 30-7 bit 7).
 * Returns the physical address; an EPT violation / misconfiguration is a VM exit.
 */
hwaddr x86_vmx_ept_translate(CPUX86State *env, uint64_t gpa, int access_type, int *prot, int kind)
{
    X86VmxState *v = &env->vmx;
    CPUState *cs = env_cpu(env);
    uint64_t eptp = v->eptp, table, e = 0, ea, perm = 0x407, rsvd, size = 0;
    int levels = (int)((eptp >> 3) & 7) + 1, l, m = vmx_maxphyaddr(env);
    bool ad = (eptp >> 6) & 1, mbec = (v->proc2 & PROC2_MBEC) != 0, final = kind == 0;
    bool write, fetch, present_all = true, ok;
    uint32_t user_la = v->ept_lattr & 1;
    VmxExit x;

    write = access_type == MMU_DATA_STORE;
    fetch = access_type == MMU_INST_FETCH;
    if (!final) {
        /* guest paging-structure accesses: writes with EPT A/D flags; PDPTE loads: reads */
        write = kind == 1 && ad;
        fetch = false;
    }
    rsvd = EPT_ADDR_MASK & ~((1ull << m) - 1);
    table = eptp & EPT_ADDR_MASK;
    if (levels == 4 && (gpa >> 48)) {
        present_all = false;
        e = 1ull << 63;                 /* U1324: no EPT entry decides: not convertible */
        goto violation;
    }
    for (l = levels; l >= 1; l--) {
        int shift = 12 + 9 * (l - 1);
        bool leaf;

        ea = table + ((gpa >> shift) & 0x1ff) * 8;
        e = x86_ldq_phys(cs, ea);
        if (!(e & 7) && !(mbec && (e & (1ull << 10)))) {
            present_all = false;
            goto violation;
        }
        leaf = l == 1 || ((l == 2 || l == 3) && (e & (1ull << 7)));
        /* 31.3.3.1: write without read, reserved bits, reserved memory types */
        if ((!(e & 1) && (e & 2)) || (e & rsvd)) {
            goto misconfig;
        }
        if (!leaf && (e & (l >= 4 ? 0xf8ull : 0x78ull))) {
            goto misconfig;
        }
        if (leaf && l > 1 && (e & ((1ull << shift) - 1) & EPT_ADDR_MASK)) {
            goto misconfig;
        }
        if (leaf) {
            uint64_t mt = (e >> 3) & 7;

            if (mt == 2 || mt == 3 || mt == 7) {
                goto misconfig;
            }
        }
        perm &= e;
        if (ad && !(e & (1ull << 8))) {
            e |= 1ull << 8;
            x86_stq_phys(cs, ea, e);
        }
        if (leaf) {
            size = 1ull << shift;
            break;
        }
        table = e & EPT_ADDR_MASK;
    }
    /* 31.3.3.2: the privileges of the access */
    ok = true;
    if (fetch) {
        ok = (mbec && user_la) ? (perm >> 10) & 1 : (perm >> 2) & 1;
    } else {
        ok = (perm & 1) != 0;
        if (write && !(perm & 2)) {
            ok = false;
        }
    }
    if (!ok) {
        goto violation;
    }
    if (ad && write && !(e & (1ull << 9))) {
        e |= 1ull << 9;
        x86_stq_phys(cs, ea, e);
    }
    if (final && prot) {
        if (!(perm & 1)) {
            *prot &= ~PAGE_READ;
        }
        if (!(perm & 2)) {
            *prot &= ~PAGE_WRITE;
        }
        if (!((mbec && user_la) ? (perm >> 10) & 1 : (perm >> 2) & 1)) {
            *prot &= ~PAGE_EXEC;
        }
    }
    return (e & EPT_ADDR_MASK & ~(size - 1)) | (gpa & (size - 1));

violation:
    if (env->retaddr) {
        cpu_restore_state(cs, env->retaddr, true);
    }
    vmx_exit_init(&x, EXIT_REASON_EPT_VIOLATION, 0);
    x.qual = (access_type == MMU_INST_FETCH && final) ? 4 : (write ? (kind == 1 ? 3 : 2) : 1);
    if (present_all) {
        x.qual |= (perm & 7) << 3;
        if (mbec) {
            x.qual |= ((perm >> 10) & 1) << 6;
        }
    }
    if (kind != 2) {
        x.qual |= 1u << 7;                          /* guest-linear address valid */
        x.gla_valid = true;
        x.gla = vmx_la_trunc(env, v->ept_gla);
    }
    if (final) {
        x.qual |= 1u << 8;
        x.qual |= (uint64_t)(v->ept_lattr & 7) << 9;    /* user, read/write, execute-disable */
    }
    if (v->iret_nmi && !v->delivering) {
        x.qual |= 1u << 12;
    }
    x.gpa_valid = true;
    x.gpa = gpa;
    /*
     * U1324: EPT-violation #VE (Vol3C 28.5.8.1 p. 28-22 / 28-23): a convertible EPT violation - bit 63
     * ("suppress #VE") clear in the not-present entry or the entry that maps the page ('e') - becomes a
     * virtualization exception (vector 20, no error code) with CR0.PE = 1, outside event delivery and
     * with the 32 bits at offset 4 of the information area 0; the area (Table 28-23) gets exit reason
     * 48, FFFFFFFFH, the qualification, guest-linear and guest-physical address and the EPTP index.
     */
    if ((v->proc2 & PROC2_EPT_VE) && !(e >> 63) && (env->cr[0] & CR0_PE_MASK) && !v->delivering &&
        vmx_ldl(env, v->ve_info + 4) == 0) {
        vmx_stl(env, v->ve_info, EXIT_REASON_EPT_VIOLATION);
        vmx_stl(env, v->ve_info + 4, 0xffffffffu);
        vmx_stq(env, v->ve_info + 8, x.qual);
        vmx_stq(env, v->ve_info + 16, x.gla_valid ? x.gla : 0);
        vmx_stq(env, v->ve_info + 24, x.gpa);
        x86_stw_phys(cs, v->ve_info + 32, (uint16_t)vmcs_get(env, VMCS_EPTP_INDEX));
        raise_exception(env, 20);
    }
    v->iret_nmi = 0;
    x.rf_one = !v->delivering;
    vmx_vmexit(env, &x);

misconfig:
    if (env->retaddr) {
        cpu_restore_state(cs, env->retaddr, true);
    }
    vmx_exit_init(&x, EXIT_REASON_EPT_MISCONFIG, 0);
    x.gpa_valid = true;
    x.gpa = gpa;
    x.rf_one = !v->delivering;
    vmx_vmexit(env, &x);
}

/* the PDPTE registers of PAE paging with EPT (Vol3A 5.4.1): true with *pdpte = PDPTE i */
bool x86_vmx_pdpte(CPUX86State *env, int i, uint64_t *pdpte)
{
    if (!env->vmx.nonroot || !(env->vmx.proc2 & PROC2_EPT) || !env->vmx.pdpte_valid) {
        return false;
    }
    *pdpte = env->vmx.pdpte[i & 3];
    return true;
}

/*
 * MOV to CR3 under PAE paging, MOV to CR0 / CR4 after which PAE paging is in use and which modifies
 * CR0.CD / NW / PG, CR4.PAE / PGE / PSE / SMEP (Vol3A 5.4.1), in VMX non-root operation with EPT:
 * before the register changes, the PDPTEs are loaded from the (new) CR3 through EPT (28.3 MOV to
 * CR3; a read without a guest-linear address); a present PDPTE with a reserved bit (2:1, 8:5,
 * 63:M, M not reduced) is #GP(0) and nothing changes. reg / val: the CR written and its new value.
 */
void x86_vmx_pdpte_load(CPUX86State *env, int reg, uint64_t val, uintptr_t ra)
{
    uint64_t cr0 = reg == 0 ? val : env->cr[0], cr3 = reg == 3 ? val : env->cr[3];
    uint64_t cr4 = reg == 4 ? val : env->cr[4], p[4], gpa, rsvd;
    int i, m;

    if (!env->vmx.nonroot || !(env->vmx.proc2 & PROC2_EPT)) {
        return;
    }
    if (!(cr0 & CR0_PG_MASK) || !(cr4 & CR4_PAE_MASK) || (env->efer & MSR_EFER_LME)) {
        env->vmx.pdpte_valid = 0;               /* no PAE paging after the write */
        return;
    }
    /* CR0.CD = bit 30, CR0.NW = bit 29 */
    if (reg == 0 && !((cr0 ^ env->cr[0]) & ((1ull << 30) | (1ull << 29) | CR0_PG_MASK))) {
        return;
    }
    if (reg == 4 && !((cr4 ^ env->cr[4]) &
                      (CR4_PAE_MASK | CR4_PGE_MASK | CR4_PSE_MASK | CR4_SMEP_MASK))) {
        return;
    }
    m = env_archcpu(env)->phys_bits;
    rsvd = 0x1e6ull | (m < 64 ? ~0ull << m : 0);
    env->retaddr = ra;
    for (i = 0; i < 4; i++) {
        gpa = (cr3 & 0xffffffe0ull) + 8 * i;
        p[i] = x86_ldq_phys(env_cpu(env), x86_vmx_ept_translate(env, gpa, MMU_DATA_LOAD, NULL, 2));
        if ((p[i] & 1) && (p[i] & rsvd)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
    }
    memcpy(env->vmx.pdpte, p, sizeof(p));
    env->vmx.pdpte_valid = 1;
}

/*
 * ---- U1307: APIC virtualization and virtual interrupts (Vol3C chapter 32 pp. 32-1 to 32-18) ----
 * The virtual-APIC page (VTPR 080H, VPPR 0A0H, VEOI 0B0H, VISR 100H-170H, VIRR 200H-270H, VICR
 * 300H / 310H), the guest interrupt status (RVI, SVI), TPR / PPR / EOI / self-IPI virtualization,
 * evaluation and delivery of virtual interrupts, CR8 with "use TPR shadow" (32.3), memory-mapped
 * accesses to the APIC-access page (32.4), MSR-based accesses with "virtualize x2APIC mode" (32.5),
 * posted-interrupt processing (32.6). Trap-like VM exits (TPR below threshold 43, virtualized EOI
 * 45, APIC write 56) are taken at the next instruction boundary, before any other event.
 */
#define EXIT_REASON_TPR_BELOW       43
#define EXIT_REASON_APIC_ACCESS     44
#define EXIT_REASON_VIRT_EOI        45
#define EXIT_REASON_APIC_WRITE      56

static uint32_t vapic_rd(CPUX86State *env, uint32_t off)
{
    return vmx_ldl(env, env->vmx.vapic + off);
}

static void vapic_wr(CPUX86State *env, uint32_t off, uint32_t val)
{
    vmx_stl(env, env->vmx.vapic + off, val);
}

/* bit vec of the 256-bit VISR (base 100H) / VIRR (base 200H): offset base | ((vec & E0H) >> 1) */
static void vapic_bit(CPUX86State *env, uint32_t base, int vec, bool set)
{
    uint32_t off = base | ((vec & 0xe0) >> 1), w = vapic_rd(env, off);

    w = set ? (w | (1u << (vec & 31))) : (w & ~(1u << (vec & 31)));
    vapic_wr(env, off, w);
}

static int vapic_highest(CPUX86State *env, uint32_t base)
{
    int i;
    uint32_t w;

    for (i = 7; i >= 0; i--) {
        w = vapic_rd(env, base | (i << 4));
        if (w) {
            return i * 32 + 31 - clz32(w);
        }
    }
    return -1;
}

static void vmx_trap_exit(CPUX86State *env, uint32_t reason, uint64_t qual)
{
    env->vmx.trap_pending = 1;
    env->vmx.trap_reason = reason;
    env->vmx.trap_qual = qual;
}

/* 32.1.3 PPR virtualization */
static void vmx_ppr_virt(CPUX86State *env)
{
    uint32_t vtpr = vapic_rd(env, 0x80), svi = env->vmx.svi;

    vapic_wr(env, 0xa0, ((vtpr >> 4) & 0xf) >= (svi >> 4) ? (vtpr & 0xff) : (svi & 0xf0));
}

/* 32.2.1 evaluation of pending virtual interrupts */
static void vmx_eval_vintr(CPUX86State *env)
{
    env->vmx.vintr_pending = !(env->vmx.proc & PROC_INTR_WINDOW) &&
                             (env->vmx.rvi >> 4) > ((vapic_rd(env, 0xa0) >> 4) & 0xf);
}

/* 32.1.2 TPR virtualization */
static void vmx_tpr_virt(CPUX86State *env)
{
    if (!(env->vmx.proc2 & PROC2_VID)) {
        if (((vapic_rd(env, 0x80) >> 4) & 0xf) < (env->vmx.tpr_threshold & 0xf)) {
            vmx_trap_exit(env, EXIT_REASON_TPR_BELOW, 0);
        }
    } else {
        vmx_ppr_virt(env);
        vmx_eval_vintr(env);
    }
}

/* 32.1.4 EOI virtualization */
static void vmx_eoi_virt(CPUX86State *env)
{
    int vec = env->vmx.svi, h;

    vapic_bit(env, 0x100, vec, false);
    h = vapic_highest(env, 0x100);
    env->vmx.svi = h < 0 ? 0 : h;
    vmx_ppr_virt(env);
    if ((env->vmx.eoi_exit[vec >> 6] >> (vec & 63)) & 1) {
        vmx_trap_exit(env, EXIT_REASON_VIRT_EOI, (uint64_t)vec);
    } else {
        vmx_eval_vintr(env);
    }
}

/* 32.1.5 self-IPI virtualization */
static void vmx_self_ipi_virt(CPUX86State *env, int vec)
{
    vapic_bit(env, 0x200, vec, true);
    if (vec > env->vmx.rvi) {
        env->vmx.rvi = vec;
    }
    vmx_eval_vintr(env);
}

/*
 * 32.2.2 delivery of a recognized virtual interrupt at an instruction boundary (RFLAGS.IF = 1, no
 * STI / MOV-SS blocking, "interrupt-window exiting" 0): the guest interrupt status and the virtual-APIC
 * page are updated and the vector is delivered as the model delivers external interrupts. Returns
 * only if nothing is delivered.
 */
static void vmx_vintr_deliver(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    int vec, h;

    if (!env->vmx.vintr_pending || !(env->vmx.proc2 & PROC2_VID) ||
        !(env->eflags & IF_MASK) || (env->hflags & HF_INHIBIT_IRQ_MASK) ||
        (env->vmx.proc & PROC_INTR_WINDOW)) {
        return;
    }
    vec = env->vmx.rvi;
    vapic_bit(env, 0x100, vec, true);
    env->vmx.svi = vec;
    vapic_wr(env, 0xa0, vec & 0xf0);
    vapic_bit(env, 0x200, vec, false);
    h = vapic_highest(env, 0x200);
    env->vmx.rvi = h < 0 ? 0 : h;
    env->vmx.vintr_pending = 0;
    env->vmx.hlt_wake = 0;
    x86_apic_deliver_event(env, vec);
    cs->exception_index = -1;
    cpu_loop_exit(cs);
}

/* the trap-like VM exits and the APIC-write emulation of the instruction just completed (32.4.3.2) */
static void vmx_apic_traps(CPUX86State *env)
{
    X86VmxState *v = &env->vmx;
    VmxExit e;

    if (v->apic_va_valid) {
        v->apic_va_valid = 0;
        tlb_flush_page(env_cpu(env), v->apic_va);
    }
    if (v->apic_write_pending) {
        uint32_t off = v->apic_write_off, icr;

        v->apic_write_pending = 0;
        if (off == 0x80) {
            vapic_wr(env, 0x80, vapic_rd(env, 0x80) & 0xff);
            vmx_tpr_virt(env);
        } else if (off == 0xb0 && (v->proc2 & PROC2_VID)) {
            vapic_wr(env, 0xb0, 0);
            vmx_eoi_virt(env);
        } else if (off == 0x300 && (v->proc2 & PROC2_VID) &&
                   !((icr = vapic_rd(env, 0x300)) & 0xfff33000u) && ((icr >> 18) & 3) == 1 &&
                   !(icr & (1u << 15)) && !((icr >> 8) & 7) && ((icr >> 4) & 0xf)) {
            vmx_self_ipi_virt(env, icr & 0xff);
        } else if (off >= 0x310 && off <= 0x313) {
            vapic_wr(env, 0x310, vapic_rd(env, 0x310) & 0xff000000u);
        } else {
            vmx_trap_exit(env, EXIT_REASON_APIC_WRITE, off);
        }
    }
    if (v->trap_pending) {
        v->trap_pending = 0;
        vmx_exit_init(&e, v->trap_reason, v->trap_qual);
        vmx_vmexit(env, &e);
    }
}

/* MOV from CR8 with "use TPR shadow": VTPR[7:4] (32.3); val unchanged otherwise */
uint64_t x86_vmx_cr8_read(CPUX86State *env, uint64_t val)
{
    if (env->vmx.nonroot && (env->vmx.proc & PROC_TPR_SHADOW)) {
        return (vapic_rd(env, 0x80) >> 4) & 0xf;
    }
    return val;
}

/* MOV to CR8 with "use TPR shadow": VTPR := source[3:0] << 4, TPR virtualization (32.3); true if done */
bool x86_vmx_cr8_write(CPUX86State *env, uint64_t val)
{
    if (!env->vmx.nonroot || !(env->vmx.proc & PROC_TPR_SHADOW)) {
        return false;
    }
    vapic_wr(env, 0x80, (uint32_t)(val & 0xf) << 4);
    vmx_tpr_virt(env);
    return true;
}

/* RDMSR 800H-8FFH with "virtualize x2APIC mode" (32.5); true with *val if virtualized */
bool x86_vmx_x2apic_rdmsr(CPUX86State *env, uint32_t msr, uint64_t *val)
{
    if (!env->vmx.nonroot || !(env->vmx.proc2 & PROC2_X2APIC) || msr < 0x800 || msr > 0x8ff) {
        return false;
    }
    if (!(env->vmx.proc2 & PROC2_APIC_REG) && msr != 0x808) {
        return false;
    }
    *val = vmx_ldq(env, env->vmx.vapic + ((msr & 0xff) << 4));
    return true;
}

/*
 * WRMSR 808H / 80BH / 83FH with "virtualize x2APIC mode" (32.5): the reserved-bit #GP, the 8-byte
 * store at offset (ECX & FFH) << 4, then TPR / EOI / self-IPI virtualization (83FH with vector[7:4]
 * = 0: APIC-write VM exit, qualification 3F0H). True if virtualized.
 */
bool x86_vmx_x2apic_wrmsr(CPUX86State *env, uint32_t msr, uint64_t val, uintptr_t ra)
{
    X86VmxState *v = &env->vmx;
    bool vid = (v->proc2 & PROC2_VID) != 0;

    if (!v->nonroot || !(v->proc2 & PROC2_X2APIC)) {
        return false;
    }
    if (msr != 0x808 && !(vid && (msr == 0x80b || msr == 0x83f))) {
        return false;
    }
    if ((msr == 0x80b && val) || (msr != 0x80b && (val >> 8))) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    vmx_stq(env, v->vapic + ((msr & 0xff) << 4), val);
    if (msr == 0x808) {
        vmx_tpr_virt(env);
    } else if (msr == 0x80b) {
        vmx_eoi_virt(env);
    } else if (val & 0xf0) {
        vmx_self_ipi_virt(env, val & 0xff);
    } else {
        vmx_trap_exit(env, EXIT_REASON_APIC_WRITE, 0x3f0);
    }
    return true;
}

/* the offsets of 32.4.2 (reads) / 32.4.3.1 (writes) virtualized with "APIC-register virtualization" */
static bool vmx_apic_reg_ok(uint32_t off, bool write)
{
    uint32_t r = off & ~0xfu;

    if ((off & 0xf) > 3) {
        return false;
    }
    switch (r) {
    case 0x20: case 0x80: case 0xb0: case 0xd0: case 0xe0: case 0xf0: case 0x280: case 0x300:
    case 0x310: case 0x320: case 0x330: case 0x340: case 0x350: case 0x360: case 0x370: case 0x380:
    case 0x3e0:
        return true;
    case 0x30:
        return !write;
    default:
        return !write && r >= 0x100 && r <= 0x270;
    }
}

/*
 * A linear access to the APIC-access page with "virtualize APIC accesses" (32.4), from the page walk
 * (after the paging and EPT checks, 32.4.1): an instruction fetch, or an access 32.4.2 / 32.4.3.1
 * does not virtualize (also during event delivery: access type 3) is an APIC-access VM exit (44,
 * Table 30-6); a
 * virtualized access is redirected to the virtual-APIC page for this access only (the TLB entry is
 * dropped at the next instruction boundary), a virtualized write is followed by APIC-write emulation.
 * Returns the physical address to use.
 */
hwaddr x86_vmx_apic_access(CPUX86State *env, uint64_t la, hwaddr pa, int size, int access_type,
                           int *prot)
{
    X86VmxState *v = &env->vmx;
    uint32_t off = pa & 0xfff, type;
    bool write = access_type == MMU_DATA_STORE, ok;
    VmxExit e;

    if (!v->nonroot || !(v->proc2 & PROC2_APIC_ACCESS) ||
        (pa & ~0xfffull) != (v->apic_access & ~0xfffull)) {
        return pa;
    }
    /*
     * U1316: Intel SGX (Vol3D 42.5.3 p. 42-4). In enclave mode an access into ELRANGE is an explicit
     * enclave access (the SGX access control makes it a #PF: the APIC-access page is not EPC) and an
     * instruction fetch outside ELRANGE is a #GP(0) - neither is checked here; any other access from
     * inside an enclave that would be virtualized causes an APIC-access VM exit instead.
     */
    if (env->sgx_enclave_mode &&
        (access_type == MMU_INST_FETCH || la - env->sgx_elbase < env->sgx_elsize)) {
        return pa;
    }
    type = access_type == MMU_INST_FETCH ? 2 : v->delivering ? 3 : (write ? 1 : 0);
    ok = type != 2 && (v->proc & PROC_TPR_SHADOW) && size <= 4 && (off & 0xf) + size <= 4 &&
         !env->sgx_enclave_mode;
    if (ok && v->apic_write_pending && (!write || v->apic_write_off != off || v->apic_write_size != size)) {
        ok = false;                     /* the operation already virtualized a write */
    }
    if (ok) {
        ok = off == 0x80 || ((v->proc2 & PROC2_VID) && (off == 0xb0 || off == 0x300)) ||
             ((v->proc2 & PROC2_APIC_REG) && vmx_apic_reg_ok(off, write) &&
              vmx_apic_reg_ok(off + size - 1, write));
    }
    if (!ok) {
        if (env->retaddr) {
            cpu_restore_state(env_cpu(env), env->retaddr, true);
        }
        vmx_exit_init(&e, EXIT_REASON_APIC_ACCESS, off | (type << 12));
        e.rf_one = !v->delivering;
        vmx_vmexit(env, &e);
    }
    if (write) {
        v->apic_write_pending = 1;
        v->apic_write_off = off;
        v->apic_write_size = size;
        *prot = PAGE_WRITE;
    } else {
        *prot = PAGE_READ;
    }
    v->apic_va = la & TARGET_PAGE_MASK;
    v->apic_va_valid = 1;
    return (v->vapic & ~0xfffull) | off;
}

/*
 * 32.7 SENDUIPI in VMX non-root operation with "use TPR shadow" (IPI virtualization is not offered):
 * the notification IPI is written to VICR - xAPIC form (VICR_HI = NDST[15:8] << 24, VICR_LO = NV)
 * with "virtualize APIC accesses", else the x2APIC form (8 bytes at 300H: NV, NDST << 32) - and an
 * APIC-write VM exit (qualification 300H) follows the instruction. True if virtualized.
 */
bool x86_vmx_senduipi(CPUX86State *env, int nv, uint32_t ndst)
{
    if (!env->vmx.nonroot || !(env->vmx.proc & PROC_TPR_SHADOW)) {
        return false;
    }
    if (env->vmx.proc2 & PROC2_APIC_ACCESS) {
        vapic_wr(env, 0x310, (ndst & 0xff00u) << 16);
        vapic_wr(env, 0x300, nv & 0xff);
    } else {
        vmx_stq(env, env->vmx.vapic + 0x300, (uint64_t)(nv & 0xff) | ((uint64_t)ndst << 32));
    }
    vmx_trap_exit(env, EXIT_REASON_APIC_WRITE, 0x300);
    return true;
}

/*
 * 32.6 posted-interrupt processing: an external interrupt with "external-interrupt exiting" and
 * "process posted interrupts" (the local APIC was acknowledged: vec). Returns false if vec is not
 * the notification vector (an ordinary VM exit follows).
 */
static bool vmx_posted_intr(CPUX86State *env, int vec)
{
    X86VmxState *v = &env->vmx;
    uint64_t pir[4], on;
    int i, h = -1, b;

    if (vec != v->pi_nv) {
        return false;
    }
    on = vmx_ldq(env, v->pi_desc + 32);
    vmx_stq(env, v->pi_desc + 32, on & ~1ull);
    x86_apic_eoi(env);
    for (i = 0; i < 4; i++) {
        pir[i] = vmx_ldq(env, v->pi_desc + 8 * i);
        vmx_stq(env, v->pi_desc + 8 * i, 0);
    }
    for (i = 0; i < 4; i++) {
        for (b = 0; b < 64; b++) {
            if ((pir[i] >> b) & 1) {
                vapic_bit(env, 0x200, i * 64 + b, true);
                h = i * 64 + b;
            }
        }
    }
    if (h > v->rvi) {
        v->rvi = h;
    }
    vmx_eval_vintr(env);
    return true;
}


/*
 * U1309: ERETS / ERETU in VMX non-root operation (28.3): with "NMI exiting" 1 they do not unblock NMIs;
 * with virtual NMIs they unblock virtual NMIs instead (when augmented SS bit 18 is set). True if the
 * caller unblocks NMIs as outside VMX non-root operation.
 */
bool x86_vmx_fred_eret_nmi(CPUX86State *env)
{
    if (!env->vmx.nonroot || !(env->vmx.pin & PIN_NMI_EXIT)) {
        return true;
    }
    if (env->vmx.pin & PIN_VIRTUAL_NMI) {
        env->vmx.vnmi_blocked = 0;
    }
    return false;
}

#endif /* __Use_Original_Qemu (U1301) */
