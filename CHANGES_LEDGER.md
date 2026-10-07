# Change ledger

One entry for every change we make to code we didn't write. Each entry gives where the change came from, why, which commit holds it, and what proves it works. Our own new code (the `Emulator` project) isn't listed here.

Status values: **in** = committed and verified · **planned** = in the plan, not done yet.

## VTIL-Core (our fork, branch `novmp-emu`)

| # | Change | Origin | Why | Commit | Proof | Status |
|---|---|---|---|---|---|---|
| V1 | All `CMakeLists.txt` removed | ours | No CMake in our code; the build is `NoVmp.sln` + `.vcxproj` | `ec9e26a` | `build.cmd --release` 0 errors | in |
| V2 | `Dependencies/capstone`, `Dependencies/keystone` became real git submodules (capstone `next`, keystone `master`) instead of CMake FetchContent wrappers | ours | Pinned, updatable, pristine upstream sources | `ec9e26a` | `git submodule status` shows 992e6d7f / 0d9567f | in |
| V3 | VTIL's pre-generated LLVM config headers moved to `Dependencies/keystone_config/include/llvm/...` | ours | The keystone clone would shadow them | `ec9e26a` | content identical apart from line endings (`git diff --ignore-cr-at-eol` empty) | in |
| V4 | `arm64_disassembler.cpp`: `writeback` read from `cs_detail` when `CS_API_MAJOR >= 6` | ours | Capstone 6 moved `writeback` out of `cs_arm64` | `ec9e26a` | VTIL-Common compiles against Capstone 6 | in |
| V5 | `arm64_register_details.hpp`: `VTIL_ARM64_VBASE` / `VTIL_ARM64_VINST` macros (V0-V31 → Q0-Q31 on Capstone 6) | ours | Capstone 6 removed the V register ids; aliasing V to Q would trip `register_map`'s duplicate-key check | `ec9e26a` | VTIL-Common compiles; no duplicate-key assert | in |
| V6 | `CAPSTONE_AARCH64_COMPAT_HEADER` defined for the VTIL projects (in `msvc\VTIL-*.vcxproj`) | ours | VTIL's arm64 code uses Capstone 5 ARM64 names | `d550ec2` | VTIL builds | in |

## linux-pe (our fork, branch `novmp-emu`)

| # | Change | Origin | Why | Commit | Proof | Status |
|---|---|---|---|---|---|---|
| L1 | `CMakeLists.txt` removed | ours | No CMake in our code | `3e8e37a` | build 0 errors | in |

## NoVmp (our fork, root branch `novmp-emu`)

