# XINUSE models (ledgers U1480-U1481)

What XGETBV with ECX = 1 and the XSTATE_BV field written by XSAVE / XSAVEOPT / XSAVEC / XSAVES report for state
components 0 (x87), 1 (SSE) and 2 (AVX). Before U1480 all three were always 1. Other components keep their rules
(BNDREGS from HF_MPX_IU, AVX-512 / AMX / APX / CET value-based, PKRU always 1).

## Architecture (SDM 253665-092 / 334569-092; 093 identical; checked against the PDFs, 1-based PDF page)

| locator | content (paraphrased) |
|---|---|
| Vol1 13.6, p. 13-17/13-18 (PDF 337/338) | XINUSE[i] = 0 means component i is known to be in its initial configuration; XINUSE[i] may be 1 even then; without the init optimization XINUSE[i] is always 1. XGETBV(1) = XCR0 AND XINUSE, and always sets EAX[1] when XCR0[1] = 1 and MXCSR != 1F80H. Initial configurations: x87 = FCW 037FH, FSW 0, FTW FFFFH, FCS/FDS 0, FIP/FDP 0, ST0-ST7 0; SSE = XMM0-15 (64-bit mode) or XMM0-7 zero, MXCSR excluded; AVX = YMM0_H-YMM15_H (or 0-7) zero. |
| Vol1 13.7, p. 13-19 (PDF 339) | XSAVE writes XSTATE_BV[i] = XINUSE[i]; XSTATE_BV[1] may be written with 0 even if MXCSR != 1F80H. |
| Vol1 13.8.3, p. 13-21 (PDF 341) | XRSTOR: RFBM[i] = 0 leaves XINUSE[i]; RFBM[i] = 1 and XSTATE_BV[i] = 0: XINUSE[i] may be set to 0 or 1; XSTATE_BV[i] = 1: XINUSE[i] = 1. |
| Vol1 13.9, p. 13-23 (PDF 343) | XSAVEOPT: the init optimization does not apply to MXCSR. |
| Vol1 13.10, p. 13-23/13-24 (PDF 343/344) | XSAVEC: with RFBM[1] = 1 and MXCSR != 1F80H, XSTATE_BV[1] = 1 and the SSE state is saved even if XINUSE[1] = 0 (13.11: XSAVES the same). |
| Vol2D XGETBV, p. 6-36 (PDF 39) | XGETBV(1) = XCR0 AND XINUSE. |
| Vol2D XRSTOR Operation, p. 6-50 / 6-51 (PDF 53 / 54) | standard and compacted forms: restored -> "XINUSE[i] := 1", initialized -> "XINUSE[i] := 0". |
| Vol3A Table 12-1, p. 12-3 (PDF 375) | INIT leaves XMM, YMM_H (and the x87 state) unchanged: the tracked bits are kept by INIT, cleared by RESET. |

## Measurements (emulator\capture\xinuse; notes\xinuse_host_vs_guest_2026-10-14.txt)

Bare i5-13600K, microcode 129H: P-cores (12 LPs) and E-cores (8 LPs) each give one outcome per case; the VMware
guest's majority equals the P-cores in all 57 cases. Both behaviours are permitted by the text above.

* **P-core model** (`UC_X86_XINUSE_PCORE`, the Intel default): x87 / SSE / AVX tracked with the init optimization.
* **E-core model** (`UC_X86_XINUSE_ECORE`): x87 and SSE always 1 (XSAVE writes XSTATE_BV[1:0] = 11b), AVX as P.
* **ALWAYS** (`UC_X86_XINUSE_ALWAYS`): no init optimization for 0-2 (the behaviour before U1480; the AMD default).

## Rules of the P-core model (fpu_helper.c, translate.c, decode-new.c.inc, emit.c.inc)

