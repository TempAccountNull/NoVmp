# Approximate reciprocal and reciprocal square root (decision A9)

Scope: VRCP14PS/PD/SS/SD and VRSQRT14PS/PD/SS/SD (AVX-512F / AVX10.1), VRCPPH/VRCPSH and
VRSQRTPH/VRSQRTSH (AVX512-FP16 / AVX10.1), VRCPBF16 and VRSQRTBF16 (AVX10.2), and the legacy
RCPPS/RCPSS/RSQRTPS/RSQRTSS (+ VEX.128/256) for reference. Ledger U81 (legacy), U236 (14-bit forms),
U337 (FP16), U373 (BF16), U940-U959 (this review), U1240-U1243 (AMB-2: requirements checker, SDE labelled
reference, verified_forms release; section 8).

**Result in one line:** every form meets every documented architectural requirement; only the legacy
forms are bit-exact to Intel hardware. The 14-bit, FP16 and BF16 forms return the correctly rounded
value, which is inside Intel's documented bounds but is **not** claimed to be the bits an Intel CPU
returns.

## 1. Which forms exist (checked per format, not assumed)

| Form | Encoding | Formats / widths | CPUID | Source |
|---|---|---|---|---|
| RCPPS / RCPSS | NP / F3 0F 53, VEX.128/256 (PS), VEX.LIG (SS) | FP32 | SSE / AVX | SDM 092 Vol2B |
| RSQRTPS / RSQRTSS | NP / F3 0F 52, VEX as above | FP32 | SSE / AVX | SDM 092 Vol2B |
| VRCP14PS / PD | EVEX.66.0F38.W0 / W1 4C | FP32 / FP64, VL 128/256/512, {1toN} | (AVX512VL AND) AVX512F, or AVX10.1 | SDM 092 Vol2C 5-674/5-676 |
| VRCP14SS / SD | EVEX.LLIG.66.0F38.W0 / W1 4D | FP32 / FP64 scalar | AVX512F or AVX10.1 | Vol2C 5-678/5-680 |
| VRSQRT14PS / PD | EVEX.66.0F38.W0 / W1 4E | FP32 / FP64, VL 128/256/512, {1toN} | as VRCP14 | Vol2C 5-714/5-716 |
| VRSQRT14SS / SD | EVEX.LLIG.66.0F38.W0 / W1 4F | FP32 / FP64 scalar | AVX512F or AVX10.1 | Vol2C 5-718/5-720 |
| VRCPPH / VRCPSH | EVEX.66.MAP6.W0 4C / 4D | FP16 (PH: VL 128/256/512, {1toN}) | (AVX512_FP16 AND AVX512VL) or AVX10.1 | Vol2C 5-682/5-684 |
| VRSQRTPH / VRSQRTSH | EVEX.66.MAP6.W0 4E / 4F | FP16 | as VRCPPH | Vol2C 5-722/5-724 |
| VRCPBF16 | EVEX.NP.MAP6.W0 4C | BF16, VL 128/256/512, {1toN} | AVX10.2 | AVX10.2 spec 361050-007, 7.12 |
| VRSQRTBF16 | EVEX.NP.MAP6.W0 4E | BF16, VL 128/256/512, {1toN} | AVX10.2 | AVX10.2 spec 7.15 |

Not defined (verified: no page in SDM 092, the older SDM 325462-080, ISE 319433-034/-057/-058/-060/-062
or the AVX10.2 spec): a "14" form for FP16 or BF16, any scalar BF16 reciprocal (MAP6 NP 4D / 4F),
any W1 form in MAP6, a 12-bit (legacy-style) form for FP64. The emulator raises #UD for these
encodings (cases in `cases_rcp.txt` / `cases_rcp_bf16.txt`). VRCP28/VRSQRT28 (AVX512ER, Xeon Phi) are a
different instruction family and outside this review.

## 2. Documented architectural requirements