| # | Change | Origin | Why | Commit | Proof | Status |
|---|---|---|---|---|---|---|
| N1 | `CMakeLists.txt` (root, `NoVmp\`) and the old `NoVmp\NoVmp.sln` removed; one `NoVmp.sln`; hand-maintained `.vcxproj` files; `build.cmd` | ours | One VS2022 solution, static, no CMake | `d550ec2`, `9b401e6` | `build.cmd` (no args / `--release`) exit 0; `--bogus` exit 2 | in |
| N2 | `demo_compiler.hpp`: `.get()` on `explored_blocks` entries; `backup = rtn->clone()` | ours | VTIL master holds `unique_ptr<basic_block>`, and `clone()` returns `unique_ptr<routine>` | `d550ec2` | NoVmp compiles and links | in |
| N3 | `vtil_lifter.cpp`: `explored_blocks[ entry_vip ].get()` | ours | same API change | `d550ec2` | NoVmp compiles and links | in |
| N4 | Native executor (`emulator\emulator.cpp`, `rwx_allocator.cpp`) replaced by the Emulator lib's `snippet_executor` | ours | Never execute target-derived code natively | — | plan 7.4 equivalence test | planned |
| N6 | `emulator::invoke` refuses with `logger::error` (`[[noreturn]]`; NoVmp's hook throws) before the native shellcode call | ours | Safety guard until N4: target-derived code must never run on the host CPU | see `[0.4]` commit | build 0 errors; `logger::error` is `[[noreturn]]` and the hook throws, so the shellcode line is unreachable | in |
| N7 | `msvc\unicorn.vcxproj`: + `qemu/accel/tcg/tb-maint.c` (per-target), `qemu/crypto/sm4.c`, `qemu/util/int128.c` (common); `LanguageStandard_C` = `stdc11` | ours (file list and C standard taken from the branch's upstream build as reference) | the QEMU 7.2.22 branch adds these sources, and its softfloat uses C11 `_Generic` (101 C2275 errors without it) | `4c741d4` | upstream-vs-project list diff: 0 missing, 0 extra; `build.cmd --release` 0 errors | in |
| N5 | Hard-coded image base `0x100000000` (`subroutines.cpp:471`) replaced by the real image base | ours | correctness | — | plan 7.3 | planned |

## unicorn (third party; decisions D0/D9; D0 amended 2026-10-07: genuine Unicorn bugs found by our tests may be fixed directly, each with a reproducing test)

| # | Change | Origin | Why | Commit | Proof | Status |
|---|---|---|---|---|---|---|
| U1 | Local branch `novmp/qemu72` from `origin/pr2349-rebased` (`3706716d`, QEMU 7.2.22 x86 translator) | upstream Unicorn branch | AVX/AVX2/FMA/F16C/VAES execution, K0-K7 access | base `3706716d`, root `4c741d4` | builds (0 errors); `test.cmd`: test_x86 97/97, test_ctl 12/12, test_mem 20/20 (+1 ARM64-only test skipped by name) | in |
| U2 | Cherry-pick `09bd8e4f` -> `7968e7ae` (binding constants; 5 conflicts resolved by keeping both sides: +`UC_CTL_UC_PREALLOC` per file, the branch's own constants kept) and `938efd13` -> `5bab3af6` (UC_HOOK_BLOCK_ICOUNT; clean merge, reviewed against the 7.2 translator) | upstream Unicorn `dev` | missing from the 7.2 branch | `7968e7ae`, `5bab3af6` | builds; `test_add_block_icount_hook` passes (test_ctl 12/12) | in |
| U4 | `qemu/accel/tcg/cpu-exec.c`: reset `env->old_exception = -1` when a `UC_HOOK_INTR` hook handles an x86 exception | **ours** (Unicorn bug fix, allowed by the amended D0) | Unicorn skips `x86_cpu_do_interrupt()` (the only place that clears `old_exception` after delivery), so the 2nd independent contributory fault became #DF | `1514d5be` | `emu-uc72-risk` R3: #DE,#DE / #GP,#GP / mixed now `0,0` / `13,13` / `0,13` / `13,0` (before: `x,8`); Unicorn unit tests unchanged (97/97, 12/12, 20/20) | in |
| U5 | MOVBE/CRC32 0F38 F0/F1 use operand size `v` | upstream QEMU `76ad26dd17` | real mode got 32/16 instead of 16/32; 66+REX.W got 16 instead of 64 | `74a06c61` | emu-backports: 13 cases (host + real-mode SDM), all pass; before: 6 failed | in |
| U6 | ES/CS/SS/DS are null prefixes in 64-bit mode | upstream QEMU `3589cd995b` | `65 26 mov` used the ES base instead of GS | `685825c3` | emu-backports: gs+es/cs/ss/ds and es+gs read the GS-relative qword on host and Unicorn; before: unmapped read of 0x30 | in |
| U7 | F6/F7 /1 decoded as TEST (+ rip_offset for /1) | upstream QEMU `380b959618` | /1 raised #UD | `f502a6e5` | emu-backports: 6 forms incl. RIP-relative match hardware; before: #UD on all | in |
| — | Not applicable, proven by passing hardware comparisons on the unpatched branch: `83a3a20e59` BLSI CF (already in 7.2.22), `fe12f7c8a8` PUSHF / `c25f69595a` SAHF (fix regressions our tree never had), `e38d0afade` RCL/RCR count (old helpers already mod 9/17), `3afc6539a8` POP to memory (already 64-bit), `336dbe8e99` misplaced REX (already ignored), `7bdcdf1141` VEX high-byte regs (no functional effect) | upstream QEMU | — | — | emu-backports: 13 regression cases pass vs host before and after | n/a |
| U8 | ICEBP/INT1 (F1) as a trap-like #DB (`helper_icebp`) | upstream QEMU `73fb7b3c49` | F1 was compiled out (`WANT_ICEBP`) -> #UD; anti-debug code uses it | `70d35a58` | emu-backports: host and Unicorn both vector 1 with RIP after F1; before: #UD | in |
| U9 | RDSEED on its own CPUID bit; TCG advertises it | upstream QEMU `691925e5a3` + `f9e0dbae78` | RDSEED ran under the RDRAND gate and was never advertised | `14470bb5` | emu-backports: CF/flags/width vs host; Haswell -> #UD | in |
| U10 | RDPID (TSC_AUX), full-register write per SDM | upstream QEMU `6750485bf4` + `f2c04bede3` (RDTSCP rewrite not ported: Unicorn RDTSCP hooks) | absent (#UD) | `50586b3d` | emu-backports: full-register write on host+Unicorn, TSC_AUX value, memory form #UD | in |
| U11 | SHA-NI (7 instructions) + SDM Type-4 prefix/alignment checks | upstream QEMU `e582b629f0` | absent (#UD) | `ed4c1b81` | emu-backports: 12 cases bit-identical to the host CPU, misaligned #GP, 66 prefix #UD | in |
| U12 | CMPccXADD (64-bit, VEX.128.66.0F38 E0-EF) | upstream QEMU `405c7c0708` + `a9ce107fd0` | absent | `ef905013` + fixes `42356e64` | see U14 | in |
| U13 | `UC_CPU_X86_MAX` model (Icelake-Server identity + `max_features`) | **ours** (infrastructure: no model had all TCG features; CMPccXADD on none) | backported instructions were unreachable | `1067cd20` | emu-backports: CPUID leaf 7/7.1 enumerate AVX2 BMI1 BMI2 ADX RDSEED SHA RDPID CMPCCXADD | in |
| U14 | CMPccXADD port fixes: init `cpuid_7_1_eax_features`; no `xchg` special (7.2 M operands have no unit); no `MO_SIGN` on 64-bit loads (5.0-era backend aborts on MO_SQ) | **ours** (bugs in the U12 port, found by tests) | #UD on all forms, then 0xC0000409 on 64-bit signed conditions | `42356e64` | emu-backports: 96/96 SDM vectors + 3 #UD cases; full set x3 = 486/486 | in |
| — | Unreachable in Unicorn (it never delivers exceptions through the guest IDT; everything goes to UC_HOOK_INTR), so not ported: `60efba3c1b` INT n #GP error code (system-path twin is `b585edca34`), `69cb498c56` pushed EFLAGS.RF, `6dd7d8c649` HLT TF/RF. Their *requirements* move to our emulator layer (plan Phase 6: RF=1 in CONTEXT for faults, Windows IDT DPL policy for INT n, error codes). `df9a3372dd` CR2 non-canonical: already correct in our tree (never had the bug). | upstream QEMU | — | — | analysis `scratchpad\bp\exceptions.md` | n/a |
| U15 | VSIB index register 4 is a real index (`is_vsib`) | upstream QEMU `ac63755b20` | xmm4/ymm4 index became \"no index\"; dest/mask==index #UD missing | `c55a43e7` | emu-backports: 7 gather cases equal hardware | in |
| U16 | VSIB addresses masked to the address size | upstream QEMU `5e3572ef2e` | addr32 gathers read past 4 GiB | `c5ad5aa7` | emu-backports: addr32 vpgatherdd/qq wrap like hardware; before: unmapped 0x101000000 | in |
| U17 | VEX.L=1 VMOVQ/VMOVD/VMOVLPD -> #UD | upstream QEMU `2eb8d97343` | executed | `c5a8711f` | emu-backports: 6 #UD + 4 controls equal hardware | in |
| U18 | dpps/dppd name typo (cosmetic; context for later patches) | upstream QEMU `d66532600f` | — | `c4544b43` | dpps/vdpps/dppd/vdppd regression cases unchanged | in |
| U19 | legacy SSE m128 alignment #GP (X86_VEX_None; MOVUPS/MOVUPD stores marked unaligned) | upstream QEMU `73dd6e4a36` | misaligned legacy class 2/4 never faulted | `632fef8c` | emu-backports: 41 instructions x {aligned, +8} equal hardware | in |
| U20 | EXTRQ_i register from ModRM.rm, invalid forms #UD | upstream QEMU `ce0ee66044` | wrong register, missing #UD | `ec3720ba` | AMD APM vectors on EPYC (host Intel lacks SSE4A) | in |
| U21 | VEX.W validation (W0/W1-only instructions) | upstream QEMU `e000687f12` (**not** in 7.2.22; `489a0714bf` is another fix) | wrong-W forms and legacy 66 0F 38 0C..0F executed | `9a8d3cd8` | emu-backports: 15 cases equal hardware | in |
| U22 | (V)MOVLPD / (V)MOVHPD register forms -> #UD | **ours** (QEMU 7.2 and upstream both execute them) | SDM defines them m64-only; hardware #UDs | `de38dab5` | emu-backports: 4 forms #UD + movhlps/movlhps controls equal hardware | in |
| — | Already in 7.2.22, verified: `2b55e479e6` VCOMI/VUCOMI operand size (stable `eee0666a50`) | upstream QEMU | — | — | emu-backports: vcomiss/comisd/ucomiss at a page end do not fault, flags equal hardware | n/a |
| U23 | exception flags held in `int` (were `uint8_t`) | upstream QEMU `397ef415ca` (prerequisite) | 0x4000 input_denormal_used truncated; RCPSS etc. erased pending DE | `6b2bc433` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U24 | `float_class_denormal` | upstream QEMU `7ad6a56757` (infrastructure) | — | `393e32cb` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U25 | `float_flag_input_denormal_used` | upstream QEMU `029a2083a2` | no flag for a consumed denormal | `2a8e8676` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U26 | softfloat: flush output denormals after rounding | upstream QEMU `28f13bccbe` (other-target hunks dropped) | flush happened before rounding | `a2c37c16` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U27 | x86 FTZ after rounding | upstream QEMU `bc40e4fe62` | results rounding up to MIN_NORMAL were flushed (UE) | `f91a6426` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U28 | MXCSR.DE / FPUS.DE wired to input_denormal_used + FXTRACT keeps DE | upstream QEMU `57df511180` + **ours** (upstream HEAD regressed FXTRACT DE) | DE almost never set | `48a17edd` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U29 | FST m32/m64 never #D; FRNDINT #D on a denormal | **ours** (upstream wrong on both, per SDM) | spurious / missing DE | `f9d8106b` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U30 | pseudo-NaN in FPATAN/FYL2X/FYL2XP1 -> QNaN indefinite | upstream QEMU `cf10af6c70` | pseudo-NaN propagated | `e5c40934` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U31 | FCOMI/FUCOMI clear OF/SF/AF | upstream QEMU `0924d9d3db` | flags kept | `02c942e6` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U32 | FXCH clears C1 | upstream QEMU `c7cc09c85b` | C1 kept | `81971168` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U33 | all x87 compares clear C1 | upstream QEMU `6802a4b239` | C1 kept. **Manual vs hardware conflict:** the SDM says FCOMI/FUCOMI set C1=0, the i5-13600K leaves C1 unchanged; per the project rule the manual wins (case SDM-checked) - flagged to the user | `0a923c02` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U34 | FXTRACT old-ST0 tag | upstream QEMU `994fb6c218` | tag not updated | `7d54f19f` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U35 | FST/FSTP ST(i) destination tag | upstream QEMU `f2357fdcd9` | tag not updated | `85846547` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| U36 | FXCH swaps tags | upstream QEMU `a9dbd71f03` | tags not swapped | `79e04241` | emu-backports x87/MXCSR group: 46 cases vs host (29 failed before the series), all pass | in |
| — | Not applicable: `2b3bfbb21b` 0*Inf+QNaN Invalid (7.2's pickNaNMulAdd never raised it; upstream broke it later in `8adcff4ae7`) | upstream QEMU | — | — | emu-backports: QNaN no IE / SNaN IE equal hardware before and after | n/a |
| — | Known gaps left for Phase 4/5 (not backports): x87 stack overflow/underflow (IE+SF) is not modelled by QEMU at all; RCPSS/RSQRTSS return exact values where hardware returns its ~12-bit approximation; PAUSE+TF trap position (`3718523d01`) to verify with the Phase 4 TF tests | — | — | — | — | open |
| U37 | XSAVE components recomputed after feature filtering (all models); `full_cpuid_auto_level` for UC_CPU_X86_MAX | **ours** (bugs found by the 1.8 re-audit; the first one affects stock Icelake/Skylake-Server models too) | CPUID 0xD and XSETBV advertised/accepted AVX-512 state TCG cannot save; MAX's leaf 7.0 EAX was 0 | `cd40a63c` | emu-uc72-risk R4: before 5 failures, after 0 (leaf 7.0 EAX=1, 0xD.0 EAX MAX 0x21F / Icelake 0x207, xsetbv 0xE7 -> #GP) | in |
| U38 | `UC_CTL_X86_HW_QUIRKS` (bitmask, default 0 = manual) + `UC_X86_QUIRK_FCOMI_KEEPS_C1` | **ours** (your decision at Gate 1: manual-vs-hardware differences are selectable) | SDM: FCOMI/FUCOMI clear C1; i5-13600K leaves it | `be5da03a` | emu-uc72-risk R5: default C1=0, quirk C1=1, uc_ctl round trip | in |
| U3 | (placeholder for the 1.6 backports - all now listed individually as U5-U36) | upstream QEMU `3377670ee9` | — | — | — | done |

capstone and keystone: **no changes**, pristine upstream.
