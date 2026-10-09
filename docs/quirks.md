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

### HRESET without CPUID

- Behaviour: HRESET imm8 (F3 0F 3A F0 C0 ib) on a CPU whose CPUID.(EAX=07H,ECX=1):EAX[22] (HRESET) = 0.
- SDM: Vol2A HRESET, Protected Mode Exceptions: "#UD If CPUID.07H.01H:EAX.HRESET[22] = 0."
- Emulator: the MAX model reports HRESET (U804) and executes it; with the i5-13600K CPUID profile (strict, bit 22 = 0) it is #UD.
- i5-13600K (bit 22 = 0 as Windows reports it): decodes it; at CPL3 #GP(0) (the CPL check of an implemented HRESET). Other ModRM bytes than C0 are #UD on both.
- Hardware cases: `cases_sysins_hw.txt` (the two HRESET lines with ModRM C0, CPL3).
- Ledger: U804 (no quirk bit; tag only).

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
- i5-13600K: zero-extends RCX, RSI, RDI (MOVS), RCX, RDI (STOS), RCX (LODS, CMPS, SCAS). REP LODS takes the SDM path in about 4–5 % of the runs (hwcheck_gate1 case 6: 16 of 400 runs), so its tag can be reported "not observed". The rate depends on what runs on the other logical processors (U544, measured 2026-10-09, 200 runs of hwcheck_gate1 each): 8/200 serially with the pre-U540 emu-alltest, 7/200 serially with U540, 26/200 (13 %) with 8 copies side by side; so test.cmd runs hwcheck_gate1 and cases_quirks alone, before the parallel suites (U541).
- Hardware cases: `hwcheck_gate1.txt` lines 7–13; `cases_quirks.txt` lines 27–29.
- Ledger: U60/U430 (quirk bit 5), removed in U536.

### x87 compare unmasked #IA condition codes

- Behaviour: FCOM, FCOMP, FCOMPP, FUCOM, FUCOMP, FUCOMPP (and the aliases DC D0+i, DC D8+i, DE D0+i) and FCOMI, FCOMIP, FUCOMI, FUCOMIP when they raise #IA with FCW.IM = 0.
- SDM: Vol2A FCOM: "If the invalid-arithmetic-operand exception is unmasked, the condition code flags are not set"; FUCOM: "the condition code flags are set only if the exception is masked"; FCOMI: "the status flags in the EFLAGS register are set only if the exception is masked" (Operation: `IF FPUControlWord.IM = 1 THEN C3, C2, C0 := 111`).
- Emulator: C3/C2/C0 (FCOMI: ZF/PF/CF) unchanged.
- i5-13600K: sets them to "unordered" (111) anyway. (FTST and FICOM have no such condition in the SDM: 111 on both.)
- Hardware cases: `cases_quirks.txt` lines 32–37; `cases_x87_cmp_exc.txt` (216 tagged cases: FCOM/FCOMP/FCOMPP, FUCOM/FUCOMP/FUCOMPP, FCOMI/FCOMIP/FUCOMI/FUCOMIP, FCOM/FCOMP m32/m64 with an SNaN, QNaN (ordered forms) or unsupported operand and FCW.IM = 0; U861); emu-uc72-risk R18 (3494 cases: NaN operands with IM = 0).
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
- Hardware cases: `cases_backport_t1.txt` lines 197, 199 (the /0, /2, /7 lines next to them match); `cases_fix4.txt` lines 149–150 (all 16 LOCK forms /0–/7 memory and register are on lines 147–162, the 14 others match; lines 163–178 the forms without LOCK, all NOPs on both).
- Ledger: U457.

### SYSRET at CPL3 in compatibility mode

