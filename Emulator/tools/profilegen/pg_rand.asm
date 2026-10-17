; pg_rand.asm - the RDRAND primitives of ProfileGenerator's Randomizer (pg_randomize.hpp / .cpp; plan 1.J.49, ledger
; U1513-U1514). Design: the user's rid demo (rid.asm) - re-implemented for the emulator, x64 (ml64) only.
;
;   int pg_has_rdrand( void );                    1 if CPUID.(EAX=1):ECX[30] (RDRAND) is set, else 0
;   int pg_rdrand64( unsigned long long* out );   1 and *out = 64 random bits, or 0 (nothing written)
;
; Each draw is retried up to 10 times while RDRAND reports CF=0 (no random data ready; Intel's DRNG software
; implementation guide recommends 10 retries). A value of 0 or ~0 is rejected like a failed draw (some AMD parts return
; them with CF=1 after a fault), so neither can ever reach the caller.

.code

pg_has_rdrand PROC
    push    rbx                     ; CPUID writes rbx, a non-volatile register in the x64 ABI
    mov     eax, 1
    xor     ecx, ecx
    cpuid
    xor     eax, eax
    bt      ecx, 30                 ; CPUID.1:ECX[30] = RDRAND
    setc    al
    pop     rbx
    ret
pg_has_rdrand ENDP

pg_rdrand64 PROC                    ; rcx = out
    mov     edx, 10                 ; retries
again:
    rdrand  rax
    jnc     retry                   ; CF=0: no data this time
    lea     r8, [rax + 1]           ; 0 -> 1, ~0 -> 0: both <= 1
    cmp     r8, 1
    jbe     retry                   ; reject 0 and ~0
    mov     [rcx], rax
    mov     eax, 1
    ret
retry:
    dec     edx
    jnz     again
    xor     eax, eax
    ret
pg_rdrand64 ENDP

END