All forms: MXCSR.RC is ignored; no MXCSR flag is set and no SIMD floating-point exception (#XM) is
reported ("SIMD Floating-Point Exceptions: None"; the BF16 pages: MXCSR neither consulted nor updated);
SNaN -> the QNaN of the same payload, QNaN -> itself.

| Form | Error bound | Special cases (tables) | DAZ / FTZ |
|---|---|---|---|
| RCPPS/SS | \|rel err\| <= 1.5 * 2^-12 | 0 -> INF (sign kept); denormal = 0; tiny results always flushed to 0 (inputs >= 1.11111111110100000000000B*2^125 never tiny, <= 1.00000000000110000000001B*2^126 always tiny) | DAZ/FTZ irrelevant (always as if set) |
| RSQRTPS/SS | <= 1.5 * 2^-12 | 0 -> INF (sign kept); denormal = 0; negative (not -0) -> QNaN indefinite | as RCPPS |
| VRCP14* | < 2^-14 | Table 5-24/5-25: \|X\| <= 2^-128 (PS) / 2^-1024 (PD) -> INF ("very small denormal"); \|X\| > 2^126 / 2^1022 -> underflow, "up to 18 bits of fractions are returned", "mantissa shifted right by one or two bits"; X = 2^-n -> 2^n | denormal source = 0 only with MXCSR.DAZ; underflow results flushed to 0 (operand's sign) only with MXCSR.FTZ, otherwise "correct underflow result is written" |
| VRSQRT14* | < 2^-14 | Tables 5-32..5-35: any denormal -> normal (no overflow); X = 2^-2n -> 2^n; X < 0 including -INF -> QNaN indefinite; -0 -> -INF; +0 -> +INF; +INF -> +0 | DAZ as VRCP14 (FTZ cannot act) |
| VRCPPH/SH | < 2^-11 + 2^-14 | Table 5-26: 0 <= X <= 2^-16 -> INF (sign kept); +-INF -> +-0; X = 2^-n -> 2^n | FP16 denormal inputs are always used ("instructions associated with AVX512_FP16 always handle FP16 denormal number inputs"); no FTZ |
| VRSQRTPH/SH | < 2^-11 + 2^-14 | Table 5-36 (= the VRSQRT14 table) | as VRCPPH |
| VRCPBF16 | < 2^-8 + 2^-14 | Table 7.2: 0 <= X < 2^-126 -> +INF and -2^-126 < X <= 0 -> -INF (DAZ); +-INF -> +-0; X = 2^-n -> 2^n | always DAZ and FTZ, RNE ("//DAZ, FTZ, SAE") |
| VRSQRTBF16 | < 2^-8 + 2^-14 | Table 7.3: \|X\| < 2^-126 -> +-INF (DAZ); X = 2^-2n -> 2^n; X < 0 incl. -INF -> QNaN indefinite; +INF -> +0 | always DAZ |

Document inconsistencies noted (no effect on the implementation): the VRSQRT14SS description says "When
the source operand is an INF, zero with the sign of the source value is returned" while its Table 5-35
(and every other VRSQRT14 page) gives -INF -> QNaN indefinite; the table is followed. VRSQRT14PS cites
exception class "Type 4" where the other EVEX pages cite E4. Table 5-26 prints "X > +INF" / "X < -INF"
for the INF rows.

**Implementation-specific (not documented):** the bits of the result inside the bound. For VRCP14 /
VRSQRT14 the SDM states that "a numerically exact implementation ... can be found at" an Intel web
article (reference C code, not part of any document available here); the FP16 and BF16 pages give no
reference at all. Whether an underflowing VRCPPH result is a denormal or a flushed zero is also not
stated (the FP16 rules imply no FTZ).

## 3. Algorithms used (table-free)

**Legacy RCPPS / RSQRTPS (U81, bit-exact):** the result is the reciprocal (reciprocal square root) of
the MIDPOINT of the input interval selected by the top 11 mantissa bits (RSQRT: top 10 bits plus the
exponent parity), rounded to nearest at 12 fraction bits, computed with exact integer arithmetic
(`x86_rcp12` / `x86_rsqrt12` in fpu_helper.c). The interval maximum error is reached at an interval end
point (the error is monotone inside the interval), so the bound is checked analytically on 2^11
(2 x 2^10) intervals: worst |rel err| RCP 2^-11.7016, RSQRT 2^-11.5820, both <= 1.5 * 2^-12 = 2^-11.415
(`ref_rcp.py --selftest`); the SDM's tiny-result guarantees hold at their two boundary inputs.

**14-bit, FP16 and BF16 forms (stand-in):** the exact 1/x resp. 1/sqrt(x) rounded to nearest-even to the
destination precision p (24, 53, 11, 8) with an unbounded exponent; overflow -> INF. Error bound: one
rounding gives |rel err| <= 2^-p relative to the result's leading bit, i.e. 2^-24 / 2^-53 / 2^-11 / 2^-8 for
normal results, inside every documented bound (2^-14; 2^-11 + 2^-14; 2^-8 + 2^-14). Tiny results:

- VRCP14 (U940): tiny = below 2^emin after the p-bit rounding (SDM Vol1 4.9.1.5, tininess after rounding);
  with FTZ a zero of the operand's sign, otherwise the exact value rounded ONCE at the denormal quantum
  (the SDM's "correct underflow result"; the result has 22 or 21 significant bits, the "mantissa shifted
  right by one or two bits"). U236 rounded to p bits first and then again at the quantum; that double
  rounding differs from the correct underflow result in 3,144,378 of the 16,777,215 FP32 underflow
  inputs (by one denormal ulp) and was replaced. Error of an underflow result (>= 2^-128 / 2^-1024, half a quantum of 2^-149 / 2^-1074): <= 2^-22 (FP32), 2^-51 (FP64).
- VRCPPH: no FTZ, one rounding at the FP16 denormal quantum. For 1/X below 2^-14 (X > 2^14, 2,047
  positive inputs) no FP16 value meets 2^-11 + 2^-14 (the denormal grid has only 9-10 significant bits);
  the correctly rounded denormal has the smallest possible error, worst 2^-9.0056. The SDM is silent on
  these inputs; this is a limit of the format, not of the algorithm.
- VRCPBF16: tiny after rounding -> a zero of the operand's sign (FTZ); this happens exactly for
  |X| > 2^126.

How the exact value is formed (stability): FP32 1/x is computed in float64, FP64 1/x in float128. A
reciprocal 1/m of an integer m < 2^L has no run of L equal bits in its binary expansion (the remainder
r/m after any position is >= 1/m and <= 1 - 1/m), so the wider rounding (29 resp. 60 bits below the
result's last place) can never create or destroy a midpoint at the p-bit or denormal quantum; the result
equals the correctly rounded one. FP32/FP64 1/sqrt(x): float128 sqrt then divide (relative error
< 2^-111) then RNE to p bits; FP16 1/sqrt(x): exact integer square root; BF16 1/sqrt(x): float64 sqrt and
divide. These equal the correctly rounded value for every FP16, BF16 and FP32 input (exhaustive checks
below); for FP64 RSQRT14 the float128 approximation decides correctly unless 1/sqrt(x) lies within
~2^-111 (relative) of a 53-bit midpoint - none was found in the samples below, and the error stays
< 2^-52 even then. No lookup table, no host floating-point state, no measured constants.

## 4. Validation methods and what they establish

| Method | Establishes | Cannot establish |
|---|---|---|
| Independent model `Emulator/tools/isa/ref_rcp.py` (exact integers / fractions, written from the SDM / spec text) and its `--selftest` | the stand-in meets every documented requirement (tables, bound, DAZ/FTZ, NaN rules): every FP16 and BF16 encoding, every FP32 exponent x 46 mantissas x 2 signs x 6 MXCSR, every third FP64 exponent; the legacy model's bound | Intel's bits |
| Expected-value cases (`cases_rcp.txt` with `--avx512` and with `--avx10 1`, `cases_rcp_f16_all.txt`, `cases_rcp_bf16.txt` with `--avx10 2`) | the emulator equals the model for every form, VL, masking, memory / {1toN}, high registers, nine MXCSR settings, every FP16 and BF16 encoding, #UD for the undefined encodings | Intel's bits |
| Exhaustive emulator sweep `Emulator/tools/isa/rcp_exhaustive.c` (own exact-integer C model, links unicorn.lib) | every FP32 encoding (x 5 MXCSR) and every FP16/BF16 encoding: emulator == model, normal results inside the bound; FP64: 17,039,360 inputs (every exponent x 14 edge mantissas x 2 signs, the rest random) x 4 MXCSR | Intel's bits |
| Unit tests `test_x86_rc_*` (unicorn test_x86.c) | the table rows, DAZ/FTZ, scalar merging, MXCSR unchanged / no #XM with every exception unmasked, the bound model-free (every FP16/BF16, 65536 FP32) | Intel's bits |
| Hardware cases `cases_rcp_hw.txt` vs the i5-13600K | legacy forms bit-exact incl. MXCSR independence (with U81's exhaustive 2^32 x 6 MXCSR comparison) | anything about AVX-512 forms (the host lacks AVX-512) |
| Intel SDE 10.13.1 (`sde_rcp.py` + `sde_rcp_probe.c`), labelled "SDE-validated" | that Intel's own emulator meets the documented requirements, and how far the stand-in is from SDE's bits | silicon behaviour; SDE is a reference, never the definition |

SDE status: **run** in U1240-U1243 (AMB-2, 2026-10-10) through the U1040 harness
(`Emulator\tools\sde_crosscheck.ps1`, emu-alltest `--native-under-sde`), which does not need
`sde_rcp.py` / `sde_rcp_probe.c` (U942, kept unchanged; their earlier "not run" was the agent sandbox's
job object). Results: section 8.

## 5. Labels

| Form | Label | Evidence |
|---|---|---|
| RCPPS, RCPSS, RSQRTPS, RSQRTSS, VRCPPS, VRCPSS, VRSQRTPS, VRSQRTSS | **bit-exact hardware compatibility** (i5-13600K) | U81: all 2^32 inputs x 6 MXCSR, 0 mismatches; `cases_rcp_hw.txt` 2740 cases / 0 differing; SDM bound proven analytically |
| VRCP14PS/PD/SS/SD, VRSQRT14PS/PD/SS/SD | **documented architectural compliance**; bit-exact Intel compatibility **unverified** | sections 2-4, 8 |
| VRCPPH, VRCPSH, VRSQRTPH, VRSQRTSH | **documented architectural compliance** (normal results; subnormal VRCPPH results: best possible, see 3); bit-exact **unverified** | sections 2-4, 8 |
| VRCPBF16, VRSQRTBF16 | **documented architectural compliance**; bit-exact **unverified** (VRCPBF16 is bit-identical to SDE on every encoding: a labelled reference, not authoritative) | sections 2-4, 8 |

`Emulator/data/verified_forms.tsv`: since U1243 (AMB-2, user decision 2026-10-10) these 14 rows have the
status `doc` (✅ "documented compliance" in gen_instruction_table.py; before: `open`, ⏳), never
"bit-exact". Intel bit-compatibility stays unverified: it needs Intel's reference code or silicon.

## 6. Measured error (emulator, exhaustive where stated)

| Form | Inputs | Worst relative error of a normal result | Bound |
|---|---|---|---|
| VRCP14PS | all 2^32 x 5 MXCSR | 2^-24 (half ulp) | 2^-14 |
| VRSQRT14PS | all 2^32 x 5 MXCSR | 2^-24 | 2^-14 |
| VRCP14PD / VRSQRT14PD | 17,039,360 (57,344 edge = every exponent x 14 mantissas x 2 signs, rest random) x 4 MXCSR | 2^-53 / 2^-52 (double-precision check) | 2^-14 |
| VRCPPH / VRSQRTPH | all 65536 x 3 MXCSR | 2^-11.0014 / 2^-11.0018 | 2^-11 + 2^-14 = 2^-10.83 |
| VRCPBF16 / VRSQRTBF16 | all 65536 x 3 MXCSR | 2^-8.0113 / 2^-8.0142 | 2^-8 + 2^-14 = 2^-7.98 |
| RCPPS / RSQRTPS | all 2^11 / 2 x 2^10 intervals | 2^-11.7016 / 2^-11.5820 | 1.5 * 2^-12 = 2^-11.415 |

## 7. Limitations

- The 14-bit, FP16 and BF16 results are not Intel's bits. Software that compares VRCP14 output
  bit-for-bit with a real AVX-512 CPU (or uses the approximation error as a fingerprint) will see a
  difference; software that relies only on the documented bound will not.
- FP64 VRSQRT14: correct rounding is proven only up to the float128 margin (section 3).
- Documents that would settle bit-exactness (not available here, not searched online): Intel,
  "Reference Implementations for IA Approximation Instructions: VRCP14, VRSQRT14, VRCP28, VRSQRT28,
  and VEXP2" (software.intel.com article with the reference C sources; no document number) - it defines
  the VRCP14/VRSQRT14 bits; nothing equivalent is known for VRCPPH/VRSQRTPH or VRCPBF16/VRSQRTBF16.

## 8. AMB-2: requirements checker and the Intel SDE labelled reference (U1240-U1243)

Plan 1.J.32, user decision AMB-2 (2026-10-10): analytic model per A9; Intel SDE results are only a
**labelled reference**; the verified_forms rows are released as "documented compliance", never
"bit-exact". The emulator code is unchanged: the stand-ins of section 3 already meet every documented
requirement, so they are kept.

**Independent requirements checker** `Emulator/tools/isa/ref_amb2.py` (U1240): `check()` decides for one
element whether a result meets the documented requirements of its form (section 2): the strict bound
with exact rationals (rcp: |r x - 1| < B; rsqrt: (1 - B)^2 < r^2 x < (1 + B)^2), every table row, DAZ /
FTZ, the NaN rule (one-operand rule of Vol1 Table 4-8 where the page has no NaN row), and for VRCPPH
with |X| > 2^14 (1/X an FP16 denormal; the bound is unattainable there) a non-zero FP16 neighbour of 1/X.
It does not compare with any expected value. `model()` is a second, separately written analytic model
(Fraction / isqrt, one RNE rounding); `--selftest` (in test.cmd) proves it against the checker (every
FP16 / BF16 encoding, every FP32 / FP64 exponent; 410,272 element checks), shows the checker rejects
wrong results (beyond the bound, sign, zero / INF, NaN handling, DAZ / FTZ, table rows, 12-bit
accuracy), and finds ref_rcp.py bit-identical. `cases_amb2.txt` (5060 lines, `--avx10 2`): every
form's table rows and threshold boundaries under 7 MXCSR settings, scalar merging, VL128/256 masks and
the SDE distance sample (every FP32 / FP64 exponent x edge + LCG mantissas); 0 differing. Unit tests
`test_x86_amb2_rows` / `_fp64_bound` / `_tiny` (U1241) check the table rows (457 runs), the FP64 bound
and the FP16 / BF16 rows the bound cannot decide without any model.

**SDE run** (Intel SDE 10.13.1, `sde_crosscheck.ps1 -Only cases_amb2` with `-dmr`, then
`cases_rcp` `-spr`, `cases_rcp_f16_all` `-spr`, `cases_rcp_bf16` `-dmr`; every step under the build
lock; `ref_amb2.py --sde-report <pass-2 log>`). Only register-form lanes are analysed (memory forms
and masked-off lanes skipped); the 16 undefined-encoding lines (EVEX.vvvv != 1111b, MAP6 W1 forms) are
#UD on SDE as in the emulator. SDE values are evidence about SDE; they are not copied anywhere.

| Form | Bound | Lanes (file) | SDE == model | SDE vs model (ulps of the format) | max relative distance | SDE meets every documented requirement | SDE significant bits (normal results) |
|---|---|---|---|---|---|---|---|
| VRCP14PS | 2^-14 | 16,732 (amb2) + 1,914 (rcp) | 638 / 1,138 (all 607 / 1,101 table-row lanes) | 1 to 681, mostly 64-511 | 2^-14.22 | yes, all (worst 2^-14.22) | <= 17 |
| VRCP14PD | 2^-14 | 8,535 + 1,803 | 2,331 / 1,130 (all table rows) | up to 2^38 (= ~2^-15 relative) | 2^-15.00 / 2^-14.29 | yes, all | <= 15 (amb2) / <= 17 (rcp) |
| VRCP14SS / SD | 2^-14 | 126 + 584 each | 99 + 375 each (all table rows) | as PS / PD | 2^-14.83 / 2^-15.00 | yes, all | <= 17 |
| VRSQRT14PS | 2^-14 | 16,732 + 1,912 | 2,622 / 1,047 (all table rows) | 1 to 909 | 2^-14.09 | yes, all (worst 2^-14.09) | <= 17 |
| VRSQRT14PD | 2^-14 | 8,535 + 1,796 | 1,277 / 985 (all table rows) | up to 2^38.6 | 2^-14.21 | yes, all | <= 17 |
| VRSQRT14SS / SD | 2^-14 | 126 + 584 each | 84 + 321 / 86 + 320 (all table rows) | as PS / PD | 2^-14.21 | yes, all | <= 17 |
| VRCPPH | 2^-11 + 2^-14 | 65,536 (every encoding) + 472 + 1,286 | 64,282 of 65,536 | 0 or 1 (1,254 lanes) | 2^-10.00 (normal), 2^-8.03 (denormal results) | yes, all; 4,092 bound-unattainable lanes (|X| > 2^14): SDE is faithful too (picks the other neighbour in 30 of them) | 11 |
| VRCPSH | as VRCPPH | 123 + 306 | 117 + 289 | 0 or 1 | 2^-10.00 | yes, all | 11 |
| VRSQRTPH | 2^-11 + 2^-14 | 65,536 (every encoding) + 472 + 1,285 | 64,969 of 65,536 | 0 or 1 (567 lanes) | 2^-10.00 | yes, all | 11 |
| VRSQRTSH | as VRSQRTPH | 123 + 306 | 120 + 290 | 0 or 1 | 2^-10.00 | yes, all | 11 |
| VRCPBF16 | 2^-8 + 2^-14 | 66,836 (every encoding) + 472 | **all** | 0 | 0 | yes, all | 8 |
| VRSQRTBF16 | 2^-8 + 2^-14 | 66,830 (every encoding) + 472 | 66,703 of 66,830 (all 472 amb2 lanes) | 0 or 1 (127 lanes) | 2^-7.40 | yes, all | 8 |

What this shows: (1) SDE meets every documented requirement on every analysed lane, and every
documented table-row lane is bit-identical to the model (the special cases are fully specified; only
the bits inside the bound are open). (2) SDE's VRCP14 / VRSQRT14 values are short approximations (at
most 15-17 significant bits, low significand bits zero), the model is correctly rounded (24 / 53 bits):
both are inside 2^-14, at a relative distance of up to ~2^-14.1. (3) For FP16 and BF16 SDE is almost
correctly rounded: VRCPBF16 is bit-identical on every encoding; VRCPPH / VRSQRTPH / VRSQRTBF16 differ by
one ulp on 1.9 % / 0.9 % / 0.2 % of the encodings. None of this makes any form bit-exact: SDE is not
silicon and not a specification (A9: bit-exactness only with authoritative evidence), so every form
keeps the label **documented compliance**.

Logs (agent scratch, cited by the AMB-2 report): `sde\cases_amb2.log`, `sde\cases_rcp.log`,
`sde\cases_rcp_f16_all.log`, `sde\cases_rcp_bf16.log` and the `sde_report_*.txt` outputs of
`ref_amb2.py --sde-report`.
