/*
 * Local APIC with x2APIC (NoVmp, ledgers U960-U963; design: docs/apic.md).
 *
 * One local APIC per vCPU (CPUX86State.apic_*), SDM Vol3A chapter 13:
 *   - IA32_APIC_BASE (1BH): EN / EXTD state machine of Figure 13-27 and Table 13-5,
 *     reserved bits (0-7, 9, EXTD without CPUID.01H:ECX.x2APIC, MAXPHYADDR-63) #GP;
 *   - the x2APIC MSRs 800H-8FFH (Table 13-6, 13.12.1.3 reserved-bit checking, 13.12.2:
 *     #GP outside x2APIC mode);
 *   - TPR / CR8 (13.8.6.1), PPR (13.8.3.1), IRR / ISR / TMR, interrupt acceptance
 *     (13.8.1, 13.8.4), EOI (13.8.5), SVR software enable (13.4.7.2), ESR and the LVT
 *     error interrupt (13.5.3), ICR (13.12.9) and SELF IPI (13.12.11), destinations
 *     (13.6.2, 13.12.10);
 *   - the interrupt acknowledge used by x86_cpu_exec_interrupt (CPU_INTERRUPT_HARD).
 * What is not modelled (xAPIC MMIO, the timer, SMI/INIT/SIPI, ...) is listed in
 * docs/apic.md; operations that would need it stop the emulation (apic_unsupported).
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U960): whole file */
#include "cpu.h"
#include "exec/exec-all.h"
#include "uc_priv.h"
#include <unicorn/unicorn.h>

#define APIC_BASE_BSP       (1ULL << 8)
#define APIC_BASE_EXTD      (1ULL << 10)
#define APIC_BASE_EN        (1ULL << 11)
#define APIC_BASE_RESET     0xfee00000ULL

/*
 * Version register (13.4.8, Figure 13-7): integrated APIC version 15H, Max LVT Entry 6 (the
 * seven LVT registers below), bit 24 = EOI-broadcast suppression supported (SVR[12]).
 */
#define APIC_VERSION        0x01060015u

#define APIC_SVR_ENABLE     0x100u
#define APIC_SVR_VALID      0x11ffu         /* vector, APIC enable, EOI-broadcast suppression */

#define APIC_LVT_MASKED     0x10000u
#define APIC_LVT_CMCI       0
#define APIC_LVT_TIMER      1
#define APIC_LVT_THERMAL    2
#define APIC_LVT_PERF       3
#define APIC_LVT_LINT0      4
#define APIC_LVT_LINT1      5
#define APIC_LVT_ERROR      6

/*
 * LVT fields (Figure 13-8): software-writable bits, and read-only bits (delivery status 12,
 * remote IRR 14) whose written value is ignored; every other bit is reserved (#GP in x2APIC
 * mode). Timer bit 18 (TSC-deadline) is reserved: CPUID.01H:ECX.TSC_DEADLINE = 0.
 */
static const uint32_t apic_lvt_rw[7] = {
    0x000107ff, 0x000300ff, 0x000107ff, 0x000107ff, 0x0001a7ff, 0x0001a7ff, 0x000100ff,
};
static const uint32_t apic_lvt_ro[7] = {
    0x1000, 0x1000, 0x1000, 0x1000, 0x5000, 0x5000, 0x1000,
};

#define APIC_ESR_REDIR_IPI  0x10u           /* 13.5.3 bit 4 */
#define APIC_ESR_SEND_ILL   0x20u           /* bit 5 */
#define APIC_ESR_RECV_ILL   0x40u           /* bit 6 */

/* ICR in x2APIC mode (Figure 13-28): bits 12, 13, 16, 17 and 31:20 reserved */
#define APIC_ICR_VALID      0xffffffff000ccfffULL
#define APIC_ICR_LOGICAL    (1u << 11)

#define APIC_DM_FIXED       0
#define APIC_DM_LOWEST      1
#define APIC_DM_NMI         4

#define APIC_SH_NONE        0
#define APIC_SH_SELF        1
#define APIC_SH_ALL         2
#define APIC_SH_OTHERS      3

/* ---- atomic helpers: IRR / TMR and the error words may be written by another vCPU ---- */
static uint32_t apic_load(const uint32_t *p)
{
    return *(const volatile uint32_t *)p;
}

static void apic_or(uint32_t *p, uint32_t v)
{
#ifdef _MSC_VER
    InterlockedOr((volatile LONG *)p, (LONG)v);
#else
    __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST);
#endif
}

