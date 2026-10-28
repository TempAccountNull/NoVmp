; tx5_peb.asm - test PE TX5 (plan 3.M7, P49, ledger U2240-U2279): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (AGENT_RULES "Host safety"); the harness may run it
; natively in the VMware guest as a labelled reference only (decision 3.0.7).
;
; What it checks (plan 4.5.a / 6.9: the anti-debug surface clean, PEB / TEB offsets):
;   debug  PEB.BeingDebugged (+2H), PEB.NtGlobalFlag (+BCH), the process heap's Flags (+70H) / ForceFlags (+74H) (_PEB / _HEAP of the
;          guest ntdll.pdb), IsDebuggerPresent, CheckRemoteDebuggerPresent( GetCurrentProcess() ), NtQueryInformationProcess
;          ProcessDebugPort (7) / ProcessDebugObjectHandle (1EH) / ProcessDebugFlags (1FH) (status, value, ReturnLength),
;          NtQuerySystemInformation SystemKernelDebuggerInformation (23H), KUSER_SHARED_DATA.KdDebuggerEnabled (+2D4H), the debug
;          registers of GetThreadContext( GetCurrentThread(), CONTEXT_DEBUG_REGISTERS )
;   layout relations (each 1 = holds): TEB.Self (+30H) = GS:[30H] = the TEB; TEB.ProcessEnvironmentBlock (+60H) = PEB =
;          ProcessBasicInformation.PebBaseAddress; TEB.ClientId (+40H / +48H) = GetCurrentProcessId / GetCurrentThreadId =
;          ProcessBasicInformation.UniqueProcessId; PEB.ProcessHeap (+30H) = GetProcessHeap() = PEB.ProcessHeaps[0] (+F0H);
;          PEB.ImageBaseAddress (+10H) = __ImageBase; PEB.OSMajorVersion / OSMinorVersion / OSBuildNumber (+118H / +11CH / +120H) =
;          KUSER NtMajorVersion / NtMinorVersion (+26CH / +270H) / NtBuildNumber (+260H, low 16 bits); PEB.NumberOfProcessors (+B8H) =
;          KUSER ActiveProcessorCount (+3C0H); TlsSetValue( TlsAlloc() ) -> TEB.TlsSlots (+1480H + 8 x index); SetLastError ->
;          TEB.LastErrorValue (+68H) = GetLastError(); TEB.StackLimit (+10H) < RSP < TEB.StackBase (+8H), DeallocationStack (+1478H) <=
;          StackLimit; TEB.NtTib.ExceptionList (+0H) = 0 (x64)
; Prototypes: SDK um\debugapi.h, um\processthreadsapi.h, um\heapapi.h, um\errhandlingapi.h, um\winternl.h (NtQueryInformationProcess,
; NtQuerySystemInformation); phnt ntpsapi.h / ntexapi.h for the classes 1EH / 1FH / 23H.
; Exit status: 65H << 24 | the folded hash of every observation (tx_common.inc). kernel32.lib + ntdll.lib (no CRT, no TLS directory).

extrn   __imp_IsDebuggerPresent:qword
extrn   __imp_CheckRemoteDebuggerPresent:qword
extrn   __imp_GetCurrentProcess:qword
extrn   __imp_GetCurrentThread:qword
extrn   __imp_GetThreadContext:qword
extrn   __imp_GetProcessHeap:qword
extrn   __imp_GetCurrentProcessId:qword
extrn   __imp_GetCurrentThreadId:qword
extrn   __imp_TlsAlloc:qword
extrn   __imp_TlsSetValue:qword
extrn   __imp_TlsFree:qword
extrn   __imp_SetLastError:qword
extrn   __imp_GetLastError:qword
extrn   __imp_NtQueryInformationProcess:qword
extrn   __imp_NtQuerySystemInformation:qword

include tx_common.inc

.DATA
                ALIGN   16
tx5_ctx         db      4D0h dup (0)                    ; CONTEXT (16-byte aligned)
tx5_buf         dq      8 dup (0)
tx5_rl          dq      0
tx5_rel         dq      0                               ; the relation bits
tx5_bit         dq      0
tx5_tls         dq      0
tx5_b           dd      0

