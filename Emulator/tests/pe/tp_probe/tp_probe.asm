; tp_probe.asm - test PE "probe" of plan 3.M5 part 2 (ledger U2022; notes\eread\P36_phase3_m5b.md): self-written 64-bit guest
; code, never derived from the sample. GUEST CODE: the emulator only reads it as a file and writes it into emulated memory; the
; harness may run it natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; It executes the probes emu-oep --counters counts, in a fixed order, so the suite win_env_probe can check every counter exactly:
;   CPUID leaf 0, leaf 1, leaf 7 sub-leaf 0, leaf 40000000H (each with ECX 0)       -> 4 CPUID, 4 (leaf, sub-leaf) pairs
;   RDTSC twice, RDTSCP once                                                          -> RDTSC 2, RDTSCP 1
;   TEB ProcessEnvironmentBlock (gs:[60H]) once, TEB NtTib.Self (gs:[30H]) once        -> TEB reads
;   PEB BeingDebugged (+2H), PEB NtGlobalFlag (+0BCH) once each                        -> PEB reads
;   KUSER_SHARED_DATA KdDebuggerEnabled (7FFE02D4H) once                               -> KUSER read
; Offsets: the public symbols of the guest ntdll (_TEB ProcessEnvironmentBlock 60H, NtTib 0H (_NT_TIB Self 30H), _PEB
; BeingDebugged 2H, NtGlobalFlag 0BCH, _KUSER_SHARED_DATA KdDebuggerEnabled 2D4H; KUSER at MM_SHARED_USER_DATA_VA 7FFE0000H).
; Exit: one direct SYSCALL of NtTerminateProcess( NtCurrentProcess(), status ) whose service number (the 4-byte field after the
; marker "TPSYSNUM", FFFFFFFFH here) the harness patches from the environment's ntdll capture (as T3 / T4). Status 2FH when
; BeingDebugged is 0 and TEB Self = the TEB (gs:[30H] read as the linear address of the TEB), 101H / 102H otherwise. The values of
; NtGlobalFlag and KdDebuggerEnabled are read but not judged (the machine's boot / global-flag configuration decides them natively).

TP_EXIT_CODE    equ     2Fh

.CONST
                ALIGN   16
tp_marker       db      'TPSYSNUM'          ; the harness finds the patch field by this marker (exactly once)
tp_sysnum       dd      0FFFFFFFFh          ; NtTerminateProcess service number, patched per environment
                dd      0

.CODE
tp_entry PROC
                push    rbx
                xor     eax, eax
                xor     ecx, ecx
                cpuid                               ; leaf 0
                mov     eax, 1
                xor     ecx, ecx
                cpuid                               ; leaf 1
                mov     eax, 7
                xor     ecx, ecx
                cpuid                               ; leaf 7, sub-leaf 0
                mov     eax, 40000000h
                xor     ecx, ecx
                cpuid                               ; leaf 40000000H
                rdtsc
                rdtsc
                rdtscp
                pop     rbx
                mov     r8, qword ptr gs:[60h]      ; TEB ProcessEnvironmentBlock
                mov     r9, qword ptr gs:[30h]      ; TEB NtTib.Self
                movzx   eax, byte ptr [r8+2]        ; PEB BeingDebugged
                mov     ecx, dword ptr [r8+0BCh]    ; PEB NtGlobalFlag (read, not judged)
                mov     ecx, 7FFE02D4h
                movzx   ecx, byte ptr [rcx]         ; KUSER KdDebuggerEnabled (read, not judged)
                mov     edx, 101h
                test    eax, eax
                jnz     done
                mov     edx, 102h
                cmp     r9, qword ptr gs:[30h]      ; (a second Self read: the same value)
                jne     done
                mov     edx, TP_EXIT_CODE
done:           mov     eax, dword ptr [tp_sysnum]  ; RIP-relative: the patched service number
                mov     rcx, -1                     ; NtCurrentProcess()
                mov     r10, rcx
                syscall
                ud2
tp_entry ENDP

END
