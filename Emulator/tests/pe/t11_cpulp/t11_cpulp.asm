; t11_cpulp.asm - test PE T11 (P57 step 4, ledger U2375): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (emu-oep maps it as data, AGENT_RULES "Host safety");
; the harness runs it natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; What it measures (USER DECISION 2026-10-30, plan 3.M7.a P56 (4) "per-processor CPUID"; the sample's run 19 pins itself to each processor
; with NtSetInformationThread 4 and reads CPUID leaf 1 there): for every processor of the process affinity mask, in order, the thread is
; pinned to that ONE processor (SetThreadAffinityMask( GetCurrentThread(), 1 << n ): um\winbase.h; it returns the previous mask, 0 =
; failure) and executes CPUID for the leaves whose fields have the Domain "Logical Processor" / "Core" in SDM 253665-093 Vol1 Chapter 21:
;   01H.0 (EBX[31:24] Initial APIC ID, pp. 21-9 / 21-10), 04H.0 - 04H.3 (p. 21-21), 06H.0, 0BH.0 - 0BH.2 (x2APIC ID, p. 21-39),
;   1AH.0 (CORE_TYPE, p. 21-66), 1FH.0 - 1FH.2 (p. 21-71)
; One text line per (processor, leaf): "t11 lp N LL.SS EAX EBX ECX EDX" (hex) to the standard output through WriteFile (um\fileapi.h; the
; emulated run: the console capture holds it, P41), then the affinity is set back to the process mask. CPUID values only: no identifier
; (the leaves above hold no serial number; leaf 03H is not read). The exit status: 3B000000H | (FNV-1a 32 of every register value, low
; 24 bits) - one number the native and the emulated runs compare; E1000000H | n = SetThreadAffinityMask failed for processor n,
; E2000000H = GetProcessAffinityMask failed. Linked with kernel32.lib only (no CRT, no TLS).

extrn   __imp_GetProcessAffinityMask:qword
extrn   __imp_SetThreadAffinityMask:qword
extrn   __imp_GetStdHandle:qword
extrn   __imp_WriteFile:qword
extrn   __imp_ExitProcess:qword

.DATA
                ALIGN   16
t11_leaves      dd      01h, 0, 04h, 0, 04h, 1, 04h, 2, 04h, 3, 06h, 0, 0Bh, 0, 0Bh, 1, 0Bh, 2, 1Ah, 0, 1Fh, 0, 1Fh, 1, 1Fh, 2
T11_NLEAVES     equ     13
t11_pam         dq      0
t11_sam         dq      0
t11_hash        dd      811C9DC5h                       ; FNV-1a 32 offset basis
t11_written     dd      0
t11_hex         db      '0123456789ABCDEF'
t11_line        db      't11 lp '
t11_lpdec       db      '00 '
t11_ll          db      '00.'
t11_ss          db      '00 '
t11_eax         db      '00000000 '
t11_ebx         db      '00000000 '
t11_ecx         db      '00000000 '
t11_edx         db      '00000000', 13, 10
T11_LINE_LEN    equ     $ - t11_line

.CODE
; t11_hex8: write EAX as 8 hex digits at RDI (RDI advanced by 8); clobbers RCX, RDX
t11_hex8 PROC
                mov     ecx, 8
t11_h8_loop:    rol     eax, 4
                mov     edx, eax
                and     edx, 0Fh
                lea     r11, t11_hex
                mov     dl, byte ptr [r11 + rdx]
                mov     byte ptr [rdi], dl
                inc     rdi
                dec     ecx
                jnz     t11_h8_loop
                ret
t11_hex8 ENDP

; t11_hex2: write AL as 2 hex digits at RDI; clobbers RDX
t11_hex2 PROC
                movzx   edx, al
                shr     edx, 4
                lea     r11, t11_hex
                mov     dl, byte ptr [r11 + rdx]
                mov     byte ptr [rdi], dl
                movzx   edx, al
                and     edx, 0Fh
                mov     dl, byte ptr [r11 + rdx]
                mov     byte ptr [rdi + 1], dl
                ret
t11_hex2 ENDP

