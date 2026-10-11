# VMX model (plan 1.J.12, ledger U1300-U1399)

User decisions: A11 (2026-10-09, "implement FRED / VMX / TDX / LAM"); Gate 1 follow-up (2026-10-09, "VMX = full SDM
VMX (VMCS, entry/exit, EPT, VPID, APIC virtualization)", on the local APIC model of `docs/apic.md`); start
2026-10-11. Opt-in `UC_X86_FEATURE_VMX`, default off: without it CPUID, CR4, the MSR list and every VMX opcode
behave exactly as before.

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
| Vol3C 35 (SEAM), 36 (PT), Vol3D 42 (SGX) | - | document names / PT packet / SGX SVN | not part of this model (TDX = plan 1.J.13; SGX = agent/sgx) |

## Capability set (our choice, labelled)

The VMX capability MSRs report a fixed, self-consistent set chosen for the model (Vol3D Appendix A, pp. A-1 to A-9;
Vol4 Table 2-2 480H-493H, pp. 2-46 to 2-48). Controls outside this set are reserved (VM entry fails with
VM-instruction error 7) and their VMCS fields do not exist (VMREAD / VMWRITE: error 12).

| MSR | value | meaning (Appendix A) |
|---|---|---|
| 480H IA32_VMX_BASIC | 01D8_1000_0000_0001H | revision identifier 1 (our choice); VMCS / VMXON region 4096 bytes; bit 48 = 0 (full MAXPHYADDR); no dual-monitor treatment (49 = 0); memory type WB (53:50 = 6); INS/OUTS instruction information (54); TRUE_* MSRs (55); any hardware exception may be injected with or without an error code (56); nested-exception support (58) = 0 until the FRED-VMX stage |
| 481H / 48DH PINBASED / TRUE_PINBASED | 0000_00FF_0000_0016H | external-interrupt exiting, NMI exiting, virtual NMIs, VMX-preemption timer, process posted interrupts; default1 bits 1, 2, 4 stay 1 |
| 482H PROCBASED | FFFB_FFFE_0401_E172H | every primary control except bits 0 and 18 (reserved); tertiary controls (17) |
| 48EH TRUE_PROCBASED | FFFB_FFFE_0400_6172H | as 482H; CR3-load / CR3-store exiting (15, 16) may be 0 |
| 48BH PROCBASED2 | 0E51_7BFF_0000_0000H | virtualize APIC accesses, EPT, descriptor-table exiting, RDTSCP, virtualize x2APIC mode, VPID, WBINVD exiting, unrestricted guest, APIC-register virtualization, virtual-interrupt delivery, RDRAND exiting, INVPCID, VM functions, VMCS shadowing, RDSEED exiting, XSAVES/XRSTORS, mode-based execute control for EPT, TSC scaling, user wait and pause, PCONFIG. Not offered: PAUSE-loop exiting, ENCLS exiting (until SGX is merged), PML, EPT-violation #VE, PT controls, PASID translation, sub-page write permissions, bus-lock detection, instruction timeout |
| 492H PROCBASED3 | LOADIWKEY exiting (bit 0) with Key Locker, enable MSR-list instructions (bit 6) with MSRLIST, enable PBNDKB (bit 9) with PBNDKB, as the CPU model enumerates them | |
| 483H / 48FH EXIT / TRUE_EXIT | 007F_FFFF_0003_6DFFH / 007F_FFFF_0003_6DFBH | save debug controls (may be 0 in TRUE), host address-space size, acknowledge interrupt on exit, save / load IA32_PAT, save / load IA32_EFER, save VMX-preemption timer value. Not offered (stage 8 or later): load IA32_PERF_GLOBAL_CTRL, clear IA32_BNDCFGS, PT, clear IA32_LBR_CTL, clear UINV, load CET state, load PKRS, secondary VM-exit controls (FRED) |
| 484H / 490H ENTRY / TRUE_ENTRY | 0000_D3FF_0000_11FFH / 0000_D3FF_0000_11FBH | load debug controls (may be 0 in TRUE), IA-32e mode guest, load IA32_PAT, load IA32_EFER. Not offered: entry to SMM / deactivate dual-monitor treatment (no dual-monitor treatment), PERF_GLOBAL_CTRL, BNDCFGS, PT, UINV, CET, LBR, PKRS, FRED, SPEC_CTRL |
| 485H MISC | 0000_0000_6004_0065H | preemption timer = TSC >> 5; VM exits store IA32_EFER.LMA into "IA-32e mode guest" (5); activity state HLT (6); 4 CR3-target values; 512 MSRs per list; VMWRITE to any field (29); zero-length software-event injection (30); MSEG revision 0 |
| 486H / 487H CR0_FIXED0 / FIXED1 | 8000_0021H / FFFF_FFFFH | PE, NE, PG fixed to 1 in VMX operation (unrestricted guests excepted, Vol3C 26.8) |
| 488H / 489H CR4_FIXED0 / FIXED1 | 2000H / the CR4 bits the CPU model supports | VMXE fixed to 1 |
| 48AH VMCS_ENUM | 2 x the highest field index of the supported fields | Appendix A.9 |
| 48CH EPT_VPID_CAP | 0000_0F01_0673_41C1H | execute-only, page-walk length 4 and 5, UC and WB paging-structure memory types, 2-MByte and 1-GByte pages, INVEPT (single-context, all-context), EPT A/D flags, advanced VM-exit information for EPT violations, INVVPID (all four types). Not offered: supervisor shadow-stack control (23), HLAT |
| 491H VMFUNC | 1 | EPTP switching |
| 493H EXIT_CTLS2 | does not exist (EXIT_CTLS[63] = 0) | |

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
| capability MSRs 480H-492H: values above, R/O (#GP on WRMSR), absent without VMX (#GP on RDMSR), presence conditions of Table 2-2 | Vol3D Appendix A pp. A-1 to A-9; Vol4 Table 2-2 pp. 2-46 to 2-48 | `vmx_helper.c` x86_vmx_msr_exists / x86_vmx_msr_read; `misc_helper.c` msr_present / helper_rdmsr / msr_write_ok | done (U1301) |

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
| event injection (types, error code, instruction length, real-address mode guests), VM exits during injection, pending MTF | Vol3C 29.6 pp. 29-20 to 29-24 | vmx_inject | done (U1303); pending debug exceptions after VM entry: planned (stage 5) |
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
| priority of faults and VM exits | Vol3C 28.1.1 pp. 28-1 / 28-2 | every exit site | planned |
| conditional instruction exits (CLTS, HLT, IN/OUT/INS/OUTS with I/O bitmaps, INVLPG, INVPCID, LGDT/LIDT/LLDT/LTR/SGDT/SIDT/SLDT/STR, LMSW, LOADIWKEY, MONITOR, MOV CR0/3/4/8, MOV DR, MWAIT, PAUSE, PCONFIG, RDMSR / WRMSR / WRMSRNS / RDMSRLIST / WRMSRLIST / URDMSR / UWRMSR with MSR bitmaps, RDPMC, RDRAND, RDSEED, RDTSC, RDTSCP, TPAUSE, UMWAIT, VMREAD, VMWRITE, WBINVD, WBNOINVD, XRSTORS, XSAVES) | Vol3C 28.1.3 pp. 28-2 to 28-6 | helpers of each instruction | planned |
| exit qualifications and instruction information of these exits | Vol3C 30.2.1 Tables 30-1 to 30-7, 30.2.5 Tables 30-8 to 30-14 | vmx_vmexit callers | planned |
| changed behaviour in non-root operation (CR0 / CR4 shadows, CR8 / TPR shadow, RDTSC / RDTSCP / RDMSR TSC offsetting and scaling, IRET and NMI blocking, enable-controls #UD for RDTSCP / INVPCID / XSAVES / UMWAIT / TPAUSE / UMONITOR / PCONFIG / RDMSRLIST / WRMSRLIST / PBNDKB, MOV to CR3 PDPTEs, ...) | Vol3C 28.3 pp. 28-8 to 28-15 | helpers of each instruction | planned |
| exception bitmap, #PF error-code mask / match, triple fault, external interrupts (acknowledge interrupt on exit), NMIs, interrupt window, NMI window, task switches | Vol3C 28.2 pp. 28-6 to 28-8; 28.4 pp. 28-15 / 28-16 | `excp_helper.c` raise path, `seg_helper.c` x86_cpu_exec_interrupt / switch_tss | planned |

### Stage 6 - EPT, VPID
| requirement | SDM 092 | code | status |
|---|---|---|---|
| EPTP format and checks, 4- and 5-level EPT walks, 1-GByte / 2-MByte pages, execute-only, mode-based execute control | Vol3C 27.6.11 p. 27-17; 31.3.2 pp. 31-3 to 31-10 | `excp_helper.c` get_hphys -> vmx_ept_translate | planned |
| EPT misconfigurations / violations, qualification, guest-linear / guest-physical address, prioritization | Vol3C 31.3.3 pp. 31-10 to 31-16; 30.2.1 Table 30-7 | vmx_ept_translate | planned |
| EPT accessed / dirty flags | Vol3C 31.3.5 pp. 31-18 / 31-19 | vmx_ept_translate | planned |
| guest PDPTEs with EPT (PAE paging) | Vol3C 29.3.1.6, 29.3.2.4; 28.3 MOV to CR3 | vmx_load_guest, helper_write_crN | planned |
| VPID / INVVPID / INVEPT and the TLB: the model caches no translation across a VM entry, a VM exit, an INVEPT or an INVVPID (the whole TLB is flushed), which every invalidation rule allows | Vol3C 31.1 p. 31-1; 31.4 pp. 31-20 to 31-26 | vmx_flush | planned |

### Stage 7 - APIC virtualization (local APIC model, `docs/apic.md`)
| requirement | SDM 092 | code | status |
|---|---|---|---|
| virtual-APIC page, TPR shadow / TPR threshold, CR8 virtualization | Vol3C 32.1.1-32.1.2 pp. 32-2; 32.3 p. 32-7 | `vmx_helper.c` vapic_* | planned |
| PPR / EOI / self-IPI virtualization, evaluation and delivery of virtual interrupts, EOI-exit bitmap | Vol3C 32.1.3-32.1.5, 32.2 pp. 32-2 to 32-7 | vapic_* | planned |
| virtualizing memory-mapped APIC accesses (APIC-access page, APIC-access exits 44, APIC-write exits 56) | Vol3C 32.4 pp. 32-8 to 32-15 | `excp_helper.c` translation of the APIC-access page | planned |
| virtualizing MSR-based APIC accesses (x2APIC mode, APIC-register virtualization) | Vol3C 32.5 pp. 32-15 / 32-16 | `misc_helper.c` helper_rdmsr / helper_wrmsr | planned |
| posted-interrupt processing | Vol3C 32.6 pp. 32-16 to 32-18 | `seg_helper.c` interrupt acknowledge | planned |
| IPI virtualization, SENDUIPI virtualization, APIC-timer virtualization | Vol3C 32.1.6, 32.7; 28.5.10 | - | not offered |

### Stage 8 - deferred items of other agents
| requirement | SDM 092 | code | status |
|---|---|---|---|
| FRED: guest / host FRED state, "load FRED" / "save FRED" controls, secondary VM-exit controls, injected-event data, original-event data, nested-exception bit, IA32_VMX_BASIC[58] | Vol3C 27.4.1 p. 27-6, 27.5 p. 27-9, 27.8.3 p. 27-26, 29.6, 30.2.2, 30.2.4; Vol3D A.1 p. A-2 | - | planned (until then the opt-ins UC_X86_FEATURE_VMX and UC_X86_FEATURE_FRED are refused together, UC_ERR_ARG: a FRED processor must provide nested-exception support, Vol3D A.1 p. A-2) |
| ENCLV and SGX in VMX operation (ENCLS exiting, ENCLV exiting) | Vol3D 41 (ENCLV), Vol3C 28.1.3 (ENCLS) | - | pending: agent/sgx implements SGX; ENCLV is added only after SGX is merged |

### Stage 9 - tests
| requirement | where | status |
|---|---|---|
| independent Python reference for field encodings, VM-entry checks, EPT walks | `Emulator\tools\isa\ref_vmx.py` | planned |
| unit tests (prefix `vmx_`) | `unicorn\tests\unit\test_x86.c` (one block) | planned |
| registration | `Emulator\tools\test_runner.ps1` | planned |

## Design

### Opt-in
`uc_ctl_set_x86_features(uc, UC_X86_FEATURE_VMX)` (bit 30, 40000000H; chosen from the top of the free range so
the SGX / SMX bits of agent/sgx can take the low end) before the engine is initialised. It applies to CPU models
that report Intel 64 (CPUID.80000001H:EDX.LM) with the GenuineIntel vendor; a model without them stays without
VMX. With VMX the model withdraws CPUID.80000001H:ECX.SVM (an Intel processor does not report SVM; the SVM and
VMX guest modes would share QEMU's HF_GUEST flag) - our choice. A strict `UC_CTL_X86_CPUID` profile that hides
CPUID.01H:ECX[5] hides VMX like any other feature.

### VMCS storage (implementation-specific, Vol3C 27.2 p. 27-3)
The model keeps no VMCS data on the processor: every field lives in the VMCS region in guest memory. Bytes 0-3:
revision identifier and shadow-VMCS indicator, 4-7: VMX-abort indicator (both architectural); byte 8: launch state
(0 clear, 1 launched); from byte 16, one 8-byte slot per field of the model's field table (Appendix B order).
VMCLEAR writes only the launch state. Because nothing is cached, "active" VMCSs need no write-back and VMXOFF
corrupts nothing (Vol3C 27.11.1 allows corruption; VM-instruction error 6 is therefore never produced - our
choice). Software that writes a VMCS region with ordinary stores changes the fields (undefined by the SDM).

### Decoding (U1302)
With CPUID.01H:ECX.VMX the translator decodes: 0F 01 C1 VMCALL and C4 VMXOFF (66 / F2 / F3 / LOCK #UD:
the SDM gives no prefix rule, XED decodes them only without a refining prefix), F3 0F C7 /6 VMXON (66
ignored, XED IGNORE66), 66 0F C7 /6 VMCLEAR, NP 0F C7 /6 VMPTRLD, NP 0F C7 /7 VMPTRST (memory forms;
F2 / LOCK #UD), NP 0F 78 VMREAD, NP 0F 79 VMWRITE (LOCK #UD; the 66 / F2 forms keep their old decoding),
66 0F 38 80 INVEPT and 81 INVVPID (memory only). VMLAUNCH, VMRESUME and VMFUNC follow in U1303. The
VM-exit instruction information (30.2.5 Tables 30-9, 30-14, 30-15) and exit qualification (30.2.1, the
displacement; RIP-relative: displacement + next RIP) of these instructions are computed by the
translator and passed to the helpers.

### Not modelled / pending
- APX together with VMX: the VM-exit extended instruction-information field (2406H) and 5-bit register
  IDs (APX spec 355828-009 3.1.4.4.1), EVEX-promoted INVEPT / INVVPID: pending; the opt-ins APX
  (UC_CTL_X86_APX) and UC_X86_FEATURE_VMX are refused together (UC_ERR_ARG).
- FRED together with VMX: stage 8; refused together (UC_ERR_ARG).
- VMXOFF and active VMCSs: the VMCS data stay valid in memory (our choice; Vol3C 27.11.1 allows
  corruption), so VM-instruction error 6 never occurs.
- SMX operation (GETSEC[SENTER]) does not exist in the model: VMXON is enabled by IA32_FEATURE_CONTROL
  bit 2 only. A20M and INIT are not modelled (nothing to block); MONITOR keeps no armed state (nothing
  to clear).

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
- Unconditional exits (28.1.2): CPUID (before the UC_HOOK_INSN CPUID hooks), GETSEC (with CR4.SMXE = 1),
  INVD, XSETBV (after its #UD and CPL checks), the VMX instructions, VMCALL; triple fault (28.2).

