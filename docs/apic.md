# Local APIC / x2APIC model (ledgers U960-U963)

User decision A12 (2026-10-09). Source: `unicorn/qemu/target/i386/apic_model.c`; reference: Intel
SDM Vol3A chapter 13 (APIC) and chapter 9 (user interrupts), revision 092. All expected values in
the tests are **specification-validated** (independent reading of the SDM): the i5-13600K's local
APIC cannot be read or programmed from user mode, so nothing here is hardware-validated.

## Design

### State
One local APIC per vCPU, stored in `CPUX86State` (`apic_*`, inside the reset area, so
`uc_context_save` / `uc_context_restore` carry it; a restore re-evaluates pending interrupts):
IA32_APIC_BASE, TPR, LDR, DFR (reset value only), SVR, ESR (+ the error latch), ICR, the seven LVT
entries (CMCI, timer, thermal, performance monitoring, LINT0, LINT1, error), timer initial count
and divide configuration, and the 256-bit IRR / ISR / TMR. The x2APIC ID is not stored: it is the
vCPU's initial APIC ID (`x86_apic_id`), the same value CPUID reports (CPUID.0BH:EDX, and its bits
7:0 in CPUID.01H:EBX[31:24]): `UC_CTL_X86_APIC_ID` if given, else the `UC_CTL_X86_CPUID` profile's
leaf 0BH (else leaf 1) value, else the CPU model's (0). The xAPIC ID is its bits 7:0.

### Several vCPUs: engines on an APIC bus (U961)
Unicorn runs one CPU per `uc_engine`, and its TCG state, memory map, hooks and context API are
all per engine. Two designs were weighed:
- one engine with N CPU states: every API that names "the CPU" (`uc_reg_*`, contexts, hooks,
  `uc_emu_start`, the TB cache, the TLB) would need a vCPU selector and a scheduler inside
  `uc_emu_start` - a rewrite of Unicorn's single-CPU core;
- **several engines sharing an APIC bus** (chosen): each engine stays an ordinary Unicorn
  instance; `UC_CTL_X86_APIC_BUS` connects their local APICs. The host drives the engines
  (for example round-robin from one host thread with `uc_emu_start(..., count)`), exactly as a
  scheduler would run vCPUs. Guest memory the vCPUs share is mapped into each engine from the
  same host buffer with `uc_mem_map_ptr`.

API:
- `uc_ctl_set_x86_apic_id(uc, id)` / `uc_ctl_get_x86_apic_id(uc, &id)` (`UC_CTL_X86_APIC_ID`): the
  vCPU's x2APIC ID; CPUID.01H:EBX[31:24] and CPUID.0BH/1FH:EDX follow it. FFFF_FFFFH is reserved
  (13.12.1.3) and IDs on one bus must differ (`UC_ERR_ARG`). Changing it in x2APIC mode
  re-derives the LDR. Reading initialises the engine.
- `uc_ctl_set_x86_apic_bus(uc, peer)` (`UC_CTL_X86_APIC_BUS`): joins the bus of `peer` (created
  with the peer as its first member if it has none); `NULL` leaves; `uc_close` leaves. The
  joining engine is an AP: its IA32_APIC_BASE.BSP is 0 (also after a reset); the creator keeps
  BSP = 1.
- An IPI reaches every APIC on the sender's bus whose destination matches. For another engine
  the IRR bit (and TMR bit) are set atomically and the target gets `CPU_INTERRUPT_POLL` plus a
  TB-exit request; it evaluates its APIC at its next instruction boundary, i.e. when the host
  next runs it (immediately if it is running on another host thread).
- Concurrency: the IRR / TMR / error words and the interrupt-request word are updated with
  atomic operations, but QEMU's own updates of the interrupt-request word are not atomic, so
  running bus members concurrently on several host threads is **not validated**; the tests run
  them one at a time from one thread. Joining / leaving / closing must not race with running
  engines. Translated code is per engine: guest code written through a shared mapping by one
  engine is not invalidated in another engine's TB cache.

### Reset (13.4.7.1, 13.12.5.1)
IA32_APIC_BASE = FEE00900H (base FEE00000H, EN = 1, EXTD = 0, BSP = 1; an AP on a bus: FEE00800H):
enabled, xAPIC mode,
**software-disabled** (SVR = 000000FFH). IRR / ISR / TMR / ICR / LDR / TPR / timer registers 0,
LVT entries 00010000H (masked), DFR FFFFFFFFH, ESR 0.

