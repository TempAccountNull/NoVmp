# Intel SGX and SMX (GETSEC / TXT) model (plan 1.J.11, ledgers U1260-U1299)

User decision A10 (2026-10-09): implement SGX and GETSEC (TXT). Gate 1 follow-up: "SGX = full
enclave model (EPC, ENCLS/ENCLU, measurement + key derivation; launch control / attestation keys
documented as platform-specific)". Open note: "GETSEC leaves beyond CAPABILITIES need a TXT chipset
model". Order: SGX first, then GETSEC/TXT. VMX (the ENCLS-exiting bitmap, SGX in VMX
non-root operation, EPT / APIC-virtualization interactions) belongs to plan 1.J.12 (the VMX agent):
done in U1313-U1316 (docs/vmx.md); ENCLV is not modelled (removed from the SDM in 325462-088).

Validation label of everything in this document: **specification-validated** (independent reading of
the SDM and an independent Python reference model). The i5-13600K has neither SGX nor SMX/TXT, and
Intel SDE cannot run CPL0 enclave-management code, so nothing here is SDE- or hardware-validated.

Sources (local copies, `E:\pdfs\x86docs\Intel\SDMs` (formerly `emulator\Intel docs`), text in `emulator\intel_docs_text`):
- **3D** = SDM 092 Vol3D, 332831-092 (June 2026), chapters 37-43 (the plan's "34-41" is the numbering
  of older revisions). SDM 093 (332831-093, September 2026) was extracted with PyMuPDF and compared:
  chapters 37-43 are identical except Table 42-2 (IA32_SGX_SVN_STATUS gains SEAMLDR_SVN 63:56) and a
  moved footnote (42.13 item 3).
- **3A** = SDM 092 Vol3A 253668-092; **1** = Vol1 253665-092; **2D** = Vol2D 334569-092 (chapter 7,
  SMX); **4** = Vol4 335592-092 (093 compared for the SGX MSRs: identical).

Page numbers are the printed page labels.

## 1. Requirement-to-code mapping

Status: **impl** = implemented in this branch, **plan** = planned (not yet implemented),
**ph** = placeholder for a platform-specific value (our choice, documented, never an invented
"real" value), **def** = deferred to another plan item, **blk** = blocked on a missing document,
**no** = not modelled (reason given). Code paths are relative to `unicorn/`.

### 1.1 Enumeration, enabling, MSRs

| Requirement | Source | Code | Status |
|---|---|---|---|
| Opt-in: `UC_CTL_X86_FEATURES` bit `UC_X86_FEATURE_SGX` = 16 (default off; the existing models are unchanged) | our API (U1120 pattern) | `include/unicorn/unicorn.h`, `uc.c`, `qemu/target/i386/cpu.c` | impl (U1261) |
| CPUID.(07H,0):EBX.SGX[2], ECX.SGX_LC[30] | 1 Table 21-19 p.21-26, Table 21-20 p.21-28; 3D 37.7 p.37-4 | `cpu.c` (features after the TCG filter) | impl (U1261) |
| CPUID.12H valid with 07H.0:EBX.SGX and MAX_LEAF >= 12H; sub-leaf 0: SGX1[0], SGX2[1], EVERIFYREPORT2[7], EUPDATESVN[10], EDECCSSA[11], MISCSELECT (EBX), MAX_ENCLAVE_SIZE_NOT_64 / _64 (EDX) | 1 Table 21-47 p.21-52 | `cpu.c` leaf 12H | impl (U1261) |
| CPUID.12H.1: ECREATE-settable SECS.ATTRIBUTES[127:0] (EAX..EDX) | 1 Table 21-48 p.21-53; 3D 38.7.1 Table 38-3 p.38-5 | `cpu.c` | impl (U1261) |
| CPUID.12H.n>=2: EPC sections (type 1: base 51:12, size 51:12, property), type 0 = invalid (all zero) | 1 Table 21-49 p.21-53..21-54; 3D 37.7.2 p.37-5 | `cpu.c` | impl (U1261) |
| Leaf 12H content depends on enablement ("Valid*") | 3D Table 37-3 p.37-4..37-5 | `cpu.c` | impl (U1261): same values in every state (**question**) |
| IA32_FEATURE_CONTROL (3AH): LOCK[0], LE_WR (SGX launch control enable)[17], SGX_ENABLE[18]; R/WL; exists if any enumeration condition holds | 4 Table 2-2 3AH p.2-5..2-6; 3D 37.7.1 p.37-4, 42.2 p.42-1; 2D Table 7-1 p.7-1..7-2 | `misc_helper.c` msr_present / msr_write_ok (U906, already conditional on the SGX bits) | impl (U906) |
| ENCLS / ENCLU: #UD without SGX1, #GP(0) unless LOCK = SGX_ENABLE = 1 | 3D Table 37-3, ENCLS p.41-8..41-9, ENCLU p.41-10..41-12 | `sgx_model.c` | impl (U1262: ENCLS; ENCLU U1263) |
| IA32_SGXLEPUBKEYHASH0-3 (8CH-8FH): read if SGX1 and SGX_LC; write if SGX1, FEATURE_CONTROL[17] = 1 and [0] = 1; reset value "the digest of Intel's signing key" | 4 Table 2-2 8CH-8FH p.2-11; 3D 39.1.4 p.39-3, 42.2.2 p.42-1 | `misc_helper.c`, `sgx_model.c` | impl (U1261); reset value **ph** (host-provided, default 0) |
| IA32_SGXLEPUBKEYHASH4/5 (90H/91H, "bits 383:256 of the SHA256 digest") | 4 Table 2-2 90H-91H p.2-11..2-12 (also 093) | - | **question** (a 256-bit digest has no bits 383:256; Vol3D uses a 256-bit hash). Not modelled: #GP(0) |
| IA32_SGX_SVN_STATUS (500H, R/O): Lock[0] = a non-faulting SGX instruction was executed; SGX_SVN_SINIT 23:16 (only with SMX) | 4 Table 2-2 500H p.2-49; 3D 42.11.3 Table 42-2 p.42-11 | `misc_helper.c`, `sgx_model.c` | impl (U1261, Lock set by the leaves later); SVN threshold **ph** (host-provided, default 0) |
| INVD: #GP(0) once processor reserved memory protections are activated | 3D 39.6.5 p.39-15; 2A INVD (U1093 row in docs/quirks.md) | `misc_helper.c` helper_invd | impl (U1261, activation rule: our choice) |
| EPC: processor reserved memory, base/size configured by the platform ("BIOS"); the host configures it (`UC_CTL_X86_SGX_PLATFORM`) and the model maps the RAM at engine init | 3D 37.5 p.37-2, 39.5 p.39-10 | `uc.c`, `sgx_model.c` | impl (U1261) |
| EPCM: one entry per EPC page (VALID, R, W, X, PT, ENCLAVESECS, ENCLAVEADDRESS, BLOCKED, PENDING, MODIFIED, PR) | 3D 37.5.1 p.37-3, 38.20 Table 38-28 p.38-18 | `sgx_model.c` (outside the reset area: survives CPU reset, 42.9 p.42-10) | impl (U1261: state; access control later) |

