# emu-alltest report

Unicorn 2.1 `UC_CPU_X86_MAX` vs host CPU. Mode **full**, 6 iterations per form, sample 1/1, quirks 0x0.

Universe: **13504 forms** decoded by Capstone (evex 9950, legacy 1976, vex 1441, x87 137, ). Run: **13504 forms** in 26 s.

## Buckets

| bucket | forms |
|---|---|
| match | 2977 |
| differs | 13 |
| unicorn-#UD (hw runs it) | 23 |
| host lacks + unicorn #UD | 10170 |
| host lacks, unicorn runs (needs SDM check) | 16 |
| not native-safe, unicorn runs (needs SDM check) | 193 |
| not native-safe, unicorn #UD | 12 |
| privileged (CPL0 in raw unicorn; Phase 2 CPL3) | 100 |
| harness error | 0 |

## Per ISA group (Capstone groups)

| group | match | differs | unicorn #UD | host lacks + uc #UD | host lacks, uc runs | not native, uc runs | not native, uc #UD | privileged | error |
|---|---|---|---|---|---|---|---|---|---|
| 3dnow | 1 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 |
| adx | 4 | 0 | 0 | 0 | 0 | 4 | 0 | 0 | 0 |
| aes | 6 | 0 | 0 | 0 | 0 | 6 | 0 | 0 | 0 |
| avx | 434 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| avx+aes | 12 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| avx+novlx | 186 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| avx+pclmul | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| avx2 | 197 | 0 | 0 | 16 | 0 | 0 | 0 | 0 | 0 |
| avx2+novlx | 72 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| avx512 | 0 | 0 | 0 | 1122 | 0 | 0 | 0 | 0 | 0 |
| avx512+vlx | 0 | 0 | 0 | 622 | 0 | 0 | 0 | 0 | 0 |
| base | 1147 | 0 | 23 | 7342 | 5 | 159 | 12 | 76 | 0 |
| bmi | 24 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| bmi2 | 32 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| bwi | 0 | 0 | 0 | 172 | 0 | 0 | 0 | 0 | 0 |
| bwi+vlx | 0 | 0 | 0 | 292 | 0 | 0 | 0 | 0 | 0 |
| cdi | 0 | 0 | 0 | 10 | 0 | 0 | 0 | 0 | 0 |
| cmov | 97 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| cmov+fpu | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| dqi | 0 | 0 | 0 | 27 | 0 | 0 | 0 | 0 | 0 |
| dqi+vlx | 0 | 0 | 0 | 22 | 0 | 0 | 0 | 0 | 0 |
| fc16 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| fma4 | 0 | 0 | 0 | 60 | 0 | 0 | 0 | 0 | 0 |
| fpu | 122 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| fsgsbase | 4 | 0 | 0 | 0 | 0 | 4 | 0 | 0 | 0 |
| mmx | 133 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| pclmul | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| pfi | 0 | 0 | 0 | 16 | 0 | 0 | 0 | 0 | 0 |
| rtm | 0 | 0 | 0 | 1 | 4 | 4 | 0 | 0 | 0 |
| sha | 7 | 0 | 0 | 0 | 0 | 7 | 0 | 0 | 0 |
| sse1 | 93 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sse2 | 253 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sse3 | 19 | 0 | 0 | 0 | 0 | 0 | 0 | 2 | 0 |
| sse41 | 90 | 0 | 0 | 0 | 0 | 4 | 0 | 0 | 0 |
| sse42 | 10 | 0 | 0 | 0 | 0 | 4 | 0 | 0 | 0 |
| sse4a | 0 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| ssse3 | 15 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 |
| vlx | 0 | 0 | 0 | 462 | 0 | 0 | 0 | 0 | 0 |
| vm | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| xop | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 | 0 |

## Differences (first iteration that differs)

