; t3_tls.asm - test PE T3 of plan 3.M5 (ledger U2015; notes\eread\P35_phase3_m5.md): self-written 64-bit guest code, never
; derived from the sample. GUEST CODE: the emulator only reads it as a file and writes it into emulated memory; the harness may
; run it natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; A TLS directory (the linker points IMAGE_DIRECTORY_ENTRY_TLS at the public symbol _tls_used: winnt.h IMAGE_TLS_DIRECTORY64)
; with one callback (PIMAGE_TLS_CALLBACK: DllHandle, Reason, Reserved). The callback records its calls and arguments; the entry
; point checks: exactly one call before the entry, Reason = DLL_PROCESS_ATTACH (1, winnt.h), DllHandle = the PEB's
; ImageBaseAddress, and the thread's TLS block (TEB ThreadLocalStoragePointer gs:[58H] -> slot [_tls_index]) = the template
; (two qwords). SizeOfZeroFill is 0: the first native reference run (P35) with SizeOfZeroFill 10H found NON-zero bytes after the
; template in the block the Windows 10 loader gave the thread (exit 105H) - the native result is recorded in P35, the test does
; not depend on it (emu::win zero-fills as the PE format describes; selftest win_env_hle_tls). No imports: the exit is one direct SYSCALL of
; NtTerminateProcess( NtCurrentProcess(), 2EH ) whose service number (the 4-byte field after the marker "T3SYSNUM", FFFFFFFFH
; here) the harness patches from the environment's ntdll capture (decision 3.0.8), as for T1. A failed check exits 101H..105H.
; Under emu-oep --HLE the loader's TLS start is done by emu::win (index 0, block, callbacks from a start trampoline); under --Sys
; the image is refused (the real ntdll!LdrInitializeThunk start, which runs TLS callbacks, is not modelled in M5).

T3_EXIT_CODE    equ     2Eh

PUBLIC _tls_used

.DATA
                ALIGN   16
t3_calls        dd      0
                dd      0
t3_handle       dq      0
t3_reason       dq      0
t3_reserved     dq      0
_tls_index      dd      0FFFFFFFFh          ; written by the loader (the TLS index of this module)
                ALIGN   16
t3_tls_start    dq      1122334455667788h   ; the TLS template (raw data of the thread's TLS block)
                dq      99AABBCCDDEEFF00h
t3_tls_end      LABEL   BYTE

.CONST
                ALIGN   16
t3_marker       db      'T3SYSNUM'          ; the harness finds the patch field by this marker (exactly once)
t3_sysnum       dd      0FFFFFFFFh          ; NtTerminateProcess service number, patched per environment
                dd      0
                ALIGN   16
_tls_used       dq      t3_tls_start        ; StartAddressOfRawData
                dq      t3_tls_end          ; EndAddressOfRawData
                dq      _tls_index          ; AddressOfIndex
                dq      t3_callbacks        ; AddressOfCallBacks
                dd      0                   ; SizeOfZeroFill (see the header)
                dd      0                   ; Characteristics
t3_callbacks    dq      t3_callback
                dq      0

.CODE
t3_callback PROC                            ; ( DllHandle = RCX, Reason = RDX, Reserved = R8 )
                inc     dword ptr [t3_calls]
                mov     qword ptr [t3_handle], rcx
                mov     qword ptr [t3_reason], rdx
                mov     qword ptr [t3_reserved], r8
                ret
t3_callback ENDP

t3_entry PROC
                cmp     dword ptr [t3_calls], 1
                jne     fail1
                cmp     qword ptr [t3_reason], 1        ; DLL_PROCESS_ATTACH
                jne     fail2
                mov     rcx, qword ptr gs:[60h]         ; PEB
                mov     rcx, qword ptr [rcx+10h]        ; ImageBaseAddress
                cmp     qword ptr [t3_handle], rcx
                jne     fail3
                mov     rcx, qword ptr gs:[58h]         ; TEB ThreadLocalStoragePointer
                mov     edx, dword ptr [_tls_index]
                mov     rcx, qword ptr [rcx+rdx*8]      ; this module's TLS block
                mov     rax, 1122334455667788h
                cmp     qword ptr [rcx], rax
                jne     fail4
                mov     rax, 99AABBCCDDEEFF00h
                cmp     qword ptr [rcx+8], rax          ; the template's second qword
                jne     fail5
                mov     edx, T3_EXIT_CODE
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
done:           mov     eax, dword ptr [t3_sysnum]      ; RIP-relative: the patched service number
                mov     rcx, -1                         ; NtCurrentProcess()
                mov     r10, rcx
                syscall
                ud2
t3_entry ENDP

END