static void apic_and(uint32_t *p, uint32_t v)
{
#ifdef _MSC_VER
    InterlockedAnd((volatile LONG *)p, (LONG)v);
#else
    __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST);
#endif
}

static uint32_t apic_xchg(uint32_t *p, uint32_t v)
{
#ifdef _MSC_VER
    return (uint32_t)InterlockedExchange((volatile LONG *)p, (LONG)v);
#else
    return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
#endif
}

/* highest set bit of a 256-bit IRR / ISR / TMR, -1 if none */
static int apic_fls256(const uint32_t *tab)
{
    int i;

    for (i = 7; i >= 0; i--) {
        uint32_t w = apic_load(&tab[i]);
        if (w) {
            return i * 32 + 31 - clz32(w);
        }
    }
    return -1;
}

static bool apic_x2(CPUX86State *env)
{
    return (env->apic_base & (APIC_BASE_EN | APIC_BASE_EXTD)) == (APIC_BASE_EN | APIC_BASE_EXTD);
}

/* CPUID.01H:ECX.x2APIC[21] as the CPU reports it (model, strict UC_CTL_X86_CPUID profile) */
static bool apic_has_x2apic(CPUX86State *env)
{
    return (env->features[FEAT_1_ECX] & CPUID_EXT_X2APIC) &&
           (x86_cpuid_profile_mask(env, 1, 0, 2) & CPUID_EXT_X2APIC);
}

/*
 * The vCPU's x2APIC ID (13.12.5.1: initialised by hardware; 13.12.8.1: equal to CPUID.0BH:EDX,
 * its bits 7:0 to CPUID.01H:EBX[31:24]): UC_CTL_X86_APIC_ID if given, else a UC_CTL_X86_CPUID
 * profile's (leaf 0BH sub-leaf 0 EDX, else leaf 1 EBX[31:24]), else the CPU model's (0).
 */
uint32_t x86_apic_id(CPUX86State *env)
{
    struct uc_struct *uc = env->uc;

    if (uc && uc->x86_apic_id_set) {
        return uc->x86_apic_id;                 /* UC_CTL_X86_APIC_ID (U961) */
    }
    if (uc && uc->x86_cpuid_count) {
        size_t i;
        const struct uc_x86_cpuid *l1 = NULL;

        for (i = 0; i < uc->x86_cpuid_count; i++) {
            if (uc->x86_cpuid[i].leaf == 0xb && uc->x86_cpuid[i].subleaf == 0) {
                return uc->x86_cpuid[i].edx;
            }
            if (uc->x86_cpuid[i].leaf == 1) {
                l1 = &uc->x86_cpuid[i];
            }
        }
        if (l1) {
            return l1->ebx >> 24;
        }
    }
    return env_archcpu(env)->apic_id;
}

/* CPUID adjustments for the local APIC (called with the effective leaf) */
void x86_apic_cpuid_fixup(CPUX86State *env, uint32_t index, uint32_t *eax, uint32_t *ebx,
                          uint32_t *ecx, uint32_t *edx)
{
    struct uc_struct *uc = env->uc;

    /* 13.4.3: with IA32_APIC_BASE[11] = 0 the CPUID feature flag for the APIC is 0 */
    if (index == 1 && !(env->apic_base & APIC_BASE_EN)) {
        *edx &= ~CPUID_APIC;
    }
    /* an explicit UC_CTL_X86_APIC_ID: initial APIC ID / x2APIC ID (13.12.8.1, U961) */
    if (uc && uc->x86_apic_id_set) {
        if (index == 1) {
            *ebx = (*ebx & 0x00ffffffu) | (uc->x86_apic_id << 24);
        } else if ((index == 0xb || index == 0x1f) && (*eax | *ebx | *ecx | *edx)) {
            *edx = uc->x86_apic_id;
        }
    }
}

/* ---- priorities (13.8.3.1) ---- */
static uint32_t apic_ppr(CPUX86State *env)
{
    int isrv = apic_fls256(env->apic_isr);
    uint32_t tpr = env->apic_tpr & 0xff;

    if (isrv < 0) {
        isrv = 0;
    }
    /* PPR[7:4] = max(TPR[7:4], ISRV[7:4]); equal classes: PPR[3:0] = TPR[3:0] (model-specific) */
    return (tpr >> 4) >= ((uint32_t)isrv >> 4) ? tpr : ((uint32_t)isrv & 0xf0);
}