### IA32_APIC_BASE (1BH; 13.4.4, 13.12.1, Figure 13-27, Table 13-5)
- Reserved bits (#GP(0)): 7:0, 9, 63:MAXPHYADDR (MAXPHYADDR = the CPU's physical-address width,
  CPUID 80000008H), and 10 (EXTD) when CPUID.01H:ECX.x2APIC = 0.
- Invalid transitions (#GP(0)): to EN = 0 / EXTD = 1; x2APIC -> xAPIC; disabled -> x2APIC.
- EN 1 -> 0: the APIC registers return to their power-up values (the ID is kept); CPUID.01H:EDX.APIC
  reads 0 while EN = 0 (13.4.3).
- xAPIC -> x2APIC: LDR = (x2APIC ID[19:4] << 16) | (1 << x2APIC ID[3:0]) (13.12.10.2), ICR[63:32]
  cleared (not preserved, 13.12.5); everything else kept.
- The BSP flag (bit 8) is the processor's role: WRMSR does not change it.
- The base field is stored; nothing is mapped there (xAPIC MMIO is not modelled).

### x2APIC MSRs 800H-8FFH (Table 13-6, 13.12.1.3, 13.12.2)
| MSR | register | access |
|---|---|---|
| 802H | x2APIC ID | RO |
| 803H | version = 01060015H (version 15H, Max LVT Entry 6, EOI-broadcast suppression) | RO |
| 808H | TPR (bits 7:0) | RW |
| 80AH | PPR | RO |
| 80BH | EOI | WO, non-zero #GP |
| 80DH | LDR (logical x2APIC ID) | RO |
| 80FH | SVR (bits 7:0 vector, 8 enable, 12 EOI-broadcast suppression) | RW |
| 810H-817H / 818H-81FH / 820H-827H | ISR / TMR / IRR | RO |
| 828H | ESR | RW, non-zero #GP |
| 82FH, 832H-837H | LVT CMCI, timer, thermal, perf. mon., LINT0, LINT1, error | RW |
| 830H | ICR (64-bit) | RW |
| 838H / 839H / 83EH | timer initial count / current count / divide configuration | RW / RO / RW |
| 83FH | SELF IPI (bits 7:0) | WO |

- Outside x2APIC mode every MSR in 800H-8FFH raises #GP(0) (13.12.2).
- RDMSR of a write-only register, WRMSR of a read-only register, any other address in 800H-8FFH
  (80EH, 831H, ...), a set reserved bit, and bits 63:32 of any register but the ICR: #GP(0).
- LVT fields per Figure 13-8; the delivery-status (12) and remote-IRR (14) bits are read-only (always
  0: the model accepts every interrupt at once); timer bit 18 is reserved (no TSC-deadline mode,
  CPUID.01H:ECX.TSC_DEADLINE = 0).
- SVR[8] = 0 (software-disabled) sets every LVT mask bit; writes cannot clear them (13.4.7.2).
- The **host API** (`uc_reg_read` / `uc_reg_write` with `UC_X86_REG_MSR`) reaches 800H-8FFH in
  any APIC mode (the xAPIC MMIO page is not modelled) and drops an invalid write instead of
  raising #GP (the convention of the other MSRs). The ICR is interpreted in the xAPIC format
  (destination in bits 63:56) when written through the API in xAPIC mode.

### TPR, PPR, CR8 (13.8.3.1, 13.8.6.1)
- MOV CR8 writes TPR[7:4] = CR8[3:0], TPR[3:0] = 0; MOV from CR8 reads TPR[7:4]. CR8[63:4] set:
  #GP(0).
- PPR[7:4] = max(TPR[7:4], ISRV[7:4]); PPR[3:0] = TPR[3:0] when TPR[7:4] >= ISRV[7:4], else 0
  (the equal case is model-specific in the SDM; this model takes TPR[3:0]).

### Accepting and delivering interrupts (13.8)
- A fixed interrupt sets its IRR bit (TMR bit: 1 level, 0 edge); a second one with the same vector
  collapses into the IRR bit. Vectors 0-15 are never put in the IRR: Receive Illegal Vector.
- A globally disabled APIC (EN = 0) accepts nothing. A software-disabled one (SVR[8] = 0) accepts
  NMIs, discards fixed interrupts and holds its IRR / ISR without dispatching them.
- Dispatch: the highest IRR vector whose class (bits 7:4) is above PPR[7:4] is "INTR"
  (`CPU_INTERRUPT_HARD`); the core takes it at an instruction boundary when RFLAGS.IF = 1 and no
  interrupt shadow (STI, MOV SS) blocks it. The acknowledge moves it from the IRR to the ISR.
- EOI clears the highest ISR bit. A level-triggered vector would also broadcast an EOI message to
  the I/O APICs unless SVR[12] = 1: the model has no I/O APIC, so that message has no receiver.
- NMI (delivery mode 100b) bypasses IRR / ISR / PPR / RFLAGS.IF; NMIs are blocked from delivery
  until the next IRET (SDM Vol3A 7.7).
- Spurious interrupts (13.9) do not arise: acknowledge and priority evaluation are one step at an
  instruction boundary, so there is no INTR/INTA window. The SVR vector is stored only.
- Delivery of the accepted interrupt (Unicorn has no IDT delivery for exceptions): the vector is
  handed to `UC_HOOK_INTR` like an exception, with RIP at the interrupted instruction boundary;
  `UC_CTL_X86_EXCEPTION` reports it with `external = 1` (the former `reserved` byte of
  `uc_x86_exception`). The hook plays the interrupt handler: the ISR bit stays set until an EOI
  (guest WRMSR 80BH, or the host writing MSR 80BH = 0). Without a `UC_HOOK_INTR` hook the
  emulation stops with `UC_ERR_EXCEPTION`, as for an unhandled exception.

### Errors (13.5.3)
- ESR bits used: 4 Redirectable IPI, 5 Send Illegal Vector, 6 Receive Illegal Vector. Bit 7
  (Illegal Register Address) needs the xAPIC MMIO page; bits 0-3 are P6/Pentium only.
- Errors are logged in a latch; a write of 0 to the ESR loads the latch into the ESR, clears the
  latch and re-arms the error interrupt. The LVT error interrupt (edge, fixed) is signalled once
  per arming when the LVT entry is unmasked; a masked entry still consumes the arming.
- A fixed IPI with an illegal vector: the sender logs Send Illegal Vector and the message is still
  sent; each receiver logs Receive Illegal Vector (a SELF IPI with an illegal vector logs both).
- Writing an illegal vector into an LVT entry does not log an error (the SDM says "may").

### Sending IPIs (13.6, 13.12.9-13.12.11)
- ICR write (x2APIC: one WRMSR 830H; reserved bits 12, 13, 16, 17, 31:20 #GP). Delivery modes:
  000b fixed and 100b NMI are sent; 001b lowest priority (reserved in the x2APIC ICR, Figure
  13-28): this APIC "does not support the sending of lowest-priority IPIs" - nothing is sent and
  ESR bit 4 (Redirectable IPI) is logged, also with an illegal vector (13.5.3).
- Trigger mode / level bits are ignored: IPIs are edge-triggered (Table 13-3 note 2).
- Destination: shorthand Self / All Including Self / All Excluding Self; otherwise physical (32-bit
  x2APIC ID; FFFF_FFFFH broadcast) or logical x2APIC cluster mode (LDR[31:16] = destination[31:16]
  and a common bit in bits 15:0; FFFF_FFFFH broadcast). An xAPIC-format message (8-bit
  destination) matches the receivers' xAPIC IDs (x2APIC ID[7:0]), FFH broadcast. Logical messages
  reach only receivers in x2APIC mode (the xAPIC LDR / DFR are MMIO registers, not modelled).
- No destination APIC: the message is discarded, no error (13.8.1; Send Accept Error is
  P6/Pentium only).
- SELF IPI (83FH): a self-targeted, edge-triggered fixed interrupt; logged in the IRR when the WRMSR
  completes and delivered at the next instruction boundary that allows it.

### User-interrupt notifications (U962; SDM Vol2B SENDUIPI, Vol3A 9.5)
- SENDUIPI posts in the UPID (unchanged, U104) and, when it sets ON, sends "an ordinary IPI with
  vector NV" through its local APIC: to the 32-bit physical APIC ID NDST in x2APIC mode, to the
  8-bit ID NDST[15:8] in xAPIC mode; fixed, edge, no shorthand, so it reaches any vCPU on the APIC
  bus (or nobody: discarded). A globally disabled APIC (EN = 0) sends nothing (13.4.3: "as if no
  APIC"); NV 0-15 follows the ICR error rules.
- The receiving vCPU treats it like any fixed interrupt: IRR, PPR, RFLAGS.IF, interrupt shadow
  (pending, not dropped, while IF = 0 or the PPR blocks it). When its APIC dispatches the vector
  and CR4.UINTR = IA32_EFER.LMA = 1 and the vector is UINV, user-interrupt notification
  identification writes 0 to EOI and notification processing (9.5.2: ON := 0, PIR -> UIRR, with
  supervisor accesses to the UPID at IA32_UINTR_PD) follows at the same boundary; no event is
  delivered. Any other vector (or CR4.UINTR = 0) is an ordinary interrupt.
- A notification needs a software-enabled APIC at the receiver (SVR[8] = 1; reset leaves it
  disabled, see Reset). The U104 tests and `cases_keylocker.txt` enable it (x2APIC mode +
  SVR 1FFH, or the host API's SVR write).
- Several vCPUs share the UITT / UPID through memory mapped into each engine from the same host
  buffer (`uc_mem_map_ptr`).
- Not modelled: the EXT bit in the error code of a fault during notification processing (9.5.2);
  notifications sent by agents other than SENDUIPI (devices); posted-interrupt virtualization.

### Not modelled (operations that need these stop the emulation)
These stop `uc_emu_start` with `UC_ERR_INSN_INVALID`, RIP at the instruction, no state changed
(`UC_CTL_X86_EXCEPTION` vector stays -1); a host-API write of the same value is dropped:
- ICR delivery modes SMI (010b), INIT (101b, assert or de-assert), start-up (110b) and the
  reserved encodings 011b / 111b;
- NMI with the Self or All-Including-Self shorthand (invalid combinations, Table 13-3);
- a non-zero timer initial count (838H): the APIC timer (one-shot / periodic countdown, its
  interrupt, the current count) is not modelled; the current count register reads 0.

Not modelled, without a stop (documented here instead):
- the xAPIC MMIO interface at IA32_APIC_BASE[MAXPHYADDR-1:12] (xAPIC-mode software can reach the
  registers only through the host API); the xAPIC ID / LDR / DFR registers, flat / cluster
  logical mode of the xAPIC, ESR bit 7;
- TSC-deadline mode (CPUID.01H:ECX[24] = 0, IA32_TSC_DEADLINE untouched by this model);
- interrupt sources other than IPIs and the APIC error: LINT0 / LINT1 pins, ExtINT / 8259A, the
  thermal sensor, performance-monitoring overflow, CMCI - their LVT entries are registers only,
  nothing ever signals them;
- I/O APIC, MSI, interrupt remapping (VT-d), EOI messages to I/O APICs;
- INIT / SIPI / wait-for-SIPI, the APIC state after INIT;
- spurious interrupts (they cannot arise, see above);
- the arbitration ID / APR (P6 only), focus processor, lowest-priority arbitration.

## Tests
`unicorn/tests/unit/test_x86.c`, block `ap_` (U960-U989): `test_x86_ap_reset_state`,
`test_x86_ap_base_transitions`, `test_x86_ap_tpr_cr8`, `test_x86_ap_self_ipi`,
`test_x86_ap_priority_eoi`, `test_x86_ap_nmi`, `test_x86_ap_errors`, `test_x86_ap_unsupported`,
`test_x86_ap_no_hook`, `test_x86_ap_context`, `test_x86_ap_bus_api`, `test_x86_ap_cross_ipi`,
`test_x86_ap_multi_vcpu`, `test_x86_ap_uintr_cross`, `test_x86_ap_uintr_if0`,
`test_x86_ap_uintr_cross_delivery` (CPL3 user-interrupt delivery on the other vCPU); the U104 tests
`test_x86_uintr_senduipi` / `test_x86_uintr_delivery` and `Emulator/data/cases_keylocker.txt`
(SENDUIPI lines) run with the APIC enabled.
