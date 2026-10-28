; tx6_align.asm - test PE TX6 (plan 3.M7, P49, ledger U2240-U2279): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (AGENT_RULES "Host safety"); the harness may run it
; natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; What it checks: the ALIGNMENT CHECK (#AC, vector 17; 253668-093 Vol3A 7.15 "Interrupt 17 - Alignment Check Exception (#AC)": CPL 3,
; CR0.AM = 1 and EFLAGS.AC = 1, an unaligned memory reference) as Windows delivers it: a vectored handler (First 1) records the event
; (tx_rec_exc), clears EFLAGS.AC in the CONTEXT and continues at the resume label. E1: EFLAGS.AC := 1 (POPFQ), a 4-byte load from an
; address = 1 mod 4, EFLAGS.AC := 0. The line "TX6 end" follows when the process survived.
; Exit status: 66H << 24 | the folded hash of every observation (tx_common.inc). kernel32.lib only (no CRT, no TLS).

extrn   __imp_AddVectoredExceptionHandler:qword

include tx_common.inc

.DATA
                ALIGN   16
tx6_resume      dq      0
tx6_frsp        dq      0
tx6_data        dq      1122334455667788h, 99AABBCCDDEEFF00h

.CODE
tx6_tstart      db      'TX6 start', 0
tx6_tE1         db      'TX6 E1', 0
tx6_tend        db      'TX6 end', 0
tx6_tval        db      'loaded', 0

tx6_veh PROC FRAME
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                mov     rbx, [rcx]
                mov     rsi, [rcx + 8]
                mov     rcx, rbx
                mov     rdx, rsi
                mov     r8, tx6_frsp
                lea     r9, tx6_tE1
                call    tx_rec_exc
                call    tx_flush
                and     dword ptr [rsi + 44h], NOT 40000h   ; EFLAGS.AC := 0 in the CONTEXT
                mov     rax, tx6_resume
                mov     [rsi + 0F8h], rax
                mov     eax, -1                         ; EXCEPTION_CONTINUE_EXECUTION
                add     rsp, 28h
                pop     rsi
                pop     rbx
                ret
tx6_veh ENDP

tx6_entry PROC FRAME
                push    rbx
                .pushreg rbx
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                lea     rcx, tx6_tstart
                call    tx_puts
                call    tx_flush
                mov     ecx, 1
                lea     rdx, tx6_veh
                call    qword ptr [__imp_AddVectoredExceptionHandler]
                lea     rax, tx6_r1
                mov     tx6_resume, rax
                lea     rcx, tx6_data
                xor     edx, edx                        ; RDX: a fixed value
                xor     ebx, ebx
                pushfq
                or      dword ptr [rsp], 40000h
                popfq
                mov     tx6_frsp, rsp
                mov     ebx, dword ptr [rcx + 1]        ; unaligned
tx6_r1:         pushfq
                and     dword ptr [rsp], NOT 40000h
                popfq
                lea     rcx, tx6_tend
                call    tx_puts
                lea     rcx, tx6_tval
                mov     edx, ebx
                call    tx_rec
                call    tx_flush
                mov     ecx, 66h
                call    tx_exit
tx6_entry ENDP

END
