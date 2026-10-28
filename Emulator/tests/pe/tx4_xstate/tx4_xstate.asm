; tx4_xstate.asm - test PE TX4 (plan 3.M7, P49, ledger U2240-U2279): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (AGENT_RULES "Host safety"); the harness may run it
; natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; What it checks: the CONTEXT / CONTEXT_EX / XSTATE an exception handler gets for the AVX state and the restore of a changed state:
;   - XGETBV(0) (XCR0) and KUSER_SHARED_DATA.XState (7FFE0000H + 3D8H, _XSTATE_CONFIGURATION of the guest ntdll.pdb: EnabledFeatures,
;     EnabledVolatileFeatures, Size, ControlFlags, Features[2] (AVX) Offset / Size, EnabledSupervisorFeatures, AlignedFeatures,
;     AllFeatureSize)
;   E1  YMM0 - YMM15 loaded with patterns, UD2; the vectored handler records ContextFlags, the CONTEXT_EX after the CONTEXT (phnt
;       ntrtl.h:5007-5019 CONTEXT_CHUNK {LONG Offset; ULONG Length} All / Legacy / XState / KernelCet), the XSAVE header the XState chunk
;       points at (Mask, CompactionMask: _XSAVE_AREA_HEADER), whether the YMM upper halves there and XMM0 - 15 in the CONTEXT equal the
;       patterns, then CHANGES Xmm1 in the CONTEXT and YMM1's upper half in the XSAVE area (when present) and continues; after the
;       resume the test stores YMM0 - YMM2 and records them (YMM1 must carry the change, YMM0 / YMM2 the patterns)
;       the frame after the CONTEXT_EX (CONTEXT + 4F0H .. + 5C0H: the record and what follows) and FltSave's first 30H bytes
;   E3  the frame's place: UD2 with RSP AND 38H = 0, 8, .. 38H - RSP - CONTEXT for each (Windows aligns the frame to 64 bytes)
;   E1 / E2 fill the 2000H bytes below RSP with 0CCH first: a frame byte Windows does not write keeps the fill
;   E2  after VZEROUPPER (the AVX state back in its initial configuration), UD2: the same record
; Exit status: 64H << 24 | the folded hash of every observation (tx_common.inc). kernel32.lib only (no CRT, no TLS).

extrn   __imp_AddVectoredExceptionHandler:qword

include tx_common.inc

.DATA
                ALIGN   16
tx4_pat         dq      64 dup (0)                      ; 16 x 32 bytes, filled by tx4_fill
tx4_out         dq      12 dup (0)                      ; YMM0 - YMM2 after the resume
tx4_resume      dq      0
tx4_frsp        dq      0
tx4_tag         dq      0
tx4_mod         dq      0                               ; 1: the handler changes YMM1; 2: the sweep E3
tx4_dist        dq      8 dup (0)                       ; E3: RSP - CONTEXT by RSP bits 5:3
tx4_k           dq      0

.CODE
tx4_tstart      db      'TX4 start', 0
tx4_tE1         db      'TX4 E1', 0
tx4_tE2         db      'TX4 E2', 0
tx4_tE3         db      'TX4 E3 rsp-ctx by rsp&38', 0
tx4_tafter      db      'TX4 after', 0
tx4_txcr0       db      'xcr0', 0
tx4_tku         db      'TX4 kuser.xstate', 0
tx4_tke         db      'enabled', 0
tx4_tkv         db      'volatile', 0
tx4_tks         db      'size', 0
tx4_tkc         db      'control', 0
tx4_tkao        db      'avx.off', 0
tx4_tkas        db      'avx.size', 0
tx4_tksup       db      'supervisor', 0
tx4_tkal        db      'aligned', 0
tx4_tkall       db      'allsize', 0
tx4_tcxa        db      'ex.all-lo6', 0
tx4_tcxl        db      'ex.legacy', 0
tx4_tcxx        db      'ex.xstate-lo6', 0
tx4_tcxk        db      'ex.+18', 0
tx4_tmask       db      'xs.mask', 0
tx4_tcmask      db      'xs.compaction', 0
tx4_tymmh       db      'ymmh=pat', 0
tx4_txmm        db      'xmm=pat', 0
tx4_tymm0       db      'ymm0', 0
tx4_tymm1       db      'ymm1', 0
tx4_tymm2       db      'ymm2', 0
tx4_tframe      db      ' frame+4F0', 0
tx4_tflt        db      ' fltsave', 0
tx4_tfres       db      ' fltsave+1A0', 0
tx4_txsc        db      'TX4 kuser.xstate.all', 0
tx4_tq          db      'q', 0
tx4_tctxo       db      ' ctx-other', 0

