; tx1_seh.asm - test PE TX1 (plan 3.M7, P49, ledger U2240-U2279): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (AGENT_RULES "Host safety"); the harness may run it
; natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; What it checks: STRUCTURED EXCEPTION HANDLING through the x64 unwind data (the function tx1_cases has an UNWIND_INFO with an exception
; handler, MASM PROC FRAME:tx1_handler) and the exception codes / ExceptionInformation / CONTEXT Windows delivers for each CPU event a
; user-mode program can raise:
;   e01 INT3 (CCH)            e02 INT 3 (CDH 03H)      e03 UD2                   e04 DIV by zero          e05 IDIV overflow (80000000H / -1)
;   e06 HLT (CPL 3)           e07 read of address 0    e08 write to .rdata       e09 execute in .data (NX) e10 read of a non-canonical address
;   e11 INT 4 (CDH 04H)       e12 INT1 / ICEBP (F1H)   e13 INT 2CH               e14 INT 1 (CDH 01H)       e15 IN AL, DX (CPL 3, IOPL 0)
;   e16 MOV RAX, CR0 (CPL 3)  e17 CLI (CPL 3, IOPL 0)  e18 INT 2DH (then STC: CF shows whether the next byte ran)
;   e19 STI   e20 OUT DX, AL   e21 RDMSR   e22 WBINVD   e23 MOV DR7, RCX   e24 INVLPG   e25 LGDT   e26 SWAPGS   e27 RDPMC   e28 INSB
;   e29 INT 41H (the CPL 3 #GP cases: which ones Windows reports as STATUS_PRIVILEGED_INSTRUCTION)
;   e30 / e31 / e32 / e33 / e34 / e35 INT 2DH with EAX 0 / 3 / 12345H / 2 / 4 / 5
; (the alignment check #AC is test PE TX6)
; For each, the handler (the language-specific handler of tx1_cases, called by RtlDispatchException for the frame) records the
; EXCEPTION_RECORD and the CONTEXT it got (tx_rec_exc), plus the EstablisherFrame and the DispatcherContext's ControlPc / ImageBase /
; EstablisherFrame, sets CONTEXT.Rip to the case's resume label and returns ExceptionContinueExecution (0); the context is restored by
; Windows (RtlRestoreContext / NtContinue). A case that raises nothing records "none". After the resume the test records RSP (it must
; equal the value before the instruction) and RBX (a value preserved across the event).
; Exit status: 61H << 24 | the folded hash of every observation (tx_common.inc).
; Linked with the SDK import library kernel32.lib only (no CRT, no TLS).

include tx_common.inc

.DATA
                ALIGN   16
tx1_resume      dq      0                               ; the current case's resume label
tx1_frsp        dq      0                               ; RSP right before the case's instruction
tx1_tag         dq      0                               ; the current case's tag string
tx1_code        db      0C3h, 0CCh, 0CCh, 0CCh          ; e09: a RET in a non-executable section
tx1_scratch     dq      0, 0                            ; e28: the INSB destination

.CONST
                ALIGN   16
tx1_ro          dq      1122334455667788h               ; e08: read-only data

.CODE
tx1_t01         db      'TX1 e01', 0
tx1_t02         db      'TX1 e02', 0
tx1_t03         db      'TX1 e03', 0
tx1_t04         db      'TX1 e04', 0
tx1_t05         db      'TX1 e05', 0
tx1_t06         db      'TX1 e06', 0
tx1_t07         db      'TX1 e07', 0
tx1_t08         db      'TX1 e08', 0
tx1_t09         db      'TX1 e09', 0
tx1_t10         db      'TX1 e10', 0
tx1_t11         db      'TX1 e11', 0
tx1_t12         db      'TX1 e12', 0
tx1_t13         db      'TX1 e13', 0
tx1_t14         db      'TX1 e14', 0
tx1_t15         db      'TX1 e15', 0
tx1_t16         db      'TX1 e16', 0
tx1_t17         db      'TX1 e17', 0
tx1_t18         db      'TX1 e18', 0
tx1_t19         db      'TX1 e19', 0
tx1_t20         db      'TX1 e20', 0
tx1_t21         db      'TX1 e21', 0
tx1_t22         db      'TX1 e22', 0
tx1_t23         db      'TX1 e23', 0
tx1_t24         db      'TX1 e24', 0
tx1_t25         db      'TX1 e25', 0
tx1_t26         db      'TX1 e26', 0
tx1_t27         db      'TX1 e27', 0
tx1_t28         db      'TX1 e28', 0
tx1_t29         db      'TX1 e29', 0
tx1_t30         db      'TX1 e30', 0
tx1_t31         db      'TX1 e31', 0
tx1_t32         db      'TX1 e32', 0
tx1_t33         db      'TX1 e33', 0
tx1_t34         db      'TX1 e34', 0
tx1_t35         db      'TX1 e35', 0
tx1_tnone       db      ' none', 0
tx1_tafter      db      'after', 0
tx1_trbx        db      'rbx', 0
tx1_testab      db      'est-at', 0
tx1_tdpc        db      'dc.pc', 0
tx1_tdib        db      'dc.ib', 0
tx1_tdest       db      'dc.est-est', 0
tx1_tdfe        db      'dc.fe', 0
tx1_tdctx       db      'dc.ctx-ctx', 0
tx1_tdlh        db      'dc.lh', 0
tx1_tstart      db      'TX1 start', 0
tx1_tend        db      'TX1 end', 0
tx1_tcount      db      'events', 0

.DATA
tx1_events      dq      0

.CODE
; the language-specific handler: EXCEPTION_DISPOSITION ( PEXCEPTION_RECORD RCX, ULONG64 EstablisherFrame RDX, PCONTEXT R8,
; PDISPATCHER_CONTEXT R9 ) - winnt.h EXCEPTION_ROUTINE; DISPATCHER_CONTEXT (winnt.h, _AMD64_): +0 ControlPc, +8 ImageBase, +10H FunctionEntry,
; +18H EstablisherFrame, +20H TargetIp, +28H ContextRecord, +30H LanguageHandler, +38H HandlerData
tx1_handler PROC FRAME
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                push    rdi
                .pushreg rdi
                push    r12
                .pushreg r12
                push    r13
                .pushreg r13
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                test    dword ptr [rcx + 4], 66h        ; EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND | TARGET_UNWIND | COLLIDED_UNWIND
                jz      tx1_h_disp
                mov     eax, 1                          ; ExceptionContinueSearch
                jmp     tx1_h_x
tx1_h_disp:     mov     rbx, rcx
                mov     rsi, r8
                mov     rdi, rdx
                mov     r12, r9
                inc     tx1_events
                mov     rdx, rsi
                mov     r8, tx1_frsp
                mov     r9, tx1_tag
                call    tx_rec_exc
                lea     rcx, tx1_testab                 ; EstablisherFrame - the recorded RSP
                mov     rdx, rdi
                sub     rdx, tx1_frsp
                call    tx_rec
                mov     rcx, [r12]                      ; DispatcherContext->ControlPc
                call    tx_ibrel
                lea     rcx, tx1_tdpc
                mov     rdx, rax
                call    tx_rec
                mov     rcx, [r12 + 8]                  ; ImageBase
                call    tx_ibrel
                lea     rcx, tx1_tdib
                mov     rdx, rax
                call    tx_rec
                lea     rcx, tx1_tdest                  ; DispatcherContext->EstablisherFrame - EstablisherFrame
                mov     rdx, [r12 + 18h]
                sub     rdx, rdi
                call    tx_rec
                mov     rcx, [r12 + 10h]                ; FunctionEntry (in .pdata)
                call    tx_ibrel
                lea     rcx, tx1_tdfe
                mov     rdx, rax
                call    tx_rec
                lea     rcx, tx1_tdctx                  ; DispatcherContext->ContextRecord - ContextRecord
                mov     rdx, [r12 + 28h]
                sub     rdx, rsi
                call    tx_rec
                mov     rcx, [r12 + 30h]                ; LanguageHandler (= tx1_handler)
                call    tx_ibrel
                lea     rcx, tx1_tdlh
                mov     rdx, rax
                call    tx_rec
                call    tx_flush
                mov     rax, tx1_resume
                mov     [rsi + 0F8h], rax               ; CONTEXT.Rip := the resume label
                mov     rax, tx1_frsp
                mov     [rsi + 98h], rax                ; CONTEXT.Rsp := the recorded RSP (e09 enters by CALL)
                xor     eax, eax                        ; ExceptionContinueExecution
tx1_h_x:        add     rsp, 20h
                pop     r13
                pop     r12
                pop     rdi
                pop     rsi
                pop     rbx
                ret
tx1_handler ENDP

; tx1_none: the case raised nothing - " none" on its line
tx1_none PROC FRAME
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                mov     rcx, tx1_tag
                call    tx_puts
                lea     rcx, tx1_tnone
                call    tx_puts
                mov     ecx, 0EEh
                call    tx_mix
                call    tx_flush
                add     rsp, 28h
                ret
tx1_none ENDP

; tx1_after: the state after the resume - RSP - the recorded RSP (0 expected), RBX (the case put 5A5A0000H + n there)
tx1_after PROC FRAME
                push    rbx
                .pushreg rbx
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                lea     rcx, tx1_tafter
                call    tx_puts
                lea     rcx, tx1_tagr
                lea     rdx, [rsp + 30h]                ; the caller's RSP before the CALL: ours + 20H + 8 (RBX) + 8 (return address)
                sub     rdx, tx1_frsp
                call    tx_rec
                lea     rcx, tx1_trbx
                mov     rdx, [rsp + 20h]                ; the caller's RBX (pushed above)
                call    tx_rec
                call    tx_flush
                add     rsp, 20h
                pop     rbx
                ret
tx1_after ENDP
tx1_tagr        db      'rsp-before', 0

; tx1_regs: " regs" + RAX, RCX, RDX as they are and CF of R11 (RFLAGS read by the caller; e18: after INT 2DH)
tx1_tregs       db      ' regs', 0
tx1_tcf         db      'cf', 0
tx1_regs PROC FRAME
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                push    rdi
                .pushreg rdi
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                mov     ebx, r11d
                and     ebx, 1                          ; CF (R11 = RFLAGS, read by the caller)
                mov     rsi, rax
                mov     rdi, rcx
                mov     [rsp + 18h], rdx
                mov     rcx, tx1_tag
                call    tx_puts
                lea     rcx, tx1_tregs
                call    tx_puts
                lea     rcx, tx_tag_rax
                mov     rdx, rsi
                call    tx_rec
                lea     rcx, tx_tag_rcx
                mov     rdx, rdi
                call    tx_rec
                lea     rcx, tx_tag_rdx
                mov     rdx, [rsp + 18h]
                call    tx_rec
                lea     rcx, tx1_tcf
                mov     rdx, rbx
                call    tx_rec
                call    tx_flush
                add     rsp, 20h
                pop     rdi
                pop     rsi
                pop     rbx
                ret
tx1_regs ENDP

; TX1_CASE n, tag: arm the handler for the next instruction (resume label tx1_rN), RBX := 5A5A0000H + n, RSP recorded
TX1_CASE MACRO n, tag
                mov     ebx, 5A5A0000h + n
                xor     ecx, ecx                        ; RCX / RDX: fixed values (a call before left whatever its code left there)
                xor     edx, edx
                cmp     ebx, ebx                        ; RFLAGS: a fixed state (ZF PF 1, CF SF OF AF 0) whatever ran before
                lea     rax, tag
                mov     tx1_tag, rax
                lea     rax, tx1_r&n
                mov     tx1_resume, rax
                mov     tx1_frsp, rsp
ENDM
; TX1_END n: the case raised nothing (fall-through) -> "none"; the resume label; the state after
TX1_END MACRO n
                call    tx1_none
tx1_r&n:
                call    tx1_after
ENDM

tx1_cases PROC FRAME:tx1_handler
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                push    rdi
                .pushreg rdi
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                TX1_CASE 1, tx1_t01
                mov     eax, 0A1h
                int     3                               ; CCH (MASM encodes INT 3 as the one-byte form)
                TX1_END 1
                TX1_CASE 2, tx1_t02
                mov     eax, 0A2h
                db      0CDh, 03h                       ; INT 3 in the two-byte form
                nop
                TX1_END 2
                TX1_CASE 3, tx1_t03
                mov     eax, 0A3h
                ud2
                TX1_END 3
                TX1_CASE 4, tx1_t04
                mov     eax, 1
                xor     edx, edx
                xor     ecx, ecx
                div     ecx
                TX1_END 4
                TX1_CASE 5, tx1_t05
                mov     eax, 80000000h
                cdq
                mov     ecx, -1
                idiv    ecx
                TX1_END 5
                TX1_CASE 6, tx1_t06
                mov     eax, 0A6h
                hlt
                TX1_END 6
                TX1_CASE 7, tx1_t07
                xor     ecx, ecx
                mov     eax, 0A7h
                mov     rax, [rcx]
                TX1_END 7
                TX1_CASE 8, tx1_t08
                lea     rcx, tx1_ro
                mov     eax, 0A8h
                mov     byte ptr [rcx], 1
                TX1_END 8
                TX1_CASE 9, tx1_t09
                lea     rcx, tx1_code
                mov     eax, 0A9h
                call    rcx                             ; a CALL: the return address lets the unwinder find tx1_cases' frame
                TX1_END 9
                TX1_CASE 10, tx1_t10
                mov     rcx, 8000000000000000h
                mov     eax, 0AAh
                mov     rax, [rcx]
                TX1_END 10
                TX1_CASE 11, tx1_t11
                mov     eax, 0ABh
                db      0CDh, 04h                       ; INT 4
                nop
                TX1_END 11
                TX1_CASE 12, tx1_t12
                mov     eax, 0ACh
                db      0F1h                            ; INT1 / ICEBP
                nop
                TX1_END 12
                TX1_CASE 13, tx1_t13
                mov     eax, 0ADh
                db      0CDh, 2Ch                       ; INT 2CH
                nop
                TX1_END 13
                TX1_CASE 14, tx1_t14
                mov     eax, 0AEh
                db      0CDh, 01h                       ; INT 1 (the two-byte form)
                nop
                TX1_END 14
                TX1_CASE 15, tx1_t15
                mov     edx, 80h
                mov     eax, 0AFh
                in      al, dx
                TX1_END 15
                TX1_CASE 16, tx1_t16
                mov     eax, 0B0h
                mov     rax, cr0
                TX1_END 16
                TX1_CASE 17, tx1_t17
                mov     eax, 0B1h
                cli
                TX1_END 17
                TX1_CASE 18, tx1_t18
                mov     eax, 1                          ; the debug-service number (BREAKPOINT_PRINT 1)
                xor     ecx, ecx
                xor     edx, edx
                clc
                db      0CDh, 2Dh                       ; INT 2DH
                stc                                     ; one byte: CF tells whether it ran (a resume past it would skip it)
                pushfq
                pop     r11                             ; RFLAGS for tx1_regs (its prologue changes the flags)
                call    tx1_regs                        ; RAX / RCX / RDX / CF after the instruction
                TX1_END 18
                TX1_CASE 19, tx1_t19
                mov     eax, 0C3h
                sti                              ; STI
                TX1_END 19
                TX1_CASE 20, tx1_t20
                mov     eax, 0C4h
                mov     edx, 80h
                out     dx, al                              ; OUT DX, AL
                TX1_END 20
                TX1_CASE 21, tx1_t21
                mov     eax, 0C5h
                mov     ecx, 10h
                rdmsr                              ; RDMSR (ECX 10H)
                TX1_END 21
                TX1_CASE 22, tx1_t22
                mov     eax, 0C6h
                wbinvd                              ; WBINVD
                TX1_END 22
                TX1_CASE 23, tx1_t23
                mov     eax, 0C7h
                xor     ecx, ecx
                mov     dr7, rcx                              ; MOV DR7, RAX
                TX1_END 23
                TX1_CASE 24, tx1_t24
                mov     eax, 0C8h
                lea     rcx, tx1_ro
                invlpg  byte ptr [rcx]                              ; INVLPG [RCX]
                TX1_END 24
                TX1_CASE 25, tx1_t25
                mov     eax, 0C9h
                lea     rcx, tx1_ro
                lgdt    fword ptr [rcx]                              ; LGDT [RCX]
                TX1_END 25
                TX1_CASE 26, tx1_t26
                mov     eax, 0CAh
                swapgs                              ; SWAPGS
                TX1_END 26
                TX1_CASE 27, tx1_t27
                mov     eax, 0CBh
                xor     ecx, ecx
                rdpmc                              ; RDPMC (ECX 0)
                TX1_END 27
                TX1_CASE 28, tx1_t28
                mov     eax, 0CCh
                mov     edx, 80h
                lea     rdi, tx1_scratch
                insb                              ; INSB
                TX1_END 28
                TX1_CASE 29, tx1_t29
                mov     eax, 0CDh
                db      0CDh, 41h
                nop                              ; INT 41H
                TX1_END 29
                TX1_CASE 30, tx1_t30
                mov     eax, 0h                         ; the debug-service number
                xor     ecx, ecx
                xor     edx, edx
                clc
                db      0CDh, 2Dh                       ; INT 2DH
                stc
                pushfq
                pop     r11
                call    tx1_regs
                TX1_END 30
                TX1_CASE 31, tx1_t31
                mov     eax, 3h                         ; the debug-service number
                xor     ecx, ecx
                xor     edx, edx
                clc
                db      0CDh, 2Dh                       ; INT 2DH
                stc
                pushfq
                pop     r11
                call    tx1_regs
                TX1_END 31
                TX1_CASE 32, tx1_t32
                mov     eax, 12345h                         ; the debug-service number
                xor     ecx, ecx
                xor     edx, edx
                clc
                db      0CDh, 2Dh                       ; INT 2DH
                stc
                pushfq
                pop     r11
                call    tx1_regs
                TX1_END 32
                TX1_CASE 33, tx1_t33
                mov     eax, 2h                         ; the debug-service number
                xor     ecx, ecx
                xor     edx, edx
                clc
                db      0CDh, 2Dh                       ; INT 2DH
                stc
                pushfq
                pop     r11
                call    tx1_regs
                TX1_END 33
                TX1_CASE 34, tx1_t34
                mov     eax, 4h                         ; the debug-service number
                xor     ecx, ecx
                xor     edx, edx
                clc
                db      0CDh, 2Dh                       ; INT 2DH
                stc
                pushfq
                pop     r11
                call    tx1_regs
                TX1_END 34
                TX1_CASE 35, tx1_t35
                mov     eax, 5h                         ; the debug-service number
                xor     ecx, ecx
                xor     edx, edx
                clc
                db      0CDh, 2Dh                       ; INT 2DH
                stc
                pushfq
                pop     r11
                call    tx1_regs
                TX1_END 35
                add     rsp, 20h
                pop     rdi
                pop     rsi
                pop     rbx
                ret
tx1_cases ENDP

tx1_entry PROC FRAME
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                lea     rcx, tx1_tstart
                call    tx_puts
                call    tx_flush
                call    tx1_cases
                lea     rcx, tx1_tend
                call    tx_puts
                lea     rcx, tx1_tcount
                mov     rdx, tx1_events
                call    tx_rec
                call    tx_flush
                mov     ecx, 61h
                call    tx_exit
tx1_entry ENDP

END
