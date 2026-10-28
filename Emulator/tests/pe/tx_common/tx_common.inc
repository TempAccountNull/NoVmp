; tx_common.inc - shared helpers of the exception test PEs TX1 .. TX6 (plan 3.M7, P49, ledger U2240-U2279): self-written 64-bit guest
; code, never derived from the sample. GUEST CODE: the emulator only reads the test PEs as files and writes them into emulated memory
; (emu-oep maps them as data, AGENT_RULES "Host safety"); the harness may run them natively in the VMware guest as a labelled reference
; only (decision 3.0.7).
;
; Every observation a test makes is
;   (1) printed as "tag=value" on one text line per event (WriteFile to the standard output handle: natively the harness reads the
;       lines from a pipe; an emulated process - the model of a DETACHED process, P41 - has no standard handle, its attempts are
;       recorded by the console capture <out-dir>\console_capture.txt, which the harness reads the same way), and
;   (2) mixed into one FNV-1a hash (32-bit, 8 bytes per value) - the exit status = the test's marker << 24 | the hash folded to 24 bits
;       (tx_exit), so the native and the emulated runs compare as one number, like T2 - T9.
; Values are printed and hashed only in a form that does not depend on where the process was loaded: an address inside the image as
; 1000000000000000H + its RVA (tx_ibrel), stack addresses as distances to a stack address the test recorded itself. No identifier, no
; random value is ever printed or hashed.
; Build note: each TXn project names this file as an AdditionalDependencies input of its MASM item (masm.targets does not track
; include files), so a change here rebuilds TX1 - TX6.
; Prototypes: Windows SDK 10.0.26100.0 um\processenv.h GetStdHandle (STD_OUTPUT_HANDLE = (DWORD)-11), um\fileapi.h WriteFile,
; um\processthreadsapi.h ExitProcess.

extrn   __imp_GetStdHandle:qword
extrn   __imp_WriteFile:qword
extrn   __imp_ExitProcess:qword
extrn   __ImageBase:byte

TX_LINE_MAX     equ     480                             ; < the console capture's 512 units per write

.DATA
                ALIGN   16
tx_hash         dd      811C9DC5h                       ; FNV-1a 32-bit offset basis
tx_wr           dd      0
tx_len          dq      0
tx_line         db      TX_LINE_MAX + 16 dup (0)
tx_eq           db      '=', 0
tx_hexd         db      '0123456789ABCDEF'

.CODE
; tx_mix: the hash := FNV-1a over the 8 bytes of RCX (little-endian), prime 01000193H. Leaf; RAX, RCX, RDX, R8 destroyed.
tx_mix PROC
                mov     eax, tx_hash
                mov     r8d, 8
tx_mix_l:       movzx   edx, cl
                xor     eax, edx
                imul    eax, eax, 01000193h
                shr     rcx, 8
                dec     r8d
                jnz     tx_mix_l
                mov     tx_hash, eax
                ret
tx_mix ENDP

; tx_putc: append CL to the line (dropped beyond TX_LINE_MAX). Leaf; RAX destroyed.
tx_putc PROC
                mov     rax, tx_len
                cmp     rax, TX_LINE_MAX
                jae     tx_putc_x
                lea     rdx, tx_line
                mov     [rdx + rax], cl
                inc     rax
                mov     tx_len, rax
tx_putc_x:      ret
tx_putc ENDP

; tx_puts: append the zero-terminated string at RCX. Leaf; RAX, RCX, RDX, R9 destroyed.
tx_puts PROC
                mov     r9, rcx
tx_puts_l:      movzx   ecx, byte ptr [r9]
                test    ecx, ecx
                jz      tx_puts_x
                call    tx_putc
                inc     r9
                jmp     tx_puts_l
tx_puts_x:      ret
tx_puts ENDP

; tx_puthex: append RCX in hexadecimal, upper case, no leading zeros ("0" for 0). Leaf; RAX, RCX, RDX, R9 - R11 destroyed.
tx_puthex PROC
                mov     r10, rcx
                mov     r11d, 60                        ; the shift of the first nibble
                xor     r9d, r9d                        ; 1 once a non-zero nibble was written
tx_ph_l:        mov     rax, r10
                mov     ecx, r11d
                shr     rax, cl
                and     eax, 0Fh
                test    r11d, r11d
                jz      tx_ph_put                       ; the last nibble is always written
                test    r9d, r9d
                jnz     tx_ph_put
                test    eax, eax
                jz      tx_ph_next
tx_ph_put:      mov     r9d, 1
                lea     rdx, tx_hexd
                movzx   ecx, byte ptr [rdx + rax]
                call    tx_putc
tx_ph_next:     sub     r11d, 4
                jns     tx_ph_l
                ret
