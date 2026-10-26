; t5_stdout.asm - test PE T5 of plan 3.M5 (P41, ledger U2085; notes\eread\P41_console_frontier.md): self-written 64-bit guest code,
; never derived from the sample. GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (emu-oep maps it
; as data, AGENT_RULES "Host safety"); the harness may run it natively in the VMware guest as a labelled reference only (decision
; 3.0.7), and only STARTED WITHOUT A CONSOLE (CreateProcessW with DETACHED_PROCESS through tm_nt run.detached).
;
; What it does: it "prints" to its standard output the way a console program does, and checks the results a process WITHOUT a console
; gets (USER DECISION 2026-10-26: the emulated process is the model of a detached process; measured natively by tm_nt con.*):
;   1. GetStdHandle( STD_OUTPUT_HANDLE ) = 0 (no standard handles; tm_nt con.std.1)                               else exit 101H
;   2. WriteFile( that handle, "t5_stdout: hello from WriteFile" + CR LF (33 bytes), &written, NULL ) = FALSE, written 0       else exit 102H
;      and GetLastError() = 6 ERROR_INVALID_HANDLE (tm_nt con.api.wfile)                                          else exit 103H
;   3. WriteConsoleW( that handle, L"t5_stdout: hello from WriteConsoleW" + CR LF (37 characters), &written, NULL ) = FALSE         else exit 104H
;      and GetLastError() = 6 (tm_nt con.api.wcon)                                                              else exit 105H
;   then ExitProcess( 2FH ). The emulator logs both output ATTEMPTS (their text) in its console capture file (P41, observation only).
; Prototypes: Windows SDK 10.0.26100.0 um\processenv.h GetStdHandle, um\fileapi.h WriteFile, um\consoleapi.h WriteConsoleW,
; um\errhandlingapi.h GetLastError, um\processthreadsapi.h ExitProcess; STD_OUTPUT_HANDLE ((DWORD)-11) um\processenv.h,
; ERROR_INVALID_HANDLE 6 shared\winerror.h.

T5_EXIT_CODE    equ     2Fh
STD_OUTPUT      equ     -11

extrn   __imp_GetStdHandle:qword
extrn   __imp_WriteFile:qword
extrn   __imp_WriteConsoleW:qword
extrn   __imp_GetLastError:qword
extrn   __imp_ExitProcess:qword

.CONST
t5_text         db      't5_stdout: hello from WriteFile'
                db      13, 10
t5_text_len     equ     $ - t5_text
                ALIGN   2
t5_wtext        dw      't', '5', '_', 's', 't', 'd', 'o', 'u', 't', ':', ' ', 'h', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm', ' '
                dw      'W', 'r', 'i', 't', 'e', 'C', 'o', 'n', 's', 'o', 'l', 'e', 'W', 13, 10
t5_wtext_len    equ     ( $ - t5_wtext ) / 2

.DATA
                ALIGN   8
t5_written      dd      0CCCCCCCCh
                dd      0

.CODE
t5_entry PROC
                push    rbx
                sub     rsp, 30h                        ; shadow space + the 5th argument; RSP 16-aligned at the CALLs (entry RSP = 8 mod 16)
                mov     ecx, STD_OUTPUT
                call    qword ptr [__imp_GetStdHandle]
                mov     rbx, rax
                test    rax, rax
                jnz     fail1
                mov     rcx, rbx                        ; hFile = the standard output handle
                lea     rdx, t5_text                    ; lpBuffer
                mov     r8d, t5_text_len                ; nNumberOfBytesToWrite
                lea     r9, t5_written                  ; lpNumberOfBytesWritten
                mov     qword ptr [rsp+20h], 0          ; lpOverlapped = NULL
                call    qword ptr [__imp_WriteFile]
                test    eax, eax
                jnz     fail2
                cmp     dword ptr [t5_written], 0
                jne     fail2
                call    qword ptr [__imp_GetLastError]
                cmp     eax, 6
                jne     fail3
                mov     rcx, rbx                        ; hConsoleOutput
                lea     rdx, t5_wtext                   ; lpBuffer
                mov     r8d, t5_wtext_len               ; nNumberOfCharsToWrite
                lea     r9, t5_written                  ; lpNumberOfCharsWritten
                mov     qword ptr [rsp+20h], 0          ; lpReserved = NULL
                call    qword ptr [__imp_WriteConsoleW]
                test    eax, eax
                jnz     fail4
                call    qword ptr [__imp_GetLastError]
                cmp     eax, 6
                jne     fail5
                mov     ecx, T5_EXIT_CODE
                jmp     done
fail1:          mov     ecx, 101h
                jmp     done
fail2:          mov     ecx, 102h
                jmp     done
fail3:          mov     ecx, 103h
                jmp     done
fail4:          mov     ecx, 104h
                jmp     done
fail5:          mov     ecx, 105h
done:           call    qword ptr [__imp_ExitProcess]   ; does not return
                ud2
t5_entry ENDP

END