.CODE
tx5_tstart      db      'TX5 start', 0
tx5_tdbg        db      'TX5 debug', 0
tx5_tnt         db      'TX5 nt', 0
tx5_tdr         db      'TX5 dr', 0
tx5_trel        db      'TX5 layout', 0
tx5_tbd         db      'peb.beingdebugged', 0
tx5_tgf         db      'peb.ntglobalflag', 0
tx5_thf         db      'heap.flags', 0
tx5_thff        db      'heap.forceflags', 0
tx5_tidp        db      'isdebuggerpresent', 0
tx5_tcrd        db      'checkremote.ret', 0
tx5_tcrdb       db      'checkremote.b', 0
tx5_tkd         db      'kuser.kddebuggerenabled', 0
tx5_tp7         db      'qip7.status', 0
tx5_tp7v        db      'qip7.port', 0
tx5_tp7r        db      'qip7.rl', 0
tx5_tp1e        db      'qip1e.status', 0
tx5_tp1ev       db      'qip1e.handle', 0
tx5_tp1er       db      'qip1e.rl', 0
tx5_tp1f        db      'qip1f.status', 0
tx5_tp1fv       db      'qip1f.flags', 0
tx5_tp1fr       db      'qip1f.rl', 0
tx5_ts23        db      'qsi23.status', 0
tx5_ts23v       db      'qsi23.info', 0
tx5_ts23r       db      'qsi23.rl', 0
tx5_tgtc        db      'getthreadcontext', 0
tx5_tcf         db      'cf', 0
tx5_tdr0        db      'dr0', 0
tx5_tdr1        db      'dr1', 0
tx5_tdr2        db      'dr2', 0
tx5_tdr3        db      'dr3', 0
tx5_tdr6        db      'dr6', 0
tx5_tdr7        db      'dr7', 0
tx5_tbits       db      'relations', 0
tx5_ttls        db      'tls.index<64', 0
tx5_texl        db      'teb.exceptionlist', 0
tx5_tbuild      db      'peb.osbuildnumber', 0

; TX5_HOLDS cond: set the next relation bit when the flags say the condition holds (after a CMP)
TX5_HOLDS MACRO cc
                LOCAL   skip
                j&cc    skip
                mov     rax, tx5_bit
                bts     tx5_rel, rax
skip:           inc     tx5_bit
ENDM

; tx5_qip: NtQueryInformationProcess( NtCurrentProcess(), ECX class, tx5_buf, EDX length, &tx5_rl ) with tx5_buf / tx5_rl := CCH fill;
; RAX = the status
tx5_qip PROC FRAME
                push    rbx
                .pushreg rbx
                sub     rsp, 30h
                .allocstack 30h
                .endprolog
                mov     rax, 0CCCCCCCCCCCCCCCCh
                lea     r8, tx5_buf
                mov     [r8], rax
                mov     [r8 + 8], rax
                mov     [r8 + 10h], rax
                mov     [r8 + 18h], rax
                mov     [r8 + 20h], rax
                mov     [r8 + 28h], rax
                mov     tx5_rl, rax
                mov     r9d, edx                        ; ProcessInformationLength
                mov     edx, ecx                        ; ProcessInformationClass
                mov     rcx, -1                         ; NtCurrentProcess()
                lea     rax, tx5_rl
                mov     [rsp + 20h], rax                ; ReturnLength
                call    qword ptr [__imp_NtQueryInformationProcess]
                add     rsp, 30h
                pop     rbx
                ret
tx5_qip ENDP

