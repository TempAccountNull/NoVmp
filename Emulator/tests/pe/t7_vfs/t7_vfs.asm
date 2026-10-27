; t7_vfs.asm - test PE T7 (P45, ledger U2133; notes\eread\P45_vfs_run10.md): self-written 64-bit guest code, never derived from the
; sample. GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (emu-oep maps it as data, AGENT_RULES "Host
; safety"); the harness may run it natively in the VMware guest as a labelled reference only (decision 3.0.7), started detached (tm_nt
; run.detached, like T2 / T3 / T5).
;
; What it checks (USER DECISION 2026-10-27 "virtual FS, measured misses"): two file-system misses next to its own image, with the
; arguments of the calls that reach them in the sample's start (sample run 9 system call 539: ntdll LdrpMapResourceFile's NtOpenFile):
;   A  NtOpenFile( &h, SYNCHRONIZE | FILE_READ_DATA (100001H), "\??\<its directory>\t7_vfs_absent_5d2c.dat" OBJ_CASE_INSENSITIVE (40H),
;      &iosb, FILE_SHARE_READ | FILE_SHARE_DELETE (5), FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE (60H) )  - a missing file
;   B  the same for "\??\<the parent of its directory>\SystemResources\<its file name>.mun"                           - a missing folder
;      (the .mun search of ntdll for an image with resources: SystemResources is a SIBLING of the image's folder, as of System32 -
;      sample red9 / run 9 system call 539 opened "\??\C:\Users\<user>\SystemResources\<image>.mun" for the image in ...\Desktop)
;   C  NtQueryAttributesFile( "\??\<its directory>\t7_vfs_absent_5d2c.dat" 40H, &basic_info )                        - a missing file
; Its directory and file name come from its own process parameters (PEB +20H ProcessParameters, +60H ImagePathName: the guest ntdll.pdb
; _PEB / _RTL_USER_PROCESS_PARAMETERS - emu/win_fields.hpp), so the native run and the emulated run use their own directory.
; The exit status ENCODES what it got (ExitProcess( code ), so the native and the emulated runs compare as one number):
;   bits 7:0 = A's status low byte, 15:8 = B's, 23:16 = C's (each status must be C00000xxH, else bit 31 is set);
;   bit 24 = A's handle not written, 25 = A's IoStatusBlock not written, 26 = B's handle not written, 27 = B's IoStatusBlock not written,
;   bit 28 = C's FILE_BASIC_INFORMATION (28H bytes) not written; bits 30:29 0.
; Prototypes: phnt ntioapi.h:2294-2301 NtOpenFile, :3053-3057 NtQueryAttributesFile (System Informer, MIT, reference only); the Windows
; SDK 10.0.26100.0 um\processthreadsapi.h ExitProcess; OBJECT_ATTRIBUTES / UNICODE_STRING shared\ntdef.h. Linked with the SDK import
; libraries ntdll.lib / kernel32.lib only (no CRT, no TLS).

T7_FILL         equ     0CCCCCCCCCCCCCCCCh

extrn   __imp_NtOpenFile:qword
extrn   __imp_NtQueryAttributesFile:qword
extrn   __imp_ExitProcess:qword

.CONST
                ALIGN   2
t7_missing      dw      't','7','_','v','f','s','_','a','b','s','e','n','t','_','5','d','2','c','.','d','a','t'
t7_missing_n    equ     22                              ; characters
t7_sysres       dw      'S','y','s','t','e','m','R','e','s','o','u','r','c','e','s','\'
t7_sysres_n     equ     16
t7_mun          dw      '.','m','u','n'
t7_mun_n        equ     4
t7_prefix       dw      '\','?','?','\'

.DATA
                ALIGN   8
t7_us           dw      0, 0                            ; UNICODE_STRING Length, MaximumLength
                dd      0
                dq      t7_path                         ; Buffer
t7_oa           dd      30h, 0                          ; OBJECT_ATTRIBUTES Length 30H
                dq      0                               ; RootDirectory
                dq      t7_us                           ; ObjectName
                dd      40h, 0                          ; Attributes OBJ_CASE_INSENSITIVE
                dq      0, 0                            ; SecurityDescriptor, SecurityQualityOfService
t7_ha           dq      T7_FILL
t7_iosba        dq      T7_FILL, T7_FILL
t7_hb           dq      T7_FILL
t7_iosbb        dq      T7_FILL, T7_FILL
t7_fbi          dq      T7_FILL, T7_FILL, T7_FILL, T7_FILL, T7_FILL
t7_dirlen       dq      0                               ; characters of "\??\<directory>\" in t7_path
t7_pdirlen      dq      0                               ; characters of "\??\<the parent of the directory>\" (a prefix of it)
t7_name         dq      0                               ; the image's file name (pointer into the parameters) and its length
t7_namelen      dq      0
t7_path         dw      600 dup (0)

.CODE
; t7_build: t7_path[0 .. dirlen) is "\??\<directory>\"; append RDX characters from RSI, then (if R8 != 0) R9 characters from R8;
; set t7_us; returns nothing (RDI, RCX, RSI used)
t7_append PROC
                lea     rdi, t7_path
                mov     rax, t7_dirlen
                lea     rdi, [rdi + rax*2]
                mov     rcx, rdx
                rep movsw
                test    r8, r8
                jz      t7_set
                mov     rsi, r8
                mov     rcx, r9
                rep movsw
t7_set:         lea     rax, t7_path
                sub     rdi, rax                        ; bytes used
                mov     word ptr [t7_us], di            ; Length
                add     edi, 2
                mov     word ptr [t7_us + 2], di        ; MaximumLength
                mov     word ptr [rax + rdi - 2], 0     ; NUL after the name (not counted)
                ret