- `f2xm1` (fpu, differs, 4/6): iter 0: x87 fcw/fsw/ftw hw=037F/0020/FF80 uc=037F/0001/FF82 (fsw mask BAFF); st(0) differs; 
- `fyl2x` (fpu, differs, 1/6): iter 1: x87 fcw/fsw/ftw hw=037F/0820/FF03 uc=037F/0A20/FF03 (fsw mask BAFF); st(0) differs; 
- `fptan` (fpu, differs, 4/6): iter 0: x87 fcw/fsw/ftw hw=037F/3820/3F00 uc=037F/3800/3F00 (fsw mask BEFF); st(1) differs; 
- `fpatan` (fpu, differs, 1/6): iter 2: x87 fcw/fsw/ftw hw=037F/0820/2003 uc=037F/0A20/2003 (fsw mask BAFF); 
- `fprem1` (fpu, differs, 1/6): iter 2: x87 fcw/fsw/ftw hw=037F/4100/8000 uc=037F/0000/8000 (fsw mask FFFF); 
- `fsincos` (fpu, differs, 4/6): iter 0: x87 fcw/fsw/ftw hw=037F/3A20/3F00 uc=037F/3800/3F00 (fsw mask BEFF); st(0) differs; 
- `fsin` (fpu, differs, 5/6): iter 0: x87 fcw/fsw/ftw hw=037F/0001/FF02 uc=037F/0000/FF02 (fsw mask BEFF); 
- `fcos` (fpu, differs, 5/6): iter 0: x87 fcw/fsw/ftw hw=037F/0020/FF20 uc=037F/0000/FF20 (fsw mask BEFF); st(0) differs; 
- `cvtpi2ps xmm0, qword ptr [rsi]` (sse1, differs, 5/6): iter 0: x87 fcw/fsw/ftw hw=037F/0000/FF00 uc=037F/0000/0000 (fsw mask FFFF); 
- `xsavec ptr [rsi]` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `xsavec64 ptr [rsi]` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `gf2p8mulb xmm0, xmm0` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `gf2p8affineqb xmm0, xmm0, 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `gf2p8affineinvqb xmm0, xmm0, 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `ptwrite eax` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `ptwrite dword ptr [rsi]` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `ptwrite rax` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `ptwrite qword ptr [rsi]` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vrsqrtps xmm0, xmm0` (avx, differs, 1/6): iter 5: ymm0 differs; 
- `vrcpps xmm0, xmmword ptr [rsi]` (avx, differs, 1/6): iter 3: ymm0 differs; 
- `vrsqrtps ymm0, ymmword ptr [rsi]` (avx, differs, 1/6): iter 2: ymm0[255:128] differs; 
- `vrcpps ymm0, ymm0` (avx, differs, 1/6): iter 4: ymm0 differs; 
- `vgf2p8mulb xmm0, xmm1, xmm0` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8mulb xmm0, xmm1, xmmword ptr [rsi]` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8mulb ymm0, ymm1, ymm0` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8mulb ymm0, ymm1, ymmword ptr [rsi]` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vpclmulqdq ymm0, ymm1, ymm0, 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vpclmulqdq ymm0, ymm1, ymmword ptr [rsi], 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8affineqb xmm0, xmm1, xmm0, 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8affineqb xmm0, xmm1, xmmword ptr [rsi], 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8affineinvqb xmm0, xmm1, xmm0, 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8affineinvqb xmm0, xmm1, xmmword ptr [rsi], 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8affineqb ymm0, ymm1, ymm0, 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8affineqb ymm0, ymm1, ymmword ptr [rsi], 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8affineinvqb ymm0, ymm1, ymm0, 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6
- `vgf2p8affineinvqb ymm0, ymm1, ymmword ptr [rsi], 0x5b` (base, unicorn-#UD (hw runs it), 6/6): iter 0: outcome hw=ok uc=vector 6

## Manual forms not reachable by the sweep yet

1847 of 4712 forms in `Emulator\data\isa_manual_forms.tsv` have a mnemonic/encoding Capstone 6 never produced (no decoder yet - plan 5.3/5.3b; some are naming differences to review):

- **3DNOW** (24): pavgusb, pf2id, pf2iw, pfacc, pfadd, pfcmpeq, pfcmpge, pfcmpgt, pfmax, pfmin, pfmul, pfnacc, pfpnacc, pfrcp, pfrcpit1, pfrcpit2, pfrsqit1, pfrsqrt, pfsub, pfsubr, pi2fd, pi2fw, pmulhrw, pswapd
- **?** (12): addr32, data16, fclex, finit, fsave, fstcw, fstenv, fstsw, lock, notrack, repnz, rex64
- **ACE_1** (15): bsrinit, bsrmovf, bsrmovh, bsrmovl, tilemovcol, top2bf16ps, top4bssd, top4bsud, top4busd, top4buud, top4mxbf8ps, top4mxbhf8ps, top4mxbssps, top4mxhbf8ps, top4mxhf8ps
- **ACE_1+AMX_AVX512** (1): tilemovrow
- **AMD_INVLPGB** (2): invlpgb, tlbsync
- **AMX_AVX512** (5): tcvtrowd2ps, tcvtrowps2bf16h, tcvtrowps2bf16l, tcvtrowps2phh, tcvtrowps2phl
- **AMX_BF16** (1): tdpbf16ps
- **AMX_COMPLEX** (2): tcmmimfp16ps, tcmmrlfp16ps
- **AMX_FP16** (1): tdpfp16ps
- **AMX_FP8** (4): tdpbf8ps, tdpbhf8ps, tdphbf8ps, tdphf8ps
- **AMX_INT8** (4): tdpbssd, tdpbsud, tdpbusd, tdpbuud
- **AMX_MOVRS** (2): tileloaddrs, tileloaddrst1
- **AMX_TILE** (3): tileloadd, tileloaddt1, tilestored
- **AMX_TILE_BASE** (4): ldtilecfg, sttilecfg, tilerelease, tilezero
- **APX_F** (4): crc32, jmpabs, popp, pushp
- **APX_F+APX_F_N3** (25): adc, add, and, dec, div, idiv, imul, inc, mul, neg, not, or, rcl, rcr, rol, ror, sal, sar, sbb, shl, shld, shr, shrd, sub, xor
- **APX_F_ADX+APX_F_ADX_N3** (2): adcx, adox
- **APX_F_AMX** (3): tileloadd, tileloaddt1, tilestored
- **APX_F_AMX_BASE** (2): ldtilecfg, sttilecfg
- **APX_F_AMX_MOVRS** (2): tileloaddrs, tileloaddrst1
- **APX_F_BMI1+APX_F_BMI1_N3** (6): andn, bextr, blsi, blsmsk, blsr, tzcnt
- **APX_F_BMI2** (7): mulx, pdep, pext, rorx, sarx, shlx, shrx
- **APX_F_BMI2+APX_F_BMI2_N3** (1): bzhi
- **APX_F_CET** (4): wrssd, wrssq, wrussd, wrussq
- **APX_F_CMPCCXADD** (22): cmpaexadd, cmpaxadd, cmpbexadd, cmpbxadd, cmpexadd, cmpgexadd, cmpgxadd, cmplexadd, cmplxadd, cmpnbexadd, cmpnbxadd, cmpnexadd, cmpnlexadd, cmpnlxadd, cmpnoxadd, cmpnpxadd, cmpnsxadd, cmpnzxadd, cmpoxadd, cmppxadd, cmpsxadd, cmpzxadd
- **APX_F_ENQCMD** (2): enqcmd, enqcmds
- **APX_F_INVPCID** (1): invpcid
- **APX_F_KOPB** (1): kmovb
- **APX_F_KOPD** (1): kmovd
- **APX_F_KOPQ** (1): kmovq
- **APX_F_KOPW** (1): kmovw
- **APX_F_LZCNT+APX_F_LZCNT_N3** (1): lzcnt
- **APX_F_MOVBE** (1): movbe
- **APX_F_MOVDIR64B** (1): movdir64b
- **APX_F_MOVDIRI** (1): movdiri
- **APX_F_MOVRS** (1): movrs
- **APX_F_MSR_IMM** (2): rdmsr, wrmsrns
- **APX_F_N3** (84): ccmpb, ccmpbe, ccmpf, ccmpl, ccmple, ccmpnb, ccmpnbe, ccmpnl, ccmpnle, ccmpno, ccmpns, ccmpnz, ccmpo, ccmps, ccmpt, ccmpz, cfcmovb, cfcmovbe, cfcmovl, cfcmovle, cfcmovnb, cfcmovnbe, cfcmovnl, cfcmovnle, cfcmovno, cfcmovnp, cfcmovns, cfcmovnz, cfcmovo, cfcmovp, cfcmovs, cfcmovz, cmova, cmovae, cmovb, cmovbe, cmove, cmovg, cmovge, cmovl, …
- **APX_F_POPCNT+APX_F_POPCNT_N3** (1): popcnt
- **APX_F_RAO_INT** (4): aadd, aand, aor, axor
- **APX_F_USER_MSR** (2): urdmsr, uwrmsr
- **APX_F_VMX** (2): invept, invvpid
- **AVX** (128): vcmpeq_ospd, vcmpeq_osps, vcmpeq_ossd, vcmpeq_osss, vcmpeq_uqpd, vcmpeq_uqps, vcmpeq_uqsd, vcmpeq_uqss, vcmpeq_uspd, vcmpeq_usps, vcmpeq_ussd, vcmpeq_usss, vcmpeqpd, vcmpeqps, vcmpeqsd, vcmpeqss, vcmpfalse_ospd, vcmpfalse_osps, vcmpfalse_ossd, vcmpfalse_osss, vcmpfalsepd, vcmpfalseps, vcmpfalsesd, vcmpfalsess, vcmpge_oqpd, vcmpge_oqps, vcmpge_oqsd, vcmpge_oqss, vcmpgepd, vcmpgeps, vcmpgesd, vcmpgess, vcmpgt_oqpd, vcmpgt_oqps, vcmpgt_oqsd, vcmpgt_oqss, vcmpgtpd, vcmpgtps, vcmpgtsd, vcmpgtss, …
- **AVX10_2_BF16_128** (28): vaddbf16, vcmpbf16, vdivbf16, vfmadd132bf16, vfmadd213bf16, vfmadd231bf16, vfmsub132bf16, vfmsub213bf16, vfmsub231bf16, vfnmadd132bf16, vfnmadd213bf16, vfnmadd231bf16, vfnmsub132bf16, vfnmsub213bf16, vfnmsub231bf16, vfpclassbf16, vgetexpbf16, vgetmantbf16, vmaxbf16, vminbf16, vmulbf16, vrcpbf16, vreducebf16, vrndscalebf16, vrsqrtbf16, vscalefbf16, vsqrtbf16, vsubbf16
- **AVX10_2_BF16_256** (28): vaddbf16, vcmpbf16, vdivbf16, vfmadd132bf16, vfmadd213bf16, vfmadd231bf16, vfmsub132bf16, vfmsub213bf16, vfmsub231bf16, vfnmadd132bf16, vfnmadd213bf16, vfnmadd231bf16, vfnmsub132bf16, vfnmsub213bf16, vfnmsub231bf16, vfpclassbf16, vgetexpbf16, vgetmantbf16, vmaxbf16, vminbf16, vmulbf16, vrcpbf16, vreducebf16, vrndscalebf16, vrsqrtbf16, vscalefbf16, vsqrtbf16, vsubbf16
- **AVX10_2_BF16_512** (28): vaddbf16, vcmpbf16, vdivbf16, vfmadd132bf16, vfmadd213bf16, vfmadd231bf16, vfmsub132bf16, vfmsub213bf16, vfmsub231bf16, vfnmadd132bf16, vfnmadd213bf16, vfnmadd231bf16, vfnmsub132bf16, vfnmsub213bf16, vfnmsub231bf16, vfpclassbf16, vgetexpbf16, vgetmantbf16, vmaxbf16, vminbf16, vmulbf16, vrcpbf16, vreducebf16, vrndscalebf16, vrsqrtbf16, vscalefbf16, vsqrtbf16, vsubbf16
- **AVX10_2_BF16_SCALAR** (1): vcomisbf16
- **AVX10_MOVRS_128** (4): vmovrsb, vmovrsd, vmovrsq, vmovrsw
- **AVX10_MOVRS_256** (4): vmovrsb, vmovrsd, vmovrsq, vmovrsw
- **AVX10_MOVRS_512** (4): vmovrsb, vmovrsd, vmovrsq, vmovrsw
- **AVX10_V2_AUX_128** (21): vcvtbf42hf8, vcvtbf62hf8, vcvtbf82bf4s, vcvtbf82bf6s, vcvtbf82ps, vcvtbiasps2bf8, vcvtbiasps2bf8s, vcvtbiasps2hf8, vcvtbiasps2hf8s, vcvthf62hf8, vcvthf82bf4s, vcvthf82hf6s, vcvthf82ps, vcvtps2bf8, vcvtps2bf8s, vcvtps2hf8, vcvtps2hf8s, vcvtrops2hf8, vcvtrops2hf8s, vpmovssdb, vunpackb
- **AVX10_V2_AUX_256** (21): vcvtbf42hf8, vcvtbf62hf8, vcvtbf82bf4s, vcvtbf82bf6s, vcvtbf82ps, vcvtbiasps2bf8, vcvtbiasps2bf8s, vcvtbiasps2hf8, vcvtbiasps2hf8s, vcvthf62hf8, vcvthf82bf4s, vcvthf82hf6s, vcvthf82ps, vcvtps2bf8, vcvtps2bf8s, vcvtps2hf8, vcvtps2hf8s, vcvtrops2hf8, vcvtrops2hf8s, vpmovssdb, vunpackb
- **AVX10_V2_AUX_512** (21): vcvtbf42hf8, vcvtbf62hf8, vcvtbf82bf4s, vcvtbf82bf6s, vcvtbf82ps, vcvtbiasps2bf8, vcvtbiasps2bf8s, vcvtbiasps2hf8, vcvtbiasps2hf8s, vcvthf62hf8, vcvthf82bf4s, vcvthf82hf6s, vcvthf82ps, vcvtps2bf8, vcvtps2bf8s, vcvtps2hf8, vcvtps2hf8s, vcvtrops2hf8, vcvtrops2hf8s, vpmovssdb, vunpackb
- **AVX512F_128** (62): vcmpeq_ospd, vcmpeq_osps, vcmpeq_uqpd, vcmpeq_uqps, vcmpeq_uspd, vcmpeq_usps, vcmpeqpd, vcmpeqps, vcmpfalsepd, vcmpfalseps, vcmpge_oqpd, vcmpge_oqps, vcmpgepd, vcmpgeps, vcmpgt_oqpd, vcmpgt_oqps, vcmpgtpd, vcmpgtps, vcmple_oqpd, vcmple_oqps, vcmplepd, vcmpleps, vcmplt_oqpd, vcmplt_oqps, vcmpltpd, vcmpltps, vcmpneq_oqpd, vcmpneq_oqps, vcmpneq_ospd, vcmpneq_osps, vcmpneq_uspd, vcmpneq_usps, vcmpneqpd, vcmpneqps, vcmpnge_uqpd, vcmpnge_uqps, vcmpngepd, vcmpngeps, vcmpngt_uqpd, vcmpngt_uqps, …
- **AVX512F_256** (62): vcmpeq_ospd, vcmpeq_osps, vcmpeq_uqpd, vcmpeq_uqps, vcmpeq_uspd, vcmpeq_usps, vcmpeqpd, vcmpeqps, vcmpfalsepd, vcmpfalseps, vcmpge_oqpd, vcmpge_oqps, vcmpgepd, vcmpgeps, vcmpgt_oqpd, vcmpgt_oqps, vcmpgtpd, vcmpgtps, vcmple_oqpd, vcmple_oqps, vcmplepd, vcmpleps, vcmplt_oqpd, vcmplt_oqps, vcmpltpd, vcmpltps, vcmpneq_oqpd, vcmpneq_oqps, vcmpneq_ospd, vcmpneq_osps, vcmpneq_uspd, vcmpneq_usps, vcmpneqpd, vcmpneqps, vcmpnge_uqpd, vcmpnge_uqps, vcmpngepd, vcmpngeps, vcmpngt_uqpd, vcmpngt_uqps, …
- **AVX512F_512** (62): vcmpeq_ospd, vcmpeq_osps, vcmpeq_uqpd, vcmpeq_uqps, vcmpeq_uspd, vcmpeq_usps, vcmpeqpd, vcmpeqps, vcmpfalsepd, vcmpfalseps, vcmpge_oqpd, vcmpge_oqps, vcmpgepd, vcmpgeps, vcmpgt_oqpd, vcmpgt_oqps, vcmpgtpd, vcmpgtps, vcmple_oqpd, vcmple_oqps, vcmplepd, vcmpleps, vcmplt_oqpd, vcmplt_oqps, vcmpltpd, vcmpltps, vcmpneq_oqpd, vcmpneq_oqps, vcmpneq_ospd, vcmpneq_osps, vcmpneq_uspd, vcmpneq_usps, vcmpneqpd, vcmpneqps, vcmpnge_uqpd, vcmpnge_uqps, vcmpngepd, vcmpngeps, vcmpngt_uqpd, vcmpngt_uqps, …
- **AVX512F_SCALAR** (62): vcmpeq_ossd, vcmpeq_osss, vcmpeq_uqsd, vcmpeq_uqss, vcmpeq_ussd, vcmpeq_usss, vcmpeqsd, vcmpeqss, vcmpfalsesd, vcmpfalsess, vcmpge_oqsd, vcmpge_oqss, vcmpgesd, vcmpgess, vcmpgt_oqsd, vcmpgt_oqss, vcmpgtsd, vcmpgtss, vcmple_oqsd, vcmple_oqss, vcmplesd, vcmpless, vcmplt_oqsd, vcmplt_oqss, vcmpltsd, vcmpltss, vcmpneq_oqsd, vcmpneq_oqss, vcmpneq_ossd, vcmpneq_osss, vcmpneq_ussd, vcmpneq_usss, vcmpneqsd, vcmpneqss, vcmpnge_uqsd, vcmpnge_uqss, vcmpngesd, vcmpngess, vcmpngt_uqsd, vcmpngt_uqss, …
- **AVX512_BF16_128** (3): vcvtne2ps2bf16, vcvtneps2bf16, vdpbf16ps
- **AVX512_BF16_256** (3): vcvtne2ps2bf16, vcvtneps2bf16, vdpbf16ps
- **AVX512_BF16_512** (3): vcvtne2ps2bf16, vcvtneps2bf16, vdpbf16ps
- **AVX512_COM_EF_SCALAR** (6): vcomxsd, vcomxsh, vcomxss, vucomxsd, vucomxsh, vucomxss
- **AVX512_FP16_128** (92): vaddph, vcmpeq_osph, vcmpeq_uqph, vcmpeq_usph, vcmpeqph, vcmpfalse_osph, vcmpfalseph, vcmpge_oqph, vcmpgeph, vcmpgt_oqph, vcmpgtph, vcmple_oqph, vcmpleph, vcmplt_oqph, vcmpltph, vcmpneq_oqph, vcmpneq_osph, vcmpneq_usph, vcmpneqph, vcmpnge_uqph, vcmpngeph, vcmpngt_uqph, vcmpngtph, vcmpnle_uqph, vcmpnleph, vcmpnlt_uqph, vcmpnltph, vcmpord_sph, vcmpordph, vcmpph, vcmptrue_usph, vcmptrueph, vcmpunord_sph, vcmpunordph, vcvtdq2ph, vcvtpd2ph, vcvtph2dq, vcvtph2pd, vcvtph2psx, vcvtph2qq, …
- **AVX512_FP16_128N+AVX512_MOVZXC_128** (1): vmovw
- **AVX512_FP16_256** (92): vaddph, vcmpeq_osph, vcmpeq_uqph, vcmpeq_usph, vcmpeqph, vcmpfalse_osph, vcmpfalseph, vcmpge_oqph, vcmpgeph, vcmpgt_oqph, vcmpgtph, vcmple_oqph, vcmpleph, vcmplt_oqph, vcmpltph, vcmpneq_oqph, vcmpneq_osph, vcmpneq_usph, vcmpneqph, vcmpnge_uqph, vcmpngeph, vcmpngt_uqph, vcmpngtph, vcmpnle_uqph, vcmpnleph, vcmpnlt_uqph, vcmpnltph, vcmpord_sph, vcmpordph, vcmpph, vcmptrue_usph, vcmptrueph, vcmpunord_sph, vcmpunordph, vcvtdq2ph, vcvtpd2ph, vcvtph2dq, vcvtph2pd, vcvtph2psx, vcvtph2qq, …
- **AVX512_FP16_512** (92): vaddph, vcmpeq_osph, vcmpeq_uqph, vcmpeq_usph, vcmpeqph, vcmpfalse_osph, vcmpfalseph, vcmpge_oqph, vcmpgeph, vcmpgt_oqph, vcmpgtph, vcmple_oqph, vcmpleph, vcmplt_oqph, vcmpltph, vcmpneq_oqph, vcmpneq_osph, vcmpneq_usph, vcmpneqph, vcmpnge_uqph, vcmpngeph, vcmpngt_uqph, vcmpngtph, vcmpnle_uqph, vcmpnleph, vcmpnlt_uqph, vcmpnltph, vcmpord_sph, vcmpordph, vcmpph, vcmptrue_usph, vcmptrueph, vcmpunord_sph, vcmpunordph, vcvtdq2ph, vcvtpd2ph, vcvtph2dq, vcvtph2pd, vcvtph2psx, vcvtph2qq, …
- **AVX512_FP16_CONVERT_128** (1): vcvt2ps2phx
- **AVX512_FP16_CONVERT_256** (1): vcvt2ps2phx
- **AVX512_FP16_CONVERT_512** (1): vcvt2ps2phx
- **AVX512_FP16_SCALAR** (77): vaddsh, vcmpeq_ossh, vcmpeq_uqsh, vcmpeq_ussh, vcmpeqsh, vcmpfalse_ossh, vcmpfalsesh, vcmpge_oqsh, vcmpgesh, vcmpgt_oqsh, vcmpgtsh, vcmple_oqsh, vcmplesh, vcmplt_oqsh, vcmpltsh, vcmpneq_oqsh, vcmpneq_ossh, vcmpneq_ussh, vcmpneqsh, vcmpnge_uqsh, vcmpngesh, vcmpngt_uqsh, vcmpngtsh, vcmpnle_uqsh, vcmpnlesh, vcmpnlt_uqsh, vcmpnltsh, vcmpord_ssh, vcmpordsh, vcmpsh, vcmptrue_ussh, vcmptruesh, vcmpunord_ssh, vcmpunordsh, vcomish, vcvtsd2sh, vcvtsh2sd, vcvtsh2si, vcvtsh2ss, vcvtsh2usi, …
- **AVX512_FP8_CONVERT_128** (13): vcvt2ph2bf8, vcvt2ph2bf8s, vcvt2ph2hf8, vcvt2ph2hf8s, vcvtbiasph2bf8, vcvtbiasph2bf8s, vcvtbiasph2hf8, vcvtbiasph2hf8s, vcvthf82ph, vcvtph2bf8, vcvtph2bf8s, vcvtph2hf8, vcvtph2hf8s
- **AVX512_FP8_CONVERT_256** (13): vcvt2ph2bf8, vcvt2ph2bf8s, vcvt2ph2hf8, vcvt2ph2hf8s, vcvtbiasph2bf8, vcvtbiasph2bf8s, vcvtbiasph2hf8, vcvtbiasph2hf8s, vcvthf82ph, vcvtph2bf8, vcvtph2bf8s, vcvtph2hf8, vcvtph2hf8s
- **AVX512_FP8_CONVERT_512** (13): vcvt2ph2bf8, vcvt2ph2bf8s, vcvt2ph2hf8, vcvt2ph2hf8s, vcvtbiasph2bf8, vcvtbiasph2bf8s, vcvtbiasph2hf8, vcvtbiasph2hf8s, vcvthf82ph, vcvtph2bf8, vcvtph2bf8s, vcvtph2hf8, vcvtph2hf8s
- **AVX512_MEDIAX_128** (1): vmpsadbw
- **AVX512_MEDIAX_256** (1): vmpsadbw
- **AVX512_MEDIAX_512** (1): vmpsadbw
- **AVX512_MINMAX_128** (4): vminmaxbf16, vminmaxpd, vminmaxph, vminmaxps
- **AVX512_MINMAX_256** (4): vminmaxbf16, vminmaxpd, vminmaxph, vminmaxps
- **AVX512_MINMAX_512** (4): vminmaxbf16, vminmaxpd, vminmaxph, vminmaxps
- **AVX512_MINMAX_SCALAR** (3): vminmaxsd, vminmaxsh, vminmaxss
- **AVX512_SAT_CVT_128** (12): vcvtbf162ibs, vcvtbf162iubs, vcvtph2ibs, vcvtph2iubs, vcvtps2ibs, vcvtps2iubs, vcvttbf162ibs, vcvttbf162iubs, vcvttph2ibs, vcvttph2iubs, vcvttps2ibs, vcvttps2iubs
- **AVX512_SAT_CVT_256** (12): vcvtbf162ibs, vcvtbf162iubs, vcvtph2ibs, vcvtph2iubs, vcvtps2ibs, vcvtps2iubs, vcvttbf162ibs, vcvttbf162iubs, vcvttph2ibs, vcvttph2iubs, vcvttps2ibs, vcvttps2iubs
- **AVX512_SAT_CVT_512** (12): vcvtbf162ibs, vcvtbf162iubs, vcvtph2ibs, vcvtph2iubs, vcvtps2ibs, vcvtps2iubs, vcvttbf162ibs, vcvttbf162iubs, vcvttph2ibs, vcvttph2iubs, vcvttps2ibs, vcvttps2iubs
- **AVX512_SAT_CVT_DS_128** (8): vcvttpd2dqs, vcvttpd2qqs, vcvttpd2udqs, vcvttpd2uqqs, vcvttps2dqs, vcvttps2qqs, vcvttps2udqs, vcvttps2uqqs
- **AVX512_SAT_CVT_DS_256** (8): vcvttpd2dqs, vcvttpd2qqs, vcvttpd2udqs, vcvttpd2uqqs, vcvttps2dqs, vcvttps2qqs, vcvttps2udqs, vcvttps2uqqs
- **AVX512_SAT_CVT_DS_512** (8): vcvttpd2dqs, vcvttpd2qqs, vcvttpd2udqs, vcvttpd2uqqs, vcvttps2dqs, vcvttps2qqs, vcvttps2udqs, vcvttps2uqqs
- **AVX512_SAT_CVT_DS_SCALAR** (4): vcvttsd2sis, vcvttsd2usis, vcvttss2sis, vcvttss2usis
- **AVX512_VNNI_FP16_128** (1): vdpphps
- **AVX512_VNNI_FP16_256** (1): vdpphps
- **AVX512_VNNI_FP16_512** (1): vdpphps
- **AVX512_VNNI_INT16_128** (6): vpdpwsud, vpdpwsuds, vpdpwusd, vpdpwusds, vpdpwuud, vpdpwuuds
- **AVX512_VNNI_INT16_256** (6): vpdpwsud, vpdpwsuds, vpdpwusd, vpdpwusds, vpdpwuud, vpdpwuuds
- **AVX512_VNNI_INT16_512** (6): vpdpwsud, vpdpwsuds, vpdpwusd, vpdpwusds, vpdpwuud, vpdpwuuds
- **AVX512_VNNI_INT8_128** (6): vpdpbssd, vpdpbssds, vpdpbsud, vpdpbsuds, vpdpbuud, vpdpbuuds
- **AVX512_VNNI_INT8_256** (6): vpdpbssd, vpdpbssds, vpdpbsud, vpdpbsuds, vpdpbuud, vpdpbuuds
- **AVX512_VNNI_INT8_512** (6): vpdpbssd, vpdpbssds, vpdpbsud, vpdpbsuds, vpdpbuud, vpdpbuuds
- **AVX512_VP2INTERSECT_128** (2): vp2intersectd, vp2intersectq
- **AVX512_VP2INTERSECT_256** (2): vp2intersectd, vp2intersectq
- **AVX512_VP2INTERSECT_512** (2): vp2intersectd, vp2intersectq
- **AVX_IFMA** (2): vpmadd52huq, vpmadd52luq
- **AVX_NE_CONVERT** (7): vbcstnebf162ps, vbcstnesh2ps, vcvtneebf162ps, vcvtneeph2ps, vcvtneobf162ps, vcvtneoph2ps, vcvtneps2bf16
- **AVX_VNNI** (4): vpdpbusd, vpdpbusds, vpdpwssd, vpdpwssds
- **AVX_VNNI_INT16** (6): vpdpwsud, vpdpwsuds, vpdpwusd, vpdpwusds, vpdpwuud, vpdpwuuds
- **AVX_VNNI_INT8** (6): vpdpbssd, vpdpbssds, vpdpbsud, vpdpbsuds, vpdpbuud, vpdpbuuds
- **CMPCCXADD** (22): cmpaexadd, cmpaxadd, cmpbexadd, cmpbxadd, cmpexadd, cmpgexadd, cmpgxadd, cmplexadd, cmplxadd, cmpnbexadd, cmpnbxadd, cmpnexadd, cmpnlexadd, cmpnlxadd, cmpnoxadd, cmpnpxadd, cmpnsxadd, cmpnzxadd, cmpoxadd, cmppxadd, cmpsxadd, cmpzxadd
- **ENQCMD** (2): enqcmd, enqcmds
- **FAT_NOP** (7): nop3, nop4, nop5, nop6, nop7, nop8, nop9
- **FCMOV** (1): fcmovnp
- **FCOMI** (2): fcomip, fucomip
- **FRED** (2): erets, eretu
- **HRESET** (1): hreset
- **I186** (3): bound, popaw, pushaw
- **I286PROTECTED** (1): arpl
- **I386** (8): jcxz, jecxz, popal, popfd, popfl, pushal, pushfd, pushfl
- **I86** (12): aaa, aad, aam, aas, daa, das, into, lds, les, nop2, salc, udb
- **IBHF** (1): ibhf
- **ICACHE_PREFETCH** (2): prefetchit0, prefetchit1
- **KEYLOCKER** (7): aesdec128kl, aesdec256kl, aesenc128kl, aesenc256kl, encodekey128, encodekey256, loadiwkey
- **KEYLOCKER_WIDE** (4): aesdecwide128kl, aesdecwide256kl, aesencwide128kl, aesencwide256kl
- **LKGS** (1): lkgs
- **LWP** (4): llwpcb, lwpins, lwpval, slwpcb
- **MCOMMIT** (1): mcommit
- **MOVRS** (2): movrs, prefetchrst2
- **MSRLIST** (2): rdmsrlist, wrmsrlist
- **MSR_IMM** (2): rdmsr, wrmsrns
- **PBNDKB** (1): pbndkb
- **RAO_INT** (4): aadd, aand, aor, axor
- **RDPRU** (1): rdpru
- **SERIALIZE** (1): serialize
- **SHA512** (3): vsha512msg1, vsha512msg2, vsha512rnds2
- **SM3** (3): vsm3msg1, vsm3msg2, vsm3rnds2
- **SM4** (2): vsm4key4, vsm4rnds4
- **SM4_128** (2): vsm4key4, vsm4rnds4
- **SM4_256** (2): vsm4key4, vsm4rnds4
- **SM4_512** (2): vsm4key4, vsm4rnds4
- **SNP** (4): psmash, pvalidate, rmpadjust, rmpupdate
- **SSE** (64): cmpeq_osps, cmpeq_osss, cmpeq_uqps, cmpeq_uqss, cmpeq_usps, cmpeq_usss, cmpeqps, cmpeqss, cmpfalse_osps, cmpfalse_osss, cmpfalseps, cmpfalsess, cmpge_oqps, cmpge_oqss, cmpgeps, cmpgess, cmpgt_oqps, cmpgt_oqss, cmpgtps, cmpgtss, cmple_oqps, cmple_oqss, cmpleps, cmpless, cmplt_oqps, cmplt_oqss, cmpltps, cmpltss, cmpneq_oqps, cmpneq_oqss, cmpneq_osps, cmpneq_osss, cmpneq_usps, cmpneq_usss, cmpneqps, cmpneqss, cmpnge_uqps, cmpnge_uqss, cmpngeps, cmpngess, …
- **SSE2** (64): cmpeq_ospd, cmpeq_ossd, cmpeq_uqpd, cmpeq_uqsd, cmpeq_uspd, cmpeq_ussd, cmpeqpd, cmpeqsd, cmpfalse_ospd, cmpfalse_ossd, cmpfalsepd, cmpfalsesd, cmpge_oqpd, cmpge_oqsd, cmpgepd, cmpgesd, cmpgt_oqpd, cmpgt_oqsd, cmpgtpd, cmpgtsd, cmple_oqpd, cmple_oqsd, cmplepd, cmplesd, cmplt_oqpd, cmplt_oqsd, cmpltpd, cmpltsd, cmpneq_oqpd, cmpneq_oqsd, cmpneq_ospd, cmpneq_ossd, cmpneq_uspd, cmpneq_ussd, cmpneqpd, cmpneqsd, cmpnge_uqpd, cmpnge_uqsd, cmpngepd, cmpngesd, …
- **TBM** (9): blcfill, blci, blcic, blcmsk, blcs, blsfill, blsic, t1mskc, tzmsk
- **TDX** (4): seamcall, seamops, seamret, tdcall
- **TSX_LDTRK** (2): xresldtrk, xsusldtrk
- **UINTR** (5): clui, senduipi, stui, testui, uiret
- **USER_MSR** (2): urdmsr, uwrmsr
- **WRMSRNS** (1): wrmsrns
- **XOP** (117): vfrczpd, vfrczps, vfrczsd, vfrczss, vpcmov, vpcomb, vpcomd, vpcomeqb, vpcomeqd, vpcomeqq, vpcomequb, vpcomequd, vpcomequq, vpcomequw, vpcomeqw, vpcomfalseb, vpcomfalsed, vpcomfalseq, vpcomfalseub, vpcomfalseud, vpcomfalseuq, vpcomfalseuw, vpcomfalsew, vpcomgeb, vpcomged, vpcomgeq, vpcomgeub, vpcomgeud, vpcomgeuq, vpcomgeuw, vpcomgew, vpcomgtb, vpcomgtd, vpcomgtq, vpcomgtub, vpcomgtud, vpcomgtuq, vpcomgtuw, vpcomgtw, vpcomleb, …