/* the vector the local APIC dispatches to the core now (13.8.4), -1 if none */
static int apic_deliverable(CPUX86State *env)
{
    int irrv;

    if (!(env->apic_base & APIC_BASE_EN) || !(env->apic_svr & APIC_SVR_ENABLE)) {
        return -1;
    }
    irrv = apic_fls256(env->apic_irr);
    if (irrv < 0 || (uint32_t)(irrv >> 4) <= (apic_ppr(env) >> 4)) {
        return -1;
    }
    return irrv;
}

/* ---- errors (13.5.3) ---- */
static void apic_error(CPUX86State *env, uint32_t bits)
{
    apic_or(&env->apic_esr_latch, bits);
    apic_or(&env->apic_err_new, bits);
}

static void apic_kick(CPUX86State *env, uint32_t request)
{
    CPUState *cs = env_cpu(env);

    apic_or((uint32_t *)&cs->interrupt_request, request);
    /* leave the TB chain at the next TB start, as cpu_exit does */
    *(volatile int16_t *)&cs->icount_decr_ptr->u16.high = -1;
}

/* a fixed interrupt arrives at 'env' (13.8.1 step 3, 13.8.4); edge: TMR bit cleared */
static void apic_set_irr(CPUX86State *env, int vector, bool level)
{
    uint32_t bit = 1u << (vector & 31);

    if (level) {
        apic_or(&env->apic_tmr[vector >> 5], bit);
    } else {
        apic_and(&env->apic_tmr[vector >> 5], ~bit);
    }
    apic_or(&env->apic_irr[vector >> 5], bit);
}

/*
 * An interrupt message accepted by the local APIC of 'env' (the destination already
 * matched). A globally disabled APIC (EN = 0) is as if absent (13.4.3); a software-disabled
 * one (SVR[8] = 0) still takes NMIs but discards fixed interrupts (13.4.7.2). Vectors 0-15
 * are never put in the IRR: Receive Illegal Vector (13.5.2, 13.5.3).
 */
static void apic_accept(CPUX86State *env, int mode, int vector, bool level)
{
    if (!(env->apic_base & APIC_BASE_EN)) {
        return;
    }
    if (mode == APIC_DM_NMI) {
        apic_kick(env, CPU_INTERRUPT_NMI);
        return;
    }
    if (vector < 16) {
        apic_error(env, APIC_ESR_RECV_ILL);
    } else if (env->apic_svr & APIC_SVR_ENABLE) {
        apic_set_irr(env, vector, level);
    } else {
        return;
    }
    apic_kick(env, CPU_INTERRUPT_POLL);
}

/* the LVT error interrupt: once per arming (reset, ESR write), unless masked (13.5.3) */
static void apic_error_interrupt(CPUX86State *env)
{
    uint32_t lvt;
    int vector;

    if (!apic_xchg(&env->apic_err_new, 0) || !env->apic_err_armed) {
        return;
    }
    env->apic_err_armed = 0;
    lvt = env->apic_lvt[APIC_LVT_ERROR];
    if (lvt & APIC_LVT_MASKED) {
        return;
    }
    vector = lvt & 0xff;
    if (vector < 16) {
        apic_error(env, APIC_ESR_RECV_ILL);     /* a locally generated illegal vector */
    } else {
        apic_set_irr(env, vector, false);       /* error interrupts are edge (13.5.1) */
    }
}

/*
 * Re-evaluates this vCPU's APIC: the pending error interrupt, then CPU_INTERRUPT_HARD = "a
 * fixed interrupt is deliverable". Called on this vCPU's thread only.
 */
void x86_apic_update(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    apic_error_interrupt(env);
    if (apic_deliverable(env) >= 0) {
        apic_or((uint32_t *)&cs->interrupt_request, CPU_INTERRUPT_HARD);
    } else {
        apic_and((uint32_t *)&cs->interrupt_request, ~(uint32_t)CPU_INTERRUPT_HARD);
    }
}

/* INTA (13.8.4): the highest deliverable IRR vector moves to the ISR; -1 if none */
int x86_apic_acknowledge(CPUX86State *env)
{
    int v = apic_deliverable(env);

    if (v >= 0) {
        apic_and(&env->apic_irr[v >> 5], ~(1u << (v & 31)));
        env->apic_isr[v >> 5] |= 1u << (v & 31);
    }
    x86_apic_update(env);
    return v;
}

/*
 * EOI (13.8.5): clears the highest ISR bit. For a level-triggered vector (TMR) the APIC would
 * also broadcast an EOI message to the I/O APICs unless SVR[12] is set; the model has no
 * I/O APIC, so that message has no receiver.
 */