; tx4_fill: pattern byte i of register r = r * 20H + i + 1 (32 bytes each)
tx4_fill PROC
                lea     rdx, tx4_pat
                xor     eax, eax
tx4_f_l:        lea     ecx, [eax + 1]
                mov     [rdx + rax], cl
                inc     eax
                cmp     eax, 512
                jb      tx4_f_l
                ret
tx4_fill ENDP

; the vectored handler: LONG ( PEXCEPTION_POINTERS RCX )
tx4_veh PROC FRAME
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
                mov     rbx, [rcx]
                mov     rsi, [rcx + 8]
                xor     eax, eax
                cmp     dword ptr [rbx], 0C000001Dh     ; only our UD2
                jne     tx4_v_x
                cmp     tx4_mod, 2                      ; E3 (the sweep): only the frame distance, indexed by the RSP's bits 5:3
                jne     tx4_v_full
                mov     rax, tx4_frsp
                mov     rcx, rax
                sub     rax, rsi
                and     ecx, 38h
                lea     rdx, tx4_dist
                mov     [rdx + rcx], rax
                jmp     tx4_v_cont
tx4_v_full:     mov     rcx, rbx
                mov     rdx, rsi
                mov     r8, tx4_frsp
                mov     r9, tx4_tag
                call    tx_rec_exc
                ; CONTEXT_EX at CONTEXT + 4D0H: the chunks as 8-byte {Offset, Length}; All.Length and XState.Length end at the event's RSP
                ; (measured), so they are printed less RSP's low 6 bits (the stack's load-time randomization)
                mov     r12, [rsi + 98h]
                and     r12d, 3Fh
                lea     rcx, tx4_tcxa
                mov     rdx, [rsi + 4D0h]
                shl     r12, 32
                sub     rdx, r12
                shr     r12, 32
                call    tx_rec
                lea     rcx, tx4_tcxl
                mov     rdx, [rsi + 4D8h]
                call    tx_rec
                lea     rcx, tx4_tcxx
                mov     rdx, [rsi + 4E0h]
                shl     r12, 32
                sub     rdx, r12
                shr     r12, 32
                call    tx_rec
                lea     rcx, tx4_tcxk
                mov     rdx, [rsi + 4E8h]
                call    tx_rec
                call    tx_flush
                ; the frame after the CONTEXT_EX: CONTEXT + 4F0H .. + 5C0H (the EXCEPTION_RECORD, then what Windows puts after it) as
                ; qwords - a stack address as 2000000000000000H + ( value - the recorded RSP ), an image address image-relative
                mov     rcx, tx4_tag
                call    tx_puts
                lea     rcx, tx4_tframe
                call    tx_puts
                mov     r12d, 4F0h
tx4_v_fl:       mov     rcx, [rsi + r12]
                mov     rax, rcx
                sub     rax, tx4_frsp
                add     rax, 10000h
                cmp     rax, 20000h
                jae     tx4_v_fi
                sub     rax, 10000h
                bts     rax, 61
                mov     rdx, rax
                jmp     tx4_v_fr
tx4_v_fi:       call    tx_ibrel
                mov     rdx, rax
tx4_v_fr:       lea     rcx, tx4_tq
                call    tx_rec
                add     r12d, 8
                cmp     r12d, 5C0h
                jb      tx4_v_fl
                call    tx_flush
                ; the CONTEXT's other fields: P1Home .. P6Home (+0H .. +30H) and +300H .. +4D0H (VectorRegister[26], VectorControl,
                ; DebugControl, LastBranchToRip, LastBranchFromRip, LastExceptionToRip, LastExceptionFromRip), 12 qwords per line
                xor     r12d, r12d
tx4_v_cl:       mov     eax, r12d
                xor     edx, edx
                mov     ecx, 60h
                div     ecx
                test    edx, edx
                jnz     tx4_v_cq
                test    r12d, r12d
                jz      tx4_v_ch
                call    tx_flush
tx4_v_ch:       mov     rcx, tx4_tag
                call    tx_puts
                lea     rcx, tx4_tctxo
                call    tx_puts
tx4_v_cq:       mov     r8d, r12d                       ; offset: 0 .. 2FH -> +0H, then +300H ..
                cmp     r8d, 30h
                jb      tx4_v_co
                add     r8d, 300h - 30h
