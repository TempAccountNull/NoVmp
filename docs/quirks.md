# Hardware quirk bits (`UC_CTL_X86_HW_QUIRKS`)

The emulator follows the Intel SDM by default (quirks = 0), so its results can be checked against the SDM pseudocode. Wherever the i5-13600K does something the SDM does not allow, that behaviour gets its own bit in `UC_CTL_X86_HW_QUIRKS` (`uc_ctl_set_x86_hw_quirks`, bits in `unicorn/include/unicorn/x86.h`). Setting every bit the CPU needs reproduces the hardware. Where the SDM is silent, or says "undefined" or "implementation specific", the emulator copies the hardware and no bit is needed, because nothing deviates from the SDM.

Each bit is checked at run time (in a helper), so changing the mask also takes effect in code that was already translated.

| bit | name | SDM default (quirks 0) | i5-13600K behaviour with the bit | ledger |
|---|---|---|---|---|
| 0 | `UC_X86_QUIRK_FCOMI_KEEPS_C1` | FCOMI/FCOMIP/FUCOMI/FUCOMIP set FSW.C1 = 0 | C1 left unchanged | U38 |
| 1 | `UC_X86_QUIRK_CVTPI2PS_M64_KEEPS_X87` | CVTPI2PS xmm, m64 makes the x87→MMX transition (TOP = 0, all tags valid) and takes a pending #MF | no transition, x87 state unchanged | U44/U50 |
| 2 | `UC_X86_QUIRK_FYL2XP1_BELOW_M1` | FYL2XP1 with a finite ST0 < −1 is #IA, giving QNaN indefinite when masked (Vol1 Table 8-10), for every ST1 | result ST0 with PE; ST1 = ±0/±∞ gives ±0/±∞ (sign x XOR y), no #IA | U53/U432 |
| 3 | `UC_X86_QUIRK_PTWRITE_NOP` | PTWRITE #UD (CPUID.14.0:EBX[4] = 0) | operand read, nothing else happens | U80 |
| 4 | `UC_X86_QUIRK_DPPD_NAN_ORDER` | DPPD with two NaN products: p0 + p1 in both elements, so both get the first NaN, p0 (DPPD Operation, Vol1 Table 4-8) | element i := p[i] + p[i^1], so element 1 gets p1 | U98 |
| 5 | `UC_X86_QUIRK_REP_ZERO_COUNT_ZX` | 67h REP MOVS/STOS/LODS/CMPS/SCAS in 64-bit mode with ECX = 0 writes no register (REP Operation: `WHILE CountReg ≠ 0`) | zero-extends RCX/RSI/RDI (MOVS), RCX/RDI (STOS), RCX (LODS/CMPS/SCAS) | U60/U430 |
| 6 | `UC_X86_QUIRK_X87_CMP_UNMASKED_IA_SETS_CC` | FCOM/FCOMP/FCOMPP/FUCOM*/FCOMI*/FUCOMI* with an unmasked #IA leave C3/C2/C0 (ZF/PF/CF) unchanged ("set only if the exception is masked") | sets them to "unordered" (111) anyway | U54/U431 |

All bits the i5-13600K needs are bits 0–6 = `0x7F`.

## Harness

- `emu-alltest --quirks N|cpu|sdm`. `cpu` = `QUIRKS_I5_13600K` (`Emulator/tests/alltest/at_engine.hpp`, the one constant for this CPU), `sdm` = 0.
- Defaults when no `--quirks` is given:
  - hardware comparisons use `cpu`: the quick and `--full` runs, and `--cases` lines without `=>`;
  - expected-value (`=>`) lines use 0.
  - `--quirks` sets both. The `--cases` summary prints the quirks it used.
- `test.cmd`:
  - every SDM-model file runs with `--quirks 0`: expect_selftest, cases_opmask, cases_amx, cases_evex_m1, cases_nan_evex, cases_keylocker, cases_sha_sm, cases_vnni_ifma, cases_dp_nan_sdm;
  - the hardware files run with `--quirks cpu`: cases_nan, cases_dp_nan, cases_quirks;
  - `unicorn-test_x86 test_x86_hw_quirk_bits` checks every bit both off and on.
- `Emulator/data/cases_quirks.txt` has at least one hardware case per bit. With `--quirks cpu` 0 of 22 cases differ; with `--quirks 0` all 22 differ.

## Hardware behaviour left out on purpose (SDM only, no bit)

