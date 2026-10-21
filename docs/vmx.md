# VMX model (plan 1.J.12, ledger U1300-U1399)

User decisions: A11 (2026-10-09, "implement FRED / VMX / TDX / LAM"); Gate 1 follow-up (2026-10-09, "VMX = full SDM
VMX (VMCS, entry/exit, EPT, VPID, APIC virtualization)", on the local APIC model of `docs/apic.md`); start
2026-10-11. Opt-in `UC_X86_FEATURE_VMX`, default off: without it CPUID, CR4, the MSR list and every VMX opcode
behave exactly as before. U1660 (plan 1.J.59, user decision "Profiles enable VMX" 2026-10-19): a strict
`UC_CTL_X86_CPUID` profile that reports CPUID.01H:ECX.VMX[5] = 1 - both i5-13600K profiles do - turns the same
model on ("Platform profiles" below); without a profile the default stays off.

**Validation label: specification-validated only.** The i5-13600K supports VMX, but the VMX instructions need
CPL 0 and the test host is a VMware guest; Intel SDE cannot run CPL 0 VMX code either. Every expected value in the
tests comes from an independent reading of the SDM (and, where noted, the Python reference
`Emulator\tools\isa\ref_vmx.py`). Nothing here is hardware-validated or SDE-validated.

Sources (lookup order of `AGENT_RULES.md`): Intel SDM 325462-092 (June 2026; volumes 326019-092 Vol3C and
332831-092 Vol3D, text `emulator\intel_docs_text\sdm_vol3c_092.md` / `sdm_vol3d_092.md`), compared with SDM 093
(`emulator\Intel docs\current`, 326019-093 / 332831-093) for the chapters change document 252046-085 lists as
changed (Vol3C 28, 29, 35, 36; Vol3D 42). Page numbers below are the printed page labels of 092 (same chapter and
section numbering in 093). QEMU 11.1 has no TCG VMX (its nested virtualization in TCG is SVM only), so no QEMU
code is reused; the SVM plumbing of this tree (`svm_helper.c`) is the pattern only.

## 093 changes that matter here

| 093 change (252046-085) | 092 text | 093 text | model |
|---|---|---|---|
| Vol3C 29.3.1.1 (p. 29-9): DR7 field check | "bits 63:32 in the DR7 field must be 0" | "bits in the DR7 field corresponding to bits reserved in DR7 must be 0" (upper DR7 bits are no longer all reserved: DRx_PT_LOG, Vol3C 36.7.2.3) | 093 wording: the reserved bits of the model's DR7. The model has no Intel PT, so DR7[63:32] stay reserved: same result as 092 |
| Vol3C 29.5 (p. 29-20) TAPT with FRED | - | VM exits caused by TAPT during VM entry with FRED | not applicable: no Intel PT (IA32_VMX_MISC[14] = 0, no "Intel PT uses guest physical addresses" control) |
| Vol3C 28.2 footnote on bus locks | - | clarification | not applicable: "VMM bus-lock detection" is not enumerated |
| Vol3C 35 (SEAM), 36 (PT), Vol3D 42 (SGX) | - | document names / PT packet / SGX SVN | not part of this model (TDX = plan 1.J.13: Stage A opt-in U1331-U1335, Stage B design, docs/tdx.md; SGX = agent/sgx) |

## Capability set (our choice, labelled)

The VMX capability MSRs report a fixed, self-consistent set chosen for the model (Vol3D Appendix A, pp. A-1 to A-9;
Vol4 Table 2-2 480H-493H, pp. 2-46 to 2-48). Controls outside this set are reserved (VM entry fails with
VM-instruction error 7) and their VMCS fields do not exist (VMREAD / VMWRITE: error 12).

| MSR | value | meaning (Appendix A) |
|---|---|---|
| 480H IA32_VMX_BASIC | 01D8_1000_0000_0001H | revision identifier 1 (our choice); VMCS / VMXON region 4096 bytes; bit 48 = 0 (full MAXPHYADDR); no dual-monitor treatment (49 = 0); memory type WB (53:50 = 6); INS/OUTS instruction information (54); TRUE_* MSRs (55); any hardware exception may be injected with or without an error code (56); nested-exception support (58) = 1 only with FRED (U1309, value 05D8_1000_0000_0001H; without FRED 0 - the value shown; U1733: this row said "0 until the FRED-VMX stage") |
| 481H / 48DH PINBASED / TRUE_PINBASED | 0000_00FF_0000_0016H | external-interrupt exiting, NMI exiting, virtual NMIs, VMX-preemption timer, process posted interrupts; default1 bits 1, 2, 4 stay 1 |
| 482H PROCBASED | FFFB_FFFE_0401_E172H | every primary control except bits 0 and 18 (reserved); tertiary controls (17) |
| 48EH TRUE_PROCBASED | FFFB_FFFE_0400_6172H | as 482H; CR3-load / CR3-store exiting (15, 16) may be 0 |
| 48BH PROCBASED2 | 0E55_7BFF_0000_0000H; with SGX 0E55_FBFF_0000_0000H (U1313: + enable ENCLS exiting, bit 15; U1324: + EPT-violation #VE, bit 18) | virtualize APIC accesses, EPT, descriptor-table exiting, RDTSCP, virtualize x2APIC mode, VPID, WBINVD exiting, unrestricted guest, APIC-register virtualization, virtual-interrupt delivery, RDRAND exiting, INVPCID, VM functions, VMCS shadowing, RDSEED exiting, XSAVES/XRSTORS, mode-based execute control for EPT, TSC scaling, user wait and pause, PCONFIG. Not offered: PAUSE-loop exiting, PML, PT controls, PASID translation, sub-page write permissions, bus-lock detection, instruction timeout |
| 492H PROCBASED3 | LOADIWKEY exiting (bit 0) with Key Locker, enable MSR-list instructions (bit 6) with MSRLIST, enable PBNDKB (bit 9) with PBNDKB, as the CPU model enumerates them; U1331: SEAM guest-physical address width (bit 5, which also enumerates the SEAM instructions) with UC_X86_FEATURE_SEAM (docs/tdx.md) | |
| 483H / 48FH EXIT / TRUE_EXIT | 007F_EFFF_0003_6DFFH / 007F_EFFF_0003_6DFBH (U1317: bit 12 was wrongly offered by U1301-U1316) | save debug controls (may be 0 in TRUE), host address-space size, acknowledge interrupt on exit, save / load IA32_PAT, save / load IA32_EFER, save VMX-preemption timer value. Not offered (stage 8 or later): load IA32_PERF_GLOBAL_CTRL, clear IA32_BNDCFGS, PT, clear IA32_LBR_CTL, clear UINV, load CET state, load PKRS. U1309: with FRED, "activate secondary controls" (bit 31) and the secondary VM-exit controls save FRED / load FRED (493H = 3) |
| 484H / 490H ENTRY / TRUE_ENTRY | 0000_D3FF_0000_11FFH / 0000_D3FF_0000_11FBH | load debug controls (may be 0 in TRUE), IA-32e mode guest, load IA32_PAT, load IA32_EFER. Not offered: entry to SMM / deactivate dual-monitor treatment (no dual-monitor treatment), PERF_GLOBAL_CTRL, BNDCFGS, PT, UINV, CET, LBR, PKRS, FRED, SPEC_CTRL. U1331: allow SEAM-guest telemetry (bit 25) with UC_X86_FEATURE_SEAM (docs/tdx.md) |
| 485H MISC | 0000_0000_6004_0065H | preemption timer = TSC >> 5; VM exits store IA32_EFER.LMA into "IA-32e mode guest" (5); activity state HLT (6); 4 CR3-target values; 512 MSRs per list; VMWRITE to any field (29); zero-length software-event injection (30); MSEG revision 0 |
| 486H / 487H CR0_FIXED0 / FIXED1 | 8000_0021H / FFFF_FFFFH | PE, NE, PG fixed to 1 in VMX operation (unrestricted guests excepted, Vol3C 26.8) |
| 488H / 489H CR4_FIXED0 / FIXED1 | 2000H / the CR4 bits the CPU model supports | VMXE fixed to 1 |
| 48AH VMCS_ENUM | 2 x the highest field index of the supported fields | Appendix A.9 |
| 48CH EPT_VPID_CAP | 0000_0F01_0673_41C1H | execute-only, page-walk length 4 and 5, UC and WB paging-structure memory types, 2-MByte and 1-GByte pages, INVEPT (single-context, all-context), EPT A/D flags, advanced VM-exit information for EPT violations, INVVPID (all four types). Not offered: supervisor shadow-stack control (23), HLAT |
| 491H VMFUNC | 1 | EPTP switching |
| 493H EXIT_CTLS2 | without FRED: does not exist (EXIT_CTLS[63] = 0); with FRED (U1309): EXIT_CTLS allowed-1 bit 31 (activate secondary controls) is set and 493H reads 3 (U1733: this row said "does not exist" unconditionally) | the allowed 1-settings of the secondary VM-exit controls: save FRED (0), load FRED (1) |

## Requirement-to-code mapping

Status: **planned** (not yet implemented), **done** (implemented, unit-tested), **pending** (waits for another
agent's work), **not offered** (outside the capability set above: the architecture's "not supported" behaviour).
Code locations are in `unicorn/qemu/target/i386/` unless a path is given.

### Stage 2 - opt-in and capability reporting
| requirement | SDM 092 | code | status |
|---|---|---|---|
| CPUID.01H:ECX.VMX[5] | Vol3C 26.6 p. 26-2; Vol2A CPUID Table 3-10 | `cpu.c` x86_cpu_realizefn (UC_X86_FEATURE_VMX) | done (U1301) |
| CR4.VMXE (bit 13) settable only with VMX | Vol3A 2.5 p. 2-20; Vol3C 26.7 p. 26-3 | `cpu.h` cr4_reserved_bits | done (U1301) |
| IA32_FEATURE_CONTROL lock / bit 1 (VMX in SMX) / bit 2 (VMX outside SMX), reset 0 | Vol3C 26.7 p. 26-3; Vol4 Table 2-2 3AH p. 2-5 | `misc_helper.c` msr_present / msr_write_ok (existing U905 rules, enabled by CPUID.VMX) | done (U1301) |
| capability MSRs 480H-492H (493H with FRED, U1309): values above, R/O (#GP on WRMSR), absent without VMX (#GP on RDMSR), presence conditions of Table 2-2 | Vol3D Appendix A pp. A-1 to A-9; Vol4 Table 2-2 pp. 2-46 to 2-48 | `vmx_helper.c` x86_vmx_msr_exists / x86_vmx_msr_read; `misc_helper.c` msr_present / helper_rdmsr / msr_write_ok | done (U1301) |
| a strict CPUID profile that reports CPUID.01H:ECX.VMX[5] = 1 turns the model on (Intel 64 model, no APX; until the first instruction, then fixed) | SDM 093 Vol3C 26.6 p. 26-2 | `cpu.c` x86_cpu_vmx_profile_sync (from x86_cpu_realizefn and `unicorn.c` x86_cpuid_changed) | done (U1660) |
| emu-alltest `--host` / `--guest` / `--vm`: IA32_FEATURE_CONTROL = 5 at engine start (platform firmware) | Vol3C 26.7 p. 26-3; Vol4 Table 2-2 3AH p. 2-5 | `Emulator\tests\alltest\at_engine.hpp` platform_feature_control / open_configured | done (U1661) |

### Stage 3 - VMX operation, VMCS lifecycle and access
| requirement | SDM 092 | code | status |
|---|---|---|---|
| VMXON: #UD (register operand, CR0.PE = 0, CR4.VMXE = 0, RFLAGS.VM, compatibility mode), #GP (CPL, A20M, CR0 / CR4 fixed bits, IA32_FEATURE_CONTROL), VMfailInvalid (alignment, MAXPHYADDR, revision / bit 31), VMfail 15 in root, VM exit in non-root | Vol3C 33.3 VMXON pp. 33-28 to 33-30; 27.11.5 p. 27-34 | `vmx_helper.c` helper_vmxon; `translate.c` F3 0F C7 /6 | done (U1302) |
| VMXOFF: leave VMX operation, VMfail 23 not reachable (no dual-monitor treatment) | Vol3C 33.3 VMXOFF pp. 33-26 / 33-27 | helper_vmxoff; `translate.c` 0F 01 C4 | done (U1302) |
| VMCLEAR (errors 2, 3; launch state clear; current pointer invalidated) | Vol3C 33.3 VMCLEAR pp. 33-11 / 33-12; 27.1 p. 27-1; 27.11.3 p. 27-34 | helper_vmclear; `translate.c` 66 0F C7 /6 | done (U1302) |
| VMPTRLD (errors 9, 10, 11; shadow-VMCS indicator) / VMPTRST | Vol3C 33.3 pp. 33-17 to 33-20; 27.2 p. 27-3 | helper_vmptrld / helper_vmptrst; `translate.c` NP 0F C7 /6, /7 | done (U1302) |
| VMREAD / VMWRITE: every field of Appendix B with its width (16 / 32 / 64 / natural, full / high access), 64-bit mode vs outside IA-32e mode operand size, error 12 (unsupported field), error 13 (not reachable: IA32_VMX_MISC[29] = 1), VMfailInvalid | Vol3C 27.11.2 pp. 27-32 / 27-33; 33.3 VMREAD pp. 33-21 / 33-22, VMWRITE pp. 33-24 / 33-25; Vol3D Appendix B pp. B-1 to B-13 | `vmx_helper.c` vmx_field table, helper_vmread / helper_vmwrite; `translate.c` NP 0F 78 / 79 | done (U1302) |
| VMsucceed / VMfailInvalid / VMfailValid flag settings and VM-instruction error numbers | Vol3C 33.2 p. 33-2; 33.4 Table 33-1 pp. 33-30 / 33-31 | vmx_succeed / vmx_fail_invalid / vmx_fail_valid | done (U1302) |
| CR0 / CR4 fixed bits in VMX operation: CLTS, LMSW, MOV CR #GP | Vol3C 26.8 pp. 26-3 / 26-4; Appendix A.7 / A.8 p. A-7 | `misc_helper.c` helper_write_crN | done (U1302) |
| VMCALL in VMX root operation: VMfail 1 (no dual-monitor treatment) | Vol3C 33.3 VMCALL pp. 33-9 / 33-10 | helper_vmcall | done (U1302) |
| VMFUNC outside non-root: #UD | Vol3C 33.3 VMFUNC p. 33-13 | helper_vmfunc | done (U1303) |
| INVEPT / INVVPID in root: #UD, #GP, VMfail 28, invalidation | Vol3C 33.3 pp. 33-3 to 33-8; 31.4.3 p. 31-22 | helper_invept / helper_invvpid; `decode-new.c.inc` 66 0F 38 80 / 81 | done (U1302) |
| shadow VMCS (VMREAD / VMWRITE in non-root via the link pointer, VMREAD / VMWRITE bitmaps) | Vol3C 27.10 p. 27-31; 28.1.3 VMREAD / VMWRITE p. 28-5; 33.3 | helper_vmread / helper_vmwrite (vmx_shadow_check) | done (U1303) |

### Stage 4 - VM entry, VM exit, VMLAUNCH / VMRESUME / VMCALL / VMFUNC, preemption timer
| requirement | SDM 092 | code | status |
|---|---|---|---|
| basic VM-entry checks (#UD, #GP, VMfailInvalid, error 26 MOV SS blocking, errors 4 / 5, shadow VMCS current) | Vol3C 29.1 p. 29-2; 33.3 VMLAUNCH/VMRESUME pp. 33-14 to 33-16 | `vmx_helper.c` helper_vmlaunch | done (U1303) |
| checks on VMX controls (execution, exit, entry, event injection) -> error 7 | Vol3C 29.2.1.1-29.2.1.3 pp. 29-3 to 29-7 | vmx_check_controls | done (U1303) |
| checks on host state (CRs, MSRs, segments, address-space size) -> error 8 | Vol3C 29.2.2-29.2.4 pp. 29-7 / 29-8 | vmx_check_host | done (U1303) |
| checks on guest state (CRs, DR7, MSRs, segments, descriptor tables, RIP / RFLAGS, non-register state, PDPTEs) -> exit reason 33 with bit 31 | Vol3C 29.3.1.1-29.3.1.6 pp. 29-9 to 29-16; 093 29.3.1.1 (DR7) | vmx_check_guest | done (U1303); see "Not modelled" for the activity states and SMM |
| loading guest state (CRs, DR7, MSRs, segments, RIP / RSP / RFLAGS, PDPTEs, non-register state, monitor clear) | Vol3C 29.3.2 pp. 29-16 to 29-19; 29.3.3 p. 29-19 | vmx_load_guest | done (U1303) |
| VM-entry MSR-load list -> exit reason 34, qualification = entry number | Vol3C 29.4 pp. 29-19 / 29-20 | vmx_load_msrs | done (U1303) |
| VM-entry failure after guest loading (load host state, exit reason bit 31) | Vol3C 29.8 pp. 29-28 / 29-29 | vmx_entry_fail | done (U1303) |
| event injection (types, error code, instruction length, real-address mode guests), VM exits during injection, pending MTF | Vol3C 29.6 pp. 29-20 to 29-24 | vmx_inject | done (U1303); pending debug exceptions after VM entry (29.7.3): done (U1310) |
| interruptibility / activity state, pending debug exceptions, window exits and TPR threshold right after entry | Vol3C 29.7 pp. 29-24 to 29-28 | vmx_entry_special | partly (U1303: interruptibility, HLT activity, MTF, timer, windows at the first boundary); TPR threshold: stage 7 |
| VM exit: architectural state before the exit, exit reason, qualification, guest-linear / physical address, event information, original-event (IDT-vectoring) information, instruction length / information | Vol3C 30.1-30.2 pp. 30-1 to 30-22; Appendix C pp. C-1 to C-4 | `vmx_helper.c` vmx_vmexit | done for the exits of U1303 (instruction exits, triple fault, timer, MTF, windows); event exits: stage 5 |
| saving guest state, MSR-store list, loading host state, MSR-load list, VMX abort | Vol3C 30.3-30.7 pp. 30-22 to 30-35 | vmx_save_guest / vmx_load_host / vmx_abort | done (U1303); pending debug exceptions saved as 0 |
| VMCALL / VMLAUNCH / VMRESUME / VMX instructions / CPUID / GETSEC / INVD / XSETBV unconditional exits | Vol3C 28.1.2 p. 28-2 | helpers of each instruction | done (U1303) |
| VMFUNC: #UD conditions, EPTP switching (EPTP list, EPTP index), VM exit 59 | Vol3C 28.5.7 pp. 28-20 to 28-22 | helper_vmfunc | done (U1303); the new EPTP takes effect with EPT (stage 6) |
| VMX-preemption timer (count, expiry exit 52, save value, priority, activity states) | Vol3C 28.5.1 p. 28-16; 29.7.4 p. 29-26; 30.3.4 | vmx_timer | done (U1303): evaluated at TB boundaries (time-based, not cycle-exact) |
| monitor trap flag | Vol3C 28.5.2 pp. 28-17 / 28-18 | vmx_mtf | done (U1303); HLT / exceptions with MTF: stage 5 |

### Stage 5 - exiting controls
| requirement | SDM 092 | code | status |
|---|---|---|---|
| priority of faults and VM exits | Vol3C 28.1.1 pp. 28-1 / 28-2 | every exit site | done (U1304) for the instruction exits |
| conditional instruction exits (CLTS, HLT, IN/OUT/INS/OUTS with I/O bitmaps, INVLPG, INVPCID, LGDT/LIDT/LLDT/LTR/SGDT/SIDT/SLDT/STR, LMSW, LOADIWKEY, MONITOR, MOV CR0/3/4/8, MOV DR, MWAIT, PAUSE, PCONFIG, RDMSR / WRMSR / WRMSRNS / RDMSRLIST / WRMSRLIST / URDMSR / UWRMSR with MSR bitmaps, RDPMC, RDRAND, RDSEED, RDTSC, RDTSCP, TPAUSE, UMWAIT, VMREAD, VMWRITE, WBINVD, WBNOINVD, XRSTORS, XSAVES) | Vol3C 28.1.3 pp. 28-2 to 28-6 | helpers of each instruction; `translate.c` / `emit.c.inc` gen_vmx_insn -> helper_vmx_insn | done (U1304); ENCLS: pending (SGX); PAUSE-loop exiting: not offered |
| exit qualifications and instruction information of these exits | Vol3C 30.2.1 Tables 30-1 to 30-7, 30.2.5 Tables 30-8 to 30-14 | vmx_vmexit callers | done (U1304) for the instruction exits (Tables 30-3 to 30-5, 30-8 to 30-16); debug / #PF / task switch / EPT: with their exits |
| changed behaviour in non-root operation (CR0 / CR4 shadows, CR8 / TPR shadow, RDTSC / RDTSCP / RDMSR TSC offsetting and scaling, IRET and NMI blocking, enable-controls #UD for RDTSCP / INVPCID / XSAVES / UMWAIT / TPAUSE / UMONITOR / PCONFIG / RDMSRLIST / WRMSRLIST / PBNDKB, MOV to CR3 PDPTEs, ...) | Vol3C 28.3 pp. 28-8 to 28-15 | helpers of each instruction | done (U1304): CR0 / CR4 shadows and masks (MOV CR, CLTS, LMSW, SMSW), unrestricted-guest CR0 checks, TSC offsetting / scaling, WRMSR 79H, the enable-control #UDs; CR8 / TPR shadow: stage 7; IRET / NMI blocking: done (U1305); MOV to CR3 with EPT: stage 6 |
| exception bitmap, #PF error-code mask / match, triple fault, external interrupts (acknowledge interrupt on exit), NMIs, interrupt window, NMI window, task switches | Vol3C 28.2 pp. 28-6 to 28-8; 28.4 pp. 28-15 / 28-16 | `excp_helper.c` raise path, `seg_helper.c` x86_cpu_exec_interrupt / switch_tss | done (U1305) |
| original-event (IDT-vectoring) information of every event delivered in non-root operation, NMI unblocking due to IRET, IRET with NMI exiting / virtual NMIs, the HLT activity state left by VMX events | Vol3C 30.2.3, 30.2.4, 28.3 IRET, 30.1, 30.3.4 | `vmx_helper.c` x86_vmx_delivery_start / x86_vmx_iret_* / x86_vmx_hlt | done (U1305) |

### Stage 6 - EPT, VPID
| requirement | SDM 092 | code | status |
|---|---|---|---|
| EPTP format and checks, 4- and 5-level EPT walks, 1-GByte / 2-MByte pages, execute-only, mode-based execute control | Vol3C 27.6.11 p. 27-17; 31.3.2 pp. 31-3 to 31-10 | `excp_helper.c` get_hphys -> x86_vmx_ept_translate | done (U1306) |
| EPT misconfigurations / violations, qualification, guest-linear / guest-physical address, prioritization | Vol3C 31.3.3 pp. 31-10 to 31-16; 30.2.1 Table 30-7 | x86_vmx_ept_translate | done (U1306) |
| EPT accessed / dirty flags | Vol3C 31.3.5 pp. 31-18 / 31-19 | x86_vmx_ept_translate | done (U1306) |
| guest PDPTEs with EPT (PAE paging) | Vol3C 29.3.1.6, 29.3.2.4; 28.3 MOV to CR3; 30.3.4 | vmx_load_guest, vmx_save_guest, helper_write_crN -> x86_vmx_pdpte_load | done (U1306) |
| VPID / INVVPID / INVEPT and the TLB: the model caches no translation across a VM entry, a VM exit, an INVEPT or an INVVPID (the whole TLB is flushed), which every invalidation rule allows | Vol3C 31.1 p. 31-1; 31.4 pp. 31-20 to 31-26 | VM entry / exit, helper_invept, helper_vmfunc: tlb_flush | done (U1302-U1306) |

### Stage 7 - APIC virtualization (local APIC model, `docs/apic.md`)
| requirement | SDM 092 | code | status |
|---|---|---|---|
| virtual-APIC page, TPR shadow / TPR threshold, CR8 virtualization | Vol3C 32.1.1-32.1.2 pp. 32-2; 32.3 p. 32-7; 29.7.7 | `vmx_helper.c` x86_vmx_cr8_*, vmx_tpr_virt | done (U1307) |
| PPR / EOI / self-IPI virtualization, evaluation and delivery of virtual interrupts, EOI-exit bitmap | Vol3C 32.1.3-32.1.5, 32.2 pp. 32-2 to 32-7; 29.3.2.5 | vmx_ppr_virt, vmx_eoi_virt, vmx_self_ipi_virt, vmx_eval_vintr, vmx_vintr_deliver | done (U1307) |
| virtualizing memory-mapped APIC accesses (APIC-access page, APIC-access exits 44, APIC-write exits 56) | Vol3C 32.4 pp. 32-8 to 32-15 | `excp_helper.c` handle_mmu_fault -> x86_vmx_apic_access, vmx_apic_traps | done (U1307) for linear accesses; guest-physical / physical accesses: see the design notes |
| virtualizing MSR-based APIC accesses (x2APIC mode, APIC-register virtualization) | Vol3C 32.5 pp. 32-15 / 32-16 | `misc_helper.c` helper_rdmsr / helper_wrmsr -> x86_vmx_x2apic_* | done (U1307) |
| posted-interrupt processing | Vol3C 32.6 pp. 32-16 to 32-18 | x86_vmx_interrupt -> vmx_posted_intr | done (U1307) |
| SENDUIPI virtualization | Vol3C 32.7 pp. 32-18 | `seg_helper.c` helper_senduipi -> x86_vmx_senduipi | done (U1307) |
| IPI virtualization, APIC-timer virtualization | Vol3C 32.1.6; 28.5.10 | - | not offered (capability set) |

### Stage 8 - deferred items of other agents
| requirement | SDM 092 | code | status |
|---|---|---|---|
| FRED: guest / host FRED state, "load FRED" / "save FRED" controls, secondary VM-exit controls, injected-event data, original-event data, nested-exception bit, IA32_VMX_BASIC[58], FRED event injection (event data, STI blocking, SYSCALL / SYSENTER as type 7), VM-entry checks of the FRED fields and of FRED guests, ERETS / ERETU with NMI exiting | Vol3C 27.4.1 p. 27-6, 27.5 p. 27-9, 27.8.3 p. 27-26, 29.2.1-29.3.2, 29.6.1.1, 30.2.2, 30.2.4, 30.3, 30.5, 28.3; Vol3D A.1 p. A-2, A.4.2 | `vmx_helper.c` vmx_fred*, `seg_helper.c` x86_fred_deliver / fred_eret_end | done (U1309); the opt-ins UC_X86_FEATURE_VMX and UC_X86_FEATURE_FRED may be combined |
| ENCLS exiting: "enable ENCLS exiting" (secondary bit 15, allowed with SGX), the ENCLS-exiting bitmap (202EH), exit reason 60 after the #UD / CPL checks and before the #GP(0) checks | Vol3C 27.6.2, 27.6.16 p. 27-21, 28.1.3; Vol3D ENCLS p. 41-8, 42.5.1 p. 42-3 | `vmx_helper.c` x86_vmx_encls_check, `sgx_model.c` helper_encls | done (U1313) |
| ENCLV, "enable ENCLV exiting", the ENCLV-exiting bitmap, SGX-conflict VM exits | specified in SDM 325462-065 to -087 (ENCLV: opcode NP 0F 01 C0, exit reason 70, ENCLV-exiting bitmap 2036H, "enable ENCLV exiting"; SGX-conflict VM exits: 087 Table 40-1 "SGX Conflict Exit Qualification"), removed in -088 (checked in -064 / -065 / -087 / -088; ISE 319433-062 change list: "Removed ENCLV from Section 14.1", "Removed SGX_CONFLICT VM Exit Qualification column") | - | not modelled (the model follows SDM 092 / 093): ENCLV (NP 0F 01 C0) stays #UD, CPUID.12H.0:EAX[5] = 0, secondary bit 28 not allowed |

### Stage 9 - tests
| requirement | where | status |
|---|---|---|
| independent Python reference: field encodings, capability classes, exit qualifications / instruction information, I/O / MSR bitmaps, exception bitmap, TSC scaling, EPT walk, virtual APIC; `--selftest` checks the unit-test values | `Emulator\tools\isa\ref_vmx.py` | done (U1308) |
| unit tests (prefix `vmx_`) | `unicorn\tests\unit\test_x86.c` (one block) | done (U1301-U1330: 40 tests) |
| registration | `Emulator\tools\test_runner.ps1` (`ref_vmx_selftest`, groups vmx / tools; the unit tests run in `unicorn-test_x86`) | done (U1308) |

## Design

### Opt-in
`uc_ctl_set_x86_features(uc, UC_X86_FEATURE_VMX)` (bit 30, 40000000H; chosen from the top of the free range so
the SGX / SMX bits of agent/sgx can take the low end) before the engine is initialised. It applies to CPU models
that report Intel 64 (CPUID.80000001H:EDX.LM) with the GenuineIntel vendor; a model without them stays without
VMX. With VMX the model withdraws CPUID.80000001H:ECX.SVM (an Intel processor does not report SVM; the SVM and
VMX guest modes would share QEMU's HF_GUEST flag) - our choice. A strict `UC_CTL_X86_CPUID` profile that hides
CPUID.01H:ECX[5] hides VMX like any other feature; one that reports it turns VMX on without the bit (U1660,
"Platform profiles").

### Platform profiles (U1660 / U1661, plan 1.J.59)
User decision "Profiles enable VMX" (2026-10-19), finding of the gating agent (notes\eread\G13_gating_only.md section 3):
both i5-13600K CPUID profiles report CPUID.01H:ECX.VMX[5] = 1 (bare host 01H ECX 7FFAFBFFH, VMware guest 7FFA3223H,
captures Host_clean_20261016 / Run3_clean_guest_probes_20261015), but without UC_X86_FEATURE_VMX MOV to CR4 with VMXE = 1
was #GP(0): a hypervisor driver saw VMX advertised and then failed. SDM 093 Vol3C 26.6 p. 26-2: "If CPUID.01H:ECX.VMX[5]
= 1, then VMX operation is supported."

- **U1660 (engine).** A strict profile (UC_CTL_X86_CPUID_STRICT on, the U435 default while a profile is installed) whose
  CPUID.01H:ECX[5] = 1 turns the VMX model on exactly as the opt-in does (`cpu.c` x86_cpu_vmx_profile_sync: CPUID.01H:ECX.VMX
  in the model, CPUID.80000001H:ECX.SVM withdrawn, so IA32_EFER.SVME becomes reserved as on Intel). Same conditions as the
  opt-in (Intel 64 CPU model); not with UC_CTL_X86_APX (VMX + APX is not modelled, U1302: the profile then reports VMX and
  CR4.VMXE stays reserved, as before). Decided when the CPU is created and after every UC_CTL_X86_CPUID /
  UC_CTL_X86_CPUID_STRICT write until the first instruction (removing the profile or switching strict off before it turns
  VMX off again and gives SVM back); from the first instruction on it is fixed, like the strict profile itself (U1580).
  A non-strict profile only reports CPUID (U68): no change. UC_CTL_X86_FEATURES reads back the mask that was written;
  UC_X86_FEATURE_SEAM still needs the explicit VMX bit. Without a profile nothing changes (the default stays off).
- **U1661 (emu-alltest).** IA32_FEATURE_CONTROL is "cleared to zero when a logical processor is reset"; "to enable VMX
  support in a platform, BIOS must set bit 1, bit 2, or both ... as well as the lock bit" (Vol3C 26.7 p. 26-3; Vol4 Table
  2-2 3AH p. 2-5). The engine keeps the architectural reset value 0. The platform switches `--host`, `--guest` and `--vm`
  model the machine after its firmware ran and write 5 (lock, VMX outside SMX) before the first instruction, where the
  engine models VMX (strict profile, no `--apx`). Source: CPU-Z MSR dumps of both machines (`E:\CPU Info\cpuz-host`, 20
  logical processors, and `cpuz-vmguest`, 8 vCPUs): MSR 3AH = 00000000_00000005H on every logical processor - evidence, not
  a project CPL0 capture (none exists for MSR 3AH). User decision "Use CPU-Z 5 now" (2026-10-19): the value 5 is
  from CPU-Z (host tool), not a project capture; replaced if a kernel-mode probe is run.
  VMX in SMX (bit 1) and the SENTER enables (15:8) are 0 on both.
  `--cpuid FILE` alone (the API path) and `--AMD` write nothing.
- **U1780 (IA32_EFER, plan 1.J.63).** IA32_EFER.SVME follows the strict profile's CPUID.80000001H:ECX.SVM[2] as well as
  the model's (misc_helper.c x86_efer_writable): reserved under both i5-13600K profiles also where the engine does not
  model VMX (`--apx`, a strict profile that hides VMX, an AMD CPU model under Intel rows), where the model's SVM bit had
  made it writable before (VMRUN & co. then no longer #UD). SCE / NXE / FFXSR follow their 80000001H EDX bits the same way.
- **U1800 (IA32_EFER across VM entry / exit, plan 1.J.63.b).** The VM-entry checks reuse the U1780 rule: a guest
  IA32_EFER field with SVME (or any reserved bit) and "load IA32_EFER" is a VM-entry failure, exit reason 80000021H,
  qualification 0 (Vol3C 326019-093 29.3.1.1 p. 29-10, 29.8 p. 29-28); a host field with SVME and the VM-exit "load
  IA32_EFER" is VMfailValid(8) (29.2.2 p. 29-7); an IA32_EFER entry with SVME in the VM-entry MSR-load area is exit reason
  80000022H with its index, in the VM-exit MSR-load area VMX abort 4 (29.4 p. 29-19, 30.6 / 30.7 p. 30-34). New: a VM exit
  with "load IA32_EFER" loads only the field's writable bits, "Bits that are reserved in that MSR are maintained with
  their reserved values" (30.5.1 p. 30-31); before, a host field changed in the VMCS region after the entry check (an
  ordinary memory write, 27.11.1 p. 27-32) set EFER.SVME under an Intel profile. Tests test_x86_efv_* (--guest rows,
  --host rows, VMX opt-in without profile); CPL0 only, so no emu-alltest cases.

| | `--host` (P / E) | `--guest` / `--vm` | `--cpuid` same files (API path) | no profile / `--no-strict` |
|---|---|---|---|---|
| CPUID.01H:ECX.VMX | 1 (profile) | 1 (profile) | 1 | 0 / 1 (reported only) |
| MOV CR4.VMXE = 1 before U1660 | #GP(0) | #GP(0) | #GP(0) | #GP(0) |
| MOV CR4.VMXE = 1 after | accepted | accepted | accepted | #GP(0) |
| RDMSR 3AH before / after | 0 (MSR present through SMX) / 5, locked | #GP(0) (absent) / 5, locked | host 0 / 0, guest #GP(0) / 0 | unchanged |
| RDMSR 480H-492H before / after | #GP(0) / the values below | #GP(0) / the values below | #GP(0) / the values below | #GP(0) |
| WRMSR IA32_EFER.SVME before / after | accepted / #GP(0) | accepted / #GP(0) | accepted / #GP(0) | accepted |
| VMXON / VMXOFF after (CPL0, valid region) | VMsucceed | VMsucceed | #GP(0) until software writes 3AH = 5 | #UD |
| RDMSR 489H CR4_FIXED1 before / after U1680 | 03FF7FFFH / 01FF6FFFH | 03FF7FFFH / 01F72FFFH | as the platform switch | #GP(0) (no VMX) |

Capability MSR values per profile: **model defaults only** (the capability set above; the i5-13600K's own values are not
captured, so nothing is taken from hardware). Identical on all four profiles: 480H 01D81000_00000001H (revision identifier
1, our choice - a VMXON region must carry 1), 481H / 48DH 000000FF_00000016H, 482H FFFBFFFE_0401E172H, 48EH
FFFBFFFE_04006172H, 483H 007FEFFF_00036DFFH, 48FH 007FEFFF_00036DFBH, 484H 0000D3FF_000011FFH, 490H 0000D3FF_000011FBH, 485H
00000000_60040065H, 486H 80000021H, 487H FFFFFFFFH, 488H 2000H, 48AH 3EH, 48BH 0E557BFF_00000000H (no SGX), 48CH
00000F01_067341C1H, 491H 1; 493H absent. Profile-dependent through the profile mask (vmx_feat): 492H PROCBASED_CTLS3 = 1
on `--host` (Key Locker enumerated: LOADIWKEY exiting), 0 on `--guest` / `--vm`. 489H CR4_FIXED1 = `~cr4_reserved_bits`:
U1680 (plan 1.J.61) - every CR4 bit the CPU supports, i.e. the MAX model's bits (03FF7FFFH) restricted to the ones the
strict profile enumerates (cpu.c `x86_cr4_reserved`): **`--host` P / E 01FF6FFFH** (less LA57 12, UINTR 25), **`--guest` /
`--vm` 01F72FFFH** (less LA57 12, SMXE 14, KL 19, UINTR 25). Before U1680 it was 03FF7FFFH on all of them (only VMXE, FRED
and LAM_SUP followed the profile), and MOV CR4 accepted SMXE / KL / UINTR under `--guest` (UINTR also under `--host`)
although the profile hides them. The same mask decides MOV to CR4 (#GP(0)) and the CR4 checks of VMXON and VM entry
(Vol3C 26.8 p. 26-3, Vol3D A.8 p. A-7); without a profile or with `--no-strict` the model's 03FF7FFFH stands.
Details, the reset CR4 per profile and the CPUID bit of each CR4 bit: `docs/quirks.md` "CR4 at engine start".

Tests: `test_x86_vpf_profiles`, `_profile_timing`, `_profile_off` (unit); expected-value files
`Emulator\data\cases_vmx_profile_host.txt` (`--host`, `--host --host-core E`), `cases_vmx_profile_guest.txt` (`--guest`,
`--vm`), `cases_vmx_profile_api.txt` (`--cpuid` of the three profile files: the lock / bit-2 #GP rules),
`cases_vmx_noprofile.txt` (no profile, `--no-strict`); suites `vmx_prof_*`. Specification-validated only (CPL0). U1680 / U1681:
`test_x86_cr4p_profiles`, `_each_bit`, `_off` (unit), `cases_cr4_profile_host.txt` / `_guest.txt` / `cases_cr4_noprofile.txt`
(suites `cr4_prof_*`).

### VMCS storage (implementation-specific, Vol3C 27.2 p. 27-3)
The model keeps no VMCS data on the processor: every field lives in the VMCS region in guest memory. Bytes 0-3:
revision identifier and shadow-VMCS indicator, 4-7: VMX-abort indicator (both architectural); byte 8: launch state
(0 clear, 1 launched); from byte 16, one 8-byte slot per field of the model's field table (Appendix B order).
VMCLEAR writes only the launch state. Because nothing is cached, "active" VMCSs need no write-back and VMXOFF
corrupts nothing (Vol3C 27.11.1 allows corruption; VM-instruction error 6 is therefore never produced - our
choice). Software that writes a VMCS region with ordinary stores changes the fields (undefined by the SDM).

### Decoding (U1302)
With CPUID.01H:ECX.VMX the translator decodes: 0F 01 C1 VMCALL and C4 VMXOFF (prefixes as below),
F3 0F C7 /6 VMXON (66
ignored, XED IGNORE66), 66 0F C7 /6 VMCLEAR, NP 0F C7 /6 VMPTRLD, NP 0F C7 /7 VMPTRST (memory forms;
F2 / LOCK #UD), NP 0F 78 VMREAD, NP 0F 79 VMWRITE (LOCK #UD; the 66 / F2 forms keep their old decoding),
66 0F 38 80 INVEPT and 81 INVVPID (memory only). VMLAUNCH, VMRESUME and VMFUNC follow in U1303. The
VM-exit instruction information (30.2.5 Tables 30-9, 30-14, 30-15) and exit qualification (30.2.1, the
displacement; RIP-relative: displacement + next RIP) of these instructions are computed by the
translator and passed to the helpers.
- Prefixes on 0F 01 C1 VMCALL, C2 VMLAUNCH, C3 VMRESUME, C4 VMXOFF (U1302 / U1303, sources U1312;
  audit `notes\vmx_prefix_lam_audit_2026-10-11.md`): LOCK #UD (SDM 093 Vol2A LOCK p. 3-565); REX
  ignored (Vol2A 2.2.1 p. 2-8); F2 / F3 / 66 / 67 and segment overrides are reserved in the SDM
  (Vol2A 2.1.1 p. 2-2; Vol2D Table A-6 p. A-18: the 66 / F2 / F3 cells of these opcodes are blank),
  resolved by the Intel VT-x specification C97063-002 (public; local copies of -001 / -002 in
  E:\pdfs\x86docs\Intel\VT-x\) ch. 7 "Use of Prefixes" (VMCALL p. 7-4,
  VMLAUNCH / VMRESUME p. 7-10, VMXOFF p. 7-24; C97063-001 ch. 4 the same): F2 / F3 / 66 #UD,
  segment overrides and 67 ignored. The #UD is a decode fault (Vol3A Table 7-2): before the CPL
  #GP(0) in VMX root operation and before the VM exit in VMX non-root operation (Vol3C 28.1.1).
  The VM-exit instruction length counts the prefixes (30.2.5). XED (no_refining_prefix) agrees and
  is a labelled reference only. Status: **PROVISIONAL** until VMX hardware confirms it. Unresolved
  cells (provisional #UD, docs/quirks.md): 66 + REX.W, and REX2 (the APX and VMX opt-ins are refused
  together). Tests: test_x86_vmx_prefix_root / _guest / _misc.

### Not modelled / pending
- APX together with VMX: the VM-exit extended instruction-information field (2406H) and 5-bit register
  IDs (APX spec 355828-009 3.1.4.4.1), EVEX-promoted INVEPT / INVVPID: pending; the opt-ins APX
  (UC_CTL_X86_APX) and UC_X86_FEATURE_VMX are refused together (UC_ERR_ARG).
- FRED together with VMX: done (U1309), see "FRED in VMX operation".
- VMXOFF and active VMCSs: the VMCS data stay valid in memory (our choice; Vol3C 27.11.1 allows
  corruption), so VM-instruction error 6 never occurs.
- SMX operation (GETSEC[SENTER]) does not exist in the model: VMXON is enabled by IA32_FEATURE_CONTROL
  bit 2 only. A20M and INIT are not modelled (nothing to block).
- Documented "not modelled" (accepted, user decision 2026-10-17 (G10), Gate 1 checklist item 15, U1703;
  details in `docs/quirks.md` "VMX: not modelled (open gaps)"): the pending-debug-exceptions field of
  APIC-write and virtualized-EOI VM exits is saved as 0 (SDM 326019-093 Vol3C 30.3.4, pp. 30-27 / 30-28);
  a data-breakpoint #DB is raised at the access, before the MTF VM exit of its instruction (326019-093
  Vol3C 28.5.2, p. 28-18 gives the MTF VM exit priority over debug traps; 253668-093 Vol3A 7.15 Table 7-3,
  p. 7-26: a data breakpoint is a trap). Citations U1812.
- Out of scope (G10): INIT / SMI VM exits and their pending debug exceptions (plan 1.J.12.e; 326019-093
  Vol3C 30.3.4, p. 30-27); the host PDPTE check at VM exit with a 32-bit PAE VMX host (VMX abort 2,
  plan 1.J.12.d; 326019-093 Vol3C 30.5.4, pp. 30-32 / 30-33, and 30.7, p. 30-34) - implemented (U1328)
  but never executed, as the unit harness runs only 64-bit VMX hosts.

### VM entries and VM exits (U1303)
- VM entry (helper_vmlaunch): the checks of 29.1 (in order), 29.2 (controls -> error 7, host state -> error
  8) and 29.3.1 (guest state -> VM-entry failure, exit reason 33 with bit 31; qualification 2 PDPTEs,
  3 NMI injection with STI blocking, 4 VMCS link pointer, else 0) are all made before any guest state is
  loaded (29.3 lets checking and loading happen in any order); then the guest state (29.3.2), the
  VM-entry MSR-load area (29.4: failure -> exit reason 34, qualification = the entry number), the
  launch state, the activity state, the preemption timer, event injection (29.6). A VM entry clears the
  whole TLB (VPIDs are not used to keep translations, 31.4).
- The VMCS controls are cached at VM entry (27.11.1 lets the processor keep VMCS data); a VM exit
  writes every field it changes to the VMCS region.
- VM exit (vmx_vmexit): 30.2 exit information (unused information fields keep their value: "undefined"),
  30.3 guest state, 30.4 MSR-store area, 30.5 host state, 30.6 MSR-load area, 30.7 VMX abort (abort
  indicator at offset 4; the VMX-abort shutdown stops the emulation with UC_ERR_EXCEPTION and leaves
  the vCPU halted; only a CPU reset leaves it).
- Instruction exits restore the state to the instruction's start; the VM-exit instruction length comes
  from the translator (insn_start word 2), so prefixes count. RFLAGS.RF is saved as 0 (30.3.3).
- The model's "unusable" segment is a segment register whose cached P bit is 0 (what QEMU keeps for a
  null-selector load). VM entry loads an unusable segment with P = 0 (base / limit per 29.3.2.2, the
  undefined parts: limit 0); a VM exit saves P = 0 as unusable (30.3.2).
- STI blocking and MOV-SS blocking: QEMU keeps one interrupt shadow; with VMX enumerated the translator
  records which instruction set it (the FRED marker of U1171), so the interruptibility state saves and
  loads bits 0 and 1 separately. As for FRED, QEMU sets the shadow for every STI, also with IF already 1.
- Event injection: the event is delivered as the model delivers events of its kind (UC_CTL_X86_EVENT_DELIVERY:
  through the guest IDT, or to UC_HOOK_INTR / UC_HOOK_INSN_INVALID); the pushed RIP and error code follow
  29.6.1.1 (RIP + VM-entry instruction length for types 4-6; the error code only with bit 11, also for a
  vector that normally has one, IA32_VMX_BASIC[56] = 1).
- VMX non-root operation uses QEMU's HF_GUEST_MASK translation flag (SVM is withdrawn with VMX, so the flag
  is unambiguous); the SVM code paths that depend on it are skipped in VMX non-root operation. Every TB of
  non-root code starts with helper_vmx_boundary (instruction boundary: pending MTF, VMX-preemption timer,
  NMI window, interrupt window, in the priority of 28.2); with the monitor trap flag every TB holds one
  instruction (VM entry flushes the TB cache when it sets the control).
- The VMX-preemption timer counts the model's TSC (host time) >> 5; it is evaluated at TB boundaries, so
  expiry is time-based, not instruction-exact (a value of 0 exits before the first guest instruction).
  U1671 (plan 1.J.60): a WRMSR to IA32_TIME_STAMP_COUNTER (10H) or IA32_TSC_ADJUST (3BH) moves the TSC but is
  no "TSC increment" (Vol3C 28.5.1 p. 28-17, SDM 093): the running timer keeps its value and counts on from the moved
  TSC (x86_vmx_tsc_moved); a TPAUSE / UMWAIT wait (U1620) is time passing and counts.
- Unconditional exits (28.1.2): CPUID (before the UC_HOOK_INSN CPUID hooks), GETSEC (with CR4.SMXE = 1),
  INVD, XSETBV (after its #UD and CPL checks), the VMX instructions, VMCALL; triple fault (28.2).


### Instruction exits and non-root behaviour (U1304)
- Where the check sits: in the instruction's helper when it has one (HLT, RDPMC, RDTSC, RDTSCP, MONITOR,
  MWAIT, PAUSE, RDMSR / WRMSR and their immediate, list and user forms, LOADIWKEY, PCONFIG); otherwise the
  translator emits helper_vmx_insn (kind VMXI_*) with the instruction information and qualification parts
  it knows: MOV CR / CLTS / LMSW, MOV DR, INVLPG, INVPCID, LGDT / LIDT / SGDT / SIDT, LLDT / LTR / SLDT /
  STR, RDRAND, RDSEED, XSAVES, XRSTORS, WBINVD / WBNOINVD, RDPID, UMONITOR / UMWAIT / TPAUSE, RDMSRLIST /
  WRMSRLIST (#UD), WRMSRNS, IN / OUT / INS / OUTS (from gen_check_io, after the IOPL / TSS check).
- Priority (28.1.1): the checks come after the #UD and privilege #GP checks of the instruction and before
  its other faults (MONITOR / MWAIT before the ECX #GP, MSRs before the #GP of an MSR the model lacks,
  LOADIWKEY after its CPL #GP, UMWAIT / TPAUSE after the CR4.TSD #GP); MOV-DR exiting comes before the CPL
  #GP and the CR4.DE #UD (DR8-DR15 stay #UD); the enable controls' #UD (RDTSCP, RDPID, INVPCID, XSAVES /
  XRSTORS, UMONITOR / UMWAIT / TPAUSE, PCONFIG, PBNDKB, RDMSRLIST / WRMSRLIST) come first. The MSR-bitmap checks of
  RDMSR / WRMSR run before the UC_HOOK_INSN hooks (as CPUID in U1303).
- MSRs: the bitmaps of 27.6.9; RDMSR 10H and RDTSC / RDTSCP return the offset (and scaled) TSC (RDMSR 3BH,
  IA32_TSC_ADJUST, is not offset; WRMSR 10H / 3BH load the TSC / IA32_TSC_ADJUST themselves, Vol3C 28.3 p. 28-13,
  U1671); WRMSR 79H
  does nothing; MSR-store / -load areas and uc_reg_* MSR accesses are not instructions (no exit, no
  offset). WRMSRNS exits with qualification 1; RDMSR / WRMSRNS immediate forms use reasons 84 / 85 with
  qualification 0 (SDM 092 defines no qualification or instruction information for them). RDMSRLIST /
  WRMSRLIST: without MSR bitmaps the instruction exits before reading anything; with them, per entry
  (qualification = the entry index, WRMSRLIST saves the data read into the MSR-data field).
- I/O: the bitmaps of 27.6.4 (a wrap past FFFFH always exits); the REP bit of Table 30-5 is set whenever a
  REP prefix is present; INS / OUTS store the instruction information (address size, segment: ES for INS)
  and, if the segment is usable, the guest-linear address segment base + (E/R)SI / (E/R)DI at the
  address size (our reading of "(E)DI" / "(E)SI").
- MWAIT exit qualification: MONITOR arms the monitoring hardware, a completed MWAIT disarms it; stores to
  the monitored range are not tracked and UMONITOR is not counted (the SDM does not say it arms the same
  hardware). A logical processor waiting after MWAIT is saved with activity state active (30.1 footnote 3).
- TSC scaling: ((TSC * multiplier) >> 48) + offset with a 128-bit product. UMWAIT / TPAUSE compare their
  deadline with the guest TSC; their wait takes no time in the model, so the physical-delay conversion
  of 28.3 has no visible effect.

### Events in VMX non-root operation (U1305)
- U1329: VM-entry injection of a hardware exception (type 3) sets the #DF state from its vector, so a
  contributory or page-fault-class exception met during its delivery becomes #DF (29.6.1 p. 29-21 and
  footnote 1: #VE is page-fault class because "EPT-violation #VE" is supported); other event types
  are benign there. U1324 / U1326: #VE is page-fault class for the #DF rules (Vol3A Table 7-4).
- U1328: the PDPTE checks use the paging MAXPHYADDR of 29.3.1.6 / 30.5.4 (Vol3A 5.1.4 p. 5-6): reduced
  by IA32_TME_ACTIVATE[39:36] for the guest PDPTEs read from memory ("enable EPT" 0) and for the host
  PDPTEs at VM exit (VMX abort 2); the full width for the guest PDPTE fields under EPT and for the
  PDPTE loads of MOV to CR3 through EPT (x86_vmx_pdpte_load).
- U1434 (TME-VMX integration): one MAXPHYADDR for VMX and TME-MK - vmx_maxphyaddr is the shared
  x86_maxphyaddr (U1430: reduced by IA32_TME_ACTIVATE[39:36] after the lock, outside SEAM), and
  x86_paging_maxphyaddr(env, ept) gives the width of CR3 / paging-structure addresses: the CPUID
  width for guest-physical addresses under EPT (Vol3A 5.1.4 p. 5-6, Vol3C 31.3.1), else the reduced
  one. Users: the guest-CR3 and guest-PDPTE VM-entry checks, x86_vmx_pdpte_load, MOV to CR3 in VMX
  non-root operation with EPT ("the value M is not reduced outside SEAM", Vol3C 28.3 p. 28-10) and
  the guest's IA-32 page walk under EPT (paging_maxphyaddr). Host-physical addresses (VMX pointers,
  VMCS addresses, the EPTP, EPT entries, the host CR3 / PDPTEs) keep the reduced width. Tests:
  test_x86_ti_vmx_ptr, _vmx_entry, _ept_entries, _ept_guest_width.
- Exceptions: raise_interrupt2 consults the exception bitmap (#PF: reversed when PFEC & PFEC_MASK !=
  PFEC_MATCH) before the #DF escalation, so an exception met while delivering another event exits with
  the original event (30.2.4); a #DF produced by the escalation is checked again and exits without an
  original event; a triple fault exits (U1303). Event types: hardware exception 3, INT1 5 (helper_icebp),
  INT3 / INTO 6 (the FRED software-exception marker of U1171, set with VMX too), INT n 4 (never exits by
  the bitmap). The exiting-event error code is valid for hardware exceptions with an error code outside
  real-address mode. A VM exit caused directly by #PF leaves CR2 unchanged (qualification = the linear
  address), by #DB leaves DR6 / DR7 unchanged (qualification = B3-B0 / BD / BS as the #DB raise computed
  them; BLD / RTM are not modelled).
- Original-event information: every IDT delivery in non-root operation records the event (type,
  vector, error code, the instruction length of software events, which the VM exit stores as the VM-exit
  instruction length); the record ends when the delivery completes. Events delivered to UC_HOOK_INTR
  instead of the guest IDT are not delivered by the model, so nothing is recorded for them.
- IRET: with "NMI exiting" 0 it unblocks NMIs at its start (also if it faults); with "NMI exiting" 1 it
  leaves blocking by NMI alone and, with virtual NMIs, clears virtual-NMI blocking at its start; a fault
  of that IRET that exits by the bitmap reports bit 12 (NMI unblocking due to IRET).
- Interrupts at an instruction boundary (x86_cpu_exec_interrupt): pending MTF, VMX-preemption timer, NMI
  window, then an NMI (NMI exiting: exit 0 with information 80000202H, NMIs blocked after the exit), the
  interrupt window, then an external interrupt (external-interrupt exiting: RFLAGS.IF ignored; blocking
  by STI / MOV SS still applies - our choice where 28.4.1 leaves it implementation-specific; with
  "acknowledge interrupt on exit" the local APIC is acknowledged and the vector saved, else the
  interrupt stays pending and the information is invalid).
- Task switches (CALL / JMP / IRET through a TSS or task gate, a task gate in the IDT) exit (9) after
  the checks of 28.4.2 steps 1-4 with the qualification of Table 30-2; nothing of the switch happens.
- HLT: the VMX events that end the HLT state at once (pending MTF, an expired timer, NMI window,
  interrupt window) exit from HLT with activity state HLT and RIP after the HLT. Otherwise HLT ends the
  emulation run as outside VMX operation (Unicorn has no wake-up source while halted).

### EPT (U1306)
- With "enable EPT" every guest-physical address of VMX non-root operation goes through
  x86_vmx_ept_translate (QEMU's get_hphys hook, the place of SVM's nested paging): a 4- or 5-level walk
  (EPTP[5:3]; with 4 levels a guest-physical address above bit 47 is an EPT violation), 1-GByte and
  2-MByte pages, execute-only entries (no misconfiguration for X without R), mode-based execute control
  (bit 10 executes user-mode linear addresses; an entry is present with bit 10 alone), the EPT A/D flags
  (EPTP[6]: A in every entry used, D in the entry that maps a page written; guest paging-structure
  accesses are then writes). Misconfigurations: W without R, reserved bits (the address bits from
  MAXPHYADDR, bits 7:3 / 6:3 of non-leaf entries, the address bits below a large page), memory types 2, 3,
  7. Violations: not present, or the access not allowed by the AND of the entries.
- The EPT-violation qualification (Table 30-7): access type (paging-structure accesses are reads, with
  EPT A/D flags reads and writes), the R / W / X (and with MBEC the user-execute) permissions (0 when an
  entry is not present), guest-linear address valid (not for PDPTE loads), translation of the linear
  address (bit 8) vs. a paging-structure access, and - advanced information, EPT_VPID_CAP bit 22 - user
  / read-write / execute-disable of the linear address (bits 9-11), NMI unblocking due to IRET (bit 12).
  The guest-physical and guest-linear address fields; RFLAGS.RF is saved as 1 outside event delivery.
- The EPT translation is folded into the model's TLB entries (the permissions narrow the entry); VM entry,
  VM exit, INVEPT, INVVPID and VMFUNC EPTP switching flush the whole TLB, which every invalidation rule
  of 31.4 allows.
- PAE paging with EPT keeps the PDPTE registers: VM entry loads them from the guest PDPTE fields, MOV to
  CR0 / CR3 / CR4 loads them through EPT as Vol3A 5.4.1 says (a present PDPTE with a reserved bit is
  #GP(0) before the register changes), the PAE page walk uses them, VM exits save them.
- Not modelled: the guest paging-structure A / D writes of the model's page walk are not checked against
  the EPT write permission (the walk checks each paging-structure access as a read, or as a write with
  EPT A/D flags); page-modification logging, sub-page write permissions, supervisor
  shadow-stack control, guest-paging verification are not offered by the capability set.
- EPT-violation #VE (U1324; 27.6.19, 28.5.8, 29.2.1.1): secondary bit 18, the virtualization-exception
  information address (202AH, page-aligned within MAXPHYADDR at VM entry) and the EPTP index (0004H).
  A convertible EPT violation (bit 63 clear in the not-present entry or in the entry that maps the
  page) becomes a #VE (vector 20, no error code; page-fault class for #DF) with CR0.PE = 1, outside
  event delivery and with offset 4 of the area 0; the area gets Table 28-23 (48, FFFFFFFFH,
  qualification, guest-linear address - 0 when not valid -, guest-physical address, EPTP index).
  An EPT violation for a guest-physical address beyond the 4-level walk (bits 51:48) has no entry
  whose bit 63 could decide: not converted (documented interpretation, user decision 2026-10-14,
  docs/quirks.md; test_x86_vmx_ve_gpa48). The #VE goes through the exception bitmap
  (bit 20) like any exception. VMFUNC EPTP switching writes ECX[15:0] to the EPTP index (28.5.7.3).
  Not modelled: the PT and PEBS exclusions (Intel PT and PEBS are absent from the model). The
  prematurely-busy-shadow-stack exclusion cannot arise: CET event delivery and far transfers plan and
  probe every shadow-stack access before the token's busy bit is set (seg_helper.c cet2_plan_* /
  cet2_commit), so an EPT violation never follows a busy-bit store (U1326).

### APIC virtualization (U1307)
- The virtual-APIC page and the guest interrupt status (RVI / SVI, loaded by VM entry with
  virtual-interrupt delivery and saved by VM exits) implement TPR, PPR, EOI and self-IPI virtualization,
  the evaluation of pending virtual interrupts (VM entry, TPR / EOI / self-IPI virtualization, posted-
  interrupt processing) and virtual-interrupt delivery at an instruction boundary (RFLAGS.IF = 1, no
  STI / MOV-SS blocking, no interrupt-window exiting; after NMIs and the interrupt window, before external
  interrupts; it also ends HLT). The virtual interrupt is delivered as the model delivers external
  interrupts (guest IDT or UC_HOOK_INTR).
- Trap-like VM exits (TPR below threshold 43, virtualized EOI 45, APIC write 56) and APIC-write emulation
  are taken at the next instruction boundary before any other event; the instructions that cause them
  end their translation block (MOV to CR8, WRMSR) or run one per block (APIC-access virtualization).
  29.7.7: with TPR shadow, APIC-access virtualization and no virtual-interrupt delivery, VM entry makes a
  TPR-below-threshold exit pending when the threshold exceeds VTPR[7:4].
- CR8 with "use TPR shadow" (32.3); x2APIC-MSR virtualization (32.5): RDMSR 808H (and with APIC-register
  virtualization every 800H-8FFH MSR) from the virtual-APIC page, WRMSR 808H / 80BH / 83FH with their
  reserved-bit #GP, in any APIC mode, after the MSR-bitmap exit check and the UC_HOOK_INSN hooks.
- The APIC-access page: a linear access whose final physical address is on the page is decided in the
  page walk after the paging and EPT checks (32.4.1): an instruction fetch, an access larger than 4
  bytes or not within the low 4 bytes of a 16-byte block, any access without "use TPR shadow", an access
  after a virtualized write of the same instruction with another offset / size, or an offset 32.4.2 /
  32.4.3.1 do not list is an APIC-access exit (44: offset, access type 0 read / 1 write / 2 fetch / 3
  during event delivery; RFLAGS.RF saved as 1 outside event delivery). A virtualized access gets a TLB
  entry to the virtual-APIC page that allows only its own access type and is dropped at the next
  boundary. With "virtualize APIC accesses" every translation block holds one instruction, so APIC-write
  emulation runs right after the instruction (a REP string instruction is one instruction here).
  Guest-physical and physical accesses to the APIC-access page (32.4.6) are not treated specially (the
  SDM allows either outcome for physical accesses; a guest paging structure on the APIC-access page
  with EPT is not given the APIC-access exit of 32.4.6.1 - open gap, docs/quirks.md).
- Posted interrupts (32.6): with "process posted interrupts" the acknowledged notification vector
  clears ON, EOIs the local APIC, moves PIR into VIRR / RVI and evaluates pending virtual interrupts,
  without a VM exit; any other vector exits as usual.
- SENDUIPI (32.7) with "use TPR shadow": the notification is written to VICR (xAPIC form with "virtualize
  APIC accesses", else the x2APIC form) and an APIC-write VM exit (300H) follows the instruction (IPI
  virtualization is not offered, so case 4 of 32.7 applies).
- Not offered (capability set): IPI virtualization, APIC-timer virtualization (tertiary controls),
  "virtualize IA32_SPEC_CTRL".

### FRED in VMX operation (U1309)
- With FRED transitions (CPUID.(07H,1):EAX.FRED as reported) the capability set adds IA32_VMX_BASIC[58],
  "load FRED" (VM-entry control 23), "activate secondary controls" (VM-exit control 31) with
  IA32_VMX_EXIT_CTLS2 = 3 ("save FRED", "load FRED"); the FRED VMCS fields, the injected-event data and
  the original-event data exist then (Vol3D Appendix B footnotes).
- VM entry: the secondary VM-exit controls against IA32_VMX_EXIT_CTLS2; injection type 7 with vector 1 /
  2 when the guest CR4.FRED is 1; the nested-exception bit only for hardware exceptions; the FRED MSR
  fields ("load FRED", host and guest: CONFIG bits 2, 5:4, 11 zero, RSP1-3 64-byte aligned, SSP1-3
  8-byte aligned, all CPU canonical); a FRED guest's SS.DPL 0 (CS.L = 1) or 3 (IOPL 0, no STI blocking).
  "load FRED" loads IA32_FRED_CONFIG / RSP1-3 / STKLVLS / SSP1-3; VM exits save them ("save FRED") and
  load the host values ("load FRED").
- Event injection into a FRED guest (29.6.1.1): FRED event delivery with the CPL of the loaded SS.DPL,
  the injected-event data as event data, augmented SS bit 16 = the guest's blocking by STI, the
  nested-exception bit for the stack level and the saved event information, SYSCALL / SYSENTER (type 7)
  with RIP + the VM-entry instruction length.
- VM exits: an exception encountered during FRED event delivery sets bit 13 of the exiting-event
  information; the original-event information of an exception nested on FRED delivery has bit 13; the
  original-event data field receives the event data of the delivery; SYSCALL / SYSENTER delivered with
  FRED are original events of type 7.
- ERETS / ERETU (28.3): with "NMI exiting" 1 they leave blocking by NMI alone; with virtual NMIs they
  clear virtual-NMI blocking for augmented SS bit 18.

### CR3 and LAM across VM exits (U1311)
- With UC_X86_FEATURE_VMX and UC_X86_FEATURE_LAM, a VM exit loads CR3 bits MAXPHYADDR-1:0 from the
  host CR3 field and keeps bits 63:MAXPHYADDR - LAM_U57 (61) / LAM_U48 (62) included - of the CR3 in
  use at the VM exit (30.5.1 p. 30-30). U1319 (user decision 2026-10-14): the VM-entry checks reject
  only the reserved CR3 bits of the host and guest CR3 fields (29.2.2 p. 29-7, 29.3.1.1 p. 29-9 in SDM
  092 / 093); with LAM enumerated bits 62:61 are not reserved (Vol3A 2.5 Figure 2-7 p. 2-15, Figure
  5-11 note 2 p. 5-32; ISE 319433-042 to -053 6.6.2 said so explicitly), so both fields may carry LAM
  bits; host-field LAM bits are accepted and, per 30.5.1, not loaded by VM exits. The "should not set
  bits 63:MAXPHYADDR" sentences are guidance, not checks. The guest-CR3 MAXPHYADDR is reduced by
  IA32_TME_ACTIVATE[39:36] only with "enable EPT" 0 (29.3.1.1); the host-CR3 one always (29.2.2,
  outside SEAM). Note: 30.5.1 keeps CR3 bits 63:MAXPHYADDR with the TME-reduced MAXPHYADDR, so with
  EPT a guest CR3 bit between the reduced and the full width (allowed by 29.3.1.1) reaches the host
  CR3 at the VM exit - the SDM as written, modelled as such (user decision 2026-10-14, docs/quirks.md;
  test_x86_vmx_host_cr3_tme). Regression test: test_x86_vmx_lam_cr3.

### Pending debug exceptions (U1310)
- 29.7.3: a VM entry that injects nothing (or only a pending MTF VM exit), outside shutdown /
  wait-for-SIPI and without blocking by MOV SS, takes valid pending debug exceptions (BS or enabled
  breakpoint in the guest field) as a #DB at the first boundary - after TPR-threshold and MTF VM exits,
  before the timer, NMIs, interrupts and the windows. DR6 gets B3-B0 / BS as the model's #DB raise sets
  them; the exception bitmap applies (qualification B3-B0 / BS). With blocking by MOV SS the model loses
  them ("held pending or lost as would normally be the case"); the MOV-SS cases of injected software
  events are not modelled. VM exits save the pending-debug-exceptions field as 0, except MTF and
  TPR-below-threshold VM exits, which save the debug exceptions loaded by VM entry and still pending
  (U1320, 30.3.4; docs/quirks.md). U1321: a pending MTF VM exit comes before the single-step #DB trap
  of the instruction (28.5.2) and saves BS in the field; the #DB follows the next VM entry (29.7.3).
  U1325: before that, the APIC-write emulation and the trap-like VM exits of the instruction (TPR below
  threshold, virtualized EOI, APIC write; 32.1.2, 32.4.3.2) - a TPR-below-threshold VM exit saves BS
  as well (30.3.4); APIC-write / virtualized-EOI exits save 0.

### Intel SGX in VMX operation (U1313-U1316; the SGX model: docs/sgx.md)
- ENCLS exiting (U1313): see Stage 8. ENCLV, its exiting control / bitmap and SGX-conflict VM exits
  were specified in SDM 325462-065 to -087 (ENCLV NP 0F 01 C0, exit reason 70, bitmap 2036H) and
  removed in -088: not modelled (the model follows 092 / 093), ENCLV stays #UD.
- VM exits in enclave mode (U1314; Vol3C 30.1 p. 30-3, 30.2.1, 30.2.5, 30.3.3, 30.3.4; Vol3D 42.6):
  vmx_vmexit first runs the AEX (sgx_model.c x86_sgx_aex) - an exception VM exit hands the SSA its
  vector / error code (EXITINFO, MISCSELECT EXINFO with the #PF address; CR2 itself stays as 30.1
  says), every other VM exit is an AEX without exception information - and then saves the state the
  AEX left: RIP = AEP, RFLAGS with CF..OF and RF 0, the synthetic GPRs. Exit-reason bit 27 and guest
  interruptibility bit 4 are set; the VM-exit instruction length / information and the I/O RCX / RSI /
  RDI / RIP fields are cleared; bits 11:0 of the #PF and APIC-access qualifications, of the
  EPT-violation guest-linear address and of the EPT guest-physical address are cleared.
- Events incident to enclave mode outside enclave mode (U1314): a VM exit during the delivery of an
  event whose AEX preceded it, or due to / during the delivery of an event injected or made pending
  (pending debug exceptions, 29.7.3) by a VM entry with guest interruptibility bit 4, sets bit 27 and
  bit 4 (nothing is cleared). FRED event delivery of such an event saves the enclave bit (29.6.1.1,
  29.7.3) - the same flag the AEX sets (env->sgx_enc_event).
- SGX-induced #PF (an EPC operand of a leaf, the enclave access control) remember CR2 for a #PF that
  exits by the exception bitmap (30.1), as the paging #PF does.
- APIC virtualization (U1316; Vol3D 42.5.3 p. 42-4): implicit enclave accesses (SECS / TCS / SSA)
  are physical and never checked. The EPC operand of a leaf and, in enclave mode, an access into
  ELRANGE are explicit enclave accesses: not checked against the APIC-access page - the leaf's EPC
  check / the enclave access control make them a #PF (the APIC-access page is not EPC); an
  instruction fetch outside ELRANGE in enclave mode is the #GP(0) of the SGX model. Any other access
  in enclave mode to the APIC-access page causes an APIC-access VM exit, also where it would be
  virtualized. Not modelled: the same rule for the non-EPC operands of ENCLS leaves (PAGEINFO,
  SECINFO, ...) - they are 8-byte accesses, which cause an APIC-access VM exit anyway (32.4.2,
  32.4.3.1: an access of more than 32 bits).
- EPT (42.5.2): leaf operands are translated by the page walk with EPT, which sets the EPT A/D
  flags of the accesses it completes.
- Monitor trap flag (U1315; Vol3D 43.2.2 / 43.2.3, EENTER / ERESUME / EEXIT pseudocode, AEX 40.4.1):
  inside an enclave entered through an opt-out entry no MTF VM exit is pended; an opt-out EENTER /
  ERESUME and an opt-in ERESUME (TMP_NOTIFY = 0: AEX-Notify is not enumerated) clear a pending one;
  EEXIT, an AEX and an opt-in EENTER pend one at their end. ENCLS and EREPORT / EGETKEY follow the
  ordinary per-instruction rule (43.2.1 / 43.2.2).