void x86_apic_eoi(CPUX86State *env)
{
    int v = apic_fls256(env->apic_isr);

    if (v >= 0) {
        env->apic_isr[v >> 5] &= ~(1u << (v & 31));
    }
    x86_apic_update(env);
}

/* ---- reset ---- */
/* the register state after power-up / reset (13.4.7.1, 13.12.5.1); the ID is not stored */
static void apic_regs_reset(CPUX86State *env)
{
    int i;

    memset(env->apic_irr, 0, sizeof(env->apic_irr));
    memset(env->apic_isr, 0, sizeof(env->apic_isr));
    memset(env->apic_tmr, 0, sizeof(env->apic_tmr));
    env->apic_icr = 0;
    env->apic_ldr = 0;
    env->apic_tpr = 0;
    env->apic_tmict = 0;
    env->apic_tdcr = 0;
    env->apic_dfr = 0xffffffffu;
    for (i = 0; i < 7; i++) {
        env->apic_lvt[i] = APIC_LVT_MASKED;
    }
    env->apic_svr = 0xff;
    env->apic_esr = 0;
    env->apic_esr_latch = 0;
    env->apic_err_new = 0;
    env->apic_err_armed = 1;
}

/* power-up / reset: enabled, xAPIC mode, base FEE00000H, BSP flag (13.4.4, 13.12.5.1) */
void x86_apic_reset(CPUX86State *env)
{
    apic_regs_reset(env);
    env->apic_base = APIC_BASE_RESET | APIC_BASE_EN |
                     ((env->uc && env->uc->x86_apic_ap) ? 0 : APIC_BASE_BSP);
}

/*
 * ---- the APIC bus (U961): local APICs of several engines (one vCPU each) ----
 * UC_CTL_X86_APIC_BUS connects engines; an IPI reaches every APIC on the bus of its sender
 * (13.6.3, the system bus of 13.8.1). The engines run one at a time (round-robin on one host
 * thread); a message for another engine sets its IRR (atomic) and CPU_INTERRUPT_POLL, which it
 * evaluates at its next instruction boundary - when the host next runs it.
 */
typedef struct X86ApicBus {
    int n, cap;
    struct uc_struct **cpu;
} X86ApicBus;

static CPUX86State *apic_env_of(struct uc_struct *uc)
{
    return uc->cpu ? (CPUX86State *)uc->cpu->env_ptr : NULL;
}

/* another APIC on this engine's bus (or the peer itself) already has 'id' */
static bool apic_id_taken(struct uc_struct *uc, struct uc_struct *peer, uint32_t id)
{
    X86ApicBus *bus = peer ? (X86ApicBus *)peer->x86_apic_bus : (X86ApicBus *)uc->x86_apic_bus;
    int i;

    if (peer && !bus) {
        return apic_env_of(peer) && x86_apic_id(apic_env_of(peer)) == id;
    }
    for (i = 0; bus && i < bus->n; i++) {
        if (bus->cpu[i] != uc && apic_env_of(bus->cpu[i]) &&
            x86_apic_id(apic_env_of(bus->cpu[i])) == id) {
            return true;
        }
    }
    return false;
}

uint32_t x86_apic_get_id(struct uc_struct *uc)
{
    CPUX86State *env = apic_env_of(uc);

    return env ? x86_apic_id(env) : uc->x86_apic_id;
}

void x86_apic_leave(struct uc_struct *uc)
{
    X86ApicBus *bus = (X86ApicBus *)uc->x86_apic_bus;
    int i;

    if (!bus) {
        return;
    }
    for (i = 0; i < bus->n; i++) {
        if (bus->cpu[i] == uc) {
            memmove(&bus->cpu[i], &bus->cpu[i + 1], (bus->n - i - 1) * sizeof(bus->cpu[0]));
            bus->n--;
            break;
        }
    }
    uc->x86_apic_bus = NULL;
    if (bus->n == 0) {
        g_free(bus->cpu);
        g_free(bus);
    }
}

static void apic_bus_add(X86ApicBus *bus, struct uc_struct *uc)
{
    if (bus->n == bus->cap) {
        bus->cap = bus->cap ? bus->cap * 2 : 4;
        bus->cpu = g_renew(struct uc_struct *, bus->cpu, bus->cap);
    }
    bus->cpu[bus->n++] = uc;
    uc->x86_apic_bus = bus;
}

