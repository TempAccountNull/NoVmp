; t9_rng.asm - test PE T9 (P47, ledger U2167; notes\eread\P47_ksecdd_run12.md): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (emu-oep maps it as data, AGENT_RULES "Host safety");
; the harness may run it natively in the VMware guest as a labelled reference only (decision 3.0.7), started detached (tm_nt run.detached,
; like T2 / T3 / T5 / T7 / T8).
;
; What it checks (USER DECISION 2026-10-28, P47 STEP 2 "\Device\KsecDD: model it as a device"; sample run 11's stop is bcrypt.dll's
; DllMain opening \Device\KsecDD): bcrypt.dll is imported, so its DllMain runs at the process start as in the sample, and then
;   call 1  BCryptGenRandom( NULL, buf1, 20H, BCRYPT_USE_SYSTEM_PREFERRED_RNG )
;   call 2  BCryptGenRandom( NULL, buf2, 20H, BCRYPT_USE_SYSTEM_PREFERRED_RNG )
; Prototype: Windows SDK 10.0.26100.0 shared\bcrypt.h BCryptGenRandom( BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer,
; ULONG dwFlags ), BCRYPT_USE_SYSTEM_PREFERRED_RNG 00000002H (Learn "BCryptGenRandom function": with this flag hAlgorithm must be NULL).
; The random bytes themselves are never printed or kept: the exit status ENCODES only relations (ExitProcess( code ), so the native and the
; emulated runs compare as one number):
;   bit 0  call 1 returned STATUS_SUCCESS (0)        bit 3  call 1 wrote nothing past 20H (the 10H guard bytes unchanged)
;   bit 1  call 2 returned STATUS_SUCCESS (0)        bit 4  call 2 wrote nothing past 20H
;   bit 2  the two 20H-byte outputs differ           bit 5  buf1 is not the fill any more (written)
;                                                    bit 6  buf2 is not the fill any more (written)
;   bits 31:24  39H (the T9 marker)                  -> 3900007FH expected
;   call 1 not STATUS_SUCCESS: exit E9000000H | the status' low 16 bits
; Linked with the SDK import libraries bcrypt.lib / kernel32.lib only (no CRT, no TLS); ExitProcess: um\processthreadsapi.h.

T9_FILL         equ     0CCCCCCCCCCCCCCCCh

extrn   __imp_BCryptGenRandom:qword
extrn   __imp_ExitProcess:qword

.DATA
                ALIGN   16
t9_buf1         dq      6 dup (T9_FILL)                 ; 30H bytes: 20H answer + 10H guard
t9_buf2         dq      6 dup (T9_FILL)

.CODE
; t9_gen: BCryptGenRandom( NULL, RCX = buffer, 20H, BCRYPT_USE_SYSTEM_PREFERRED_RNG ) -> EAX status
t9_gen PROC
                sub     rsp, 28h
                mov     rdx, rcx                        ; pbBuffer
                xor     ecx, ecx                        ; hAlgorithm NULL
                mov     r8d, 20h                        ; cbBuffer
                mov     r9d, 2                          ; BCRYPT_USE_SYSTEM_PREFERRED_RNG
                call    qword ptr [__imp_BCryptGenRandom]
                add     rsp, 28h
                ret
t9_gen ENDP

; t9_guard: ZF = 1 when the 10H bytes at RCX + 20H are all the fill (nothing written past 20H)
t9_guard PROC
                mov     rax, T9_FILL
                cmp     [rcx + 20h], rax
                jne     t9_g_done
                cmp     [rcx + 28h], rax
t9_g_done:      ret
t9_guard ENDP

; t9_filled: ZF = 1 when the 20H bytes at RCX are all still the fill (not written)
t9_filled PROC
                mov     rax, T9_FILL
                cmp     [rcx], rax
                jne     t9_f_done
                cmp     [rcx + 8], rax
                jne     t9_f_done
                cmp     [rcx + 10h], rax
                jne     t9_f_done
                cmp     [rcx + 18h], rax
t9_f_done:      ret
t9_filled ENDP

t9_entry PROC
                push    rbx
                push    rsi
                push    rdi
                sub     rsp, 20h                        ; 3 pushes + 20H: RSP 16-aligned at each CALL (entry RSP = 8 mod 16)
                lea     rcx, t9_buf1
                call    t9_gen
                test    eax, eax
                jz      t9_ok1
                movzx   ecx, ax
                or      ecx, 0E9000000h
                jmp     t9_done
t9_ok1:         mov     ebx, 39000001h                  ; the marker + bit 0
                lea     rcx, t9_buf2
                call    t9_gen
                test    eax, eax
                jnz     t9_b2
                or      ebx, 2
t9_b2:          ; bit 2: the two outputs differ
                lea     rsi, t9_buf1
                lea     rdi, t9_buf2
                mov     ecx, 4
                repe cmpsq
                je      t9_b3
                or      ebx, 4
t9_b3:          lea     rcx, t9_buf1
                call    t9_guard
                jne     t9_b4
                or      ebx, 8
t9_b4:          lea     rcx, t9_buf2
                call    t9_guard
                jne     t9_b5
                or      ebx, 10h
t9_b5:          lea     rcx, t9_buf1
                call    t9_filled
                je      t9_b6
                or      ebx, 20h
t9_b6:          lea     rcx, t9_buf2
                call    t9_filled
                je      t9_end
                or      ebx, 40h
t9_end:         mov     ecx, ebx
t9_done:        call    qword ptr [__imp_ExitProcess]   ; does not return
                ud2
t9_entry ENDP

END