tx4_v_co:       mov     rcx, [rsi + r8]
                call    tx_ibrel
                lea     rcx, tx4_tq
                mov     rdx, rax
                call    tx_rec
                add     r12d, 8
                cmp     r12d, 30h + 1D0h
                jb      tx4_v_cl
                call    tx_flush
                ; FltSave (CONTEXT + 100H, _XSAVE_FORMAT): ControlWord .. ErrorOpcode, ErrorOffset / ErrorSelector, DataOffset / DataSelector,
                ; MxCsr / MxCsr_Mask, FloatRegisters[0]
                mov     rcx, tx4_tag
                call    tx_puts
                lea     rcx, tx4_tflt
                call    tx_puts
                mov     r12d, 100h
tx4_v_ffl:      lea     rcx, tx4_tq
                mov     rdx, [rsi + r12]
                call    tx_rec
                add     r12d, 8
                cmp     r12d, 130h
                jb      tx4_v_ffl
                call    tx_flush
                ; FltSave's last 60H bytes (CONTEXT + 2A0H .. + 300H: _XSAVE_FORMAT Reserved4)
                mov     rcx, tx4_tag
                call    tx_puts
                lea     rcx, tx4_tfres
                call    tx_puts
                mov     r12d, 2A0h
tx4_v_frl:      lea     rcx, tx4_tq
                mov     rdx, [rsi + r12]
                call    tx_rec
                add     r12d, 8
                cmp     r12d, 300h
                jb      tx4_v_frl
                call    tx_flush
                ; the XSAVE header the XState chunk names (when its Length is not 0)
                xor     edi, edi
                movsxd  rax, dword ptr [rsi + 4E0h]     ; XState.Offset (from the CONTEXT_EX)
                mov     ecx, dword ptr [rsi + 4E4h]     ; XState.Length
                test    ecx, ecx
                jz      tx4_v_noxs
                lea     rdi, [rsi + 4D0h]
                add     rdi, rax                        ; the XSAVE_AREA_HEADER
tx4_v_noxs:     mov     rcx, tx4_tag
                call    tx_puts
                lea     rcx, tx4_tmask
                xor     edx, edx
                test    rdi, rdi
                jz      @F
                mov     rdx, [rdi]
@@:             call    tx_rec
                lea     rcx, tx4_tcmask
                xor     edx, edx
                test    rdi, rdi
                jz      @F
                mov     rdx, [rdi + 8]
@@:             call    tx_rec
                ; the header's other 30H bytes (Reserved2[6])
                test    rdi, rdi
                jz      tx4_v_nores
                mov     r12d, 10h
tx4_v_hres:     lea     rcx, tx4_tq
                mov     rdx, [rdi + r12]
                call    tx_rec
                add     r12d, 8
                cmp     r12d, 40h
                jb      tx4_v_hres
tx4_v_nores:
                ; ymmh=pat: bit r set when YMMr's upper 16 bytes in the XSAVE area (header + 40H + r * 10H) equal the pattern
                xor     r12d, r12d
                test    rdi, rdi
                jz      tx4_v_ymmd
                test    byte ptr [rdi], 4               ; Mask (XSTATE_BV) bit 2: the AVX component was saved (else its bytes are not written)
                jz      tx4_v_ymmd
                xor     ecx, ecx
tx4_v_yl:       mov     eax, ecx
                shl     eax, 5
                lea     rdx, tx4_pat
                mov     r8, [rdx + rax + 10h]
                mov     r9, [rdx + rax + 18h]
                mov     eax, ecx
                shl     eax, 4
                cmp     r8, [rdi + rax + 40h]
                jne     tx4_v_yn
                cmp     r9, [rdi + rax + 48h]
                jne     tx4_v_yn
                bts     r12d, ecx
tx4_v_yn:       inc     ecx
                cmp     ecx, 16
                jb      tx4_v_yl
tx4_v_ymmd:     lea     rcx, tx4_tymmh
                mov     edx, r12d
                call    tx_rec
                ; xmm=pat: bit r set when CONTEXT.Xmm r (1A0H + r * 10H) equals the low 16 pattern bytes
                xor     r12d, r12d
                xor     ecx, ecx
tx4_v_xl:       mov     eax, ecx
                shl     eax, 5
                lea     rdx, tx4_pat
                mov     r8, [rdx + rax]
                mov     r9, [rdx + rax + 8]
                mov     eax, ecx
                shl     eax, 4
                cmp     r8, [rsi + rax + 1A0h]
                jne     tx4_v_xn
                cmp     r9, [rsi + rax + 1A8h]
                jne     tx4_v_xn
                bts     r12d, ecx
