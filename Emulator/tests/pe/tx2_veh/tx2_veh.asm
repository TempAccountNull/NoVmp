; tx2_veh.asm - test PE TX2 (plan 3.M7, P49, ledger U2240-U2279): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (AGENT_RULES "Host safety"); the harness may run it
; natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; What it checks: VECTORED EXCEPTION HANDLING (kernel32 AddVectoredExceptionHandler / RemoveVectoredExceptionHandler /
; AddVectoredContinueHandler / RemoveVectoredContinueHandler - Windows SDK um\errhandlingapi.h; Learn "Vectored Exception Handling":
; the handlers are called in list order, First != 0 puts a handler at the head, before the frame-based handlers) and its order against
; structured exception handling (tx2_raise has an exception handler in its unwind data) and the continue handlers:
;   handlers registered: C (First 1), A (First 1, registered before C), B (First 0); continue handler X (First 0)
;   E1  UD2, B returns EXCEPTION_CONTINUE_EXECUTION (-1)                    -> the order of the calls
;   E2  UD2, every vectored handler EXCEPTION_CONTINUE_SEARCH (0), the frame handler S ExceptionContinueExecution
;   E3  after RemoveVectoredExceptionHandler( C ): as E1
;   E4  after removing A, B and X (and a second removal of A, which must fail): only S
; Each handler appends its letter to the event's order, records the EXCEPTION_RECORD / CONTEXT it got the first time (tx_rec_exc) and
; whether every later handler of the event got the SAME record and context (pointer distance to the first). Every handler that
; continues sets CONTEXT.Rip to the resume label and CONTEXT.Rsp to the recorded RSP.
; Exit status: 62H << 24 | the folded hash of every observation (tx_common.inc). kernel32.lib only (no CRT, no TLS).

extrn   __imp_AddVectoredExceptionHandler:qword
extrn   __imp_RemoveVectoredExceptionHandler:qword
extrn   __imp_AddVectoredContinueHandler:qword
extrn   __imp_RemoveVectoredContinueHandler:qword

include tx_common.inc

.DATA
                ALIGN   16
tx2_resume      dq      0
tx2_frsp        dq      0
tx2_mode        dq      0                               ; 1: B continues the execution; 0: B searches on
tx2_order       dq      0                               ; the letters of this event, first in the low byte
tx2_oshift      dq      0
tx2_rec0        dq      0                               ; the record / context of the event's first handler call
tx2_ctx0        dq      0
tx2_same        dq      0                               ; OR of every later call's ( record - record0 ) | ( context - context0 )
tx2_ha          dq      0
tx2_hb          dq      0
tx2_hc          dq      0
tx2_hx          dq      0
tx2_tag         dq      0

.CODE
tx2_tstart      db      'TX2 start', 0
tx2_tE1         db      'TX2 E1', 0
tx2_tE2         db      'TX2 E2', 0
tx2_tE3         db      'TX2 E3', 0
tx2_tE4         db      'TX2 E4', 0
tx2_tord        db      'order', 0
tx2_tsame       db      'same', 0
tx2_treg        db      'TX2 registered', 0
tx2_trem        db      'TX2 removed', 0
tx2_tnz         db      'handles-nonzero', 0
tx2_trc         db      'rm.c', 0
tx2_tra         db      'rm.a', 0
tx2_trb         db      'rm.b', 0
tx2_trx         db      'rm.x', 0
tx2_tra2        db      'rm.a-again', 0
tx2_tnone       db      ' none', 0

; tx2_note: RCX = the letter, RDX = PEXCEPTION_RECORD, R8 = PCONTEXT - append the letter; the first call of an event records the
; event (tx_rec_exc), later calls compare the pointers. Non-leaf.
tx2_note PROC FRAME
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                push    rdi
                .pushreg rdi
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                mov     rsi, rdx
                mov     rdi, r8
                mov     rax, rcx
                mov     rcx, tx2_oshift
                shl     rax, cl
                or      tx2_order, rax
                add     tx2_oshift, 8
                cmp     tx2_rec0, 0
                jne     tx2_n_later
                mov     tx2_rec0, rsi
                mov     tx2_ctx0, rdi
                mov     rcx, rsi
                mov     rdx, rdi
                mov     r8, tx2_frsp
                mov     r9, tx2_tag
                call    tx_rec_exc
                jmp     tx2_n_x
tx2_n_later:    mov     rax, rsi
                sub     rax, tx2_rec0
                mov     rdx, rdi
                sub     rdx, tx2_ctx0
                or      rax, rdx
                or      tx2_same, rax
tx2_n_x:        add     rsp, 20h
                pop     rdi
                pop     rsi
                pop     rbx
                ret
tx2_note ENDP

; tx2_cont: CONTEXT (RCX) Rip := the resume label, Rsp := the recorded RSP. Leaf.
tx2_cont PROC
                mov     rax, tx2_resume
                mov     [rcx + 0F8h], rax
                mov     rax, tx2_frsp
                mov     [rcx + 98h], rax
                ret
tx2_cont ENDP

; the vectored handlers: LONG CALLBACK ( PEXCEPTION_POINTERS RCX ) - {ExceptionRecord +0, ContextRecord +8} (winnt.h)
TX2_VEH MACRO name, letter, continues
name PROC FRAME
                push    rbx
                .pushreg rbx
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                mov     rbx, rcx
                mov     ecx, letter
                mov     rdx, [rbx]
                mov     r8, [rbx + 8]
                call    tx2_note
                xor     eax, eax                        ; EXCEPTION_CONTINUE_SEARCH
IF continues
                cmp     tx2_mode, 1
                jne     @F
                mov     rcx, [rbx + 8]
                call    tx2_cont
                mov     eax, -1                         ; EXCEPTION_CONTINUE_EXECUTION
