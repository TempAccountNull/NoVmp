; t1_exit.asm - test PE T1 of plan 3.M4 (ledger U1980; notes\eread\P34_phase3_m4.md): self-written 64-bit guest code,
; never derived from the sample. It is GUEST CODE: the emulator only reads it as a file and writes it into emulated memory
; (emu-oep maps it as data, AGENT_RULES "Host safety"); the test harness may run it natively in the VMware guest as a
; labelled reference only (decision 3.0.7).
;
; What it does (no imports, no CRT, no TLS): one direct SYSCALL of NtTerminateProcess( NtCurrentProcess() = -1,
; ExitStatus = T1_EXIT_CODE ) - the Windows x64 system-call convention: service number in EAX, first argument in R10
; (SYSCALL overwrites RCX with the return RIP: 253667-093 Vol2B SYSCALL Operation p. 4-700 "RCX := RIP;"), second in RDX.
;
; The service number is NOT built in: Windows renumbers system services between builds, so it comes from the emulated
; environment's own ntdll (decision 3.0.8: the environment's DLL capture, local only, never committed). The 4-byte field
; t1_sysnum after the 8-byte marker "T1SYSNUM" is patched by the harness (Emulator\tools\win_env_t1.py) in a copy written
; to the build output: the harness reads the number from the NtTerminateProcess stub of the captured ntdll
; (4C 8B D1 B8 imm32 = mov r10, rcx; mov eax, imm32) and cross-checks it with emu-oep's own parser. The unpatched value
; FFFFFFFFH is no service of any table (EAX AND FFFFH = FFFFH).
;
; Sections: .text (code, RX), .rdata (the marker and the number, R), .data (an absolute pointer to the number, RW) and
; .reloc: t1_sysnum_ptr holds an absolute address, so the linker emits one IMAGE_REL_BASED_DIR64 base relocation (with
; /DYNAMICBASE) - loading T1 at another base exercises the loader's relocation path (emu-oep --image-base).

T1_EXIT_CODE    equ     2Ah                 ; the expected exit status (harness, emu-oep, native reference)

.CONST
                ALIGN   16
t1_marker       db      'T1SYSNUM'          ; the harness finds the patch field by this marker (exactly once in the file)
t1_sysnum       dd      0FFFFFFFFh          ; NtTerminateProcess service number, patched per environment
                dd      0

.DATA
                ALIGN   16
t1_sysnum_ptr   dq      t1_sysnum           ; absolute address: the one DIR64 base relocation

.CODE
t1_entry PROC
                mov     rax, qword ptr [t1_sysnum_ptr]  ; RIP-relative load of the relocated pointer
                mov     eax, dword ptr [rax]            ; EAX = service number
                mov     rcx, -1                         ; ProcessHandle = NtCurrentProcess()
                mov     edx, T1_EXIT_CODE               ; ExitStatus
                mov     r10, rcx                        ; argument 1 in R10 (RCX is overwritten by SYSCALL)
                syscall
                ud2                                     ; NtTerminateProcess( NtCurrentProcess() ) does not return
t1_entry ENDP

END