; t11_fnv: fold EAX (4 bytes, low first) into t11_hash; clobbers RCX, RDX
t11_fnv PROC
                mov     edx, t11_hash
                mov     ecx, 4
t11_fnv_loop:   xor     dl, al
                imul    edx, edx, 01000193h             ; FNV-1a 32 prime
                shr     eax, 8
                dec     ecx
                jnz     t11_fnv_loop
                mov     t11_hash, edx
                ret
t11_fnv ENDP

t11_entry PROC
                push    rbx
                push    rsi
                push    rdi
                push    r12
                push    r13
                push    r14
                push    r15
                sub     rsp, 40h                        ; 7 pushes + 40H: RSP 16-aligned at each CALL (entry RSP = 8 mod 16)
                mov     rcx, -1                         ; GetCurrentProcess() pseudo handle
                lea     rdx, t11_pam
                lea     r8, t11_sam
                call    qword ptr [__imp_GetProcessAffinityMask]
                test    eax, eax
                jnz     t11_ok_pam
                mov     ecx, 0E2000000h
                jmp     t11_exit
t11_ok_pam:     mov     ecx, -11                        ; STD_OUTPUT_HANDLE
                call    qword ptr [__imp_GetStdHandle]
                mov     r15, rax                        ; the standard output (0 / INVALID_HANDLE_VALUE when there is none: WriteFile fails)
                xor     r12d, r12d                      ; n = the processor number
t11_lp_loop:    cmp     r12d, 64
                jae     t11_done
                mov     rax, t11_pam
                bt      rax, r12
                jnc     t11_lp_next
                mov     edx, 1
                mov     ecx, r12d
                shl     rdx, cl                         ; dwThreadAffinityMask = 1 << n
                mov     rcx, -2                         ; GetCurrentThread() pseudo handle
                call    qword ptr [__imp_SetThreadAffinityMask]
                test    rax, rax
                jnz     t11_pinned
                mov     ecx, r12d
                or      ecx, 0E1000000h
                jmp     t11_exit
t11_pinned:     ; the processor number as 2 decimal digits
                mov     eax, r12d
                xor     edx, edx
                mov     ecx, 10
                div     ecx
                add     al, '0'
                add     dl, '0'
                mov     byte ptr t11_lpdec, al
                mov     byte ptr t11_lpdec + 1, dl
                lea     r13, t11_leaves
                mov     r14d, T11_NLEAVES
t11_leaf_loop:  mov     eax, dword ptr [r13]
                mov     ecx, dword ptr [r13 + 4]
                cpuid
                mov     dword ptr [rsp + 28h], eax
                mov     dword ptr [rsp + 2Ch], ebx
                mov     dword ptr [rsp + 30h], ecx
                mov     dword ptr [rsp + 34h], edx
                lea     rdi, t11_ll
                mov     eax, dword ptr [r13]
                call    t11_hex2
                lea     rdi, t11_ss
                mov     eax, dword ptr [r13 + 4]
                call    t11_hex2
                lea     rdi, t11_eax
                xor     ebx, ebx
t11_reg_loop:   mov     eax, dword ptr [rsp + 28h + rbx * 4]
                call    t11_hex8
                inc     rdi                             ; the separator
                mov     eax, dword ptr [rsp + 28h + rbx * 4]
                call    t11_fnv
                inc     ebx
                cmp     ebx, 4
                jb      t11_reg_loop
                mov     rcx, r15                        ; WriteFile( stdout, line, len, &written, NULL )
                lea     rdx, t11_line
                mov     r8d, T11_LINE_LEN
                lea     r9, t11_written
                mov     qword ptr [rsp + 20h], 0
                call    qword ptr [__imp_WriteFile]
                add     r13, 8
                dec     r14d
                jnz     t11_leaf_loop
t11_lp_next:    inc     r12d
                jmp     t11_lp_loop
t11_done:       mov     rcx, -2                         ; back to the process mask
                mov     rdx, t11_pam
                call    qword ptr [__imp_SetThreadAffinityMask]
                mov     ecx, t11_hash
                and     ecx, 00FFFFFFh
                or      ecx, 3B000000h
t11_exit:       call    qword ptr [__imp_ExitProcess]
                int     3
t11_entry ENDP

END