t7_append ENDP

t7_entry PROC
                push    rbx
                push    rsi
                push    rdi
                push    r12
                sub     rsp, 38h                        ; 4 pushes + 38H: RSP 16-aligned at each CALL (entry RSP = 8 mod 16)
                ; the image path: PEB (GS:[60H]) -> ProcessParameters (+20H) -> ImagePathName (+60H: Length, +68H: Buffer)
                mov     rax, gs:[60h]
                mov     rax, [rax + 20h]
                movzx   ecx, word ptr [rax + 60h]
                shr     ecx, 1                          ; characters
                mov     rsi, [rax + 68h]
                cmp     ecx, 500
                ja      t7_bad
                ; the last backslash
                mov     edx, ecx
t7_scan:        test    edx, edx
                jz      t7_bad
                dec     edx
                cmp     word ptr [rsi + rdx*2], '\'
                jne     t7_scan
                inc     edx                             ; characters of "<directory>\"
                ; the backslash before it: the parent's "<parent>\"
                lea     r8d, [rdx - 1]
t7_pscan:       test    r8d, r8d
                jz      t7_bad
                dec     r8d
                cmp     word ptr [rsi + r8*2], '\'
                jne     t7_pscan
                inc     r8d
                add     r8d, 4
                mov     t7_pdirlen, r8
                lea     rax, [rsi + rdx*2]
                mov     t7_name, rax
                mov     eax, ecx
                sub     eax, edx
                mov     t7_namelen, rax
                ; t7_path = "\??\" + "<directory>\"
                lea     rdi, t7_path
                lea     r12, t7_prefix
                mov     rax, [r12]
                mov     [rdi], rax
                add     rdi, 8
                mov     ecx, edx
                rep movsw
                add     edx, 4
                mov     t7_dirlen, rdx
                ; A: NtOpenFile of the missing file
                lea     rsi, t7_missing
                mov     edx, t7_missing_n
                xor     r8d, r8d
                call    t7_append
                lea     rcx, t7_ha
                mov     edx, 100001h
                lea     r8, t7_oa
                lea     r9, t7_iosba
                mov     dword ptr [rsp + 20h], 5
                mov     dword ptr [rsp + 28h], 60h
                call    qword ptr [__imp_NtOpenFile]
                mov     ebx, eax                        ; status A
                ; C: NtQueryAttributesFile of the same name
                lea     rcx, t7_oa
                lea     rdx, t7_fbi
                call    qword ptr [__imp_NtQueryAttributesFile]
                mov     r12d, eax                       ; status C
                ; B: NtOpenFile of <parent>\SystemResources\<name>.mun
                mov     rax, t7_pdirlen
                mov     t7_dirlen, rax                  ; "\??\<parent>\" (already in t7_path: a prefix of "\??\<directory>\")
                lea     rsi, t7_sysres
                mov     edx, t7_sysres_n
                xor     r8d, r8d
                call    t7_append
                mov     rax, t7_dirlen
                add     rax, t7_sysres_n
                mov     t7_dirlen, rax
                mov     rsi, t7_name
                mov     rdx, t7_namelen
                lea     r8, t7_mun
                mov     r9d, t7_mun_n
                call    t7_append
                lea     rcx, t7_hb
                mov     edx, 100001h
                lea     r8, t7_oa
                lea     r9, t7_iosbb
                mov     dword ptr [rsp + 20h], 5
                mov     dword ptr [rsp + 28h], 60h
                call    qword ptr [__imp_NtOpenFile]
                ; the exit code: EAX = status B, EBX = status A, R12D = status C
                xor     ecx, ecx
                mov     edx, eax
                and     edx, 0FFFFFF00h
                cmp     edx, 0C0000000h
                jne     t7_hi
                mov     edx, ebx
                and     edx, 0FFFFFF00h
                cmp     edx, 0C0000000h
                jne     t7_hi
                mov     edx, r12d
                and     edx, 0FFFFFF00h
                cmp     edx, 0C0000000h
                je      t7_lo
t7_hi:          or      ecx, 80000000h
t7_lo:          movzx   edx, bl
                or      ecx, edx
                movzx   edx, al
                shl     edx, 8
                or      ecx, edx
                movzx   edx, r12b
                shl     edx, 16
                or      ecx, edx
                mov     rdx, T7_FILL
                cmp     t7_ha, rdx
                jne     t7_f1
                or      ecx, 01000000h
t7_f1:          cmp     qword ptr [t7_iosba], rdx
                jne     t7_f2
                cmp     qword ptr [t7_iosba + 8], rdx
                jne     t7_f2
                or      ecx, 02000000h
t7_f2:          cmp     t7_hb, rdx
                jne     t7_f3
                or      ecx, 04000000h
t7_f3:          cmp     qword ptr [t7_iosbb], rdx
                jne     t7_f4
                cmp     qword ptr [t7_iosbb + 8], rdx
                jne     t7_f4
                or      ecx, 08000000h
t7_f4:          lea     rsi, t7_fbi
                mov     eax, 5
t7_f5:          cmp     [rsi], rdx
                jne     t7_done
                add     rsi, 8
                dec     eax
                jnz     t7_f5
                or      ecx, 10000000h
                jmp     t7_done
t7_bad:         mov     ecx, 0E0000001h                 ; no usable image path (never expected)
t7_done:        call    qword ptr [__imp_ExitProcess]   ; does not return
                ud2
t7_entry ENDP

END