tx4_v_xn:       inc     ecx
                cmp     ecx, 16
                jb      tx4_v_xl
                lea     rcx, tx4_txmm
                mov     edx, r12d
                call    tx_rec
                call    tx_flush
                ; the change (E1): Xmm1 := 5555...H / 6666...H, YMM1's upper half := 7777...H / 8888...H
                cmp     tx4_mod, 1
                jne     tx4_v_cont
                mov     rax, 5555555555555555h
                mov     [rsi + 1B0h], rax
                mov     rax, 6666666666666666h
                mov     [rsi + 1B8h], rax
                test    rdi, rdi
                jz      tx4_v_cont
                mov     rax, 7777777777777777h
                mov     [rdi + 50h], rax
                mov     rax, 8888888888888888h
                mov     [rdi + 58h], rax
tx4_v_cont:     mov     rax, tx4_resume
                mov     [rsi + 0F8h], rax
                mov     rax, tx4_frsp
                mov     [rsi + 98h], rax
                mov     eax, -1                         ; EXCEPTION_CONTINUE_EXECUTION
tx4_v_x:        add     rsp, 28h
                pop     r12
                pop     rdi
                pop     rsi
                pop     rbx
                ret
tx4_veh ENDP

; tx4_dump: YMM0 - YMM2 -> tx4_out, recorded as four qwords each
tx4_dump PROC FRAME
                push    rbx
                .pushreg rbx
                sub     rsp, 20h
                .allocstack 20h
                .endprolog
                lea     rax, tx4_out
                vmovdqu ymmword ptr [rax], ymm0
                vmovdqu ymmword ptr [rax + 20h], ymm1
                vmovdqu ymmword ptr [rax + 40h], ymm2
                lea     rcx, tx4_tafter
                call    tx_puts
                xor     ebx, ebx
tx4_d_l:        lea     rcx, tx4_tymm0
                cmp     ebx, 4
                jb      @F
                lea     rcx, tx4_tymm1
                cmp     ebx, 8
                jb      @F
                lea     rcx, tx4_tymm2
@@:             lea     rax, tx4_out
                mov     rdx, [rax + rbx * 8]
                call    tx_rec
                inc     ebx
                cmp     ebx, 12
                jb      tx4_d_l
                call    tx_flush
                add     rsp, 20h
                pop     rbx
                ret
tx4_dump ENDP

tx4_kuser PROC FRAME
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                mov     rbx, 7FFE03D8h                  ; KUSER_SHARED_DATA.XState
                lea     rcx, tx4_tku
                call    tx_puts
                xor     ecx, ecx
                xgetbv
                shl     rdx, 32
                or      rdx, rax
                lea     rcx, tx4_txcr0
                call    tx_rec
                lea     rcx, tx4_tke
                mov     rdx, [rbx]
                call    tx_rec
                lea     rcx, tx4_tkv
                mov     rdx, [rbx + 8]
                call    tx_rec
                lea     rcx, tx4_tks
                mov     edx, [rbx + 10h]
                call    tx_rec
                lea     rcx, tx4_tkc
                mov     edx, [rbx + 14h]
                call    tx_rec
                lea     rcx, tx4_tkao
                mov     edx, [rbx + 18h + 2 * 8]
                call    tx_rec
                lea     rcx, tx4_tkas
                mov     edx, [rbx + 18h + 2 * 8 + 4]
                call    tx_rec
                lea     rcx, tx4_tksup
                mov     rdx, [rbx + 218h]
                call    tx_rec
                lea     rcx, tx4_tkal
                mov     rdx, [rbx + 220h]
                call    tx_rec
                lea     rcx, tx4_tkall
                mov     edx, [rbx + 228h]
                call    tx_rec
                call    tx_flush
                ; the whole _XSTATE_CONFIGURATION (338H bytes) as qwords, 12 per line
                xor     esi, esi
tx4_k_l:        mov     eax, esi
                xor     edx, edx
                mov     ecx, 60h
                div     ecx
                test    edx, edx
                jnz     tx4_k_q
                test    esi, esi
                jz      tx4_k_h
                call    tx_flush
tx4_k_h:        lea     rcx, tx4_txsc
                call    tx_puts
tx4_k_q:        lea     rcx, tx4_tq
                mov     rdx, [rbx + rsi]
                call    tx_rec
                add     esi, 8
                cmp     esi, 338h
                jb      tx4_k_l
                call    tx_flush
                add     rsp, 28h
                pop     rsi
                pop     rbx
                ret