/*
 * UC_CTL_X86_APIC_BUS: connect this engine's APIC to the bus of 'peer' (created with the peer
 * as its first member if it has none); NULL disconnects. The x2APIC IDs on a bus must differ.
 * An engine that joins an existing bus is an AP: IA32_APIC_BASE.BSP = 0 (the reset value of
 * this engine from now on); the peer that creates the bus keeps its flag.
 */
int x86_apic_join(struct uc_struct *uc, struct uc_struct *peer)
{
    X86ApicBus *bus;
    CPUX86State *env = apic_env_of(uc);

    if (peer == uc || !env || (peer && !apic_env_of(peer))) {
        return peer ? UC_ERR_ARG : UC_ERR_OK;
    }
    if (!peer) {
        x86_apic_leave(uc);
        return UC_ERR_OK;
    }
    if (peer->x86_apic_bus && peer->x86_apic_bus == uc->x86_apic_bus) {
        return UC_ERR_OK;
    }
    if (apic_id_taken(uc, peer, x86_apic_id(env))) {
        return UC_ERR_ARG;
    }
    x86_apic_leave(uc);
    bus = (X86ApicBus *)peer->x86_apic_bus;
    if (!bus) {
        bus = g_new0(X86ApicBus, 1);
        apic_bus_add(bus, peer);
    }
    apic_bus_add(bus, uc);
    uc->x86_apic_ap = 1;
    env->apic_base &= ~APIC_BASE_BSP;
    return UC_ERR_OK;
}

/*
 * UC_CTL_X86_APIC_ID after the CPU exists: FFFF_FFFFH is reserved (13.12.1.3) and the IDs on
 * a bus must be unique. In x2APIC mode the LDR follows the new ID (13.12.10.2).
 */
int x86_apic_set_id(struct uc_struct *uc, uint32_t id)
{
    CPUX86State *env = apic_env_of(uc);

    if (id == 0xffffffffu || (env && apic_id_taken(uc, NULL, id))) {
        return UC_ERR_ARG;
    }
    uc->x86_apic_id = id;
    uc->x86_apic_id_set = 1;
    if (env && apic_x2(env)) {
        env->apic_ldr = ((id & 0xffff0) << 12) | (1u << (id & 0xf));
    }
    return UC_ERR_OK;
}

/* ---- sending interrupts (13.6, 13.12.9, 13.12.10) ---- */
typedef struct ApicMsg {
    int mode;           /* APIC_DM_FIXED / APIC_DM_NMI */
    int vector;
    int shorthand;
    bool logical;
    bool x2;            /* the destination is a 32-bit x2APIC ID (else an 8-bit xAPIC ID) */
    uint32_t dest;
} ApicMsg;

/*
 * Does the APIC of 'dst' belong to the message's destination? Physical: the x2APIC ID
 * (x2APIC-format message) or its bits 7:0, the xAPIC ID (xAPIC-format message); FFFF_FFFFH /
 * FFH broadcast (13.6.2.1, 13.12.9). Logical x2APIC (13.12.10.1-2): cluster LDR[31:16] equal
 * and a common bit in LDR[15:0]; only receivers in x2APIC mode take part (the xAPIC LDR / DFR
 * are MMIO registers, not modelled).
 */
static bool apic_match(CPUX86State *src, CPUX86State *dst, const ApicMsg *m)
{
    switch (m->shorthand) {
    case APIC_SH_SELF:
        return dst == src;
    case APIC_SH_ALL:
        return true;
    case APIC_SH_OTHERS:
        return dst != src;
    default:
        break;
    }
    if (m->x2 ? m->dest == 0xffffffffu : (m->dest & 0xff) == 0xff) {
        return true;
    }
    if (!m->logical) {
        return m->x2 ? x86_apic_id(dst) == m->dest
                     : (x86_apic_id(dst) & 0xff) == (m->dest & 0xff);
    }
    if (!m->x2 || !apic_x2(dst)) {
        return false;
    }
    return (dst->apic_ldr >> 16) == (m->dest >> 16) && (dst->apic_ldr & m->dest & 0xffff);
}

/* the system bus: every local APIC the message reaches (13.6.3, 13.8.1 step 1) */
static void apic_bus_send(CPUX86State *src, const ApicMsg *m)
{
    X86ApicBus *bus = (X86ApicBus *)src->uc->x86_apic_bus;
    int i;

    if (!bus) {
        if (apic_match(src, src, m)) {
            apic_accept(src, m->mode, m->vector, false);
        }
        return;
    }
    for (i = 0; i < bus->n; i++) {
        CPUX86State *dst = apic_env_of(bus->cpu[i]);

        if (dst && apic_match(src, dst, m)) {
            apic_accept(dst, m->mode, m->vector, false);
        }
    }
}

