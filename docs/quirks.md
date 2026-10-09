# i5-13600K deviations from the Intel SDM

**The emulator implements only the Intel SDM.** There is no switch that selects hardware behaviour (user decision 2026-10-08: "IF we do not need the Quirks for now please remove them"). The quirk mechanism (`UC_CTL_X86_HW_QUIRKS`, `UC_X86_QUIRK_*`, `emu-alltest --quirks`) was removed in ledger U531–U539; this file only documents where the host CPU (Intel Core i5-13600K, Raptor Lake, microcode 0x129) does something the SDM does not allow, so that hardware comparisons can tell a documented deviation from a bug.

Where the SDM is silent or says "undefined", "reserved" or "implementation specific", following the hardware is not a deviation: those behaviours are listed at the end ("SDM undefined, our choice").

## How the harness treats a deviation

- Hardware case files (`emu-alltest --cases`, no `=>`): a case that hits a documented deviation ends with `# known deviation: NAME`, NAME being one of the headings below. Such a case is counted under "known deviations", never as "differing". A tagged case that matches the hardware is listed as "not observed" (stale tag, or a CPU that sometimes takes the SDM path). Summary: `cases: N, differing: D, known deviations: K, host state: H`.
- Differences that are not an SDM question but a different machine state on the host carry `# host state: REASON` and are counted apart (RDRAND/RDSEED values, the CPUID initial APIC ID, WRUSS under the host's CR4.CET = 1).
- `test.cmd` (`:hw_zero`) requires `differing: 0` for its hardware files, i.e. every difference is tagged.
- `emu-alltest` quick / `--full`: the forms in `Emulator/data/alltest_known_deviations.tsv` (form text → NAME) land in the bucket "known deviation (docs/quirks.md)" instead of "differs" / "unicorn-#UD", or in "known deviation not observed (matches)".
- `emu-uc72-risk` (R10/R11/R14/R17/R18): a difference that is exactly a documented deviation is printed as "known deviation (docs/quirks.md) NAME: n cases" and does not fail the run (`x87_known_deviation`).

## Deviations

### FCOMI/FUCOMI C1

- Behaviour: FCOMI, FCOMIP, FUCOMI, FUCOMIP and the FSW condition code C1.
- SDM: Vol2A FCOMI/FCOMIP/FUCOMI/FUCOMIP, "FPU Flags Affected: C1 Set to 0."
- Emulator: C1 = 0 (the original QEMU line, both builds).
- i5-13600K: C1 is left unchanged.
- Hardware cases: `Emulator/data/cases_quirks.txt` lines 9–11 (C1 = 1 before the compare); emu-uc72-risk R11 (26 cases: the scenarios with C1 preset to 1) and R18 (13612 cases; the FCOMI cases that also raise an unmasked #IA are counted under "x87 compare unmasked #IA condition codes").
- Ledger: U38 (quirk bit 0), removed in U531.

### CVTPI2PS m64 x87 transition

- Behaviour: CVTPI2PS xmm, m64 (memory source).
- SDM: Vol2A CVTPI2PS: "This instruction causes a transition from x87 FPU to MMX technology operation (that is, the x87 FPU top-of-stack pointer is set to 0 and the x87 FPU tag word is set to all 0s [valid]). If this instruction is executed while an x87 FPU floating-point exception is pending, the exception is handled before the CVTPI2PS instruction is executed." No exemption for the memory form (only CVTPI2PD xmm, m64 "does not cause a transition").
- Emulator: TOP = 0, all tags valid, pending unmasked x87 exception → #MF first.
- i5-13600K: no transition, no #MF for the m64 form (the MMX-register form transitions).
- Hardware cases: `cases_quirks.txt` line 13; `alltest_known_deviations.tsv` `cvtpi2ps xmm0, qword ptr [rsi]` (--full: x87 FTW differs in 5 of 6 iterations); emu-uc72-risk R10 (`cvtpi2ps xmm0, qword ptr [rcx]`, FSW TOP) and R14 (pending #MF: hardware runs, emulator #MF).
- Ledger: U44/U50 (quirk bit 1), removed in U532.

### FYL2XP1 below -1

- Behaviour: FYL2XP1 with a finite ST(0) = x < −1.
- SDM: Vol1 8.5.1.2, Table 8-10 "Invalid Arithmetic Operations and the Masked Responses to Them": "FYL2XP1: operand more negative than –1" → #IA; masked response "Return the QNaN floating-point indefinite value to the destination operand". #IA has priority over #D (Vol1 4.9.2).
- Emulator: #IA for every finite x < −1, also with y = ±0 / ±∞ (U432).
- i5-13600K: x is an ordinary operand: no #IA; result x itself with PE, ±0 / ±∞ (sign of x XOR y) for y = ±0 / ±∞, and a denormal y raises #D first (unmasked: nothing stored).
- Hardware cases: `cases_quirks.txt` lines 15–19; emu-uc72-risk R17 (special operands: 2376 of 29400 FYL2XP1 cases) and R18 (360 cases).
- Ledger: U53/U432 (quirk bit 2), removed in U533.

### PTWRITE without PT

- Behaviour: PTWRITE r/m32, r/m64 (F3 [REX.W] 0F AE /4) on a CPU whose CPUID.(EAX=14H,ECX=0):EBX[4] = 0.
- SDM: Vol2B PTWRITE, Protected Mode Exceptions: "#UD If CPUID.14H.00H:EBX.PTWRITE[4] = 0."
- Emulator: #UD (the fork has no Intel PT; leaf 14H is 0 on every model and on the i5-13600K profile). The #UD comes before the memory operand is accessed.
- i5-13600K (leaf 14H all zero): executes it: the operand is read (#PF/#GP as usual), nothing else happens; LOCK and 66H are #UD.
- Hardware cases: `cases_quirks.txt` lines 21–22; `alltest_known_deviations.tsv` `ptwrite eax`, `ptwrite dword ptr [rsi]`, `ptwrite rax`, `ptwrite qword ptr [rsi]` (--full: hardware runs, emulator #UD).
- Ledger: U80 (quirk bit 3), removed in U534.

### DPPD two NaN products

- Behaviour: (V)DPPD where both products are NaN and destination element 1 is selected.
- SDM: Vol2A DPPD Operation (DP_primitive): `Temp2 := Temp1[63:0] + Temp1[127:64]`, written to every selected element; with two NaN operands the SSE result is the first source operand (Vol1 4.8.3.5, Table 4-8), so both elements get product 0.
- Emulator: product 0 (quieted) in both elements.
- i5-13600K: element i := p[i] + p[i^1] (each element's own product is the first addend), so element 1 gets product 1. Repeatable (10 runs, 2026-10-08).
- Hardware cases: `cases_dp_nan.txt` 252 of 384 cases (tagged by `gen_cases_nan.py --dp-hw`, which evaluates both orders); `cases_quirks.txt` lines 24–25. The SDM model itself: `cases_dp_nan_sdm.txt` (876 expected-value cases).
- Ledger: U98 (quirk bit 4), removed in U535.

### REP 67h ECX=0 zero-extension

- Behaviour: REP/REPE/REPNE MOVS, STOS, LODS, CMPS, SCAS with a 32-bit address size (67H) in 64-bit mode and ECX = 0.
- SDM: Vol2B REP/REPE/REPZ/REPNE/REPNZ Operation: the address size selects ECX as CountReg, then `WHILE CountReg ≠ 0 DO … OD`: with ECX = 0 nothing is executed and no register is written.
- Emulator: nothing is written (the original QEMU code; the U60/U430 split is gone).
- i5-13600K: zero-extends RCX, RSI, RDI (MOVS), RCX, RDI (STOS), RCX (LODS, CMPS, SCAS). REP LODS takes the SDM path in about 4–5 % of the runs (hwcheck_gate1 case 6: 16 of 400 runs), so its tag can be reported "not observed".
- Hardware cases: `hwcheck_gate1.txt` lines 7–13; `cases_quirks.txt` lines 27–29.
- Ledger: U60/U430 (quirk bit 5), removed in U536.

### x87 compare unmasked #IA condition codes

- Behaviour: FCOM, FCOMP, FCOMPP, FUCOM, FUCOMP, FUCOMPP (and the aliases DC D0+i, DC D8+i, DE D0+i) and FCOMI, FCOMIP, FUCOMI, FUCOMIP when they raise #IA with FCW.IM = 0.
- SDM: Vol2A FCOM: "If the invalid-arithmetic-operand exception is unmasked, the condition code flags are not set"; FUCOM: "the condition code flags are set only if the exception is masked"; FCOMI: "the status flags in the EFLAGS register are set only if the exception is masked" (Operation: `IF FPUControlWord.IM = 1 THEN C3, C2, C0 := 111`).
- Emulator: C3/C2/C0 (FCOMI: ZF/PF/CF) unchanged.
- i5-13600K: sets them to "unordered" (111) anyway. (FTST and FICOM have no such condition in the SDM: 111 on both.)
- Hardware cases: `cases_quirks.txt` lines 32–37; emu-uc72-risk R18 (3494 cases: NaN operands with IM = 0).
- Ledger: U54/U431 (quirk bit 6), removed in U537.

### DPPS exception step grouping

- Behaviour: (V)DPPS with an unmasked SIMD floating-point exception.
- SDM: Vol2A DPPS Operation: DP_Primitive on each 128-bit half in turn, the handler invoked "if unmasked exception reported" after the products, after Temp2, after Temp3 and after Temp4; Exceptions: "Exceptions are determined separately for each add and multiply operation, in the order of their execution. Unmasked exceptions will leave the destination operands unchanged."
- Emulator: exactly that order (U446): a step with an unmasked exception raises #XM at once, later operations set no flags.
- i5-13600K: checks after all products (both halves of VDPPS ymm), after Temp2 and Temp3 together, after Temp4, so flags of operations the SDM would not reach yet are set (example: Temp2 exact tiny with UM = 0 and Temp3 inexact → UE and PE).
- Hardware cases: `cases_sse_exc.txt` 84 of 174028 cases (22 dpps, 62 vdpps ymm), tagged by `gen_cases_sse_exc.py` with `Emulator/tools/isa/dpps_steps.py` (an exact binary32 model of each multiply/add that evaluates both orders; the tagged set equals the set the old quirk bit changed).
- Ledger: U446 (quirk bit 7), removed in U538.

### DPPS two or more NaN products (measurement only)

- Behaviour: (V)DPPS with two or more NaN products in one dot product.
- SDM: DPPS Operation (DP_primitive) plus Table 4-8: the first NaN product in the order p0, p1, p2, p3 in every selected element. The emulator implements that (`cases_dp_nan_sdm.txt`).
- i5-13600K: not repeatable from run to run, so `cases_dpps_nan.txt` (492 cases) is a measurement file, not a gate. 40 runs on 2026-10-08:
  - 225 of the 492 cases changed result at least once.
  - Each run had 9–41 cases (2–8 %) that differed from the majority result, usually in short runs of neighbouring cases.
  - Elements 0 and 2 never varied. Element 0 always gets the first NaN in the order (1, 0, 3, 2) and element 2 in the order (3, 2, 1, 0).
  - Elements 1 and 3 usually follow the orders (0, 1, 2, 3) and (2, 3, 0, 1). In about 10 % of the affected results the order within one pair is swapped.
  - The majority rule is t[j] := p[j^1] + p[j], element i := t[i] + t[i^2].

### LOCK PREFETCHW

- Behaviour: PREFETCHW m8 (0F 0D /1) with a LOCK prefix; also the register form 0F 0D /1 mod=11b (a NOP, U77).
- SDM: Vol2B PREFETCHW, Protected/Real-Address/Virtual-8086/Compatibility/64-Bit Mode Exceptions: "#UD If the LOCK prefix is used." Vol2A LOCK: PREFETCHW is not in the list of lockable instructions.
- Emulator: #UD (U457, the old decoder's LOCK table).
- i5-13600K: runs it (no #UD, no fault) for /1 with a memory or register operand; the other 0F 0D encodings (/0, /2, /7, register /0) with LOCK are #UD, as is LOCK PREFETCHNTA (0F 18 /0).
- Hardware cases: `cases_backport_t1.txt` (the two `lock prefetchw` lines; the /0, /2, /7 lines match).
- Ledger: U457.

### SYSRET at CPL3 in compatibility mode

- Behaviour: SYSRET (0F 07, also with REX.W) executed at CPL3 in compatibility mode (IA32_EFER.LMA = 1, CS.L = 0) with IA32_EFER.SCE = 1 (Windows).
- SDM: Vol2B SYSRET Operation: `IF (CS.L ≠ 1) or (IA32_EFER.LMA ≠ 1) or (IA32_EFER.SCE ≠ 1) or (CR4.FRED = 1) THEN #UD; FI; IF (CPL ≠ 0) THEN #GP(0); FI;` and Compatibility Mode Exceptions: "#UD The SYSRET instruction is not recognized in compatibility mode."
- Emulator: #UD (U460: Intel SYSCALL/SYSRET only in 64-bit mode).
- i5-13600K: #GP(0) (Windows reports STATUS_PRIVILEGED_INSTRUCTION): the CPL check is made first. In 64-bit mode at CPL3 both give #GP(0).
- Hardware cases: `cases_backport_t1.txt` (the two compatibility-mode `sysret` lines).
- Ledger: U460.

## Host state (not SDM deviations)

| tag | cases | reason |
|---|---|---|
| `# host state: RDRAND/RDSEED values` | hwcheck_gate1 lines 82–84, 101–102 | random values (and, for the two `shr` lines, the flags computed from them) differ by nature; CF = 1 and the operand width are what the cases check |
| `# host state: CPUID initial APIC ID` | hwcheck_gate1 line 68 | CPUID.1:EBX[31:24] is the APIC ID of the core the thread happens to run on; the profile holds one core's value |
| `# host state: WRUSS under host CR4.CET = 1` | cases_reach lines 275–276, cases_tsx_cet lines 29–30 | Windows runs with CR4.CET = 1, so WRUSS at CPL3 is #GP(0); the emulator has CR4.CET = 0, where the SDM gives #UD ("#UD If CR4.CET = 0") |

## SDM undefined, our choice

Where the SDM leaves the result open the emulator copies the i5-13600K. These are not deviations and carry no tag.

| behaviour | SDM text | emulator (= i5-13600K) | ledger |
|---|---|---|---|
| x87 NaN tie: FPATAN/FYL2X/FYL2XP1/FSCALE with two NaNs of equal significand | Vol1 4.8.3.5 Table 4-8 picks "the NaN with the larger significand"; ties are not specified | the positive one | U97 |
| x87 transcendental values | Vol1 8.3.10: error bounds only (< 1 ulp, RN) | microcode-exact values | U53/U56 |
| PE on transcendentals | Vol1 8.5.6: transcendentals "by nature produce inexact results" | PE always | U53 |
| F2XM1 with \|x\| > 1, FYL2XP1 with x = −1 or x = +∞ | "undefined" / outside the documented range | as measured | U53 |
| RCP/RSQRT values | relative error bound only | as measured | U81 |
| FST/FSTP m80 and FBSTP with the operand crossing into a not-present page | not specified how a 10-byte store across a page boundary is split | bits 63:0 are stored, then #PF on bits 79:64 (TOP unchanged); nothing is stored when the low qword itself crosses (`cases_backport_t2.txt`, U480 group). The i5-13600K is not repeatable when the low qword ends exactly at the page end: it stores the qword in about 3 of 4 runs and nothing otherwise (200 runs each: FSTP 145 / 55, FBSTP 154 / 46); the emulator takes the usual outcome and those two cases are left out of the hardware file. FSAVE/FSTENV/FXSAVE/XSAVE store nothing and their restores load nothing, as the SDM requires of a faulting instruction | U480 |
| FDP/FCS/FDS | Vol1 8.1.8: updated only on unmasked exceptions / saved as 0 when CPUID.07H.0:EBX[6] / EBX[13] = 1 | both bits reported on every built-in model, behaviour as described | U64/U433 |
| undefined flags (BSF/BSR zero source, AAA, SHRD OF, IMUL, …), reserved fields (FCW 15:13/7:6, FSAVE reserved words) | "undefined" / "reserved" | as measured | various |
