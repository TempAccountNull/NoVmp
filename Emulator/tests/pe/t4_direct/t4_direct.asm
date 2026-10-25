; t4_direct.asm - test PE T4 of plan 3.M5 (ledger U2015; notes\eread\P35_phase3_m5.md): self-written 64-bit guest code, never
; derived from the sample. GUEST CODE: the emulator only reads it as a file and writes it into emulated memory; the harness may
; run it natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; Direct system calls from the image's own code (the pattern of packers that avoid ntdll), no imports, no CRT, no TLS. The five
; service numbers follow the marker "T4SYSNUM" (FFFFFFFFH here); the harness patches them from the environment's ntdll capture
; (decision 3.0.8). Windows x64 system-call convention: number in EAX, arguments R10, RDX, R8, R9, then [RSP+28H], [RSP+30H] at
; the SYSCALL (a stub CALLed puts them at the caller's [RSP+20H], [RSP+28H]); status in EAX.
;   1. NtQueryInformationProcess( NtCurrentProcess(), ProcessBasicInformation, &pbi, 30H, &len ): PebBaseAddress = gs:[60H],
;      UniqueProcessId = gs:[40H] (winternl.h PROCESS_BASIC_INFORMATION; ksamd64.inc TePeb / TeClientId)
;   2. NtQueryInformationProcess( ..., ProcessDebugPort, &port, 8, NULL ) with EAX bits 31:16 set to ABCDH (plan 6.4.a: the
;      service is EAX AND FFFFH): port 0 (no debugger)
;   3. NtAllocateVirtualMemory( NtCurrentProcess(), &base = NULL, 0, &size = 1000H, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE )
;   4. a system-call stub written into that memory (mov r10, rcx; mov eax, NtQueryVirtualMemory; syscall; ret) and CALLed: a
;      SYSCALL from allocated memory (plan 1.K.6): NtQueryVirtualMemory( NtCurrentProcess(), base, MemoryBasicInformation, &mbi,
;      30H, &ret ) - BaseAddress / AllocationBase = base, AllocationProtect 40H, RegionSize 1000H, State MEM_COMMIT, Protect 40H,
;      Type MEM_PRIVATE (um\winnt.h MEMORY_BASIC_INFORMATION)
;   5. NtFreeVirtualMemory( NtCurrentProcess(), &base, &size = 0, MEM_RELEASE )
;   6. NtTerminateProcess( NtCurrentProcess(), 2DH ); a failed step exits 101H..107H instead.

T4_EXIT_CODE    equ     2Dh

.DATA
                ALIGN   16
t4_pbi          db      30h dup (0)
t4_retlen       dq      0
t4_port         dq      0FFFFFFFFFFFFFFFFh
t4_base         dq      0
t4_size         dq      0
                ALIGN   16
t4_mbi          db      30h dup (0)
t4_mbi_ret      dq      0

.CONST
                ALIGN   16
t4_marker       db      'T4SYSNUM'          ; the harness finds the patch fields by this marker (exactly once)
t4_num_qip      dd      0FFFFFFFFh          ; NtQueryInformationProcess
t4_num_alloc    dd      0FFFFFFFFh          ; NtAllocateVirtualMemory
t4_num_qvm      dd      0FFFFFFFFh          ; NtQueryVirtualMemory
t4_num_free     dd      0FFFFFFFFh          ; NtFreeVirtualMemory
t4_num_term     dd      0FFFFFFFFh          ; NtTerminateProcess
                dd      0, 0, 0

.CODE
t4_entry PROC
                sub     rsp, 88h                        ; RSP 16-aligned (entry RSP = 8 mod 16), room for the stack arguments
                ; 1. ProcessBasicInformation
                mov     r10, -1
                xor     edx, edx
                lea     r8, t4_pbi
                mov     r9d, 30h
                lea     rax, t4_retlen
                mov     qword ptr [rsp+28h], rax
                mov     eax, dword ptr [t4_num_qip]
                syscall
                test    eax, eax
                jne     fail1
                mov     rcx, qword ptr gs:[60h]
                cmp     qword ptr [t4_pbi+8], rcx       ; PebBaseAddress
                jne     fail2
                mov     rcx, qword ptr gs:[40h]
                cmp     qword ptr [t4_pbi+20h], rcx     ; UniqueProcessId
                jne     fail2
                ; 2. ProcessDebugPort, EAX bits 31:16 set
                mov     r10, -1
                mov     edx, 7
                lea     r8, t4_port
                mov     r9d, 8
                mov     qword ptr [rsp+28h], 0
                mov     eax, dword ptr [t4_num_qip]
                or      eax, 0ABCD0000h
                syscall
                test    eax, eax
                jne     fail3
                cmp     qword ptr [t4_port], 0
                jne     fail3
                ; 3. NtAllocateVirtualMemory: one RWX page
                mov     qword ptr [t4_base], 0
                mov     qword ptr [t4_size], 1000h
                mov     r10, -1
                lea     rdx, t4_base
                xor     r8d, r8d
                lea     r9, t4_size
                mov     qword ptr [rsp+28h], 3000h      ; MEM_COMMIT | MEM_RESERVE
                mov     qword ptr [rsp+30h], 40h        ; PAGE_EXECUTE_READWRITE
                mov     eax, dword ptr [t4_num_alloc]
                syscall
                test    eax, eax
                jne     fail4
                ; 4. the stub in the new page, called: NtQueryVirtualMemory from allocated memory
                mov     rbx, qword ptr [t4_base]
                mov     dword ptr [rbx], 0B8D18B4Ch     ; 4C 8B D1 = mov r10, rcx; B8 = mov eax, imm32
                mov     eax, dword ptr [t4_num_qvm]
                mov     dword ptr [rbx+4], eax
                mov     word ptr [rbx+8], 050Fh         ; 0F 05 = syscall
                mov     byte ptr [rbx+10], 0C3h         ; C3 = ret
                mov     rcx, -1
                mov     rdx, rbx
                xor     r8d, r8d                        ; MemoryBasicInformation
                lea     r9, t4_mbi
                mov     qword ptr [rsp+20h], 30h
                lea     rax, t4_mbi_ret
                mov     qword ptr [rsp+28h], rax
                call    rbx
                test    eax, eax
                jne     fail5
                cmp     qword ptr [t4_mbi], rbx         ; BaseAddress
                jne     fail6
                cmp     qword ptr [t4_mbi+8], rbx       ; AllocationBase
                jne     fail6
                cmp     dword ptr [t4_mbi+10h], 40h     ; AllocationProtect
                jne     fail6
                cmp     qword ptr [t4_mbi+18h], 1000h   ; RegionSize
                jne     fail6
                cmp     dword ptr [t4_mbi+20h], 1000h   ; State MEM_COMMIT
                jne     fail6
                cmp     dword ptr [t4_mbi+24h], 40h     ; Protect
                jne     fail6
                cmp     dword ptr [t4_mbi+28h], 20000h  ; Type MEM_PRIVATE
                jne     fail6
                ; 5. NtFreeVirtualMemory MEM_RELEASE
                mov     qword ptr [t4_size], 0
                mov     r10, -1
                lea     rdx, t4_base
                lea     r8, t4_size
                mov     r9d, 8000h
                mov     eax, dword ptr [t4_num_free]
                syscall
                test    eax, eax
                jne     fail7
                mov     edx, T4_EXIT_CODE
                jmp     done
fail1:          mov     edx, 101h
                jmp     done
fail2:          mov     edx, 102h
                jmp     done
fail3:          mov     edx, 103h
                jmp     done
fail4:          mov     edx, 104h
                jmp     done
fail5:          mov     edx, 105h
                jmp     done
fail6:          mov     edx, 106h
                jmp     done
fail7:          mov     edx, 107h
done:           mov     r10, -1                         ; 6. NtTerminateProcess( NtCurrentProcess(), EDX )
                mov     eax, dword ptr [t4_num_term]
                syscall
                ud2
t4_entry ENDP

END