### 1.2 Data structures

| Structure | Source | Status |
|---|---|---|
| SECS (Table 38-2), ATTRIBUTES (Table 38-3), MISCSELECT (Table 38-4) | 3D 38.7 p.38-4..38-6 | impl (U1262) |
| TCS (Table 38-6), TCS.FLAGS (Table 38-7) | 3D 38.8 p.38-7 | impl (U1262 / U1263) |
| SSA frame: XSAVE / pad / MISC / GPRSGX (Tables 38-8, 38-9), EXITINFO (Table 38-10), vectors (Table 38-11), MISC EXINFO (Tables 38-12, 38-13) | 3D 38.9 p.38-8..38-11 | impl (U1263) |
| PAGEINFO (Table 38-15), SECINFO (Tables 38-16..38-18) | 3D 38.11-38.12 p.38-12..38-13 | impl (U1262) |
| PCMD (Table 38-19) | 3D 38.13 p.38-13 | impl (U1265) |
| SIGSTRUCT (Table 38-20), RSA-3072 e = 3, EMSA-PKCS1-v1.5 SHA-256, q1 / q2 | 3D 38.14 p.38-13..38-14 | impl (U1262) |
| EINITTOKEN (Table 38-21) | 3D 38.15 p.38-15 | impl (U1262) |
| REPORT (Table 38-22), REPORTDATA, TARGETINFO (Table 38-23) | 3D 38.16-38.17 p.38-15..38-16 | impl (U1264) |
| KEYREQUEST (Table 38-24), KEYNAME (Table 38-25), KEYPOLICY (Table 38-26) | 3D 38.18 p.38-17..38-18 | impl (U1264) |
| Version Array (Table 38-27) | 3D 38.19 p.38-18 | impl (U1262: EPA; U1265: EWB / ELDB / ELDU slots) |
| CET state save frame (Table 38-14), CET_ATTRIBUTES (Table 38-5), PT_SS_FIRST / PT_SS_REST | 3D 37.8 p.37-5..37-6, 38.10 p.38-11 | no: CPUID.12H.1:EAX[6] (ATTRIBUTES.CET) is not enumerated, so these fields are reserved |
| KSS: ATTRIBUTES.KSS, CONFIGID / CONFIGSVN / ISVFAMILYID / ISVEXTPRODID, KEYPOLICY bits 5:2 | 3D 39.4.1.3 p.39-6, 38.18.2 | no: ATTRIBUTES.KSS[7] is not enumerated (the reserved-field checks of ECREATE / EINIT / EGETKEY apply) |
| AEX-Notify: ATTRIBUTES.AEXNOTIFY[10], TCS.FLAGS.AEXNOTIFY, SSA.GPRSGX.AEXNOTIFY, EDECCSSA | 3D 39.2.3.2 p.39-5, EDECCSSA p.41-83 | no: not enumerated (12H.0:EAX[11] = 0, 12H.1 bit 10 = 0) |

### 1.3 Access control and enclave mode

