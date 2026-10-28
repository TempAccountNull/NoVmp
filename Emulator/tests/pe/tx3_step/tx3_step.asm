; tx3_step.asm - test PE TX3 (plan 3.M7, P49, ledger U2240-U2279): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (AGENT_RULES "Host safety"); the harness may run it
; natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; What it checks: SINGLE-STEP (EFLAGS.TF) traps through Windows' exception dispatch, in particular over REP string instructions - the
; SDM (253669-093 Vol3B 19.2.4 / 19.3.1.4, single-step trap; Vol2B REP/REPE/REPZ/REPNE/REPNZ) has the trap delivered after each
; iteration with RIP still at the instruction until the last one. A vectored handler (kernel32 AddVectoredExceptionHandler, First 1)
; records every STATUS_SINGLE_STEP event: ExceptionAddress, CONTEXT.Rip / Rcx / Rsi (image-relative) / EFlags / Dr6 / ContextFlags, sets
; EFLAGS.TF again in the CONTEXT while the run is stepping (Windows may hand it over cleared) and continues (EXCEPTION_CONTINUE_EXECUTION).
; Stepped sequence (after POPFQ sets TF): MOV ECX, 3 / REP MOVSB / XOR ECX, ECX / REP STOSB (RCX 0) / MOV ECX, 4 / REPE CMPSB (stops after
; the 2nd iteration) / PUSHFQ / POP RAX (the TF image a program sees) / MOV ECX, 2 / REP STOSQ / PUSH RAX x 8 / ADD RSP, 40H / the end
; (TF cleared by the handler).
; Exit status: 63H << 24 | the folded hash of every observation (tx_common.inc). kernel32.lib only (no CRT, no TLS).

extrn   __imp_AddVectoredExceptionHandler:qword

include tx_common.inc

.DATA
                ALIGN   16
tx3_stepping    dq      0
tx3_count       dq      0
tx3_pushed      dq      0                               ; RFLAGS as PUSHFQ stored it while stepping
tx3_frsp        dq      0                               ; RSP of tx3_run's body
tx3_src         db      'ABCDEFGH', 0, 0, 0, 0, 0, 0, 0, 0
tx3_dst         db      16 dup (0)
tx3_s1          db      'WXYZ'
tx3_s2          db      'WQYZ'
tx3_q           dq      2 dup (0)

.CODE
tx3_tstart      db      'TX3 start', 0
tx3_tev         db      'TX3 step', 0
tx3_tother      db      'TX3 other', 0
tx3_tend        db      'TX3 end', 0
tx3_tn          db      'n', 0
tx3_tpushed     db      'pushfq-image', 0
tx3_tdst        db      'dst', 0
tx3_tq          db      'q', 0
tx3_trsi        db      'rsi', 0
tx3_trdi        db      'rdi', 0
tx3_trcx        db      'rcx', 0

; the vectored handler: LONG ( PEXCEPTION_POINTERS RCX )
tx3_veh PROC FRAME
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                mov     rbx, [rcx]                      ; the record
                mov     rsi, [rcx + 8]                  ; the context
                inc     tx3_count
                lea     rcx, tx3_tother
                cmp     dword ptr [rbx], 80000004h      ; STATUS_SINGLE_STEP
                jne     tx3_v_rec
                lea     rcx, tx3_tev
tx3_v_rec:      mov     r9, rcx
                mov     rcx, rbx
                mov     rdx, rsi
                mov     r8, tx3_frsp
                call    tx_rec_exc
                lea     rcx, tx3_tn
                mov     rdx, tx3_count
                call    tx_rec
                mov     rcx, [rsi + 0A8h]               ; CONTEXT.Rsi
                call    tx_ibrel
                lea     rcx, tx3_trsi
                mov     rdx, rax
                call    tx_rec
                mov     rcx, [rsi + 0B0h]               ; CONTEXT.Rdi
                call    tx_ibrel
                lea     rcx, tx3_trdi
                mov     rdx, rax
                call    tx_rec
                call    tx_flush
                mov     eax, 0                          ; not ours: search on
                cmp     dword ptr [rbx], 80000004h
                jne     tx3_v_x
                and     dword ptr [rsi + 44h], NOT 100h
                cmp     tx3_stepping, 1
                jne     tx3_v_cont
                cmp     tx3_count, 64                   ; a bound: never step forever
                ja      tx3_v_cont
                or      dword ptr [rsi + 44h], 100h     ; EFLAGS.TF for the next instruction
tx3_v_cont:     mov     eax, -1                         ; EXCEPTION_CONTINUE_EXECUTION
tx3_v_x:        add     rsp, 28h
                pop     rsi
                pop     rbx
                ret
tx3_veh ENDP

tx3_run PROC FRAME
                push    rsi
                .pushreg rsi
                push    rdi
                .pushreg rdi
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                lea     rsi, tx3_src
                lea     rdi, tx3_dst
                mov     tx3_stepping, 1
                xor     eax, eax                        ; RAX / RDX: fixed values in the stepped CONTEXTs
                xor     edx, edx
                mov     tx3_frsp, rsp
                pushfq
                or      qword ptr [rsp], 100h
                popfq                                   ; TF = 1: the trap follows the NEXT instruction (Vol3B 19.3.1.4)
                mov     ecx, 3
                rep movsb
                xor     ecx, ecx
                rep stosb
                mov     ecx, 4
                lea     rsi, tx3_s1
                lea     rdi, tx3_s2
                repe cmpsb
                pushfq
                pop     rax
                mov     tx3_pushed, rax
                mov     ecx, 2
                lea     rdi, tx3_q
                mov     rax, 1111111111111111h
                rep stosq
                push    rax                             ; 8 steps with RSP 8 apart: the frame place for each RSP AND 38H
                push    rax
                push    rax
                push    rax
                push    rax
                push    rax
                push    rax
                push    rax
                lea     rsp, [rsp + 40h]                ; no flags change (ADD would set PF from the stack address)
                mov     tx3_stepping, 0                 ; the handler clears TF at the next trap
                nop
                nop
                mov     [rsp + 20h], rcx
                lea     rcx, tx3_tend
                call    tx_puts
                lea     rcx, tx3_trcx
                mov     rdx, [rsp + 20h]
                call    tx_rec
                lea     rcx, tx3_tpushed
                mov     rdx, tx3_pushed
                call    tx_rec
                lea     rcx, tx3_tdst
                mov     rdx, qword ptr tx3_dst
                call    tx_rec
                lea     rcx, tx3_tq
                mov     rdx, tx3_q
                xor     rdx, tx3_q + 8
                call    tx_rec
                lea     rcx, tx3_tn
                mov     rdx, tx3_count
                call    tx_rec
                call    tx_flush
                add     rsp, 28h
                pop     rdi
                pop     rsi
                ret
tx3_run ENDP

tx3_entry PROC FRAME
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                lea     rcx, tx3_tstart
                call    tx_puts
                call    tx_flush
                mov     ecx, 1
                lea     rdx, tx3_veh
                call    qword ptr [__imp_AddVectoredExceptionHandler]
                call    tx3_run
                mov     ecx, 63h
                call    tx_exit
tx3_entry ENDP

END
