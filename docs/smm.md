# System-management mode: SMI, SMRAM state-save maps, RSM (ledgers U1460-U1464)

User decision 2026-10-14 (finding S3 / F1 of `emulator/notes/eread/S3_firmware.md`): QEMU wrote its
AMD64-style map (revision 00020064H) for every CPU, a known emulator fingerprint. Source:
`unicorn/qemu/target/i386/smm_helper.c` (`#else /* ours (U1462) */`); the `__Use_Original_Qemu`
build keeps QEMU's map with the upstream fixes of U1460.

References (decisive passages checked against the PDFs):
- Intel SDM 093, Vol3C 326019-093, chapter 34: 34.3.1-34.3.2 (PDF pp. 214-216), Table 34-1
  (pp. 217-218), Table 34-3 (pp. 219-220), Table 34-4 (p. 222), 34.5.3 (p. 223), Tables 34-5 /
  34-6 (p. 225), 34.9 / Figure 34-2, 34.10 / Table 34-7 (p. 226), 34.11-34.12 / Table 34-8
  (pp. 227-228); Vol2B RSM Operation; FRED spec 346446-009 chapter 12 (CR4 at 7E40H).
- AMD64 APM Vol2 24593 r3.45 (July 2026), chapter 10: 10.2.2-10.2.4 with Table 10-1 and Figure 10-3
  (PDF pp. 390-396), Table 10-3 (pp. 398-399), 10.3.7 / 10.3.8 with Figure 10-6 (pp. 401-403), 10.4
  (p. 404).
- No PyMuPDF extract of SDM 093 Vol3C or of APM Vol2 exists in `emulator/intel_docs_text`; the
  passages were read from the PDFs and agree with `sdm_vol3c_092.md` where 092 has them.

Labels: **implemented**, **specification-validated** (unit tests `test_x86_smr_*` written from the
documents). Nothing here is hardware- or SDE-validated: SMM cannot be entered from user mode.

## Which map