- **DPPS/VDPPS with two or more NaN products.** The hardware does not give the same result on every run. `cases_dpps_nan.txt` has 492 cases; 40 runs on 2026-10-08 gave these results:
  - 225 of the 492 cases changed result at least once.
  - Each run had 9–41 cases (2–8 %) that differed from the majority result, usually in short runs of neighbouring cases.
  - Elements 0 and 2 never varied. Element 0 always gets the first NaN in the order (1, 0, 3, 2) and element 2 in the order (3, 2, 1, 0).
  - Elements 1 and 3 usually follow the orders (0, 1, 2, 3) and (2, 3, 0, 1). In about 10 % of the affected results the order within one pair is swapped.
  - The majority rule is t[j] := p[j^1] + p[j], element i := t[i] + t[i^2]. It is not implemented.
- **REP LODS with 67h and ECX = 0.** In about 5 % of runs the hardware leaves RCX unchanged, which is what the SDM says (hwcheck_gate1 case 6: 16 of 400 runs). Bit 5 models the usual result.

## SDM audit (2026-10-08)

| behaviour | SDM text (section) | hardware (i5-13600K) | before | now |
|---|---|---|---|---|
| FCOMI/FUCOMI C1 | FCOMI page: C1 "Set to 0" | C1 unchanged | bit 0 | bit 0 (unchanged) |
| CVTPI2PS xmm, m64 x87 transition | CVTPI2PS page (only CVTPI2PD m64 exempt) | no transition | bit 1 | bit 1 (unchanged) |
| FYL2XP1 x < −1, y finite non-zero | Vol1 Table 8-10 "operand more negative than −1" → #IA | ST0, PE | bit 2 | bit 2 (unchanged) |
| FYL2XP1 x < −1, y = ±0/±∞ | Vol1 Table 8-10 (as above) | ±0/±∞, no #IA | hardware default | SDM default, bit 2 (U432) |
| PTWRITE with CPUID.14 = 0 | PTWRITE: #UD if CPUID.(EAX=14H,ECX=0):EBX[4] = 0 | operand read only | bit 3 | bit 3 (unchanged) |
| DPPD with two NaN products | DPPD Operation (DP_primitive), Vol1 Table 4-8 | element i := p[i] + p[i^1] | SDM default | SDM default, bit 4 (U98) |
| DPPS with ≥2 NaN products | DPPS Operation | not repeatable (see above) | SDM default | SDM default (no bit) |
| 67h REP string, ECX = 0 | REP Operation `WHILE CountReg ≠ 0 … OD` (Vol2B REP) | RCX/RSI/RDI zero-extended | hardware default (U60) | SDM default, bit 5 (U430) |
| FCOM/FUCOM unmasked #IA: C3/C2/C0 | FCOM: "If the invalid-arithmetic-operand exception is unmasked, the condition code flags are not set"; FUCOM: "set only if the exception is masked" | 111 | hardware default (U54) | SDM default, bit 6 (U431) |
| FCOMI/FUCOMI unmasked #IA: ZF/PF/CF | FCOMI: "the status flags in the EFLAGS register are set only if the exception is masked" | ZF = PF = CF = 1 | hardware default (U54) | SDM default, bit 6 (U431) |
| FTST / FICOM unmasked #IA | Operation sets 111 with no IM condition | 111 | 111 | 111 (SDM = hardware) |
| FDP only on unmasked exceptions, FCS/FDS saved as 0 | Vol1 8.1.8: only if CPUID.07H.0:EBX[6] / EBX[13] = 1 | both bits set, behaviour as described | behaviour always on, but the built-in models reported neither bit | both bits reported on every model (U433); profiles are returned unchanged |
| x87 transcendental values (U53/U56) | Vol1 8.3.10: error bounds only (< 1 ulp, RN) | microcode-exact values | hardware | hardware (implementation defined) |
| PE on transcendentals | Vol1 8.5.6: transcendentals "by nature produce inexact results" | PE always | hardware | hardware (consistent with 8.5.6) |
| F2XM1 \|x\| > 1, FYL2XP1 x = −1 / +∞ | "undefined" / "implementation specific" | as measured | hardware | hardware |
| x87 NaN tie (U97), RCP/RSQRT values (U81) | not specified / error bound only | as measured | hardware | hardware |
| undefined flags (BSF/BSR zero source, AAA, SHRD OF, IMUL, …), reserved fields (FCW 15:13/7:6, FSAVE reserved words) | "undefined" / "reserved" | as measured | hardware | hardware |