/*
 * The emulation cannot do what the instruction asks (a feature listed as not modelled in
 * docs/apic.md): stop with UC_ERR_INSN_INVALID, RIP at the instruction, no state changed.
 * Host API accesses (UC_X86_REG_MSR) are dropped instead.
 */
static void QEMU_NORETURN apic_unsupported(CPUX86State *env, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    x86_msr_swap_restore(env);
    env->uc->invalid_error = UC_ERR_INSN_INVALID;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit_restore(cs, ra);
}

/*
 * Validates an ICR value before it is written (x2APIC: 13.12.9 and Table 13-3): SMI, INIT,
 * start-up and the reserved delivery modes, and NMI with the Self / All-Including-Self
 * shorthands (invalid combinations), are not modelled. Returns false for the host API.
 */
static bool apic_icr_supported(CPUX86State *env, uint64_t icr, uintptr_t ra)
{
    int dm = (icr >> 8) & 7, sh = (icr >> 18) & 3;

    if (dm == APIC_DM_FIXED || dm == APIC_DM_LOWEST ||
        (dm == APIC_DM_NMI && sh != APIC_SH_SELF && sh != APIC_SH_ALL)) {
        return true;
    }
    if (env->msr_api) {
        return false;
    }
    apic_unsupported(env, ra);
    return false;
}

/* the IPI of an ICR write (13.6.1); edge-triggered whatever ICR[15] says (Table 13-3 note 2) */
static void apic_send_icr(CPUX86State *env, uint64_t icr)
{
    ApicMsg m;

    m.mode = (icr >> 8) & 7;
    m.vector = icr & 0xff;
    m.shorthand = (icr >> 18) & 3;
    m.logical = (icr & APIC_ICR_LOGICAL) != 0;
    m.x2 = apic_x2(env);
    m.dest = m.x2 ? (uint32_t)(icr >> 32) : (uint32_t)(icr >> 56);
    if (m.mode == APIC_DM_LOWEST) {
        /* 13.5.3 bit 4: this APIC does not send lowest-priority IPIs; nothing is sent */
        apic_error(env, APIC_ESR_REDIR_IPI);
        return;
    }
    if (m.mode == APIC_DM_FIXED && m.vector < 16) {
        apic_error(env, APIC_ESR_SEND_ILL);     /* sent anyway: receivers log bit 6 */
    }
    apic_bus_send(env, &m);
}

/* SELF IPI (13.12.11): a self-targeted, edge-triggered fixed interrupt */
static void apic_self_ipi(CPUX86State *env, int vector)
{
    ApicMsg m = {APIC_DM_FIXED, vector, APIC_SH_SELF, false, true, 0};

    if (vector < 16) {
        apic_error(env, APIC_ESR_SEND_ILL);
    }
    apic_bus_send(env, &m);
}

/*
 * U962: the user-interrupt notification of SENDUIPI (SDM Vol2B SENDUIPI): an ordinary fixed,
 * edge-triggered IPI to a physical APIC ID, no shorthand - x2APIC mode: the 32-bit ID NDST;
 * xAPIC mode: the 8-bit ID NDST[15:8]. A globally disabled local APIC (EN = 0) is as if absent
 * and sends nothing (13.4.3). Vectors 0-15 follow the ICR rules (13.5.3).
 */
void x86_apic_send_notification(CPUX86State *env, int vector, uint32_t ndst)
{
    ApicMsg m;

    if (!(env->apic_base & APIC_BASE_EN)) {
        return;
    }
    m.mode = APIC_DM_FIXED;
    m.vector = vector;
    m.shorthand = APIC_SH_NONE;
    m.logical = false;
    m.x2 = apic_x2(env);
    m.dest = m.x2 ? ndst : ((ndst >> 8) & 0xff);
    if (vector < 16) {
        apic_error(env, APIC_ESR_SEND_ILL);
    }
    apic_bus_send(env, &m);
    x86_apic_update(env);
}

