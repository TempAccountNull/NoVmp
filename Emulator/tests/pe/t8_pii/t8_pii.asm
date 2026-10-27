; t8_pii.asm - test PE T8 (P46, ledger U2143; notes\eread\P46_run11.md): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (emu-oep maps it as data, AGENT_RULES "Host safety");
; the harness may run it natively in the VMware guest as a labelled reference only (decision 3.0.7), started detached (tm_nt run.detached,
; like T2 / T3 / T5 / T7).
;
; What it measures (USER AUTHORIZED 2026-10-27, P46 STEP 2 (a); sample run 10's stop, system call 633): NtQueryInformationProcess(
; NtCurrentProcess(), 25H ProcessImageInformation, buf, 40H, NULL ) of an image linked WITHOUT /DYNAMICBASE (DllCharacteristics without
; IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE 40H) and therefore loaded at its preferred ImageBase (140000000H, the linker's default for a 64-bit
; EXE) - the state of the sample (no DYNAMIC_BASE, at its preferred base). tm_nt pii (P45, U2141) measured an ASLR-relocated image.
;   call 1  Length 40H, ReturnLength NULL (the sample's arguments; kernelbase GetSystemInfo -> GetSystemInfoInternal)
;   call 2  Length 40H, &ReturnLength
;   call 3  Length 3FH, &ReturnLength (a short buffer)
; Prototype: phnt ntpsapi.h PROCESSINFOCLASS 37 ProcessImageInformation ("q: SECTION_IMAGE_INFORMATION"), ntmmapi.h:1274-1320
; SECTION_IMAGE_INFORMATION (System Informer 4.0.26241.138, MIT, reference only): TransferAddress +0, ZeroBits +8, MaximumStackSize +10H,
; CommittedStackSize +18H, SubSystemType +20H, SubSystemVersion +24H (Minor, Major), OperatingSystemVersion +28H (Major, Minor),
; ImageCharacteristics +2CH, DllCharacteristics +2EH, Machine +30H, ImageContainsCode +32H, ImageFlags +33H (ComPlusNativeReady 1,
; ComPlusILOnly 2, ImageDynamicallyRelocated 4, ImageMappedFlat 8, BaseBelow4gb 10H, ComPlusPrefer32bit 20H), LoaderFlags +34H,
; ImageFileSize +38H, CheckSum +3CH. Its own PE header (um\winnt.h IMAGE_NT_HEADERS64) is read from its mapped image (__ImageBase).
; The exit status ENCODES what it got (ExitProcess( code ), so the native and the emulated runs compare as one number):
;   bit 0  TransferAddress = the mapped base + AddressOfEntryPoint      bit 8   DllCharacteristics = DllCharacteristics
;   bit 1  ZeroBits 0 and bytes +0CH..+0FH 0                           bit 9   Machine = FileHeader.Machine
;   bit 2  MaximumStackSize = SizeOfStackReserve                       bit 10  ImageContainsCode = 1
;   bit 3  CommittedStackSize = SizeOfStackCommit                      bit 11  LoaderFlags = LoaderFlags
;   bit 4  SubSystemType = Subsystem                                   bit 12  ImageFileSize = the end of the last section's raw data
;   bit 5  SubSystemVersion = MajorSubsystemVersion << 16 | Minor              (= the file's size: T8 has no overlay - the suite checks it)
;   bit 6  OperatingSystemVersion = MinorOSVersion << 16 | Major       bit 13  CheckSum = CheckSum
;   bit 7  ImageCharacteristics = FileHeader.Characteristics           bit 14  the mapped header's ImageBase = the mapped base
;   bits 23:16  ImageFlags (the byte at +33H)
;   bit 24  its own DllCharacteristics have DYNAMIC_BASE (0 expected)   bit 25  the mapped base is below 4 GB
;   bit 26  call 1 wrote nothing past 40H (buffer bytes 40H..5FH unchanged)
;   bit 27  call 2: STATUS_SUCCESS, ReturnLength 40H, the same 40H bytes as call 1, nothing past 40H
;   bit 28  call 3: STATUS_INFO_LENGTH_MISMATCH (C0000004H), nothing written, ReturnLength not written
;   call 1 not STATUS_SUCCESS: exit E1000000H | the status' low 16 bits
; Linked with the SDK import libraries ntdll.lib / kernel32.lib only (no CRT, no TLS); prototypes phnt ntpsapi.h NtQueryInformationProcess,
; the Windows SDK 10.0.26100.0 um\processthreadsapi.h ExitProcess.

T8_FILL         equ     0CCCCCCCCCCCCCCCCh

extrn   __imp_NtQueryInformationProcess:qword
extrn   __imp_ExitProcess:qword
extrn   __ImageBase:byte

.DATA
                ALIGN   16
t8_buf1         dq      12 dup (T8_FILL)                ; 60H bytes: 40H answer + 20H guard
t8_buf2         dq      12 dup (T8_FILL)
t8_buf3         dq      12 dup (T8_FILL)
t8_rl2          dd      0CCCCCCCCh
t8_rl3          dd      0CCCCCCCCh

.CODE
; t8_query: NtQueryInformationProcess( NtCurrentProcess(), 25H, RCX = buffer, EDX = Length, R8 = &ReturnLength or NULL ) -> EAX status
t8_query PROC
                sub     rsp, 38h
                mov     r9d, edx                        ; ProcessInformationLength
                mov     [rsp + 20h], r8                 ; ReturnLength
                mov     r8, rcx                         ; ProcessInformation
                mov     edx, 25h                        ; ProcessImageInformation
                mov     rcx, -1                         ; NtCurrentProcess()
                call    qword ptr [__imp_NtQueryInformationProcess]
                add     rsp, 38h
                ret
t8_query ENDP

; t8_guard: ZF = 1 when the 20H bytes at RCX + 40H are all the fill (nothing written past 40H)
t8_guard PROC
                mov     rax, T8_FILL
                cmp     [rcx + 40h], rax
                jne     t8_g_done
                cmp     [rcx + 48h], rax
                jne     t8_g_done
                cmp     [rcx + 50h], rax
                jne     t8_g_done
                cmp     [rcx + 58h], rax
t8_g_done:      ret
t8_guard ENDP

t8_entry PROC
                push    rbx
                push    rsi
                push    rdi
                push    r12
                push    r13
                sub     rsp, 30h                        ; 5 pushes + 30H: RSP 16-aligned at each CALL (entry RSP = 8 mod 16)
                ; call 1: the sample's arguments
                lea     rcx, t8_buf1
                mov     edx, 40h
                xor     r8d, r8d
                call    t8_query
                test    eax, eax
                jz      t8_ok1
                movzx   ecx, ax
                or      ecx, 0E1000000h
                jmp     t8_done
t8_ok1:         lea     rsi, __ImageBase                ; the mapped base (RIP-relative: wherever the image lies)
                movsxd  rax, dword ptr [rsi + 3Ch]      ; e_lfanew
                lea     rdi, [rsi + rax]                ; IMAGE_NT_HEADERS64
                lea     r12, [rdi + 18h]                ; IMAGE_OPTIONAL_HEADER64
                lea     rbx, t8_buf1
                xor     r13d, r13d                      ; the exit code
                ; bit 0: TransferAddress = base + AddressOfEntryPoint
                mov     eax, dword ptr [r12 + 10h]
                add     rax, rsi
                cmp     [rbx], rax
                jne     t8_b1
                or      r13d, 1
t8_b1:          ; bit 1: ZeroBits 0 and +0CH..+0FH 0
                cmp     qword ptr [rbx + 8], 0
                jne     t8_b2
                or      r13d, 2
t8_b2:          ; bit 2 / 3: the stack sizes
                mov     rax, [r12 + 48h]
                cmp     [rbx + 10h], rax
                jne     t8_b3
                or      r13d, 4
t8_b3:          mov     rax, [r12 + 50h]
                cmp     [rbx + 18h], rax
                jne     t8_b4
                or      r13d, 8
t8_b4:          ; bit 4: SubSystemType (ULONG) = Subsystem (USHORT)
                movzx   eax, word ptr [r12 + 44h]
                cmp     dword ptr [rbx + 20h], eax
                jne     t8_b5
                or      r13d, 10h
t8_b5:          ; bit 5: SubSystemVersion = MajorSubsystemVersion << 16 | MinorSubsystemVersion
                movzx   eax, word ptr [r12 + 30h]
                shl     eax, 16
                movzx   edx, word ptr [r12 + 32h]
                or      eax, edx
                cmp     dword ptr [rbx + 24h], eax
                jne     t8_b6
                or      r13d, 20h
t8_b6:          ; bit 6: OperatingSystemVersion = MinorOperatingSystemVersion << 16 | MajorOperatingSystemVersion
                movzx   eax, word ptr [r12 + 2Ah]
                shl     eax, 16
                movzx   edx, word ptr [r12 + 28h]
                or      eax, edx
                cmp     dword ptr [rbx + 28h], eax
                jne     t8_b7
                or      r13d, 40h
t8_b7:          ; bit 7: ImageCharacteristics = FileHeader.Characteristics
                movzx   eax, word ptr [rdi + 16h]
                cmp     word ptr [rbx + 2Ch], ax
                jne     t8_b8
                or      r13d, 80h
t8_b8:          ; bit 8: DllCharacteristics
                movzx   eax, word ptr [r12 + 46h]
                cmp     word ptr [rbx + 2Eh], ax
                jne     t8_b9
                or      r13d, 100h
t8_b9:          ; bit 9: Machine
                movzx   eax, word ptr [rdi + 4]
                cmp     word ptr [rbx + 30h], ax
                jne     t8_b10
                or      r13d, 200h
t8_b10:         ; bit 10: ImageContainsCode 1
                cmp     byte ptr [rbx + 32h], 1
                jne     t8_b11
                or      r13d, 400h
t8_b11:         ; bit 11: LoaderFlags
                mov     eax, dword ptr [r12 + 68h]
                cmp     dword ptr [rbx + 34h], eax
                jne     t8_b12
                or      r13d, 800h
t8_b12:         ; bit 12: ImageFileSize = max over the sections of PointerToRawData + SizeOfRawData
                movzx   ecx, word ptr [rdi + 6]         ; NumberOfSections
                movzx   eax, word ptr [rdi + 14h]       ; SizeOfOptionalHeader
                lea     r8, [r12 + rax]                 ; the first IMAGE_SECTION_HEADER
                xor     r9d, r9d
t8_scn:         test    ecx, ecx
                jz      t8_scn_end
                mov     eax, dword ptr [r8 + 14h]       ; PointerToRawData
                add     eax, dword ptr [r8 + 10h]       ; + SizeOfRawData
                cmp     eax, r9d
                jbe     t8_scn_next
                mov     r9d, eax
t8_scn_next:    add     r8, 28h
                dec     ecx
                jmp     t8_scn
t8_scn_end:     cmp     dword ptr [rbx + 38h], r9d
                jne     t8_b13
                or      r13d, 1000h
t8_b13:         ; bit 13: CheckSum
                mov     eax, dword ptr [r12 + 40h]
                cmp     dword ptr [rbx + 3Ch], eax
                jne     t8_b14
                or      r13d, 2000h
t8_b14:         ; bit 14: the mapped header's ImageBase = the mapped base
                cmp     [r12 + 18h], rsi
                jne     t8_flags
                or      r13d, 4000h
t8_flags:       ; bits 23:16: ImageFlags
                movzx   eax, byte ptr [rbx + 33h]
                shl     eax, 16
                or      r13d, eax
                ; bit 24: DYNAMIC_BASE in its own header; bit 25: base below 4 GB
                test    word ptr [r12 + 46h], 40h
                jz      t8_b25
                or      r13d, 1000000h
t8_b25:         mov     rax, rsi
                shr     rax, 32
                jnz     t8_b26
                or      r13d, 2000000h
t8_b26:         ; bit 26: call 1 wrote nothing past 40H
                mov     rcx, rbx
                call    t8_guard
                jne     t8_c2
                or      r13d, 4000000h
t8_c2:          ; call 2: Length 40H, &ReturnLength
                lea     rcx, t8_buf2
                mov     edx, 40h
                lea     r8, t8_rl2
                call    t8_query
                test    eax, eax
                jnz     t8_c3
                cmp     dword ptr [t8_rl2], 40h
                jne     t8_c3
                lea     rcx, t8_buf2
                call    t8_guard
                jne     t8_c3
                lea     rsi, t8_buf1
                lea     rdi, t8_buf2
                mov     ecx, 8
                repe cmpsq                              ; the same 40H bytes as call 1
                jne     t8_c3
                or      r13d, 8000000h
t8_c3:          ; call 3: Length 3FH, &ReturnLength
                lea     rcx, t8_buf3
                mov     edx, 3Fh
                lea     r8, t8_rl3
                call    t8_query
                cmp     eax, 0C0000004h
                jne     t8_end
                cmp     dword ptr [t8_rl3], 0CCCCCCCCh
                jne     t8_end
                lea     rcx, t8_buf3
                call    t8_guard
                jne     t8_end
                mov     rax, T8_FILL
                lea     rdi, t8_buf3
                mov     ecx, 8
                repe scasq                              ; the first 40H bytes untouched too
                jne     t8_end
                or      r13d, 10000000h
t8_end:         mov     ecx, r13d
t8_done:        call    qword ptr [__imp_ExitProcess]   ; does not return
                ud2
t8_entry ENDP

END