| event | tracked bits | evidence |
|---|---|---|
| XRSTOR / XRSTORS, RFBM[i] = 1 | XINUSE[i] := XSTATE_BV[i] (compacted: restored 1, initialized 0) | Vol2D p. 6-50/6-51; X1a-X1h, C00-C02 |
| XRSTOR loading MXCSR through RFBM[2] only | XINUSE[1] unchanged | Vol2D pseudocode; X1h |
| FXRSTOR | x87 := 1; SSE := 1 with CR4.OSFXSR | R9a-R9d |
| VZEROUPPER | AVX := 0 | Z3a-Z3e |
| VZEROALL | SSE := 0, AVX := 0 | Z4a-Z4e |
| x87 instruction except FNSTCW / FNSTSW (incl. FNINIT, FLDCW) | x87 := 1 | F6a-F6e, L7a-L7e |
| MMX instruction, EMMS | x87 := 1 | not measured (an MMX register write leaves x87 non-initial anyway) |
| legacy SSE / VEX / EVEX write to XMM/YMM/ZMM0-15 | SSE := 1; destination of 256 bits or more also AVX := 1 | P2a-P2h, V5a-V5f (VEX.128 leaves AVX); VEX.256 / EVEX not measured |
| LDMXCSR / VLDMXCSR | SSE := 1 | M10a-M10f |
| reads (stores to memory, COMIS, PTEST, PEXTR to a GPR, FXSAVE, XSAVE, ...) | none | - |
| CPU reset | all 0 | Vol3A Table 12-1 |

Reported XINUSE[i] = tracked OR "component i not in its initial configuration" (value check at XGETBV / XSAVE time:
the SDM's mandatory direction holds also for state written through `uc_reg_write` or an unmarked helper). XGETBV(1)
additionally ORs bit 1 when MXCSR != 1F80H; XSAVE / XSAVEOPT do not (M10e: XGETBV1 2, XSTATE_BV 0; Z4c).

Implementation: the "set" is one TCG load / OR / store of `env->xinuse_trk` emitted after the instruction's code (a
faulting instruction sets nothing), at most once per translation block and component (`DisasContext.xinuse_done`;
VZEROUPPER / VZEROALL drop their bits from it, XRSTOR / XRSTORS end the block). No helper call per instruction.

Not modelled: the rare single-run SSE 0 -> 1 events seen on P-cores and in the guest (cause unverified); PKRU (9) keeps
its "always 1" (WRPKRU cases K8a/K8b were Skipped everywhere: OSPKE = 0, nothing measured); the SGX AEX synthetic
state (x86_sgx_xinit) leaves the tracked bits as they are (permitted: XINUSE[i] = 1 in the initial configuration).

## AMD

AMD APM Vol4 r3.27 XGETBV (p. 1763) defines XGETBV(1) = XCR0 AND XINUSE and XRSTOR (p. 1770) "If RFBM=1, the
corresponding XINUSE bit is set to the state of XSTATE_BV"; the APM states no MXCSR rule for XGETBV(1) and no
per-instruction tracking. The AuthenticAMD default stays ALWAYS (no Intel rule leaks into AMD; unchanged behaviour).
Note: the APM XRSTOR sentence reads as XRSTOR clearing XINUSE[i] for XSTATE_BV[i] = 0, which ALWAYS does not do - an
open point (docs: this file; plan LEFT TO IMPLEMENT of U1480).

## Selection (UC_CTL_X86_XINUSE_MODEL, emu-alltest)

`UC_CTL_X86_XINUSE_MODEL`: write `UC_X86_XINUSE_DEFAULT` (0: Intel P-core, AMD ALWAYS), `PCORE`, `ECORE`, `ALWAYS`;
before init the value is kept and copied into the CPU at creation (the write never initialises the engine); read =
the effective model. emu-alltest (`docs/emu-alltest.md`): `--xinuse-model p-core|e-core` overrides; `--guest` / `--vm`
P-core; `--host` the verified core type of the native runs' CPUs (CPUID.1AH and EfficiencyClass agree), else the CPUID
profile's (07H.0:EDX[15] = 1, 1AH 40H / 20H), else P-core as a logged fallback; `--AMD` ALWAYS; no switch: not
written. Tests: test_x86 `test_x86_xu_*` (the 55 measured cases for both models), `cases_xu_{pcore,ecore,always}.txt`,
`Emulator\tools\xinuse_cli_check.py` (test.cmd group `xinuse`).