tx4_kuser ENDP

; TX4_PREFILL: the 2000H bytes below RSP := 0CCH (what Windows does not write in its frame keeps the fill; RAX, RCX, RDI destroyed, DF 0)
TX4_PREFILL MACRO
                lea     rdi, [rsp - 2000h]
                mov     ecx, 2000h
                mov     al, 0CCh
                cld
                rep stosb
ENDM

; TX4_LOAD: YMM0 - YMM15 := the patterns
TX4_LOAD MACRO
                lea     rax, tx4_pat
                vmovdqu ymm0, ymmword ptr [rax]
                vmovdqu ymm1, ymmword ptr [rax + 20h]
                vmovdqu ymm2, ymmword ptr [rax + 40h]
                vmovdqu ymm3, ymmword ptr [rax + 60h]
                vmovdqu ymm4, ymmword ptr [rax + 80h]
                vmovdqu ymm5, ymmword ptr [rax + 0A0h]
                vmovdqu ymm6, ymmword ptr [rax + 0C0h]
                vmovdqu ymm7, ymmword ptr [rax + 0E0h]
                vmovdqu ymm8, ymmword ptr [rax + 100h]
                vmovdqu ymm9, ymmword ptr [rax + 120h]
                vmovdqu ymm10, ymmword ptr [rax + 140h]
                vmovdqu ymm11, ymmword ptr [rax + 160h]
                vmovdqu ymm12, ymmword ptr [rax + 180h]
                vmovdqu ymm13, ymmword ptr [rax + 1A0h]
                vmovdqu ymm14, ymmword ptr [rax + 1C0h]
                vmovdqu ymm15, ymmword ptr [rax + 1E0h]
ENDM

tx4_entry PROC FRAME
                sub     rsp, 28h
                .allocstack 28h
                .endprolog
                lea     rcx, tx4_tstart
                call    tx_puts
                call    tx_flush
                call    tx4_fill
                call    tx4_kuser
                mov     ecx, 1
                lea     rdx, tx4_veh
                call    qword ptr [__imp_AddVectoredExceptionHandler]
                ; E1: the patterns, UD2, the handler changes YMM1
                lea     rax, tx4_tE1
                mov     tx4_tag, rax
                mov     tx4_mod, 1
                lea     rax, tx4_r1
                mov     tx4_resume, rax
                TX4_PREFILL
                TX4_LOAD
                mov     tx4_frsp, rsp
                xor     ecx, ecx                        ; RCX / RDX: fixed values
                xor     edx, edx
                cmp     eax, eax                        ; RFLAGS: a fixed state
                ud2
tx4_r1:         call    tx4_dump
                ; E2: VZEROUPPER, UD2, nothing changed
                lea     rax, tx4_tE2
                mov     tx4_tag, rax
                mov     tx4_mod, 0
                lea     rax, tx4_r2
                mov     tx4_resume, rax
                TX4_PREFILL
                TX4_LOAD
                vzeroupper
                mov     tx4_frsp, rsp
                xor     ecx, ecx                        ; RCX / RDX: fixed values
                xor     edx, edx
                cmp     eax, eax                        ; RFLAGS: a fixed state
                ud2
tx4_r2:         call    tx4_dump
                ; E3: the frame's place for each RSP AND 38H - UD2 with RSP lowered by 0, 8, .. 38H
                mov     tx4_mod, 2
                mov     tx4_k, 0
tx4_e3_l:       mov     rcx, tx4_k
                sub     rsp, rcx
                lea     rax, tx4_r3
                mov     tx4_resume, rax
                mov     tx4_frsp, rsp
                xor     ecx, ecx                        ; RCX / RDX: fixed values
                xor     edx, edx
                cmp     eax, eax                        ; RFLAGS: a fixed state
                ud2
tx4_r3:         add     rsp, tx4_k
                add     tx4_k, 8
                cmp     tx4_k, 40h
                jb      tx4_e3_l
                lea     rcx, tx4_tE3
                call    tx_puts
                xor     ebx, ebx
tx4_e3_p:       lea     rcx, tx4_tq
                lea     rax, tx4_dist
                mov     rdx, [rax + rbx * 8]
                call    tx_rec
                inc     ebx
                cmp     ebx, 8
                jb      tx4_e3_p
                call    tx_flush
                mov     ecx, 64h
                call    tx_exit
tx4_entry ENDP

END