The vendor is what CPUID leaf 0 returns (a `UC_CTL_X86_CPUID` profile's vendor, else the CPU
model's), never the build or the host:

| vendor | condition | map | SMM revision identifier |
|---|---|---|---|
| AuthenticAMD | always (APM 10.2.3: AMD64 implementations do not use the legacy Table 10-2) | APM Table 10-1 | 0003_0064h: level 0_xx64h with xx = 00, bits 16 / 17 "always set" (Figure 10-3) |
| any other (GenuineIntel) | CPUID.80000001H:EDX[29] (IA-32e mode) or a Table 34-2 signature (06_17H, 06_0FH, 06_1CH) | SDM Table 34-3 | 0003_0004h |
| any other | otherwise (RSM Operation: "Non-32-Bit-Mode SMM STATE MAP") | SDM Table 34-1 | 0003_0004h |

Intel revision identifier: 34.9 defines bit 16 (I/O instruction restart) and bit 17 (SMBASE
relocation) - both supported - and 34.7 gives the I/O state information to "revision ID of 30004H or
higher"; the SDM gives no lower-word value of its own, so 0003_0004h is the smallest value that
states everything the model does. Real processors report model-specific values (open-source
firmware headers use 30100H / 30101H for the Intel 64 map - evidence class C, not used).

## Intel 64: SDM 093 Table 34-3 (offsets from SMBASE + 8000H)

| offset | size | field | writable | emulator |
|---|---|---|---|---|
| 7FF8H | 8 | CR0 | No | saved; RSM loads it |
| 7FF0H | 8 | CR3 | No | saved; RSM loads it |
| 7FE8H | 8 | RFLAGS | Yes | saved; RSM loads it |
| 7FE0H | 8 | IA32_EFER | Yes | saved; RSM loads it |
| 7FD8H | 8 | RIP | Yes | saved; RSM loads it |
| 7FD0H | 8 | DR6 | No | saved; RSM loads bits 31:0 |
| 7FC8H | 8 | DR7 | No | saved; RSM loads bits 31:0 (breakpoints re-armed) |
| 7FC4H / 7FC0H | 4 | TR / LDTR selector (bits 31:16 reserved, written 0) | No | saved; RSM loads the selector |
| 7FBCH .. 7FA8H | 4 each | GS, FS, DS, SS, CS, ES selector | No | saved; RSM loads the selectors |
| 7FA4H | 4 | IO_MISC | No | Table 34-5 for an I/O SMI, else 0 |
| 7F9CH | 8 | IO_MEM_ADDR | No | INS / OUTS: linear address of the memory operand of the iteration; else 0 |
| 7F94H .. 7F5CH | 8 each | RDI, RSI, RBP, RSP, RBX, RDX, RCX, RAX | Yes | saved; RSM loads them |
| 7F54H .. 7F1CH | 8 each | R8 .. R15 | Yes | saved; RSM loads them |
| 7F1BH-7F04H | | reserved | | not written |
| 7F02H | 2 | auto HALT restart | Yes | bit 0 = SMI in the HALT state; RSM: bit 0 = 1 returns to the HALT state |
| 7F00H | 2 | I/O instruction restart | Yes | 0 on entry; RSM: 00FFH re-executes the I/O instruction |
| 7EFCH | 4 | SMM revision identifier | No | 0003_0004h |
| 7EF8H | 4 | SMBASE | Yes | saved; RSM loads SMBASE from it (34.11) |
| 7EF7H-7EE4H | | reserved | | not written |
| 7EE0H | 4 | bit 0 EPT in VMX non-root, bit 1 incident to enclave mode | No | 0 / bit 1 = SGX enclave mode at the SMI (U1263); RSM does not read it |
| 7ED8H | 8 | EPTP | No | not written (bit 0 is never 1: no VMX non-root operation) |
| 7ED7H-7ECCH | | reserved (the SDM prints "7ECC0H") | | not written |
| 7EC8H | 8 | SSP | Yes | written with CET shadow stacks (CPUID.(07H,0):ECX[7]); RSM loads it sign-extended (bit 47, bit 56 with CR4.LA57) |
| 7EC7H-7EA0H | | reserved | | not written |
| 7E9CH / 7E94H / 7E8CH | 4 | LDT / IDT / GDT base 31:0 | No | saved; RSM loads them |
| 7E98H / 7E90H | | reserved | | not written |
| 7E8BH-7E48H | | reserved | | not written |
| 7E40H | 8 | CR4 (64 bits) | No | saved (all 64 bits, FRED spec 12); RSM loads all 64 bits |
| 7E3FH-7DF0H | | reserved | | not written |
| 7DE8H | 8 | IO_RIP | Yes | the I/O instruction for an I/O SMI, else 0; RSM with restart: RIP := IO_RIP |
| 7DE7H-7DDCH | | reserved | | not written |
| 7DD8H / 7DD4H / 7DD0H | 4 | IDT / LDT / GDT base 63:32 | No | saved; RSM loads them |
| 7DCFH-7C00H | | reserved | | not written |

## Intel without IA-32e mode: SDM 093 Table 34-1

| offset | size | field | writable |
|---|---|---|---|
| 7FFCH / 7FF8H | 4 | CR0 / CR3 | No |
| 7FF4H / 7FF0H | 4 | EFLAGS / EIP | Yes |
| 7FECH .. 7FD0H | 4 each | EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX | Yes |
| 7FCCH / 7FC8H | 4 | DR6 / DR7 | No |
| 7FC4H | 4 | TR (selector) | No |
| 7FC0H | | reserved | |
| 7FBCH .. 7FA8H | 4 each | GS, FS, DS, SS, CS, ES (selectors) | No |
| 7FA4H / 7FA0H | 4 | I/O state field / I/O memory address field (34.7) | No |
| 7F9FH-7F03H | | reserved | |
| 7F02H / 7F00H | 2 | auto HALT restart / I/O instruction restart | Yes |
| 7EFCH / 7EF8H | 4 | SMM revision identifier / SMBASE | No / Yes |
| 7EF7H-7E00H | | reserved | |

CR4, LDTR, GDTR and IDTR have no readable field here (34.4.1: "saved (but not readable)"); the model
keeps them in the CPU. Table 34-1 has no IO_RIP: an I/O restart returns to the I/O instruction
recorded at the SMI.

## Hidden state (both Intel maps)

The hidden part of CS, DS, ES, FS, GS, SS, LDTR and TR (base, limit, attributes) and the GDTR / IDTR
limits are "saved (but not readable) and restored" (34.4.1): the model keeps them in `CPUX86State`
(`smm_seg`, `smm_ldt`, `smm_tr`, `smm_gdt`, `smm_idt`, `smm_cr4`) instead of writing them into
reserved SMRAM; RSM combines them with the selectors and bases the map holds. Unicorn's API puts a
64-bit engine in IA-32e mode without a CS.L descriptor bit (hflags only); the hflags of the CS / SS
state (CPL, CS32, SS32, CS64, ADDSEG) are restored with the hidden state while CS and SS still carry
the attributes they had at the SMI.

## AMD64: APM Vol2 r3.45 Table 10-1 (offsets from SMBASE, FE00H = SMBASE + 8000H + 7E00H)

| offset | size | field | access | emulator |
|---|---|---|---|---|
| FE00H + 10H*n | 2 / 2 / 4 / 8 | ES, CS, SS, DS, FS, GS: selector, attributes, limit, base | Read-Only | saved; RSM loads them |
| FE60H-FE63H, FE66H-FE67H | | reserved (GDTR) | | not written |
| FE64H / FE68H | 2 / 8 | GDTR limit / base | Read-Only | saved (limit as a word); RSM loads them |
| FE70H-FE7FH | 2 / 2 / 4 / 8 | LDTR selector, attributes, limit, base | Read-Only | saved; RSM loads them |
| FE80H-FE83H, FE86H-FE87H | | reserved (IDTR) | | not written |
| FE84H / FE88H | 2 / 8 | IDTR limit / base | Read-Only | saved; RSM loads them |
| FE90H-FE9FH | 2 / 2 / 4 / 8 | TR selector, attributes, limit, base | Read-Only | saved; RSM loads them |
| FEA0H / FEA8H / FEB0H / FEB8H | 8 | I/O instruction restart RIP / RCX / RSI / RDI | Read-Only | the I/O instruction and its RCX / RSI / RDI (before the iteration) for an I/O SMI, else 0 |
| FEC0H | 4 | I/O instruction restart dword (Figure 10-6) | Read-Only | for an I/O SMI, else 0 |
| FEC4H-FEC7H | | reserved | | not written |
| FEC8H | 1 | I/O instruction restart | Read/Write | 0 on entry; RSM: non-zero restarts (RIP, RCX, RSI, RDI from FEA0H-FEBFH) |
| FEC9H | 1 | auto-halt restart | Read/Write | bit 0 = SMI in the halt state, bits 7:1 = 0; RSM: non-zero returns to the halt state |
| FECAH-FECFH | | reserved | | not written |
| FED0H | 8 | EFER | Read-Only | saved; RSM loads it |
| FED8H / FEE0H / FEE8H | 8 | SVM guest, guest VMCB physical address, guest virtual interrupt | Read-Only | 0 outside an SVM guest; not written inside one |
| FEF0H-FEFBH | | reserved | | not written |
| FEFCH | 4 | SMM-revision identifier | Read-Only | 0003_0064h |
| FF00H | 4 | SMBASE | Read/Write | saved; RSM loads SMBASE from it (10.2.2) |
| FF04H-FF17H | | reserved | | not written |
| FF18H | 8 | SSP | Read/Write | written with CET shadow stacks; RSM loads it as written (APM: no canonicalisation stated) |
| FF20H-FF47H | 8 each | SVM guest PAT, host EFER / CR4 / CR3 / CR0 | Read-Only | not written (SVM-guest SMIs not modelled) |
| FF48H / FF50H / FF58H | 8 | CR4 / CR3 / CR0 | Read-Only | saved (quadwords); RSM loads them |
| FF60H / FF68H | 8 | DR7 / DR6 | Read-Only | saved (quadwords); RSM loads bits 31:0 |
| FF70H | 8 | RFLAGS | Read/Write | saved (quadword); RSM loads it |
| FF78H | 8 | RIP | Read/Write | saved; RSM loads it |
| FF80H .. FFB8H | 8 each | R15 .. R8 | Read/Write | saved; RSM loads them |
| FFC0H .. FFF8H | 8 each | RDI, RSI, RBP, RSP, RBX, RDX, RCX, RAX | Read/Write | saved; RSM loads them |

The APM does not define the encoding of the "attributes" words; the model keeps QEMU's (descriptor
bits 47:40 in 7:0, 55:52 in 15:12). Changes against QEMU's map: revision 0002_0064h -> 0003_0064h,
CR0 / CR4 / DR6 / DR7 / RFLAGS stored as quadwords, GDTR / IDTR limits as words (QEMU wrote
doublewords over the reserved bytes), SSP at FF18H, the I/O restart fields and the auto-halt byte,
the SVM-guest fields, and Intel's 7EE0H enclave bit no longer written into the AMD map.