@@:
ENDIF
                add     rsp, 20h
                pop     rbx
                ret
name ENDP
ENDM
                TX2_VEH tx2_veh_a, 'a', 0
                TX2_VEH tx2_veh_b, 'b', 1
                TX2_VEH tx2_veh_c, 'c', 0
                TX2_VEH tx2_vch_x, 'x', 0

; the frame handler of tx2_raise (EXCEPTION_ROUTINE, winnt.h): RCX record, RDX EstablisherFrame, R8 context, R9 DispatcherContext
tx2_seh_s PROC FRAME
                push    rbx
                .pushreg rbx
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                mov     eax, 1                          ; ExceptionContinueSearch while unwinding
                test    dword ptr [rcx + 4], 66h
                jnz     tx2_s_x
                mov     rbx, r8
                mov     rdx, rcx
                mov     r8, rbx
                mov     ecx, 's'
                call    tx2_note
                mov     rcx, rbx
                call    tx2_cont
                xor     eax, eax                        ; ExceptionContinueExecution
tx2_s_x:        add     rsp, 20h
                pop     rbx
                ret
tx2_seh_s ENDP

; tx2_raise: RCX = the event's tag - one UD2 under its own exception handler; prints the event's line
tx2_raise PROC FRAME:tx2_seh_s
                push    rbx
                .pushreg rbx
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                mov     tx2_tag, rcx
                mov     tx2_order, 0
                mov     tx2_oshift, 0
                mov     tx2_rec0, 0
                mov     tx2_ctx0, 0
                mov     tx2_same, 0
                lea     rax, tx2_r1
                mov     tx2_resume, rax
                mov     tx2_frsp, rsp
                mov     ebx, 5A5A0E00h
                xor     ecx, ecx                        ; RCX / RDX: fixed values
                xor     edx, edx
                cmp     ebx, ebx                        ; RFLAGS: a fixed state whatever ran before
                mov     eax, 0E2h
                ud2
                mov     rcx, tx2_tag                    ; nothing raised
                call    tx_puts
                lea     rcx, tx2_tnone
                call    tx_puts
tx2_r1:         lea     rcx, tx2_tord
                mov     rdx, tx2_order
                call    tx_rec
                lea     rcx, tx2_tsame
                mov     rdx, tx2_same
                call    tx_rec
                lea     rcx, tx_tag_rax                 ; RBX after the resume (5A5A0E00H)
                mov     rdx, rbx
                call    tx_rec
                call    tx_flush
                add     rsp, 20h
                pop     rbx
                ret
tx2_raise ENDP

tx2_entry PROC FRAME
                push    rbx
                .pushreg rbx
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                lea     rcx, tx2_tstart
                call    tx_puts
                call    tx_flush
                mov     ecx, 1                          ; A at the head
                lea     rdx, tx2_veh_a
                call    qword ptr [__imp_AddVectoredExceptionHandler]
                mov     tx2_ha, rax
                xor     ecx, ecx                        ; B at the tail
                lea     rdx, tx2_veh_b
                call    qword ptr [__imp_AddVectoredExceptionHandler]
                mov     tx2_hb, rax
                mov     ecx, 1                          ; C at the head (before A)
                lea     rdx, tx2_veh_c
                call    qword ptr [__imp_AddVectoredExceptionHandler]
                mov     tx2_hc, rax
                xor     ecx, ecx                        ; X: a continue handler
                lea     rdx, tx2_vch_x
                call    qword ptr [__imp_AddVectoredContinueHandler]
                mov     tx2_hx, rax
                lea     rcx, tx2_treg
                call    tx_puts
                xor     edx, edx                        ; every handle non-zero: 1
                cmp     tx2_ha, 0
                setne   dl
                xor     eax, eax
                cmp     tx2_hb, 0
                setne   al
                and     edx, eax
                cmp     tx2_hc, 0
                setne   al
                and     edx, eax
                cmp     tx2_hx, 0
                setne   al
                and     edx, eax
                lea     rcx, tx2_tnz
                call    tx_rec
                call    tx_flush
                mov     tx2_mode, 1
                lea     rcx, tx2_tE1
                call    tx2_raise
                mov     tx2_mode, 0
                lea     rcx, tx2_tE2
                call    tx2_raise
                mov     rcx, tx2_hc
                call    qword ptr [__imp_RemoveVectoredExceptionHandler]
                mov     ebx, eax
                lea     rcx, tx2_trem
                call    tx_puts
                lea     rcx, tx2_trc
                mov     edx, ebx
                call    tx_rec
                call    tx_flush
                mov     tx2_mode, 1
                lea     rcx, tx2_tE3
                call    tx2_raise
                lea     rcx, tx2_trem
                call    tx_puts
                mov     rcx, tx2_ha
                call    qword ptr [__imp_RemoveVectoredExceptionHandler]
                lea     rcx, tx2_tra
                mov     edx, eax
                call    tx_rec
                mov     rcx, tx2_hb
                call    qword ptr [__imp_RemoveVectoredExceptionHandler]
                lea     rcx, tx2_trb
                mov     edx, eax
                call    tx_rec
                mov     rcx, tx2_hx
                call    qword ptr [__imp_RemoveVectoredContinueHandler]
                lea     rcx, tx2_trx
                mov     edx, eax
                call    tx_rec
                mov     rcx, tx2_ha
                call    qword ptr [__imp_RemoveVectoredExceptionHandler]
                lea     rcx, tx2_tra2
                mov     edx, eax
                call    tx_rec
                call    tx_flush
                mov     tx2_mode, 1
                lea     rcx, tx2_tE4
                call    tx2_raise
                mov     ecx, 62h
                call    tx_exit
tx2_entry ENDP

END
