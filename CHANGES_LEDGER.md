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
| N5 | Hard-coded image base `0x100000000` (`subroutines.cpp:471`) replaced by the real image base | ours | correctness | — | plan 7.3 | planned |

## unicorn (third party; only planned change, decisions D0/D9)

| # | Change | Origin | Why | Commit | Proof | Status |
|---|---|---|---|---|---|---|
| U1 | Local branch `novmp/qemu72` from `origin/pr2349-rebased` (`3706716d`, QEMU 7.2.22 x86 translator) | upstream Unicorn branch | AVX/AVX2/FMA/F16C/VAES execution, K0-K7 access | — | Unicorn x86 unit tests (95) pass | planned |
| U2 | Cherry-pick `938efd13` (UC_HOOK_BLOCK_ICOUNT) and `09bd8e4f` (binding constants) | upstream Unicorn `dev` | missing from the 7.2 branch | — | unit tests + `emu-oep` uses BLOCK_ICOUNT | planned |
| U3… | QEMU 11.1 backports (plan 1.6, one entry per QEMU commit) | upstream QEMU `3377670ee9` | ISA (SHA-NI, CMPccXADD, RDSEED/RDPID) and correctness fixes | — | one difftest case each | planned |

capstone and keystone: **no changes**, pristine upstream.
