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
| U3… | QEMU 11.1 backports (plan 1.6, one entry per QEMU commit) | upstream QEMU `3377670ee9` | ISA (SHA-NI, CMPccXADD, RDSEED/RDPID) and correctness fixes | — | one difftest case each | planned |

capstone and keystone: **no changes**, pristine upstream.