| Requirement | Source | Code | Status |
|---|---|---|---|
| Direct enclave access = enclave mode and linear address in ELRANGE: must map to an EPC page whose EPCM entry is valid, of the current enclave, at that linear address, PT_REG, not BLOCKED / PENDING / MODIFIED, R / W / X as EPCM allows; else #PF with PFEC.SGX[15] (P = 1, RSVD = PK = 0) | 3D 38.3 p.38-1..38-2, 38.5 p.38-2..38-3; 3A 5.7 Figure 5-12 p.5-37..5-38, 7.15 p.7-47..7-49 | `excp_helper.c` handle_mmu_fault -> `sgx_model.c` | impl (U1263) |
| Code fetch from enclave mode outside ELRANGE: #GP(0); a fetch split across ELRANGE: #GP | 3D 38.3 p.38-1, 38.5.2 p.38-2..38-3 | same | impl (U1263; a fetch split across ELRANGE faults on the page outside) |
| Non-enclave access to EPC (outside enclave mode, or from enclave mode outside ELRANGE): "altered" - the SDM gives reads of all ones and dropped writes as an example ("abort page") | 3D 38.1 p.38-1, 38.3, 38.5.1 p.38-2 | `accel/tcg/cputlb.c` (abort-page TLB entry), `excp_helper.c` | impl (U1262: the abort page) |
| CR2 / #PF event data bits 11:0 cleared for a #PF during enclave-mode execution | 3A 5.7 p.5-37, 7.15 p.7-49; 3D Table 40-1 p.40-3 | AEX | impl (U1263: AEX; FRED event data = CR2) |
| Enclave entry / exit flush linear-address translations | 3D 39.2.1 p.39-4, 42.12 p.42-11 | `sgx_model.c` (tlb_flush) | impl (U1263) |
| Instructions illegal inside an enclave (#UD): CPUID, GETSEC, RDPMC, SGDT, SIDT, SLDT, STR, VMCALL, VMFUNC, IN/INS/OUT/OUTS, far CALL/JMP/RET, INT n, INTO, IRET, LDS/LES/LFS/LGS/LSS, MOV to DS/ES/SS/FS/GS, POP DS/ES/SS/FS/GS, SYSCALL, SYSENTER, SMSW; EENTER / ERESUME #GP | 3D 39.6.1 Table 39-1 p.39-14; 42.3.4 p.42-2; 42.11.2 p.42-10 | `translate.c` (check emitted only when the model enumerates SGX) | impl (U1263) |
| RDTSC / RDTSCP legal with SGX2 | 3D 39.6.1 p.39-15 | - | impl (nothing to do: SGX2 is enumerated) |
| INT1 / INT3 inside an enclave: fault-class; opt-out #UD, opt-in #DB / #BP (no DPL check) | 3D 39.6.4 p.39-15, 43.4.1 p.43-4 | `translate.c`, `sgx_model.c` | impl (U1263) |
| POPF cannot set TF after an opt-out entry | 3D 43.2.5 p.43-3 | `translate.c` | impl (U1263) |
| Breakpoint suppression (opt-out: code breakpoints, data breakpoints in ELRANGE) | 3D 43.3 p.43-3..43-4 | `bpt_helper.c` | impl (U1263; details docs/quirks.md) |
| Performance monitoring / LBR / PEBS / Intel PT suppression | 3D 43.5-43.6 p.43-5..43-9 | - | no: the model has no LBR / PEBS / PT and no performance counters that count |

### 1.4 ENCLS leaves (3D 41.3)

| Leaf | Source | Status |
|---|---|---|
| ENCLS common checks (#UD: CR0.PE = 0, VM86, SMM, SGX1 = 0, CPL > 0, LOCK/66/F2/F3; #GP: FEATURE_CONTROL, invalid leaf, CR0.PG = 0, expand-down DS outside 64-bit mode) | ENCLS p.41-8..41-9; 42.3.2 p.42-2 | impl (U1262; VMX exiting **def**) |
| ECREATE (0) incl. MRENCLAVE init + "ECREATE" update, SSA size check (42.7.2.2) | p.41-25..41-29 | impl (U1262) |
| EADD (1) incl. "EADD" update | p.41-13..41-17 | impl (U1262) |
| EINIT (2): SIGSTRUCT header, RSA verify with q1/q2, MRENCLAVE final, MRSIGNER, controlled attributes, ATTRIBUTEMASK / MISCMASK, EINITTOKEN (CMAC with the derived launch key), COMMIT | p.41-41..41-48 | impl (U1262) |
| EREMOVE (3) | p.41-62..41-64 | impl (U1262) |
| EDBGRD (4) / EDBGWR (5) | p.41-30..41-37 | impl (U1262) |
| EEXTEND (6) | p.41-38..41-40 | impl (U1262) |
| ELDB (7) / ELDU (8): AES-GCM decrypt with CR_BASE_PK, MAC compare | p.41-49..41-53 | impl (U1265); CR_BASE_PK **ph** |
| EBLOCK (9), EPA (0AH), ETRACK (0CH) | p.41-22..41-24, 41-60..41-61, 41-65..41-66 | impl (U1262; tracking model: our choice) |
| EWB (0BH): AES-GCM encrypt, PCMD, VA slot | p.41-69..41-73 | impl (U1265); TMP_VER = CR_PAGING_VERSION + 1 (our model) |
| EAUG (0DH), EMODPR (0EH), EMODT (0FH) (SGX2) | p.41-18..41-21, 41-54..41-59 | impl (U1266) |
| EUPDATESVN (18H) | p.41-67..41-68 | no: 12H.0:EAX[10] not enumerated (microcode SVN update has no meaning in the model) |
| ENCLS leaves of SGX oversubscription (ERDINFO, ETRACKC, ELDBC/ELDUC, EINCVIRTCHILD, EDECVIRTCHILD) and ENCLV | not in SDM 092 Vol3D; ENCLV (NP 0F 01 C0, exit reason 70, ENCLV-exiting bitmap 2036H) was specified in SDM 325462-065 to -087 and removed in -088 | not modelled; ENCLV stays #UD (VMX agent, docs/vmx.md, U1313 / U1323) |
| ENCLS-exiting bitmap / VM exits | ENCLS p.41-8; 42.5 p.42-3 | **def** (VMX) |

### 1.5 ENCLU leaves (3D 41.4)

| Leaf | Source | Status |
|---|---|---|
| ENCLU common checks (#UD as ENCLS but CPL < 3; #NM CR0.TS; #GP: FEATURE_CONTROL, invalid leaf, CR0.PG / CR0.NE = 0, 16-bit CS, EENTER/ERESUME inside / others outside an enclave) | ENCLU p.41-10..41-12 | impl (U1263) |
| EREPORT (0): REPORT, report key derivation for TARGETINFO, CMAC over 384 bytes | p.41-112..41-116 | impl (U1264) |
| EGETKEY (1): KEYREQUEST checks, Table 41-62 dependencies, derivekey | p.41-99..41-108 | impl (U1264); derivekey and the root keys **ph** |
| EENTER (2) | p.41-87..41-95 | impl (U1263) |
| ERESUME (3) | p.41-117..41-128 | impl (U1263; TMP_NOTIFY = 0) |
| EEXIT (4) | p.41-96..41-98 | impl (U1263) |
| EACCEPT (5), EMODPE (6), EACCEPTCOPY (7) (SGX2) | p.41-74..41-82, 41-109..41-111 | impl (U1266) |
| EVERIFYREPORT2 (8) | p.41-129..41-130 | no: 12H.0:EAX[7] not enumerated (needs TDX SEAMREPORT / CR_REPORT_MAC_KEY) |
| EDECCSSA (9) | p.41-83..41-86 | no (AEX-Notify not enumerated) |
| AEX: SSA save (GPRSGX, XSAVE with XFRM, EXITINFO, MISC.EXINFO), synthetic state (Table 40-1), FS/GS / XCR0 / TF restore, CSSA++, TCS inactive, flush | 3D 40.1-40.4 p.40-1..40-8; 42.6 p.42-4; 42.7.5 p.42-8 | impl (U1263) |

### 1.6 Cryptography

| Item | Source | Status |
|---|---|---|
| SHA-256 (FIPS 180-4) incremental: MRENCLAVE = SHA256INITIALIZE / SHA256UPDATE (64-byte blocks) / SHA256FINAL(count * 512) | ECREATE / EADD / EEXTEND / EINIT pseudocode; 39.4.1.1 p.39-6 | impl (U1262) |
| MRSIGNER = SHA256(SIGSTRUCT.MODULUS) (384 bytes as stored) | EINIT p.41-45; 39.4.1.2 p.39-7 | impl (U1262) |
| RSA-3072 signature check (e = 3, PKCS#1 v1.5 / RFC 3447 DigestInfo SHA-256) over SIGSTRUCT bytes 0-127 and 900-1027 ("Signed" column) using q1, q2 | 38.14 p.38-13..38-14; EINIT p.41-44 | impl (U1262; q1 / q2 use: our reading) |
| AES-CMAC-128 (EINITTOKEN.MAC over bytes 0-191, REPORT.MAC over bytes 0-383) | EINIT p.41-46; EREPORT p.41-115 | impl (U1262: EINITTOKEN; U1264: REPORT) |
| AES-GCM (EWB / ELDU): key CR_BASE_PK, counter "TMP_VER << 32", 128-byte MAC header (SECINFO, LINADDR, EID, ...), 4096-byte page | EWB p.41-72; ELDB p.41-51 | impl (U1265): our IV / MAC-header layout (docs/quirks.md); CR_BASE_PK **ph** |
| derivekey(KEYDEPENDENCIES): "derivation data and package specific value" | EGETKEY p.41-99, 41-104; EINIT p.41-46 | **ph** (U1262: AES-CMAC with the host root key over our 648-byte layout) |

### 1.7 GETSEC / SMX (2D chapter 7)

| Requirement | Source | Status |
|---|---|---|
| Opt-in `UC_X86_FEATURE_SMX` = 32; CPUID.01H:ECX.SMX[6] (today always reported, U113), CR4.SMXE | 2D 7.2.1 p.7-1 | impl (U1267; bit value 32) |
| GETSEC[CAPABILITIES] (0) | p.7-7..7-9 | impl (U113 without the opt-in: EAX = 0; U1267 with it: 1FCH - leaves 2-8, no chipset) |
| GETSEC[PARAMETERS] (6) | p.7-34..7-37 | impl (U1267): Table 7-10 defaults as model values |
| GETSEC[SMCTRL] (7) | p.7-38..7-40 | impl (U1267): always #GP(0) - SENTERFLAG = 0 (no measured environment without a chipset) |
| GETSEC[ENTERACCS] (2) / [EXITAC] (3) | p.7-10..7-21 | impl up to the chipset (U1267: ENTERACCS #GP(0) "TXT chipset not present", EXITAC #GP(0) "ACMODEFLAG = 0"); the AC-module load / authentication **blk**: Intel TXT MLE Developer's Guide, TXT chipset and SINIT ACM specifications are not local |
| GETSEC[SENTER] (4) / [SEXIT] (5) / [WAKEUP] (8) | p.7-22..7-33, 7-41..7-43 | impl up to the chipset (U1267: #GP(0) "TXT chipset not present" / "SENTERFLAG = 0"); the launch itself **blk** (same documents; TPM / chipset registers) |
| FRED interactions (CR4.FRED save in Cr4High) | 3A 8.5.5 p.8-21..8-22 | **blk** with ENTERACCS / SENTER |
| GETSEC in VMX non-root operation | 3C 27.1.2 | **def** (VMX) |

## 2. Design

### 2.1 Opt-in and API (U1261)
- `uc_ctl_set_x86_features(uc, UC_X86_FEATURE_SGX)` (bit value **16**, the lowest free bit after
  TSE 1, ITD 2, FRED 4, LAM 8; coordinated with the VMX agent, which takes bits from the top of the
  free range) before the engine is initialised. Default off: the CPU model, CPUID, the MSR list and
  ENCLS / ENCLU (#UD) are unchanged.
- `uc_ctl_set_x86_sgx_platform(uc, &cfg)` (`UC_CTL_X86_SGX_PLATFORM`, before init): the EPC section
  (base, size; 4 KB aligned) and the platform-specific values the SDM does not give - the model's
  key-derivation root key, CR_CPUSVN, CR_SGXOWNEREPOCH, CR_SEAL_FUSES, CR_BASE_PK, CR_REPORT_KEYID,
  the IA32_SGXLEPUBKEYHASH reset value, IA32_SGX_SVN_STATUS.SGX_SVN_SINIT. All default to zero.
  Reading returns the effective configuration (an EPC beyond MAXPHYADDR reads back with size 0).
- `uc_ctl_get_x86_sgx_epcm(uc, pa, &e)` (`UC_CTL_X86_SGX_EPCM`): the EPCM entry of an EPC page (not
  software-visible on hardware; for tests and tools).
- The engine maps the EPC section as RAM at init (uc.c `x86_sgx_map_epc`). The host reads and
  writes it in plaintext through `uc_mem_read` / `uc_mem_write` (the hardware view); guest software
  is subject to the SGX access control.

### 2.2 State (U1261)
- Package (`X86SgxPkg`, `env->sgx`, after `end_reset_fields`: kept across CPU reset - 42.9: "EPC
  Settings: Unchanged, EPCM: Unchanged" - and not part of a `uc_context`, like memory): the EPC
  section, the EPCM array (one `X86SgxEpcm` per page: VALID, R, W, X, PT, BLOCKED, PENDING,
  MODIFIED, PR, the SECS identifier = the physical address of the SECS page, ENCLAVEADDRESS, and
  the tracking epochs), the platform CREGs (Table 41-4: CR_CPUSVN, CR_SGXOWNEREPOCH, CR_SEAL_FUSES,
  CR_BASE_PK, CR_REPORT_KEYID, CR_NEXT_EID, CR_PAGING_VERSION) and the abort page.
- Logical processor (reset area: cleared by reset, carried by a `uc_context`): CR_ENCLAVE_MODE,
  CR_DBGOPTIN, CR_SAVE_TF, CR_TCS_LA / _PA, CR_ACTIVE_SECS, CR_ELRANGE, CR_GPR_PA, CR_SAVE_XCR0,
  CR_SAVE_FS / _GS, IA32_SGXLEPUBKEYHASH0-3 and IA32_SGX_SVN_STATUS.Lock.

### 2.3 Enumeration and MSRs (U1261)
- CPUID.(07H,0):EBX.SGX[2], ECX.SGX_LC[30]; MAX_LEAF >= 12H; CPUID.12H as in docs/quirks.md
  ("CPUID.12H.0 / 12H.1 / 12H.2 values"). A strict `UC_CTL_X86_CPUID` profile that hides SGX hides
  the MSRs and (later stages) ENCLS / ENCLU, as the translator hides other instructions.
- IA32_FEATURE_CONTROL bits 17 / 18 were already accepted with SGX_LC / SGX (U906).
- IA32_SGXLEPUBKEYHASH0-3: present with SGX1 and SGX_LC; reset value from the platform; WRMSR only
  with IA32_FEATURE_CONTROL.LOCK = LE_WR = 1, else #GP(0) (the host API gets UC_ERR_EXCEPTION).
- IA32_SGX_SVN_STATUS (500H): present with SGX, read-only (WRMSR #GP(0)); Lock[0] = 0 until the
  first non-faulting SGX instruction (set by the leaf functions, later stages); SGX_SVN_SINIT only
  with CPUID.01H:ECX.SMX.
- INVD: #GP(0) once processor reserved memory protections are active (our activation rule).

### 2.4 ENCLS (U1262)
- Decode: NP 0F 01 CF routes to `helper_encls` only when the model enumerates SGX (else #UD as
  before); LOCK / 66 / F2 / F3 #UD (VEX cannot encode 0F 01); REX, segment and address-size
  overrides ignored. The helper checks the Operation order of p. 41-8/41-9 (#UD, then #GP(0) for
  IA32_FEATURE_CONTROL, an unsupported leaf - the model enumerates leaves 00H-0FH (07H, 08H, 0BH:
  U1265; 0DH-0FH: U1266); EUPDATESVN 18H is not enumerated - CR0.PG = 0, an expand-down DS
  outside 64-bit mode). 64-bit
  mode = IA32_EFER.LMA = 1 and CS.L = 1 (the translator's CS64 flag).
- Operands: DS-relative effective addresses (32-bit outside 64-bit mode, DS limit / write
  checks); explicit non-EPC operands (PAGEINFO, SECINFO, source pages, SIGSTRUCT, EINITTOKEN) are
  read through the TLB with ordinary faults; an EPC operand is translated by a translation-only
  page walk (`env->sgx_xlate`, excp_helper.c: ordinary #PF / #GP, A/D bits set, no TLB entry) and
  accessed physically. "Does not resolve within an EPC" and EPCM type / state errors are
  SGX-induced #PF (P = 1, SGX[15] = 1, W/R of the access, U/S = CPL 3).
- Flags: the helper ends with the arithmetic flags in CC_SRC (CC_OP_EFLAGS); leaves without an
  error code keep the flags; error-code leaves set ZF / CF and clear the others as their pages say.
- IA32_SGX_SVN_STATUS.Lock is set by the first leaf that completes without a fault and without an
  error code (ZF = 0).
- Non-enclave accesses to EPC pages map the abort page (all ones, writes dropped; cputlb.c
  `tlb_set_page_sgx_abort`, a RAM block outside the address space so fetches have a RAM address).
- Implemented leaves: ECREATE, EADD, EINIT, EREMOVE, EDBGRD, EDBGWR, EEXTEND, EBLOCK, EPA, ETRACK.
- Measurement: the SECS keeps the SHA-256 state (MRENCLAVE field) and the update counter; ECREATE,
  EADD and EEXTEND compress the 64-byte blocks of the pseudocode, EINIT finalises with the counter
  (SHA256FINAL) - equal to SHA-256 over the concatenated blocks (ref_sgx.py computes it that way).
- EINIT: SIGSTRUCT header / reserved checks, the event window (`SGX_UNMASKED_EVENT`), RSA-3072
  with q1 / q2, the EPCM / initialized checks, MRENCLAVE, MRSIGNER = SHA-256(MODULUS),
  CONTROLLED_ATTRIBUTES (EINITTOKEN_KEY), ATTRIBUTEMASK, MISCMASK, launch control (VALID = 0:
  MRSIGNER = IA32_SGXLEPUBKEYHASH; VALID = 1: DEBUG, reserved space, CPUSVN, the CMAC of bytes
  0-191 with the derived EINITTOKEN key, MRENCLAVE / MRSIGNER / ATTRIBUTES), commit.

### 2.5 Enclave execution (U1263)
- Decode: NP 0F 01 D7 routes to `helper_enclu` only when the model enumerates SGX; LOCK / 66 / F2 /
  F3 #UD. The helper makes the Operation checks of p. 41-10/41-11 and sets RIP for every leaf; the
  TB ends after it (no generic single-step / RF handling): RF is cleared except after ERESUME
  (which loads it from the SSA), the interrupt shadow ends, a single-step #DB pended by an opt-in
  EENTER or by EEXIT (TF = 1 after the exit) is raised by the helper (`helper_single_step`). The
  CET rule of the Operation (ERESUME may run with the tracker in WAIT_FOR_ENDBRANCH) is the TB-start
  IBT check with kind 4 (`translate.c` ibt_kind, `misc_helper.c` helper_ibt_check).
- EENTER / ERESUME: the checks in the order of the Operation sections (`sgx_entry_checks`,
  `sgx_ssa_checks`): page walks of the TCS (EENTER: read, ERESUME: read/write, per their Memory
  Parameter tables), the SSA pages and the GPR area (write) with SGX-induced #PF for EPCM errors;
  CR_XSAVE_PAGE_n / CR_GPR_PA are cached in the CPU state. Commit: enclave mode, CR_ACTIVE_SECS,
  ELRANGE, CR_TCS_PA / LA, TCS.AEP, TCS busy, saved FS / GS, FS / GS from the TCS (selector 0BH,
  descriptor bits from DS), XCR0 := XFRM with CR4.OSXSAVE, DBGOPTIN, TF save / clear and
  breakpoint suppression for an opt-out entry, the CET save (CPUID.12H.1:EAX[6] = 0: the enclave
  runs with IA32_U_CET = 0 and SSP = 0), Flush_linear_context (`tlb_flush`, which also clears the
  TB jump cache). ERESUME first checks the XRSTOR fault conditions of the SSA header (#GP(0), or
  the XFD #NM) and restores the XSAVE state (standard form, REX.W layout in 64-bit mode), then the
  GPRs and RFLAGS of its Operation, and pops the SSA frame.
- AEX (`x86_sgx_aex`): called before the delivery or report of every event in enclave mode -
  exceptions in `cpu_handle_exception` (IDT / FRED delivery and the Unicorn hooks alike), hardware
  interrupts / NMIs in `do_interrupt_all`, SMIs in `do_smm_enter` (SMRAM 7EE0H bit 1), INIT in
  `do_cpu_init`. It saves GPRs / RFLAGS / RIP / FS / GS bases (4-byte fields outside 64-bit mode),
  the XSAVE state of XFRM (through CR_XSAVE_PAGE_n), EXITINFO / EXINFO, loads the synthetic state
  of Table 40-1 (x87 / SSE INIT with the table's FCW / FSW / MXCSR, the XFRM components 2-63 INIT
  with CR4.OSXSAVE), increments CSSA, restores FS / GS, XCR0, TF (opt-out), CET, leaves enclave
  mode, marks the TCS available and flushes; RIP := AEP. CR2 bits 11:0 are cleared for a #PF
  (also in the UC_CTL_X86_EXCEPTION record). FRED event delivery sets bit 56 of the augmented SS
  for the event and exceptions during its delivery.
- Access control (`x86_sgx_access` from handle_mmu_fault after the paging checks): enclave mode and
  ELRANGE -> the EPCM checks of 38.3 with the TLB entry's rights reduced to EPCM R / W / X; a fetch
  outside ELRANGE #GP(0); other accesses to EPC pages the abort page.
- Inside an enclave (`translate.c` sgx_enclave_kind, emitted at CPL 3 when SGX is enumerated, the
  helper checks enclave mode): Table 39-1 instructions #UD; INT3 / INT1 fault-class (#UD opt-out,
  #BP / #DB opt-in); POPF clears TF after an opt-out entry. Instruction breakpoints are not
  inserted while suppressed (EENTER / ERESUME / EEXIT / AEX re-evaluate them); suppressed matches
  set no DR6 bit (`bpt_helper.c`).
- Tests (`test_x86_sgx_eenter_eexit`, `_aex_eresume`, `_enclave_access`, `_enclave_debug`): the
  ref_sgx.py enclave (64-bit, XFRM 3, CR4.OSXSAVE = 0) entered at CPL 3. Not covered by unit tests:
  32-bit enclaves, CR4.OSXSAVE = 1 with extended XFRM components, FRED delivery of an AEX event,
  SMI / INIT in enclave mode, CET-enabled applications.

### 2.6 EREPORT and EGETKEY (U1264)
- Operands: aligned (TARGETINFO / KEYREQUEST / OUTPUTDATA of EREPORT 512 bytes, REPORTDATA 128,
  EGETKEY's output 16) and within ELRANGE (#GP(0)), a page walk and the EPCM checks of their
  Operation sections (SGX #PF), then read / written through their linear addresses (enclave
  accesses: data breakpoints apply as 43.3.4 says).
- EREPORT builds the REPORT of Table 38-22 from the SECS and CR_CPUSVN / CR_REPORT_KEYID and MACs
  bytes 0-383 with the report key of the TARGETINFO enclave; EGETKEY derives the five keys of
  Table 41-62 (KEYPOLICY, ATTRIBUTEMASK | INIT | DEBUG, MISCMASK; the SVN / attribute / KEYNAME
  error codes with ZF = 1 and the output unmodified). KSS and CET are not enumerated: KEYPOLICY bits
  2-5 and a non-zero CONFIGSVN are #GP(0) (the Operation's KSS = 0 rule), the CET bytes are reserved.
- A single-step #DB after EREPORT / EGETKEY when TF is 1 at their start (opt-in entries only);
  IA32_SGX_SVN_STATUS.Lock is not set by an EGETKEY that returns an error code.
- Validation: ref_sgx.py `ereport` / `egetkey` (written from the pseudocode) give the REPORT and the
  SEAL / REPORT keys of the test enclave (`x86_sgx_vectors.inc`); its self test checks that the
  REPORT MAC verifies with the EGETKEY report key and the error codes.

### 2.7 EPC paging (U1265)
- EWB: the operand checks of p. 41-70/41-71, BLOCKED (SGX_PAGE_NOT_BLOCKED), the tracking model
  (an ETRACK of the enclave after the EBLOCK, else SGX_NOT_TRACKED), SGX_CHILD_PRESENT for a SECS
  with pages; AES-GCM-128 with CR_BASE_PK over the page, the 128-byte MAC header as AAD; the
  ciphertext to PAGEINFO.SRCPGE, the PCMD (SECINFO flags, ENCLAVEID, MAC), PAGEINFO.LINADDR, the
  version (CR_PAGING_VERSION + 1) to the VA slot (CF / SGX_VA_SLOT_OCCUPIED when it held one); the
  EPCM entry becomes invalid.
- ELDB / ELDU: the checks of p. 41-50/41-51, the MAC header from the PCMD and PAGEINFO (EID of
  PAGEINFO.SECS for REG / TCS / TRIM pages), the decrypted page written to the EPC, the MAC compared
  (SGX_MAC_COMPARE_FAIL, the EPCM entry stays invalid), the VA slot cleared, the EPCM entry from the
  header with ENCLAVESECS = the PAGEINFO.SECS page (38.5.3.2); ELDB marks REG / TCS / TRIM pages
  BLOCKED (counted as tracked: they had no translations).
- Validation: ref_sgx.py `ewb` (same layout) gives the ciphertext and PCMD of the test enclave's
  data page; its self test decrypts it back.

### 2.8 SGX2 (U1266)
- EAUG adds a zeroed PENDING PT_REG R/W page to an initialized enclave (a PAGEINFO.SECINFO is only
  for CET shadow-stack pages and is #GP(0): CPUID.12H.01H:EAX[6] = 0); EMODPR restricts R / W / X
  and sets PR, EMODT changes the type to PT_TCS / PT_TRIM and sets MODIFIED (both record the
  tracking epoch); EACCEPT accepts a PENDING / PR / MODIFIED state that matches its SECINFO (an
  ETRACK after EMODPR / EMODT, TCS content checks), EMODPE extends R / W / X, EACCEPTCOPY copies a
  page of the enclave into a PENDING page and sets its permissions. The concurrency conditions
  (SGX_EPC_PAGE_CONFLICT, "page in use", "locked") never occur with one logical processor.
- The SDM typos read as noted in docs/quirks.md (U1266).

### 2.9 GETSEC / SMX (U1267)
- `uc_ctl_set_x86_features(uc, UC_X86_FEATURE_SMX)` (bit value **32**, the next free bit from the
  bottom after SGX = 16): an SMX processor on a platform without an Intel TXT-capable chipset.
  Without it GETSEC keeps U113 (CAPABILITIES = 0, every other leaf #UD).
- `misc_helper.c` smx_getsec: #UD for CR4.SMXE = 0 and the leaves CAPABILITIES does not report;
  CAPABILITIES (EBX = 0: 1FCH); PARAMETERS (types 1-3 with the Table 7-10 defaults, then NULL); the
  other leaves raise the #GP(0) of their first condition group, which always holds without a
  chipset. A TXT chipset model (TXT configuration space, TXT.PUBLIC.KEY, the AC-module header and
  signature format, TPM localities, the MLE JOIN structure in chipset space) needs documents that
  are not local (section 4); VMX non-root VM exits and "in VMX operation" are pending (plan
  1.J.12).
- Test `test_x86_gs_smx`.

### 2.10 Validation (U1262)
- `Emulator/tools/isa/ref_sgx.py` (stdlib Python, written from the SDM): AES-128 (FIPS 197),
  AES-CMAC (RFC 4493 vectors), AES-GCM (GCM spec test case 2), SHA-256 (hashlib), RSA-3072 key
  generation / signing with e = 3, the SIGSTRUCT / EINITTOKEN layouts, MRENCLAVE as SHA-256 over
  the ECREATE / EADD / EEXTEND blocks, the model's key derivation. `--selftest` (suite
  `ref_sgx_selftest`) also checks that `unicorn/tests/unit/x86_sgx_vectors.inc` equals `--cinc`.
- Unit tests `test_x86_sgx_*` build that enclave with ENCLS and EINIT it with the Python
  SIGSTRUCT / token: a successful EINIT and the SECS MRENCLAVE / MRSIGNER equal to the Python
  values are an independent check of the measurement, the RSA check and the CMAC key derivation.

## 3. Our choices (platform-specific values, SDM gaps)

Every entry is a row of `docs/quirks.md`, section "SGX and SMX (plan 1.J.11)".

## 4. Open questions for the user

- CPUID.12H with IA32_FEATURE_CONTROL unlocked / SGX disabled: the SDM gives no values (the model
  reports the same leaf in every state).
- IA32_SGXLEPUBKEYHASH4 / 5 (90H / 91H) in SDM 092 / 093 Vol4 ("bits 383:256 of the SHA256
  digest"): not modelled; which document defines them?
- ECREATE MISCSELECT check (the pseudocode faults on MISCSELECT = 0; the model rejects unsupported
  bits only).
- ECREATE maximum size: SIZE >= 2^EDX[7:0] / 2^EDX[15:8] faults (pseudocode) although CPUID calls
  2^EDX the maximum supported size (the model follows the pseudocode).
- EINIT "SGX_INVALID_EINIT_ATTRIBUTE" (not in Table 41-3): the model returns SGX_INVALID_ATTRIBUTE.
- SIGSTRUCT "Each of the 8 HASH dwords is stored in little-endian order": the model uses the FIPS
  180-4 digest byte string for MRENCLAVE / MRSIGNER / ENCLAVEHASH.
- SGX_TCS_LIMIT of EDBGRD: the model uses 88.
- ERESUME CR_SAVE_TF of an opt-out entry: the pseudocode clears TF before saving it; the model
  saves the TF the application had (43.2.3 / 43.2.4).
- AEX: the pseudocode writes EXINFO also without MISCSELECT[0]; the model writes it only with
  MISCSELECT[0] = 1.
- Which fault an enclave access gets when an ELRANGE address maps outside the EPC (38.1 "a fault
  is generated"): the model raises the SGX-induced #PF.
- EGETKEY EINITTOKEN_KEY: the description requires "SECS.MRSIGNER equal IA32_SGXLEPUBKEYHASH", the
  Operation does not check it (the model follows the Operation).
- EWB "IF (EBLOCK is not correct) THEN #GP(0)": the condition is not defined (the model has none).
- GETSEC[WAKEUP] Operation (Vol2D p. 7-42): "(IN_SMM=0)" among the #GP(0) conditions, while 7.3 says
  the leaves are not allowed in SMM - read as IN_SMM = 1 (no effect in the model: WAKEUP always
  faults without a measured environment).
- Missing documents for a TXT chipset model: Intel TXT Measured Launched Environment Developer's
  Guide, the TXT chipset (TXT configuration space) and SINIT / BIOS ACM specifications, the TPM
  interface of the chipset - can the user provide them?
- The key derivation function and the platform keys (root key, CR_SEAL_FUSES, CR_SGXOWNEREPOCH,
  CR_CPUSVN, CR_REPORT_KEYID, CR_BASE_PK, Intel's LE signer hash): model placeholders - does the
  user have an Intel document that specifies any of them?