- Behaviour: SYSRET (0F 07, also with REX.W) executed at CPL3 in compatibility mode (IA32_EFER.LMA = 1, CS.L = 0) with IA32_EFER.SCE = 1 (Windows).
- SDM: Vol2B SYSRET Operation: `IF (CS.L ≠ 1) or (IA32_EFER.LMA ≠ 1) or (IA32_EFER.SCE ≠ 1) or (CR4.FRED = 1) THEN #UD; FI; IF (CPL ≠ 0) THEN #GP(0); FI;` and Compatibility Mode Exceptions: "#UD The SYSRET instruction is not recognized in compatibility mode."
- Emulator: #UD (U460: Intel SYSCALL/SYSRET only in 64-bit mode).
- i5-13600K: #GP(0) (Windows reports STATUS_PRIVILEGED_INSTRUCTION): the CPL check is made first. In 64-bit mode at CPL3 both give #GP(0).
- Hardware cases: `cases_backport_t1.txt` lines 308–309 (64-bit mode lines 305–306: #GP(0) on both).
- Ledger: U460.

### MASKMOVDQU-AC

- Behaviour: MASKMOVDQU (66 0F F7) with the destination [RDI] not 8-byte aligned, CR0.AM = 1, EFLAGS.AC = 1, CPL = 3.
- SDM: Vol2B MASKMOVDQU, "Other Exceptions: See Table 2-21, Exceptions Type 4" - type 4 has no #AC row (Vol2A Table 2-21, unlike types 3/5/6); Vol3A 7.15 names the 128-bit unaligned moves whose #AC "may or may not" be generated (MOVDQU, MOVUPS, MOVUPD, LDDQU), not MASKMOVDQU.
- Emulator: no #AC (the selected bytes are stored).
- i5-13600K: #AC (STATUS_DATATYPE_MISALIGNMENT) for [RDI] at 4 mod 8.
- Hardware cases: `Emulator/data/cases_ac_hw.txt` line 240 (MASKMOVQ, whose #AC row the SDM has, Vol3B Table 25-8, matches: lines 237-238).
- Ledger: U834.

### SxDT-STR-SMSW-NO-AC

- Behaviour: SGDT, SIDT, SLDT m16, STR m16 and SMSW m16 at CPL3 (Windows leaves CR4.UMIP = 0) with CR0.AM = 1, EFLAGS.AC = 1 and a misaligned operand.
- SDM: Vol2B SGDT / SIDT / SLDT / STR / SMSW, Protected Mode Exceptions: "#AC(0) If alignment checking is enabled and an unaligned memory reference is made while CPL = 3"; Vol3A 7.15: "Storing the contents of the GDTR, IDTR, LDTR, or task register in memory while at privilege level 3 can generate an alignment violation"; Table 7-7: "GDTR, IDTR, LDTR, or Task Register Contents" 4, word 2.
- Emulator: #AC(0) when the operand address is not a multiple of 4 (SGDT, SIDT, SLDT, STR) or 2 (SMSW).
- i5-13600K: no #AC at any offset (+1, +2, +4, +6 tried); the stores are made.
- Hardware cases: `Emulator/data/cases_ac_hw.txt` lines 241-246.
- Ledger: U834.
### REP CMPS/SCAS flags at a fault

- Behaviour: REPE/REPNE CMPS or SCAS (any width) whose second or a later iteration faults (here: the next element on a not-present page), so the instruction stops after one or more completed iterations.
- SDM: Vol2B REP/REPE/REPZ/REPNE/REPNZ Operation: `WHILE CountReg ≠ 0 DO Service pending interrupts (if any); Execute associated string instruction; CountReg := (CountReg – 1); ... IF (Repeat prefix is REPZ or REPE) and (ZF = 0) ... THEN exit WHILE loop; FI; OD;` — each iteration executes CMPS/SCAS, which set the status flags ("The CF, OF, SF, ZF, AF, and PF flags are set according to the temporary result of the comparison"); "A repeating string operation can be suspended by an exception or interrupt ... the ECX register has the value it held following the last successful iteration". So the faulting state has the flags of the last completed iteration (the faulting iteration changes nothing, Vol3A 6.5).
- Emulator: the flags of the last completed iteration with RCX/RSI/RDI as after it, RIP at the instruction (U774; also what memory hooks see during an iteration: the flags of the iteration before).
- i5-13600K: RCX/RSI/RDI as after the completed iterations, but RFLAGS as before the instruction (e.g. `cmp r8, r9; repe cmpsq` with an equal first pair and a not-present second one: RFLAGS 297h, the CMP's, not 246h).
- Hardware cases: `cases_fix4.txt` lines 54–67 (REPE/REPNE CMPSQ, CMPSD, SCASQ, SCASB after nine different flag setters); the four lines after them (no fault) match.
- Ledger: U774.

## Host state (not SDM deviations)

| tag | cases | reason |
|---|---|---|
| `# host state: RDRAND/RDSEED values` | hwcheck_gate1 lines 82–84, 101–102 | random values (and, for the two `shr` lines, the flags computed from them) differ by nature; CF = 1 and the operand width are what the cases check |
| `# host state: CPUID initial APIC ID` | hwcheck_gate1 line 68 | CPUID.1:EBX[31:24] is the APIC ID of the core the thread happens to run on; the profile holds one core's value |
| `# host state: the host's GDTR/IDTR ...` | cases_fix3 lines 220–221 | SGDT/SIDT store the host's descriptor-table base and limit (Windows' kernel addresses), the emulator its own; the fault and which bytes get stored are checked by the expected-value lines next to them (U704) |
| (no tag: the harness handles it) | every hardware case that faults with RFLAGS.AC = 1 (`cases_ac_hw.txt`) | Windows clears RFLAGS.AC while it dispatches a user-mode exception (seen for #AC, #GP and #UD: the context the VEH resumes has AC = 0), so after a hardware fault the host's AC bit is the OS's; emu-alltest compares Unicorn's AC bit there (U834, `docs/emu-alltest.md`) |
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
| SGDT/SIDT m (64-bit mode, 10-byte operand) crossing into a not-present page | not specified how a 10-byte store across a page boundary is split | as FSTP m80: bytes 7:0 (limit, base bits 47:0) are stored, then #PF on bytes 9:8; nothing is stored when bytes 7:0 cross (i5-13600K: bytes 7:0 stored in 120 of 120 runs; `cases_fix3.txt`, the hardware lines carry `# host state` because the host's GDTR/IDTR values are what is stored). Outside 64-bit mode (6-byte operand) QEMU's limit-then-base order is kept, not measured | U704 |
| 64-bit operand whose first byte is canonical and whose last byte is not (across 0000_7FFF_FFFF_FFFFh), the canonical page not present | Vol1 3.3.7.1 / Vol2: #GP(0) (#SS(0) for stack references) for a non-canonical memory address; which fault comes first when one part of an operand is on a not-present page is not specified | one access (MOV, ALU, MOVDQU/VMOVDQU loads and stores, string elements, CMPXCHG8B, gather elements): #GP(0) / #SS(0) before any byte; instructions the CPU splits into parts (LSS/JMP FAR m16:64, FXSAVE/FXRSTOR, XSAVE, FSAVE, FLD/FSTP m80, FBLD, ...): the lowest part's #PF first (i5-13600K, `cases_fix3.txt`: LSS / JMP FAR 240 of 240 runs under load). The i5-13600K is not repeatable for the x87 10-byte forms there: FLD m80 #GP in 56 of 240 runs under the parallel load (0 of 40 alone), FBLD in 14 of 40, FSTP m80 in 8 of 40, #PF otherwise; the emulator takes the usual #PF (FBLD reads bytes 7:0 before 9:8) and those lines are not in the hardware file | U706 |
| x87 compares with an unmasked #IS or #D (U861) | Vol1 8.5.1.1 / 8.5.2: "the top-of-stack pointer (TOP) and source operands remain unchanged" (no pop), C1 = 0 for an underflow; the "flags not set" rule of FCOM/FUCOM/FCOMI (Vol2A Table 3-23 note, FCOMI Operation) names only #IA; the #IS / #D condition codes are not specified (the Operation pseudocode writes them by the relation of the operands) | #IS: C3/C2/C0 (FCOMI: ZF/PF/CF) = "unordered" whether FCW.IM is masked or not; #D (FCW.DM = 0): the comparison result (also FTST, FICOM, FCOMI EFLAGS), pops cancelled; FTST/FICOM #IA "unordered" (their Operation has no IM condition); FXAM raises nothing (empty: C3 C0, C1 = sign). All as the i5-13600K: `cases_x87_cmp_exc.txt` 1354 cases, 0 differing (the 216 #IA cases above aside) | U54/U431/U861 |
| x87 store with an unmasked stack-fault (#IS, FCW.IM = 0) to a not-present page | Vol1 8.5.1.1 / 8.6: with IM unmasked the destination is not written and the exception is left pending; whether the memory operand is still checked is not specified | no memory access, no #PF (FSW gets IE\|SF\|ES\|B). The i5-13600K is not repeatable: no #PF in 31 of 40 runs, #PF in 9; the line is not in the hardware file (`cases_fix3.txt` U705 notes) | U705 |
| MASKMOVQ / (V)MASKMOVDQU whose 64/128-bit location crosses into a not-present page | Vol2B MASKMOVDQU/MASKMOVQ: stored with the non-temporal (WC) protocol; unselected bytes get no fault suppression ("exceptions associated with addressing memory and page faults may still be signaled (implementation dependent)", even with a mask of all 0s) | the whole location is checked first: #PF, nothing stored, whatever the mask. The i5-13600K is not repeatable here: in standalone runs it faulted with nothing stored (also with a zero MASKMOVQ mask), under the parallel test.cmd load the bytes on the mapped page were sometimes stored before the #PF and MASKMOVQ's x87-to-MMX transition was sometimes not done; the cases are expected-value lines in `cases_fix3.txt` | U702 |
| FDP/FCS/FDS | Vol1 8.1.8: updated only on unmasked exceptions / saved as 0 when CPUID.07H.0:EBX[6] / EBX[13] = 1 | both bits reported on every built-in model (as the i5-13600K); the behaviour follows the effective CPUID (U860): a UC_CTL_X86_CPUID profile with EBX[6] = 0 updates FDP/FDS on every x87 non-control instruction with a memory operand, with EBX[13] = 0 FCS/FDS hold the CS / operand segment selectors and FSTENV/FSAVE/FXSAVE/XSAVE save them (FLDENV/FRSTOR with CR0.PE = 1 and FXRSTOR/XRSTOR without REX.W load them; REX.W clears them). A register-only instruction leaves FDP/FDS unchanged ("undefined (reserved)") | U64/U433/U860 |
| undefined flags (BSF/BSR zero source, AAA, SHRD OF, IMUL, …), reserved fields (FCW 15:13/7:6, FSAVE reserved words) | "undefined" / "reserved" | as measured | various |
| BT/BTS/BTR/BTC OF, SF, AF, PF (U776) | Vol2A BT: "The ZF flag is unaffected. The OF, SF, AF, and PF flags are undefined." | unchanged, as on the i5-13600K, whatever instruction comes before (upstream QEMU computes them through a CC_OP_SAR shortcut after an arithmetic cc_op in the same translation block, so they depended on the instruction before and on UC_HOOK_CODE); `cases_fix4.txt` U776 lines | U776 |
| RDPMC counter values (U595) | Vol2B RDPMC / Vol3B 22: a counter counts only while enabled (IA32_PERFEVTSELx.EN, IA32_FIXED_CTR_CTRL, IA32_PERF_GLOBAL_CTRL); the counted events are model-specific; reset value 0 (Vol3A Table 12-1) | every counter CPUID enumerates (CPUID.0AH, the profile's on the i5-13600K) reads 0: the emulator counts no events and does not implement the counter MSRs (WRMSR ignored, RDMSR 0), so no counter can be enabled; an encoding CPUID does not enumerate is #GP(0); the built-in models report no PMU (CPUID.0AH = 0), so there every RDPMC with access allowed is #GP(0). **Not measured**: Windows keeps CR4.PCE = 0, so RDPMC at CPL3 is #GP before ECX is checked | U595 |
| GPR bits 31:16 after a task switch to a 16-bit TSS | Vol3A 10.6: "the upper 16 bits of the registers are modified and not maintained" (value not given) | FFFFh, QEMU's original value; **not measured** (a task switch needs CPL0 in legacy protected mode). Upstream QEMU a5505f6b5b keeps the old bits, which the SDM excludes; it is used only in the `__Use_Original_Qemu` build | U464 |
| CET shadow-stack address size on a far transfer that changes mode (U750-U752) | Vol1 18.2.1: the shadow stack's address size is 32-bit in 32-bit/compatibility mode, 64-bit in 64-bit mode; the far CALL/RET/IRET/INT n pseudocode does not say which mode applies to a load or store made while CS changes | pops and the token release on the old shadow stack use the mode the transfer starts in, pushes and the token acquisition on the new shadow stack the target mode (a target below 4 GB is checked first, so only the wrap at 0 can differ); **not measured** (CPL0 only) | U750 |
| registers after a faulting CET check of a far CALL / RET / IRET / event delivery (U750-U755) | the pseudocode assigns SSP / IA32_PL3_SSP before some later #GP/#CP checks; Vol1 18.2.3 only says the new token stays busy when a later push faults | no register (SSP, IA32_PL3_SSP, the IBT tracker) changes when any check of the transfer faults; stores already made stay ("prematurely busy" token); **not measured** | U750 |
| task switch step 15 token check (U754) | Vol3A 10.3 step 15 writes `expected_token_value = SSP ... shadow_stack_lock_cmpxchg8b(SSP, ...)` with the old SSP, while Table 10-1 and Vol1 18.2.4 require the token at the new task's SSP ("Address in Shadow stack token does not match SSP value from TSS") | the token at the 32-bit SSP from TSS offset 104 is checked and set busy (#TS(new TSS) otherwise); step 15's "IF (verifyCsLIP == 0) tempSSP = IA32_PL3_SSP" is applied literally (also for a new CPL < 3 reached without a frame); **not measured** | U754 |
| SSP after SYSRET / SYSEXIT to compatibility mode (U753) | Vol2B: "SSP := IA32_PL3_SSP" without a width or 4-GB check | the full 64-bit IA32_PL3_SSP value; shadow-stack accesses in compatibility mode use its low 32 bits (Vol1 18.2.1); **not measured** | U753 |
| XINUSE of CET_U / CET_S for XSAVES (U756) | Vol1 13.6: a component in its initial configuration may be saved with XSTATE_BV[i] = 0 or 1 | value-based like the other components: XSTATE_BV[11] = 1 iff IA32_U_CET or IA32_PL3_SSP is non-zero, XSTATE_BV[12] = 1 iff an IA32_PLi_SSP (i < 3) is non-zero; no modified optimization; **not measured** (XSAVES is CPL0-only) | U756 |
| a misaligned FXSAVE / FXRSTOR / XSAVE / XSAVEC / XSAVEOPT / XRSTOR operand with alignment checking enabled (U834) | Vol2A FXSAVE / Vol2D XSAVE: "If the alignment check exception (#AC) is enabled (and the CPL is 3), signaling of #AC is not guaranteed and may vary with implementation ... #AC might be signaled for a 2-byte misalignment, whereas a general protection exception might be signaled for all other misalignments" | #AC(0) when the address is not 4-byte aligned, #GP(0) when it is but not 16 / 64-byte aligned (`cases_ac_hw.txt`: +1/+2/+3/+6 #AC, +4/+8/+12 #GP) | U834 |
| unaligned 16/32-byte loads and stores (MOVUPS, MOVDQU, LDDQU, VMOVUPS, ...) with alignment checking (U834) | Vol3A 7.15: "alignment violations may or may not be generated depending on processor implementation when data addresses are not aligned on an 8-byte boundary"; Vol1 14.x: AVX "16 and 32-byte memory references will not generate #AC(0)" | no #AC; 2/4/8-byte operands (scalars, broadcast elements, MOVQ/MOVD, PINSR/PEXTR, PMOVZX/SX partial loads) are checked | U834 |
| packed BCD (FBLD/FBSTP m80), 64-bit bit strings, m16:64 far pointers under alignment checking (U834) | Table 7-7 lists double extended 8, bit strings "2 or 4 depending on the operand-size attribute", far pointers 16:16 2 / 16:32 4; not these | m80 BCD 8 (as the i5-13600K), a 64-bit bit string 8 (its operand-size unit), m16:64 offset 8 then selector 2 (per part, as the smaller far pointers) | U834 |
| order of #AC and the other faults of one instruction (U834) | Vol3A Table 7-2 does not order the faults raised while an instruction executes | a non-canonical first byte #GP/#SS(0) first, then #AC, then a page-crossing non-canonical last byte (U706) and #PF; a pending x87 #MF before; a faulting MMX instruction makes no x87 state transition (TOP, tags unchanged). The i5-13600K is not repeatable for a dword at 0000_7FFF_FFFF_FFFDh: #AC in 107 of 120 runs, #GP in 13; the emulator takes #AC and the line is not in the hardware file | U834 |
| ENDBR64 with a REX2 prefix (F3 REX2(M0 = 1) 1E FA) (U758) | the APX spec does not mention ENDBR64; 3.1.2.1 makes REX2 applicable to every map-1 opcode outside rows 3xH/8xH and the XSAVE/XRSTOR family | an ENDBRANCH for the IBT tracker while REX2 is usable (CR4.OSXSAVE, XCR0[19]); the payload bits select nothing (as REX on F3 0F 1E FA; XED's ENDBR64 pattern has no REX2 restriction); with REX2 unusable, after a REX, with M0 = 0 or with the ENDBR32 encoding it is not an ENDBRANCH; **not measured** (the i5-13600K has no APX) | U758 |