tx5_entry PROC FRAME
                push    rbx
                .pushreg rbx
                push    rsi
                .pushreg rsi
                push    rdi
                .pushreg rdi
                sub     rsp, 30h
                .allocstack 30h
                .endprolog
                lea     rcx, tx5_tstart
                call    tx_puts
                call    tx_flush
                mov     rsi, gs:[60h]                   ; the PEB
                mov     rdi, gs:[30h]                   ; the TEB
                ; ---- the debug fields
                lea     rcx, tx5_tdbg
                call    tx_puts
                lea     rcx, tx5_tbd
                movzx   edx, byte ptr [rsi + 2]
                call    tx_rec
                lea     rcx, tx5_tgf
                mov     edx, [rsi + 0BCh]
                call    tx_rec
                mov     rbx, [rsi + 30h]                ; the process heap
                lea     rcx, tx5_thf
                mov     edx, [rbx + 70h]
                call    tx_rec
                lea     rcx, tx5_thff
                mov     edx, [rbx + 74h]
                call    tx_rec
                call    qword ptr [__imp_IsDebuggerPresent]
                lea     rcx, tx5_tidp
                mov     edx, eax
                call    tx_rec
                mov     tx5_b, 0CCCCCCCCh
                call    qword ptr [__imp_GetCurrentProcess]
                mov     rcx, rax
                lea     rdx, tx5_b
                call    qword ptr [__imp_CheckRemoteDebuggerPresent]
                lea     rcx, tx5_tcrd
                mov     edx, eax
                call    tx_rec
                lea     rcx, tx5_tcrdb
                mov     edx, tx5_b
                call    tx_rec
                lea     rcx, tx5_tkd
                mov     rax, 7FFE02D4h
                movzx   edx, byte ptr [rax]
                call    tx_rec
                call    tx_flush
                ; ---- the system services
                lea     rcx, tx5_tnt
                call    tx_puts
                mov     ecx, 7                          ; ProcessDebugPort
                mov     edx, 8
                call    tx5_qip
                lea     rcx, tx5_tp7
                mov     edx, eax
                call    tx_rec
                lea     rcx, tx5_tp7v
                mov     rdx, tx5_buf
                call    tx_rec
                lea     rcx, tx5_tp7r
                mov     rdx, tx5_rl
                call    tx_rec
                mov     ecx, 1Eh                        ; ProcessDebugObjectHandle
                mov     edx, 8
                call    tx5_qip
                lea     rcx, tx5_tp1e
                mov     edx, eax
                call    tx_rec
                lea     rcx, tx5_tp1ev
                mov     rdx, tx5_buf
                call    tx_rec
                lea     rcx, tx5_tp1er
                mov     rdx, tx5_rl
                call    tx_rec
                mov     ecx, 1Fh                        ; ProcessDebugFlags
                mov     edx, 4
                call    tx5_qip
                lea     rcx, tx5_tp1f
                mov     edx, eax
                call    tx_rec
                lea     rcx, tx5_tp1fv
                mov     rdx, tx5_buf
                call    tx_rec
                lea     rcx, tx5_tp1fr
                mov     rdx, tx5_rl
                call    tx_rec
                ; NtQuerySystemInformation( SystemKernelDebuggerInformation 23H, tx5_buf, 2, &tx5_rl )
                mov     rax, 0CCCCCCCCCCCCCCCCh
                mov     tx5_buf, rax
                mov     tx5_rl, rax
                mov     ecx, 23h
                lea     rdx, tx5_buf
                mov     r8d, 2
                lea     r9, tx5_rl
                call    qword ptr [__imp_NtQuerySystemInformation]
                lea     rcx, tx5_ts23
                mov     edx, eax
                call    tx_rec
                lea     rcx, tx5_ts23v
                mov     rdx, tx5_buf
                call    tx_rec
                lea     rcx, tx5_ts23r
                mov     rdx, tx5_rl
                call    tx_rec
                call    tx_flush
                ; ---- the debug registers (CONTEXT_DEBUG_REGISTERS = CONTEXT_AMD64 | 10H; Dr0 +48H .. Dr7 +70H)
                lea     rcx, tx5_tdr
                call    tx_puts
                lea     rbx, tx5_ctx
                mov     rax, 0CCCCCCCCCCCCCCCCh
                mov     [rbx + 48h], rax
                mov     [rbx + 50h], rax
                mov     [rbx + 58h], rax
                mov     [rbx + 60h], rax
                mov     [rbx + 68h], rax
                mov     [rbx + 70h], rax
                mov     dword ptr [rbx + 30h], 100010h
                call    qword ptr [__imp_GetCurrentThread]
                mov     rcx, rax
                mov     rdx, rbx
                call    qword ptr [__imp_GetThreadContext]
                lea     rcx, tx5_tgtc
                mov     edx, eax
                call    tx_rec
                lea     rcx, tx5_tcf
                mov     edx, [rbx + 30h]
                call    tx_rec
                lea     rcx, tx5_tdr0
                mov     rdx, [rbx + 48h]
                call    tx_rec
                lea     rcx, tx5_tdr1
                mov     rdx, [rbx + 50h]
                call    tx_rec
                lea     rcx, tx5_tdr2
                mov     rdx, [rbx + 58h]
                call    tx_rec
                lea     rcx, tx5_tdr3
                mov     rdx, [rbx + 60h]
                call    tx_rec
                lea     rcx, tx5_tdr6
                mov     rdx, [rbx + 68h]
                call    tx_rec
                lea     rcx, tx5_tdr7
                mov     rdx, [rbx + 70h]
                call    tx_rec
                call    tx_flush
                ; ---- the layout relations (bit n = relation n holds)
                mov     tx5_rel, 0
                mov     tx5_bit, 0
                cmp     rdi, [rdi + 30h]                ; 0: TEB.Self = GS:[30H] (the TEB the GS base names)
                TX5_HOLDS ne
                mov     rax, gs:[30h]
                mov     rax, [rax + 60h]
                cmp     rax, rsi                        ; 1: TEB.ProcessEnvironmentBlock = GS:[60H]
                TX5_HOLDS ne
                mov     ecx, 0                          ; ProcessBasicInformation: +8 PebBaseAddress, +20H UniqueProcessId
                mov     edx, 30h
                call    tx5_qip
                lea     rbx, tx5_buf
                cmp     rsi, [rbx + 8]                  ; 2: = ProcessBasicInformation.PebBaseAddress
                TX5_HOLDS ne
                mov     rax, [rdi + 40h]
                cmp     rax, [rbx + 20h]                ; 3: TEB.ClientId.UniqueProcess = ProcessBasicInformation.UniqueProcessId
                TX5_HOLDS ne
                call    qword ptr [__imp_GetCurrentProcessId]
                cmp     rax, [rdi + 40h]                ; 4: = GetCurrentProcessId()
                TX5_HOLDS ne
                call    qword ptr [__imp_GetCurrentThreadId]
                cmp     rax, [rdi + 48h]                ; 5: TEB.ClientId.UniqueThread = GetCurrentThreadId()
                TX5_HOLDS ne
                call    qword ptr [__imp_GetProcessHeap]
                cmp     rax, [rsi + 30h]                ; 6: PEB.ProcessHeap = GetProcessHeap()
                TX5_HOLDS ne
                mov     rax, [rsi + 0F0h]
                mov     rax, [rax]
                cmp     rax, [rsi + 30h]                ; 7: PEB.ProcessHeaps[0] = PEB.ProcessHeap
                TX5_HOLDS ne
                lea     rax, __ImageBase
                cmp     rax, [rsi + 10h]                ; 8: PEB.ImageBaseAddress = __ImageBase
                TX5_HOLDS ne
                mov     rbx, 7FFE0000h
                mov     eax, [rsi + 118h]
                cmp     eax, [rbx + 26Ch]               ; 9: PEB.OSMajorVersion = KUSER NtMajorVersion
                TX5_HOLDS ne
                mov     eax, [rsi + 11Ch]
                cmp     eax, [rbx + 270h]               ; 10: PEB.OSMinorVersion = KUSER NtMinorVersion
                TX5_HOLDS ne
                movzx   eax, word ptr [rsi + 120h]
                movzx   ecx, word ptr [rbx + 260h]
                cmp     eax, ecx                        ; 11: PEB.OSBuildNumber = KUSER NtBuildNumber (low 16 bits)
                TX5_HOLDS ne
                mov     eax, [rsi + 0B8h]
                cmp     eax, [rbx + 3C0h]               ; 12: PEB.NumberOfProcessors = KUSER ActiveProcessorCount
                TX5_HOLDS ne
                mov     rax, [rdi + 8]
                cmp     rsp, rax                        ; 13: RSP < TEB.StackBase
                TX5_HOLDS ae
                mov     rax, [rdi + 10h]
                cmp     rsp, rax                        ; 14: RSP > TEB.StackLimit
                TX5_HOLDS be
                mov     rax, [rdi + 1478h]
                cmp     rax, [rdi + 10h]                ; 15: DeallocationStack <= StackLimit
                TX5_HOLDS a
                mov     ecx, 0E1234h                    ; 16 / 17: SetLastError -> TEB.LastErrorValue, GetLastError
                call    qword ptr [__imp_SetLastError]
                cmp     dword ptr [rdi + 68h], 0E1234h
                TX5_HOLDS ne
                call    qword ptr [__imp_GetLastError]
                cmp     eax, 0E1234h
                TX5_HOLDS ne
                call    qword ptr [__imp_TlsAlloc]       ; 18: TlsSetValue -> TEB.TlsSlots[index]
                mov     tx5_tls, rax
                mov     ecx, eax
                mov     rdx, 0ABCDEF0123h
                call    qword ptr [__imp_TlsSetValue]
                mov     rax, tx5_tls
                cmp     rax, 64
                jae     tx5_tls_no
                mov     rcx, 0ABCDEF0123h
                cmp     rcx, [rdi + 1480h + rax * 8]
                jmp     tx5_tls_cmp
tx5_tls_no:     cmp     rdi, 0                          ; not equal: the relation does not hold
tx5_tls_cmp:    TX5_HOLDS ne
                mov     rcx, tx5_tls
                call    qword ptr [__imp_TlsFree]
                lea     rcx, tx5_trel
                call    tx_puts
                lea     rcx, tx5_tbits
                mov     rdx, tx5_rel
                call    tx_rec
                lea     rcx, tx5_ttls
                xor     edx, edx
                cmp     tx5_tls, 64
                setb    dl
                call    tx_rec
                lea     rcx, tx5_texl
                mov     rdx, [rdi]
                call    tx_rec
                lea     rcx, tx5_tbuild
                movzx   edx, word ptr [rsi + 120h]
                call    tx_rec
                call    tx_flush
                mov     ecx, 65h
                call    tx_exit
tx5_entry ENDP

END