tx_puthex ENDP

; tx_rec: append " TAG=VALUE" (RCX = the tag string, RDX = the value) and mix the value into the hash. Leaf.
; Preserves RBX, RSI, RDI, RBP, R12 - R15 (the Windows x64 ABI non-volatiles); RAX, RCX, RDX, R8 - R11 destroyed.
tx_rec PROC
                push    rdx                             ; [RSP] = the value (no call below allocates: leaf helpers only)
                push    rcx
                mov     ecx, ' '
                call    tx_putc
                pop     rcx
                call    tx_puts
                lea     rcx, tx_eq
                call    tx_puts
                mov     rcx, [rsp]
                call    tx_puthex
                pop     rcx
                call    tx_mix
                ret
tx_rec ENDP

; tx_ibrel: RAX = RCX - __ImageBase + 1000000000000000H when RCX lies in the first 1000000H bytes from __ImageBase (the image), else
; RCX unchanged (a value that does not depend on the load address). Leaf; RAX, RDX destroyed.
tx_ibrel PROC
                lea     rdx, __ImageBase
                mov     rax, rcx
                sub     rax, rdx
                cmp     rax, 1000000h
                jae     tx_ibrel_raw
                bts     rax, 60
                ret
tx_ibrel_raw:   mov     rax, rcx
                ret
tx_ibrel ENDP

; tx_flush: append CR LF, write the line to the standard output handle (one WriteFile; its result is not used: an emulated or a
; detached process has no standard handle), start a new line. Non-leaf.
tx_flush PROC FRAME
                push    rbx
                .pushreg rbx
                sub     rsp, 30h
                .allocstack 30h
                .endprolog
                mov     ecx, 13
                call    tx_putc
                mov     ecx, 10
                call    tx_putc
                mov     ecx, -11                        ; STD_OUTPUT_HANDLE
                call    qword ptr [__imp_GetStdHandle]
                mov     rcx, rax                        ; hFile
                lea     rdx, tx_line                    ; lpBuffer
                mov     r8, tx_len                      ; nNumberOfBytesToWrite
                lea     r9, tx_wr                       ; lpNumberOfBytesWritten
                mov     qword ptr [rsp + 20h], 0        ; lpOverlapped
                call    qword ptr [__imp_WriteFile]
                mov     tx_len, 0
                add     rsp, 30h
                pop     rbx
                ret
tx_flush ENDP

; tx_exit: ExitProcess( ECX << 24 | ( hash XOR hash >> 24 ) AND 0FFFFFFH ) - ECX = the test's marker (8 bits). Does not return.
tx_exit PROC FRAME
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                shl     ecx, 24
                mov     eax, tx_hash
                mov     edx, eax
                shr     edx, 24
                xor     eax, edx
                and     eax, 0FFFFFFh
                or      ecx, eax
                call    qword ptr [__imp_ExitProcess]
                ud2
tx_exit ENDP

