; tl_ldr.asm - test PE "loader data" of plan 3.M5 part 2 (ledger U2028; notes\eread\P36_phase3_m5b.md): self-written 64-bit guest
; code, never derived from the sample. GUEST CODE: the emulator only reads it as a file and writes it into emulated memory; the
; harness runs it natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; No imports. It checks the process state a Windows x64 process finds at its entry (offsets: the guest ntdll.pdb public symbols -
; _TEB ProcessEnvironmentBlock 60H; _PEB Ldr 18H, ProcessParameters 20H, ProcessHeap 30H, NtGlobalFlag 0BCH; _PEB_LDR_DATA
; InLoadOrderModuleList 10H, InInitializationOrderModuleList 30H; _LDR_DATA_TABLE_ENTRY InInitializationOrderLinks 20H, DllBase
; 30H, BaseDllName 58H; _RTL_USER_PROCESS_PARAMETERS ImagePathName 60H; _UNICODE_STRING Length 0H, Buffer 8H; _HEAP Flags 70H,
; ForceFlags 74H):
;   101H  InLoadOrderModuleList entry 1 DllBase != PEB ImageBaseAddress
;   102H  entry 2 BaseDllName != "ntdll.dll" (ASCII case-insensitive)
;   103H  entry 3 != "kernel32.dll"
;   104H  entry 4 != "kernelbase.dll"
;   105H  InInitializationOrderModuleList entry 1 != "ntdll.dll"
;   106H  ProcessParameters->ImagePathName does not end in ".exe"
;   107H  ProcessHeap->Flags lacks HEAP_GROWABLE (2) or ProcessHeap->ForceFlags != 0
;   108H  NtGlobalFlag has a heap-checking bit (70H: tail / free check, parameter validation)
; Exit: one direct SYSCALL of NtTerminateProcess( NtCurrentProcess(), status ) (number patched after the marker "TLSYSNUM" by the
; harness from the environment's ntdll capture), status 30H when every check passed.

TL_EXIT_CODE    equ     30h

.CONST
                ALIGN   16
tl_marker       db      'TLSYSNUM'
tl_sysnum       dd      0FFFFFFFFh
                dd      0
n_ntdll         dw      'n','t','d','l','l','.','d','l','l',0
n_kernel32      dw      'k','e','r','n','e','l','3','2','.','d','l','l',0
n_kernelbase    dw      'k','e','r','n','e','l','b','a','s','e','.','d','l','l',0

.CODE
; RCX = a UNICODE_STRING, RDX = a NUL-terminated lower-case UTF-16 name -> ZF = 1 when equal (ASCII case-insensitive)
tl_eq PROC
                movzx   r8d, word ptr [rcx]         ; Length (bytes)
                shr     r8d, 1
                mov     r9, qword ptr [rcx+8]       ; Buffer
                xor     r10d, r10d
tl_next:        movzx   eax, word ptr [rdx+r10*2]
                cmp     r10d, r8d
                je      tl_tail
                test    eax, eax
                jz      tl_ne
                movzx   r11d, word ptr [r9+r10*2]
                cmp     r11d, 'A'
                jb      cmpc
                cmp     r11d, 'Z'
                ja      cmpc
                add     r11d, 20h
cmpc:           cmp     r11d, eax
                jne     tl_ne
                inc     r10d
                jmp     tl_next
tl_tail:        test    eax, eax                    ; equal only when the name ends here too
                ret
tl_ne:          or      eax, 1                      ; ZF = 0
                ret
tl_eq ENDP

tl_entry PROC
                sub     rsp, 28h
                mov     rbx, qword ptr gs:[60h]     ; PEB
                mov     rsi, qword ptr [rbx+18h]    ; Ldr
                lea     rdi, [rsi+10h]              ; InLoadOrderModuleList head
                mov     r12, qword ptr [rdi]        ; entry 1
                mov     edx, 101h
                mov     rax, qword ptr [r12+30h]
                cmp     rax, qword ptr [rbx+10h]
                jne     done
                mov     r12, qword ptr [r12]        ; entry 2
                lea     rcx, [r12+58h]
                lea     rdx, n_ntdll
                call    tl_eq
                mov     edx, 102h
                jnz     done
                mov     r12, qword ptr [r12]        ; entry 3
                lea     rcx, [r12+58h]
                lea     rdx, n_kernel32
                call    tl_eq
                mov     edx, 103h
                jnz     done
                mov     r12, qword ptr [r12]        ; entry 4
                lea     rcx, [r12+58h]
                lea     rdx, n_kernelbase
                call    tl_eq
                mov     edx, 104h
                jnz     done
                mov     r12, qword ptr [rsi+30h]    ; InInitializationOrderModuleList entry 1 (its InInitializationOrderLinks)
                lea     rcx, [r12+58h-20h]
                lea     rdx, n_ntdll
                call    tl_eq
                mov     edx, 105h
                jnz     done
                mov     rax, qword ptr [rbx+20h]    ; ProcessParameters
                movzx   ecx, word ptr [rax+60h]     ; ImagePathName.Length
                mov     r8, qword ptr [rax+68h]     ; ImagePathName.Buffer
                mov     edx, 106h
                cmp     ecx, 8
                jb      done
                mov     rax, qword ptr [r8+rcx-8]   ; the last four UTF-16 characters
                mov     r9, 0020002000200020h
                or      rax, r9                     ; ASCII lower case (".EXE" -> ".exe"; '.' = 2EH already has bit 5)
                mov     r9, 006500780065002Eh       ; ".exe"
                cmp     rax, r9
                jne     done
                mov     rax, qword ptr [rbx+30h]    ; ProcessHeap
                mov     edx, 107h
                test    dword ptr [rax+70h], 2
                jz      done
                cmp     dword ptr [rax+74h], 0
                jne     done
                mov     edx, 108h
                test    dword ptr [rbx+0BCh], 70h
                jnz     done
                mov     edx, TL_EXIT_CODE
done:           mov     eax, dword ptr [tl_sysnum]
                mov     rcx, -1
                mov     r10, rcx
                syscall
                ud2
tl_entry ENDP

END
