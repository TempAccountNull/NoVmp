# Dependencies

Every repository this tree builds from or uses as reference, with the exact commit it's pinned to.
Last verified **2026-10-07**: `git fetch` on every repo shows **0 commits behind** upstream.

Build rules:
- `NoVmp.sln` and the `.vcxproj` files are the build definition. They're ours and edited by hand; nothing regenerates them.
- Our code contains no CMake.
- Third-party dependencies are kept pristine upstream.
- Our forks read upstream changes but are never overwritten by them.

## Built into NoVmp.sln

| Path | Role | Upstream | Branch we're on | Pinned commit | Date | Upstream base |
|---|---|---|---|---|---|---|
| `.` (root) | **our fork** of NoVmp | https://github.com/can1357/NoVmp | `novmp-emu` | `9b401e6` | 2026-10-07 | `master` 6c23c9a (2021-05-05) |
| `VTIL-Core` | **our fork** of VTIL | https://github.com/vtil-project/VTIL-Core | `novmp-emu` | `ec9e26a` | 2026-10-07 | `master` 9ebee8e (2026-07-27) |
| `linux-pe` | **our fork** (header-only) | https://github.com/can1357/linux-pe | `novmp-emu` | `3e8e37a` | 2026-10-07 | `master` 1fcb057 (2025-04-24) |
| `unicorn` | third party, pristine | https://github.com/unicorn-engine/unicorn | `dev` | `938efd13` (2.1.4-33, **QEMU 5.0.1**) | 2026-08-28 | same |
| `VTIL-Core\Dependencies\capstone` | third party, pristine | https://github.com/capstone-engine/capstone | `next` | `992e6d7f` (6.0.0-Alpha11-14) | 2026-10-04 | same |
| `VTIL-Core\Dependencies\keystone` | third party, pristine | https://github.com/keystone-engine/keystone | `master` | `0d9567f` (0.9.2-39) | 2026-07-18 | same |

Planned change (plan Phase 1, decisions D0/D9): `unicorn` moves to a local branch `novmp/qemu72` built on `origin/pr2349-rebased` (`3706716d`, QEMU 7.2.22 x86 translator, 2026-08-28). On top of it go the dev commits `938efd13` + `09bd8e4f` and the recorded QEMU 11.1 backports (see `CHANGES_LEDGER.md`). This is the only planned change to a third-party dependency.

## Project files (ours)

| File | Builds |
|---|---|
| `NoVmp.sln` | the only solution: NoVmp + Dependencies (capstone, keystone, unicorn) + VTIL (Common, SymEx, Architecture, Compiler) |
| `NoVmp\NoVmp.vcxproj` | NoVmp.exe |
| `msvc\capstone.vcxproj` | capstone static lib (all architectures, as VTIL needs) |
| `msvc\keystone.vcxproj` | keystone static lib (all LLVM targets present) |
| `msvc\unicorn.vcxproj` | unicorn static lib, **x86_64 target only** |
| `msvc\VTIL-*.vcxproj` | the four VTIL static libs |
| `msvc\generated\keystone\include\llvm\Config\{AsmParsers,Targets}.def` | static files LLVM would otherwise configure |
| `VTIL-Core\Dependencies\keystone_config\include\llvm\...` | VTIL's pre-generated LLVM config headers |

Not used by `NoVmp.sln` and left as upstream ships them: VTIL-Core's own `VTIL-*.vcxproj` / `VTIL-Core.sln`, and the binding/sample `.sln` files inside capstone, keystone and unicorn.

## Reference only (never built or linked)

| Path | What | Commit | Used for |
|---|---|---|---|
| `garbage\emulator\Intel docs\`, `intel_docs_text\`, `intel_sdm_instructions.md` | Intel SDM, ISE, APX, AVX10, … | 092 / Dec-24 editions | **first** in the lookup order: the authority on semantics |
| `garbage\qemu` | QEMU, https://gitlab.com/qemu-project/qemu.git `master` | `3377670ee9` (v11.1.0-2123, 2026-10-05) | second in the lookup order; source of the Phase 1.6 backports |
| `garbage\asmjit` | asmjit, https://github.com/asmjit/asmjit `master` | `0d7f010` (2026-09-23), Zlib license | third in the lookup order: instruction database + encoder logic, rewritten in our own C++ with attribution |
| `garbage\emulator\Intel docs\xed-main` | XED datafiles | — | fourth in the lookup order, final cross-check |