; tx_rec_exc: one event line - RCX = PEXCEPTION_RECORD, RDX = PCONTEXT, R8 = the stack pointer the test recorded right before the
; instruction that raised (0: not known), R9 = the event's tag string. Prints and hashes: the record (ExceptionCode, ExceptionFlags,
; the nested record pointer, ExceptionAddress, NumberParameters, ExceptionInformation[0..1]; record - context distance), the
; context (ContextFlags, Rip, EFlags, Rsp - R8, the segment selectors, MxCsr, the x87 control word, Dr0-Dr3 / Dr6 / Dr7, Rax / Rcx /
; Rdx), the distance CONTEXT.Rsp - context (less its low 6 bits). Layouts: guest ntdll.pdb _EXCEPTION_RECORD (98H) / _CONTEXT (4D0H). The line is not flushed
; (the caller may append more). Non-leaf; preserves the non-volatile registers.
tx_tag_c        db      'ev', 0
tx_tag_code     db      'code', 0
tx_tag_fl       db      'fl', 0
tx_tag_nest     db      'nest', 0
tx_tag_addr     db      'addr', 0
tx_tag_np       db      'np', 0
tx_tag_i0       db      'i0', 0
tx_tag_i1       db      'i1', 0
tx_tag_recd     db      'rec-ctx', 0
tx_tag_cf       db      'cf', 0
tx_tag_rip      db      'rip', 0
tx_tag_efl      db      'efl', 0
tx_tag_rsp      db      'rsp-at', 0
tx_tag_seg      db      'cs.ds.es.fs', 0
tx_tag_seg2     db      'gs.ss', 0
tx_tag_mx       db      'mxcsr', 0
tx_tag_fcw      db      'fcw', 0
tx_tag_dr03     db      'dr0|1|2|3', 0
tx_tag_dr6      db      'dr6', 0
tx_tag_dr7      db      'dr7', 0
tx_tag_rax      db      'rax', 0
tx_tag_rcx      db      'rcx', 0
tx_tag_rdx      db      'rdx', 0
tx_tag_frame    db      'at-ctx-lo6', 0
tx_tag_calign   db      'ctx&3F', 0
tx_rec_exc PROC FRAME
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                push    rdi
                .pushreg rdi
                push    r12
                .pushreg r12
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                mov     rbx, rcx                        ; the record
                mov     rsi, rdx                        ; the context
                mov     rdi, r8                         ; the recorded stack pointer
                mov     rcx, r9
                call    tx_puts
                lea     rcx, tx_tag_code
                mov     edx, [rbx]
                call    tx_rec
                lea     rcx, tx_tag_fl
                mov     edx, [rbx + 4]
                call    tx_rec
                lea     rcx, tx_tag_nest
                mov     rdx, [rbx + 8]
                call    tx_rec
                mov     rcx, [rbx + 10h]
                call    tx_ibrel
                lea     rcx, tx_tag_addr
                mov     rdx, rax
                call    tx_rec
                lea     rcx, tx_tag_np
                mov     edx, [rbx + 18h]
                call    tx_rec
                mov     rcx, [rbx + 20h]
                call    tx_ibrel
                lea     rcx, tx_tag_i0
                mov     rdx, rax
                call    tx_rec
                mov     rcx, [rbx + 28h]
                call    tx_ibrel
                lea     rcx, tx_tag_i1
                mov     rdx, rax
                call    tx_rec
                lea     rcx, tx_tag_recd
                mov     rdx, rbx
                sub     rdx, rsi
                call    tx_rec
                lea     rcx, tx_tag_cf
                mov     edx, [rsi + 30h]
                call    tx_rec
                mov     rcx, [rsi + 0F8h]
                call    tx_ibrel
                lea     rcx, tx_tag_rip
                mov     rdx, rax
                call    tx_rec
                lea     rcx, tx_tag_efl
                mov     edx, [rsi + 44h]
                call    tx_rec
                lea     rcx, tx_tag_rsp
                mov     rdx, [rsi + 98h]
                sub     rdx, rdi
                call    tx_rec
                lea     rcx, tx_tag_seg
                mov     rdx, [rsi + 38h]                ; SegCs, SegDs, SegEs, SegFs (4 x 16 bits)
                call    tx_rec
                lea     rcx, tx_tag_seg2
                mov     edx, [rsi + 40h]                ; SegGs, SegSs
                call    tx_rec
                lea     rcx, tx_tag_mx
                mov     edx, [rsi + 34h]
                call    tx_rec
                lea     rcx, tx_tag_fcw
                movzx   edx, word ptr [rsi + 100h]      ; FltSave.ControlWord
                call    tx_rec
                lea     rcx, tx_tag_dr03
                mov     rdx, [rsi + 48h]
                or      rdx, [rsi + 50h]
                or      rdx, [rsi + 58h]
                or      rdx, [rsi + 60h]
                call    tx_rec
                lea     rcx, tx_tag_dr6
                mov     rdx, [rsi + 68h]
                call    tx_rec
                lea     rcx, tx_tag_dr7
                mov     rdx, [rsi + 70h]
                call    tx_rec
                mov     rcx, [rsi + 78h]
                call    tx_ibrel
                lea     rcx, tx_tag_rax
                mov     rdx, rax
                call    tx_rec
                mov     rcx, [rsi + 80h]
                call    tx_ibrel
                lea     rcx, tx_tag_rcx
                mov     rdx, rax
                call    tx_rec
                mov     rcx, [rsi + 88h]
                call    tx_ibrel
                lea     rcx, tx_tag_rdx
                mov     rdx, rax
                call    tx_rec
                ; the CONTEXT's place: Windows puts the frame 64-byte aligned below the event's RSP (CONTEXT.Rsp), so RSP - context depends
                ; on RSP's low 6 bits (the stack's load-time randomization): printed / hashed as ( RSP - context ) - ( RSP AND 3FH ) and the
                ; context's low 6 bits
                lea     rcx, tx_tag_frame
                mov     r8, [rsi + 98h]                 ; CONTEXT.Rsp (the stack pointer of the event)
                mov     rdx, r8
                sub     rdx, rsi
                mov     rax, r8
                and     eax, 3Fh
                sub     rdx, rax
                call    tx_rec
                lea     rcx, tx_tag_calign
                mov     rdx, rsi
                and     edx, 3Fh
                call    tx_rec
                add     rsp, 28h
                pop     r12
                pop     rdi
                pop     rsi
                pop     rbx
                ret
tx_rec_exc ENDP