/* ---- IA32_APIC_BASE (13.4.4, 13.12.1, 13.12.5) ---- */
static void apic_write_base(CPUX86State *env, uint64_t val, uintptr_t ra)
{
    uint64_t old = env->apic_base;
    uint64_t rsvd = 0x2ffULL | (apic_has_x2apic(env) ? 0 : APIC_BASE_EXTD) |
                    (~0ULL << env_archcpu(env)->phys_bits);
    bool en = (val & APIC_BASE_EN) != 0, extd = (val & APIC_BASE_EXTD) != 0;
    bool oen = (old & APIC_BASE_EN) != 0, oextd = (old & APIC_BASE_EXTD) != 0;

    if ((val & rsvd) ||
        (!en && extd) ||                        /* EN = 0, EXTD = 1: invalid state */
        (oen && oextd && en && !extd) ||        /* x2APIC -> xAPIC */
        (!oen && en && extd)) {                 /* disabled -> x2APIC */
        if (!env->msr_api) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        return;
    }
    if (oen && !en) {
        /* disabled: the APIC state is not preserved, except the ID (13.4.3, 13.12.5) */
        apic_regs_reset(env);
    }
    if (!oextd && extd) {
        /* xAPIC -> x2APIC: the logical x2APIC ID is derived, ICR[63:32] not kept (13.12.5) */
        uint32_t id = x86_apic_id(env);

        env->apic_ldr = ((id & 0xffff0) << 12) | (1u << (id & 0xf));
        env->apic_icr &= 0xffffffffULL;
    }
    /* the BSP flag is the processor's role, set at reset: not changed by WRMSR */
    env->apic_base = (val & ~(rsvd | APIC_BASE_BSP)) | (old & APIC_BASE_BSP);
    x86_apic_update(env);
}

/* ---- TPR and CR8 (13.8.6.1) ---- */
uint64_t x86_apic_get_cr8(CPUX86State *env)
{
    return (env->apic_tpr >> 4) & 0xf;          /* CR8[3:0] = TPR[7:4] */
}

void x86_apic_set_cr8(CPUX86State *env, uint64_t val)
{
    env->apic_tpr = (uint32_t)(val & 0xf) << 4; /* TPR[7:4] = CR8[3:0], TPR[3:0] = 0 */
    x86_apic_update(env);
}

/* ---- the x2APIC MSRs ---- */
static int apic_lvt_index(uint32_t msr)
{
    return msr == 0x82f ? APIC_LVT_CMCI : (int)(msr - 0x831);
}

/*
 * RDMSR of IA32_APIC_BASE or 800H-8FFH; false for any other MSR. Outside x2APIC mode the
 * x2APIC MSRs #GP (13.12.2) - except for the host API, which reaches them in any mode (the
 * xAPIC MMIO page is not modelled).
 */
bool x86_apic_msr_read(CPUX86State *env, uint32_t msr, uint64_t *val, uintptr_t ra)
{
    if (msr == MSR_IA32_APICBASE) {
        *val = env->apic_base;
        return true;
    }
    if (msr < 0x800 || msr > 0x8ff) {
        return false;
    }
    *val = 0;
    if (!apic_x2(env) && !env->msr_api) {
        goto gp;
    }
    switch (msr) {
    case 0x802:
        *val = x86_apic_id(env);
        break;
    case 0x803:
        *val = APIC_VERSION;
        break;
    case 0x808:
        *val = env->apic_tpr;
        break;
    case 0x80a:
        *val = apic_ppr(env);
        break;
    case 0x80d:
        *val = env->apic_ldr;
        break;
    case 0x80f:
        *val = env->apic_svr;
        break;
    case 0x810: case 0x811: case 0x812: case 0x813:
    case 0x814: case 0x815: case 0x816: case 0x817:
        *val = env->apic_isr[msr - 0x810];
        break;
    case 0x818: case 0x819: case 0x81a: case 0x81b:
    case 0x81c: case 0x81d: case 0x81e: case 0x81f:
        *val = apic_load(&env->apic_tmr[msr - 0x818]);
        break;
    case 0x820: case 0x821: case 0x822: case 0x823:
    case 0x824: case 0x825: case 0x826: case 0x827:
        *val = apic_load(&env->apic_irr[msr - 0x820]);
        break;
    case 0x828:
        *val = env->apic_esr;
        break;
    case 0x82f:
    case 0x832: case 0x833: case 0x834: case 0x835: case 0x836: case 0x837:
        *val = env->apic_lvt[apic_lvt_index(msr)];
        break;
    case 0x830:
        *val = env->apic_icr;
        break;
    case 0x838:
        *val = env->apic_tmict;
        break;
    case 0x839:
        *val = 0;               /* current count: the timer never runs (docs/apic.md) */
        break;
    case 0x83e:
        *val = env->apic_tdcr;
        break;
    default:
        /* EOI and SELF IPI are write-only; 80EH (DFR), 831H and the rest reserved */
        goto gp;
    }
    return true;
gp:
    if (!env->msr_api) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    *val = 0;
    return true;
}