## I/O SMIs and restart

An SMI an IN / OUT hook signals (below) is synchronous to that I/O instruction: the translator ends
the TB after every IN / OUT / INS / OUTS (each REP iteration already ends one), so the SMI is taken
at the next instruction boundary, after the instruction (or the REP iteration) completed (34.7); a
UC_HOOK_BLOCK hook therefore sees a new block start after every I/O instruction. The model reads the
instruction's prefixes and opcode to build:
- Intel IO_MISC (Table 34-5): bits 31:16 port, 7:4 type (Table 34-6: IN imm 1001b, IN DX 0001b, OUT
  imm 1000b, OUT DX 0000b, INS 0011b, OUTS 0010b, REP INS 0111b, REP OUTS 0110b), 3:1 length (001b,
  010b, 100b), bit 0 IO_SMI; IO_RIP = the I/O instruction; IO_MEM_ADDR = the INS / OUTS memory
  operand (the field has no description beyond its name: our reading);
- AMD I/O instruction restart dword (Figure 10-6): 31:16 PORT, 11 TF, 9 A64, 8 A32, 7 A16, 6 SZ32,
  5 SZ16, 4 SZ8, 3 REP, 2 STR, 1 VAL, 0 TYPE (1 = IN). B3-B0 (15:12, "DR6.Bn status") are 0: I/O
  breakpoint matches are not reported there. Bit 10 is also labelled "PORT" in Figure 10-6 but has no
  definition in the text: 0.

