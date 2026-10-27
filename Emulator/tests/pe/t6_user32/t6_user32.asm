; t6_user32.asm - test PE T6 (P44, ledger U2114; notes\eread\P44_user32_csr.md): self-written 64-bit guest code, never derived from the
; sample. GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (emu-oep maps it as data, AGENT_RULES "Host
; safety"); the harness may run it natively in the VMware guest as a labelled reference only (decision 3.0.7), started normally
; (CreateProcessW without creation flags: tm_nt run.normal).
;
; What it is: an ordinary GUI-subsystem program that links user32.dll (USER DECISION 2026-10-27 STEP 3): the loader runs user32's DLL
; initialization (its CSR server connection, the win32k / gdi32 start) before the entry point - Windows' own user32 code does that, T6
; does nothing for it. T6 then calls ONE simple documented user32 function that needs no window station interaction beyond the
; initialization:
;   CharUpperW( t6_text ) with t6_text = L"t6 user32" (a pointer, not a single character: the string is converted in place and the
;   pointer returned - Windows SDK um\winuser.h CharUpperW; Learn "CharUpperW function")
;   - the return value = the pointer                                                                              else exit 101H
;   - the string now L"T6 USER32" (9 characters + NUL)                                                            else exit 102H
; then ExitProcess( 36H ) - its observable result, independent of the machine (no system metric, no handle value).
; Prototypes: Windows SDK 10.0.26100.0 um\winuser.h CharUpperW, um\processthreadsapi.h ExitProcess.

T6_EXIT_CODE    equ     36h

extrn   __imp_CharUpperW:qword
extrn   __imp_ExitProcess:qword

.CONST
                ALIGN   2
t6_want         dw      'T', '6', ' ', 'U', 'S', 'E', 'R', '3', '2', 0
t6_len          equ     10                              ; characters incl. the NUL

.DATA
                ALIGN   8
t6_text         dw      't', '6', ' ', 'u', 's', 'e', 'r', '3', '2', 0

.CODE
t6_entry PROC
                push    rbx
                sub     rsp, 20h                        ; shadow space; RSP 16-aligned at the CALL (entry RSP = 8 mod 16)
                lea     rbx, t6_text
                mov     rcx, rbx                        ; lpsz = the string (its high word non-zero: a pointer)
                call    qword ptr [__imp_CharUpperW]
                cmp     rax, rbx
                jne     fail1
                lea     rsi, t6_text
                lea     rdi, t6_want
                mov     ecx, t6_len
                repe cmpsw
                jne     fail2
                mov     ecx, T6_EXIT_CODE
                jmp     done
fail1:          mov     ecx, 101h
                jmp     done
fail2:          mov     ecx, 102h
done:           call    qword ptr [__imp_ExitProcess]   ; does not return
                ud2
t6_entry ENDP

END