/* WRMSR of IA32_APIC_BASE or 800H-8FFH; false for any other MSR (see x86_apic_msr_read) */
bool x86_apic_msr_write(CPUX86State *env, uint32_t msr, uint64_t val, uintptr_t ra)
{
    int i;

    if (msr == MSR_IA32_APICBASE) {
        apic_write_base(env, val, ra);
        return true;
    }
    if (msr < 0x800 || msr > 0x8ff) {
        return false;
    }
    if (!apic_x2(env) && !env->msr_api) {
        goto gp;
    }
    /* bits 63:32 are reserved in every register but the ICR (Table 13-6 note 2) */
    if (msr != 0x830 && (val >> 32)) {
        goto gp;
    }
    switch (msr) {
    case 0x808:
        if (val & ~0xffULL) {
            goto gp;
        }
        env->apic_tpr = (uint32_t)val;
        break;
    case 0x80b:
        if (val) {
            goto gp;            /* WRMSR of a non-zero value causes #GP(0) */
        }
        x86_apic_eoi(env);
        break;
    case 0x80f:
        if (val & ~(uint64_t)APIC_SVR_VALID) {
            goto gp;
        }
        env->apic_svr = (uint32_t)val;
        if (!(val & APIC_SVR_ENABLE)) {
            /* software-disabled: every LVT mask bit is set (13.4.7.2) */
            for (i = 0; i < 7; i++) {
                env->apic_lvt[i] |= APIC_LVT_MASKED;
            }
        }
        break;
    case 0x828:
        if (val) {
            goto gp;            /* only 0 may be written (13.5.3) */
        }
        /* loads the errors logged since the last write and re-arms the error interrupt */
        env->apic_esr = apic_xchg(&env->apic_esr_latch, 0);
        env->apic_err_armed = 1;
        break;
    case 0x82f:
    case 0x832: case 0x833: case 0x834: case 0x835: case 0x836: case 0x837: {
        int idx = apic_lvt_index(msr);
        uint32_t v = (uint32_t)val;

        if (v & ~(apic_lvt_rw[idx] | apic_lvt_ro[idx])) {
            goto gp;
        }
        v = (v & apic_lvt_rw[idx]) | (env->apic_lvt[idx] & apic_lvt_ro[idx]);
        if (!(env->apic_svr & APIC_SVR_ENABLE)) {
            v |= APIC_LVT_MASKED;   /* attempts to clear the mask are ignored (13.4.7.2) */
        }
        env->apic_lvt[idx] = v;
        break;
    }
    case 0x830:
        if (val & ~APIC_ICR_VALID) {
            goto gp;
        }
        if (!apic_icr_supported(env, val, ra)) {
            return true;
        }
        env->apic_icr = val;
        apic_send_icr(env, val);
        break;
    case 0x838:
        if (val) {
            /* a non-zero initial count starts the timer, which is not modelled */
            if (env->msr_api) {
                return true;
            }
            apic_unsupported(env, ra);
        }
        env->apic_tmict = 0;
        break;
    case 0x83e:
        if (val & ~0xbULL) {
            goto gp;            /* divide value: bits 0, 1 and 3 */
        }
        env->apic_tdcr = (uint32_t)val;
        break;
    case 0x83f:
        if (val & ~0xffULL) {
            goto gp;
        }
        apic_self_ipi(env, (int)val);
        break;
    default:
        /* read-only registers (WRMSR #GP, Table 13-6 note 1) and reserved addresses */
        goto gp;
    }
    x86_apic_update(env);
    return true;
gp:
    if (!env->msr_api) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    return true;
}

/*
 * Delivers an external interrupt (vector from the local APIC, or 2 for an NMI) to the core.
 * Default (Unicorn style): like an exception, the vector goes to UC_HOOK_INTR with RIP at the
 * interrupted instruction boundary, and UC_CTL_X86_EXCEPTION reports it with external = 1;
 * without a UC_HOOK_INTR hook the emulation stops with UC_ERR_EXCEPTION.
 */
void QEMU_NORETURN x86_apic_deliver_event(CPUX86State *env, int vector)
{
    CPUState *cs = env_cpu(env);
    struct uc_x86_exception *e = &env->uc->x86_exc;

    memset(e, 0, sizeof(*e));
    e->vector = vector;
    e->external = 1;
    cs->exception_index = vector;
    env->error_code = 0;
    env->exception_is_int = 0;
    env->exception_next_eip = env->eip;
    cpu_loop_exit(cs);
}
#endif /* __Use_Original_Qemu (U960) */