RSM with I/O restart (Intel 00FFH in 7F00H, AMD non-zero in FEC8H) sets RIP to the I/O instruction;
for INS / OUTS RCX / RSI / RDI go back to their values before the iteration (Intel: kept in the CPU,
34.12 "the processor saves the necessary machine state"; AMD: from FEA8H-FEB8H), so the iteration
runs again. Other Intel restart values do not restart (Table 34-8 defines 00H and FFH only).

## Auto HALT restart

An SMI that arrives while the processor is in the HALT state (`uc_emu_start` returned after HLT, or
RSM returned to it) records it (Intel 7F02H bit 0, AMD FEC9H bit 0); the saved RIP is the instruction
after HLT. RSM with the flag set returns to the HALT state: `uc_emu_start` returns as after HLT. An
SMI latched in SMM that RSM would leave in the HALT state is taken at once and records the HALT state.

## Test-only hook: UC_CTL_X86_SMI (U1461)

Unicorn models no chipset and no SMI delivery through the local APIC (`docs/apic.md`: SMI delivery
mode not modelled). `UC_CTL_X86_SMI` plays SMI# for tests and tools (the pattern of
`UC_CTL_X86_TSE_KEY` / `UC_CTL_X86_ITD_CLASS`):
- write 1: latch an SMI. From the host it is taken before the first instruction of the next
  `uc_emu_start`; from an IN / OUT hook (`UC_HOOK_INSN` `UC_X86_INS_IN` / `OUT`) right after that I/O
  instruction (an I/O SMI); in SMM it stays latched until RSM (34.3.1: one SMI is latched). Other
  values: `UC_ERR_ARG`;
- read: bit 0 = an SMI is latched, bit 1 = the CPU is in SMM.

SMRAM is ordinary RAM in the one address space Unicorn gives a CPU. Before U1461 the first
instruction fetch in SMM crashed the emulator: QEMU selects address-space index 1 for SMM accesses
(`x86_asidx_from_attrs`, `attrs.secure`), and the TLB fill read `cpu_ases[1]` beyond Unicorn's
single entry; the index is now always 0.

## Entry state (SDM Table 34-4, APM Table 10-3)

CS = SMBASE >> 4 with base SMBASE and limit 4 GB, DS / ES / FS / GS / SS selector 0, base 0, limit
4 GB, EIP 8000H, EFLAGS 2, CR0.PE / EM / TS / PG = 0, CR4 = 0 (so CR4.CET = 0, 34.5.3), DR7 400H (the
debug-register breakpoints follow, upstream QEMU cdba90ac1b), IA32_EFER = 0 (APM Table 10-3; the
SDM lists no EFER value - QEMU's choice kept, not for Table 34-1 processors), MSR_SMI_COUNT + 1,
NMIs blocked; an SMI in SGX enclave mode performs the AEX first (U1263).

## Not modelled (emulator limitations, not CPU deviations)

- RSM shutdown checks (SDM 34.3.2: reserved CR4 bits, CR0 PG without PE / NW without CD, PCIDE
  without LMA, CET without WP, the FRED conditions, CR4.VMXE in the image (34.14.2); APM 10.4):
  RSM loads what the map holds.
- VMX / SMX interactions (34.14, 34.15 dual-monitor treatment, TXT private space), the EPTP field,
  RSM_IN_CPL0_ONLY (IA32_CORE_CAPABILITIES[3]), SMRR / SMM-only MSRs, IA32_SMBASE (9EH), the
  AMD SMM_ADDR / SMM_MASK MSRs and HWCR.SMM_PGCFG_LOCK, TSeg / ASeg protection: SMRAM is ordinary RAM.
- An asynchronous SMI that happens to follow an I/O instruction (34.7: IO_SMI would still be set):
  only an SMI signalled from the I/O hook records the I/O instruction.
- SMI blocking by STI / MOV SS ("may be blocked", 34.2) and SMI delivery through the local APIC.
- SVM-guest SMIs (FED8H-FEEFH, FF20H-FF47H): not written inside a guest; RSM never returns to one.
- Vendors other than AuthenticAMD (for example HygonGenuine) get the Intel maps.
