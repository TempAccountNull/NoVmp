; t10_cm.asm - test PE T10 (first written by P48 for object W2 4dafd820; p3cm ledger U2282, notes\eread\P50_cmapi_min_run14.md): self-written 64-bit guest code, never derived from the sample.
; GUEST CODE: the emulator only reads it as a file and writes it into emulated memory (emu-oep maps it as data, AGENT_RULES "Host safety");
; the harness may run it natively in the VMware guest as a labelled reference only (decision 3.0.7), started detached (tm_nt run.detached,
; like T2 / T3 / T5 / T7 / T8 / T9).
;
; What it checks (USER DECISION 2026-10-28, p3cm STEP 2 "CMApi MINIMAL DEVICE MODEL"; sample run 12 / 13's stop is
; cfgmgr32.dll's DllMain opening \Device\DeviceApi\CMApi): cfgmgr32.dll is imported, so its DllMain runs at the process start as in the
; sample (the loader -> cfgmgr32 entry, DLL_PROCESS_ATTACH -> the NtCreateFile of the device), and then
;   CM_Locate_DevNodeW( &dn, NULL, CM_LOCATE_DEVNODE_NORMAL )     - the root of the device tree, the first call any CM_ user makes
; Prototype / constants: Windows SDK 10.0.26100.0 um\cfgmgr32.h:3204-3208 CM_Locate_DevNodeW( PDEVINST pdnDevInst, DEVINSTID_W pDeviceID,
; ULONG ulFlags ) -> CONFIGRET; :95 DEVINST = DWORD; :1102 CM_LOCATE_DEVNODE_NORMAL 0; the CR_ codes :4693-4756 (CR_SUCCESS 0 ...
; CR_NO_CM_SERVICES 32H, CR_ACCESS_DENIED 33H). Nothing of the device tree is kept: the exit status ENCODES only the CONFIGRET and whether
; the DEVINST was written (ExitProcess( code ), so the native and the emulated runs compare as one number):
;   bits 7:0    the CONFIGRET (CR_ code, all CR_ values are below 100H)
;   bit 8       the DEVINST was written (not the fill any more)
;   bit 9       the CONFIGRET was above FFH (then bits 7:0 hold its low byte)
;   bits 31:24  3AH (the T10 marker)
; Natively the device exists: CR_SUCCESS and a DEVINST - measured 3A000100H (twice, detached). In the emulated environment the device
; is the MINIMAL device of the environment (Emulator/src/nt_kernel_vfs.cpp cm_open): cfgmgr32's open answered as measured (tm_nt
; cm.open), then its DllMain asks NtQueryLicenseValue (tm_nt lic.tsas.4) and returns TRUE; CM_Locate_DevNodeW( root ) is resolved by
; cfgmgr32 itself (the root's DEVINST 1 from its own string table, cfgmgr32.dll+C26E) WITHOUT any request to the device - nothing of
; the device tree is asked. --Sys = --HLE (hle.cpp CM_Locate_DevNodeW( root )) = native: 3A000100H (suite win_env_sys_loader). The P48
; measured-miss model (W2, branch agent/p3m7e-w2) made cfgmgr32's DllMain return FALSE - T10's code never ran there.
; Linked with the SDK import libraries cfgmgr32.lib / kernel32.lib only (no CRT, no TLS); ExitProcess: um\processthreadsapi.h.

T10_FILL        equ     0CCCCCCCCh

extrn   __imp_CM_Locate_DevNodeW:qword
extrn   __imp_ExitProcess:qword

.DATA
                ALIGN   16
t10_dn          dd      T10_FILL                        ; the DEVINST (never printed or kept)
                dd      T10_FILL

.CODE
t10_entry PROC
                push    rbx
                sub     rsp, 20h                        ; 1 push + 20H: RSP 16-aligned at each CALL (entry RSP = 8 mod 16)
                lea     rcx, t10_dn                     ; pdnDevInst
                xor     edx, edx                        ; pDeviceID NULL (the root)
                xor     r8d, r8d                        ; CM_LOCATE_DEVNODE_NORMAL
                call    qword ptr [__imp_CM_Locate_DevNodeW]
                mov     ebx, eax
                movzx   ecx, al                         ; bits 7:0 the CONFIGRET's low byte
                or      ecx, 3A000000h                  ; the marker
                cmp     ebx, 0FFh
                jbe     t10_b8
                or      ecx, 200h                       ; bit 9: above FFH
t10_b8:         cmp     dword ptr [t10_dn], T10_FILL
                je      t10_end
                or      ecx, 100h                       ; bit 8: the DEVINST was written
t10_end:        call    qword ptr [__imp_ExitProcess]   ; does not return
                ud2
t10_entry ENDP

END
