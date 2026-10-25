; t2_import.asm - test PE T2 of plan 3.M5 (ledger U2015; notes\eread\P35_phase3_m5.md): self-written 64-bit guest code,
; never derived from the sample. GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (emu-oep
; maps it as data, AGENT_RULES "Host safety"); the harness may run it natively in the VMware guest as a labelled reference only
; (decision 3.0.7).
;
; What it checks (imports from kernel32.dll, no CRT, no TLS): GetCurrentProcessId() equals the TEB's ClientId.UniqueProcess
; (gs:[40H]), GetModuleHandleW( NULL ) equals the PEB's ImageBaseAddress (gs:[60H] -> [+10H]); then ExitProcess( 2BH ). A failed
; check exits with 101H / 102H instead. Under emu-oep --HLE the imports are bound to the emulator's API stubs (plan 6.1);
; under --Sys the image is refused (the real ntdll!LdrInitializeThunk start is not modelled in M5).
; TEB / PEB offsets: Windows SDK / WDK 10.0.26100.0 shared\ksamd64.inc TeClientId 40H, TePeb 60H; PEB ImageBaseAddress 10H
; (the layouts of emu::win, checked by check_win_layout.py). Prototypes: um\processthreadsapi.h, um\libloaderapi.h.

T2_EXIT_CODE    equ     2Bh

extrn   __imp_GetCurrentProcessId:qword
extrn   __imp_GetModuleHandleW:qword
extrn   __imp_ExitProcess:qword

.CODE
t2_entry PROC
                sub     rsp, 28h                        ; shadow space, RSP 16-aligned at the CALLs (entry RSP = 8 mod 16)
                call    qword ptr [__imp_GetCurrentProcessId]
                mov     rcx, qword ptr gs:[40h]         ; TEB ClientId.UniqueProcess
                cmp     eax, ecx
                jne     fail1
                xor     ecx, ecx                        ; lpModuleName = NULL: the executable
                call    qword ptr [__imp_GetModuleHandleW]
                mov     rcx, qword ptr gs:[60h]         ; TEB ProcessEnvironmentBlock
                cmp     rax, qword ptr [rcx+10h]        ; PEB ImageBaseAddress
                jne     fail2
                mov     ecx, T2_EXIT_CODE
                jmp     done
fail1:          mov     ecx, 101h
                jmp     done
fail2:          mov     ecx, 102h
done:           call    qword ptr [__imp_ExitProcess]   ; does not return
                ud2
t2_entry ENDP

END
