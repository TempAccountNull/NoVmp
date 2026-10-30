// emu::win exception path (plan 3.M7 = 3.B 6.5 / 6.9 / 4.5.a, P49, ledger U2250-U2256): what the emulated kernel does with a CPU event of
// the user-mode program, the way Windows does it - measured with our own test PEs TX1 - TX6 run natively in the VMware guest (decision
// 3.0.7; notes\eread\P49_phase3_m7.md) and cited per item below. Nothing here touches the host: the guest's events are the fork's (SDM
// delivery through the emulated IDT), the frames are written into guest memory, the handlers run in the engine.
//
// 1. Delivery (U2250). The CPU events reach an emulated IDT (UC_CTL_X86_EVENT_DELIVERY = UC_X86_DELIVER_EXC_IDT, fork U963): the fork
//    delivers each exception / INT n / INT3 / INT1 per 253668-093 Vol3A 7.12 / 7.14 (the gate's DPL check for INT n, the error code, RIP
//    = the faulting instruction or the next one, RF in the pushed RFLAGS: Vol3B 20.3.1.1 - 1 for faults and for traps of a REP
//    iteration but the last) onto the TSS RSP0 stack. Gate DPL 3 = the vectors Windows lets user mode raise: 3, 4, 29H, 2CH, 2DH, 2EH
//    (measured: INT 1 (TX1 e14) and INT 41H (e29) end in #GP; ReactOS 0.4.16 ntoskrnl/ke/amd64/except.c:20-44 KiInterruptInitTable as
//    reference). Each gate points at its own 4-byte stub in the kernel page (HLT, never executed: the code hook at the stub runs the
//    kernel here). A #PF has no IDT path (the engine has no guest paging): the memory hook stops the run, the event is taken from the
//    CPU state at the fault (RIP, RFLAGS with RF = 1 - fault class, Vol3B 20.3.1.1) and enters the same path.
// 2. The save (U2251). A kernel stub executes FXSAVE64 and XSAVEC (EDX:EAX = the user components without x87 / SSE) into the kernel
//    page - the fork's SDM semantics give the legacy image (FCW, FSW, abridged FTW, FOP, FIP, FDP, MXCSR, MXCSR_MASK, ST0-7, XMM0-15)
//    and XSTATE_BV = XINUSE AND RFBM, XCOMP_BV = RFBM OR bit 63 (Vol1 13.10, Vol2D XSAVEC) - measured TX4: Mask 4 with AVX in use, 0
//    after VZEROUPPER, CompactionMask 8000000000000004H.
// 3. The frame (U2252), measured TX4 (the 2000H bytes below RSP pre-filled: what Windows does not write keeps the fill) and TX3 / TX4 E3:
//    CONTEXT at ( RSP AND NOT 3FH ) - 5C0H - the XSAVE size (40H header + the compacted user components: 140H with AVX), i.e. RSP -
//    CONTEXT = 700H + ( RSP AND 3FH ) for every RSP AND 38H; written: P3Home 0, ContextFlags 10005FH (CONTEXT_ALL | CONTEXT_XSTATE),
//    MxCsr, the six selectors (CS 33H, DS / ES / GS / SS 2BH, FS 53H), EFlags (the pushed image; a single-step event without TF - TX3),
//    Dr0-3 / Dr6 / Dr7 0 (no debug register is ever set in the emulated thread), RAX .. R15 (RSP = the event's), Rip, FltSave bytes
//    0 .. 19FH (the FXSAVE64 image; Reserved4 not written), LastBranchToRip .. LastExceptionFromRip 0; NOT written: P1Home, P2Home,
//    P4Home - P6Home, VectorRegister[26], VectorControl, DebugControl, FltSave + 1A0H .. + 1FFH. CONTEXT_EX at + 4D0H (phnt
//    ntrtl.h:5007-5019): All {-4D0H, RSP - CONTEXT}, Legacy {-4D0H, 4D0H}, XState {F0H, RSP - ( CONTEXT + 5C0H )}; + 4E8H not written.
//    EXCEPTION_RECORD at + 4F0H (all 98H bytes: ExceptionInformation beyond NumberParameters 0). MACHINE_FRAME: only Rip (+ 590H) and Rsp
//    (+ 5A8H) written (+ 588H / 598H / 5A0H / 5B0H / 5B8H not). The XSAVE area at + 5C0H: the header, a component only when its XSTATE_BV
//    bit is set.
// 4. The exception record per event (U2253), measured TX1 / TX3 / TX6 (ExceptionAddress A, CONTEXT.Rip R, the pushed RIP P):
//    #DE  C0000094H STATUS_INTEGER_DIVIDE_BY_ZERO when the divisor is 0, else C0000095H STATUS_INTEGER_OVERFLOW (TX1 e04 / e05); A = R = P
//    #DB  80000004H STATUS_SINGLE_STEP, A = R = P, EFlags.TF cleared in the CONTEXT (TX3; ICEBP TX1 e12)
//    #BP  80000003H STATUS_BREAKPOINT, A = R = P - 1, NumberParameters 1, Information[0] 0 (BREAKPOINT_BREAK) (TX1 e01 CCH / e02 CDH 03H)
//    #OF  INT 4: C0000095H, A = P - 1, R = P (TX1 e11)
//    #UD  C000001DH STATUS_ILLEGAL_INSTRUCTION (TX1 e03)
//    #GP  C0000096H STATUS_PRIVILEGED_INSTRUCTION for an instruction that is privileged at CPL 3 (TX1 e06 HLT, e15 IN, e16 MOV CR0, e17 CLI,
//         e19 STI, e20 OUT, e21 RDMSR, e22 WBINVD, e23 MOV DR7, e24 INVLPG, e25 LGDT, e26 SWAPGS, e27 RDPMC, e28 INSB; the remaining
//         instructions of that class per the SDM's "#GP(0) if the CPL is not 0" and ReactOS except.c:446-560 KiIsPrivilegedInstruction as
//         reference); else C0000005H with NumberParameters 2 {0, -1} (TX1 e10 non-canonical read, e14 INT 1, e29 INT 41H)
//    #PF  C0000005H, NumberParameters 2 {0 read / 1 write / 8 execute, the address} (TX1 e07 / e08 / e09)
//    #AC  80000002H STATUS_DATATYPE_MISALIGNMENT (TX6)
//    2CH  C0000420H STATUS_ASSERTION_FAILURE, A = R = P - 2 (TX1 e13)
//    2DH  the debug service: RAX 1 / 3 / 4 / 5 - no exception, the thread continues at P + 1 (TX1 e18 / e31 / e34 / e35: the byte after
//         INT 2DH is skipped); any other RAX - 80000003H, A = P + 1, R = P, NumberParameters 1, Information[0] = RAX (e30 / e32 / e33)
//    any other vector (29H fast fail, 2EH, #NM, #SS, #MF, #XM, ...) - a loud stop (not measured)
// 5. --Sys (U2254): IRETQ to the environment's ntdll!KiUserExceptionDispatcher with RSP = the CONTEXT, RCX = the record, RDX = the
//    CONTEXT (ReactOS except.c KiDispatchExceptionToUser as reference), RFLAGS = the event's without TF / DF / AC (Wine 11.0
//    dlls/ntdll/unix/signal_x86_64.c:1631, reference); the real ntdll dispatches (RtlDispatchException: vectored handlers, the frame
//    handlers through RtlVirtualUnwind, the continue handlers) and continues with NtContinue (RtlGuardRestoreContext ->
//    RtlRestoreContext -> ZwContinue for a CONTEXT with CONTEXT_DEBUG_REGISTERS: the capture's ntdll, IDA).
// 6. NtContinue with CONTEXT_XSTATE (U2255): a kernel stub executes FXRSTOR64 (FltSave, MXCSR := CONTEXT.MxCsr AND MXCSR_MASK) and XRSTOR64
//    (the CONTEXT_EX XState area, RFBM = the user components) - then IRETQ to the CONTEXT (the user RFLAGS bits, IF set).
// 7. --HLE (U2256): our documented implementation of RtlDispatchException (Microsoft Learn "x64 exception handling": UNWIND_INFO,
//    UNWIND_CODE, the unwind procedure; Learn "Vectored Exception Handling", AddVectoredExceptionHandler / AddVectoredContinueHandler):
//    the vectored handlers in list order (EXCEPTION_CONTINUE_EXECUTION -1 ends the search), then the frames from the CONTEXT: the image's
//    RUNTIME_FUNCTION for the control PC (none: a leaf, RIP = [RSP], RSP + 8), the virtual unwind, the language handler (UNW_FLAG_EHANDLER)
//    with ( record, EstablisherFrame, the CONTEXT, DISPATCHER_CONTEXT ), ExceptionContinueExecution ends the search; then the continue
//    handlers (measured TX2: called after a vectored and after a frame handler), then the restore of the (changed) CONTEXT as NtContinue
//    does. DISPATCHER_CONTEXT.ContextRecord = the unwound copy at CONTEXT - 6D0H (measured TX1: where the capture's RtlDispatchException
//    keeps it). An unhandled exception, a nested / collided disposition, an invalid frame: a loud stop (not measured).
#include "win_env_hooks.hpp"

#include <emu/win_fields.hpp>

#include <capstone/capstone.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace emu::win
{
	namespace
	{
		std::string hx( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		uint64_t rreg( uc_engine* uc, int id )
		{
			uint64_t v = 0;
			uc_reg_read( uc, id, &v );
			return v;
		}

		constexpr int gpr_ids[ 16 ] = { UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX, UC_X86_REG_RSP, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
										UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11, UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15 };

		// NTSTATUS values (Windows SDK 10.0.26100.0 shared\ntstatus.h)
		constexpr uint32_t st_access_violation = 0xC0000005, st_illegal_instruction = 0xC000001D, st_integer_divide_by_zero = 0xC0000094,
						   st_integer_overflow = 0xC0000095, st_privileged_instruction = 0xC0000096, st_breakpoint = 0x80000003, st_single_step = 0x80000004,
						   st_datatype_misalignment = 0x80000002, st_assertion_failure = 0xC0000420;

		// the frame layout (measured TX3 / TX4, see the file header); _CONTEXT offsets: the guest ntdll.pdb (emu/win_fields.hpp)
		constexpr uint64_t ctx_size = fields::context_size, ctx_ex_off = 0x4D0, rec_off = 0x4F0, rec_size = 0x98, mframe_rip_off = 0x590, mframe_rsp_off = 0x5A8,
						   xsave_off = 0x5C0;
		static_assert( ctx_size == 0x4D0 && ctx_ex_off + 0x20 == rec_off && rec_off + rec_size + 8 == mframe_rip_off && mframe_rip_off + 0x30 == xsave_off,
					   "the exception frame: CONTEXT, CONTEXT_EX (20H), EXCEPTION_RECORD (98H), 8, MACHINE_FRAME (28H), 8 (Wine 11.0 signal_x86_64.c:418-429)" );
		constexpr uint32_t c_p3home = 0x10, c_flags = 0x30, c_mxcsr = 0x34, c_segcs = 0x38, c_eflags = 0x44, c_dr0 = 0x48, c_dr7 = 0x70, c_rax = 0x78, c_rsp = 0x98, c_rip = 0xF8,
						   c_fltsave = 0x100, c_lastbranch = 0x4B0, c_xmm0 = 0x1A0;
		static_assert( fields::offset_of( fields::context, "ContextFlags" ) == c_flags && fields::offset_of( fields::context, "MxCsr" ) == c_mxcsr &&
						   fields::offset_of( fields::context, "SegCs" ) == c_segcs && fields::offset_of( fields::context, "EFlags" ) == c_eflags &&
						   fields::offset_of( fields::context, "Dr0" ) == c_dr0 && fields::offset_of( fields::context, "Dr7" ) == c_dr7 &&
						   fields::offset_of( fields::context, "Rax" ) == c_rax && fields::offset_of( fields::context, "Rsp" ) == c_rsp &&
						   fields::offset_of( fields::context, "Rip" ) == c_rip && fields::offset_of( fields::context, "FltSave" ) == c_fltsave &&
						   fields::offset_of( fields::context, "LastBranchToRip" ) == c_lastbranch && fields::offset_of( fields::context, "P3Home" ) == c_p3home &&
						   fields::offset_of( fields::context, "Xmm0" ) == c_xmm0,
					   "_CONTEXT (guest ntdll.pdb)" );
		constexpr uint32_t context_all_xstate = 0x10005F;   // measured TX1 - TX6 (winnt.h CONTEXT_ALL 10001FH | CONTEXT_XSTATE 100040H)
		constexpr uint64_t rf_flag = 0x10000, tf_flag = 0x100, df_flag = 0x400, ac_flag = 0x40000;
		// the user RFLAGS bits a CONTEXT may set (as process::resume_context: ReactOS ketypes.h:207 EFLAGS_USER_SANITIZE without VM)
		constexpr uint64_t eflags_user_sanitize = 0x3F4DD7 & ~0x20000ull;

		// the gate DPL of each vector (1 = DPL 3): Windows lets user mode raise 3, 4, 29H, 2CH, 2DH, 2EH (file header item 1)
		bool user_gate( unsigned v )
		{
			return v == 3 || v == 4 || v == 0x29 || v == 0x2C || v == 0x2D || v == 0x2E;
		}

		// ---- Capstone helpers (the instruction at an event: the divisor of #DE, the class of #GP)
		struct decoded
		{
			bool ok = false;
			unsigned id = 0;
			cs_x86 x{};
			uint64_t next = 0;
		};

		decoded decode_at( uc_engine* uc, uint64_t rip )
		{
			decoded d;
			uint8_t b[ 16 ] = {};
			size_t n = 0;
			for ( ; n < 15; n++ )
				if ( uc_mem_read( uc, rip + n, b + n, 1 ) != UC_ERR_OK ) break;
			if ( !n ) return d;
			csh h = 0;
			if ( cs_open( CS_ARCH_X86, CS_MODE_64, &h ) != CS_ERR_OK ) return d;
			cs_option( h, CS_OPT_DETAIL, CS_OPT_ON );
			cs_insn* in = nullptr;
			if ( cs_disasm( h, b, n, rip, 1, &in ) == 1 && in->detail )
			{
				d.ok = true;
				d.id = in->id;
				d.x = in->detail->x86;
				d.next = rip + in->size;
			}
			if ( in ) cs_free( in, 1 );
			cs_close( &h );
			return d;
		}

		// a Capstone general-purpose register -> its value at the event (the sub-register's bits; G = RAX .. R15 as the event saved them -
		// the save stub has changed RAX / RCX / RDX since)
		bool cs_gpr_value( const uint64_t* G, x86_reg r, uint64_t& v )
		{
			struct m
			{
				x86_reg r;
				int uc;
				unsigned bits, shift;
			};
			static const m t[] = {
				{ X86_REG_RAX, UC_X86_REG_RAX, 64, 0 }, { X86_REG_RCX, UC_X86_REG_RCX, 64, 0 }, { X86_REG_RDX, UC_X86_REG_RDX, 64, 0 }, { X86_REG_RBX, UC_X86_REG_RBX, 64, 0 },
				{ X86_REG_RSP, UC_X86_REG_RSP, 64, 0 }, { X86_REG_RBP, UC_X86_REG_RBP, 64, 0 }, { X86_REG_RSI, UC_X86_REG_RSI, 64, 0 }, { X86_REG_RDI, UC_X86_REG_RDI, 64, 0 },
				{ X86_REG_R8, UC_X86_REG_R8, 64, 0 }, { X86_REG_R9, UC_X86_REG_R9, 64, 0 }, { X86_REG_R10, UC_X86_REG_R10, 64, 0 }, { X86_REG_R11, UC_X86_REG_R11, 64, 0 },
				{ X86_REG_R12, UC_X86_REG_R12, 64, 0 }, { X86_REG_R13, UC_X86_REG_R13, 64, 0 }, { X86_REG_R14, UC_X86_REG_R14, 64, 0 }, { X86_REG_R15, UC_X86_REG_R15, 64, 0 },
				{ X86_REG_EAX, UC_X86_REG_RAX, 32, 0 }, { X86_REG_ECX, UC_X86_REG_RCX, 32, 0 }, { X86_REG_EDX, UC_X86_REG_RDX, 32, 0 }, { X86_REG_EBX, UC_X86_REG_RBX, 32, 0 },
				{ X86_REG_ESP, UC_X86_REG_RSP, 32, 0 }, { X86_REG_EBP, UC_X86_REG_RBP, 32, 0 }, { X86_REG_ESI, UC_X86_REG_RSI, 32, 0 }, { X86_REG_EDI, UC_X86_REG_RDI, 32, 0 },
				{ X86_REG_R8D, UC_X86_REG_R8, 32, 0 }, { X86_REG_R9D, UC_X86_REG_R9, 32, 0 }, { X86_REG_R10D, UC_X86_REG_R10, 32, 0 }, { X86_REG_R11D, UC_X86_REG_R11, 32, 0 },
				{ X86_REG_R12D, UC_X86_REG_R12, 32, 0 }, { X86_REG_R13D, UC_X86_REG_R13, 32, 0 }, { X86_REG_R14D, UC_X86_REG_R14, 32, 0 }, { X86_REG_R15D, UC_X86_REG_R15, 32, 0 },
				{ X86_REG_AX, UC_X86_REG_RAX, 16, 0 }, { X86_REG_CX, UC_X86_REG_RCX, 16, 0 }, { X86_REG_DX, UC_X86_REG_RDX, 16, 0 }, { X86_REG_BX, UC_X86_REG_RBX, 16, 0 },
				{ X86_REG_SP, UC_X86_REG_RSP, 16, 0 }, { X86_REG_BP, UC_X86_REG_RBP, 16, 0 }, { X86_REG_SI, UC_X86_REG_RSI, 16, 0 }, { X86_REG_DI, UC_X86_REG_RDI, 16, 0 },
				{ X86_REG_R8W, UC_X86_REG_R8, 16, 0 }, { X86_REG_R9W, UC_X86_REG_R9, 16, 0 }, { X86_REG_R10W, UC_X86_REG_R10, 16, 0 }, { X86_REG_R11W, UC_X86_REG_R11, 16, 0 },
				{ X86_REG_R12W, UC_X86_REG_R12, 16, 0 }, { X86_REG_R13W, UC_X86_REG_R13, 16, 0 }, { X86_REG_R14W, UC_X86_REG_R14, 16, 0 }, { X86_REG_R15W, UC_X86_REG_R15, 16, 0 },
				{ X86_REG_AL, UC_X86_REG_RAX, 8, 0 }, { X86_REG_CL, UC_X86_REG_RCX, 8, 0 }, { X86_REG_DL, UC_X86_REG_RDX, 8, 0 }, { X86_REG_BL, UC_X86_REG_RBX, 8, 0 },
				{ X86_REG_SPL, UC_X86_REG_RSP, 8, 0 }, { X86_REG_BPL, UC_X86_REG_RBP, 8, 0 }, { X86_REG_SIL, UC_X86_REG_RSI, 8, 0 }, { X86_REG_DIL, UC_X86_REG_RDI, 8, 0 },
				{ X86_REG_R8B, UC_X86_REG_R8, 8, 0 }, { X86_REG_R9B, UC_X86_REG_R9, 8, 0 }, { X86_REG_R10B, UC_X86_REG_R10, 8, 0 }, { X86_REG_R11B, UC_X86_REG_R11, 8, 0 },
				{ X86_REG_R12B, UC_X86_REG_R12, 8, 0 }, { X86_REG_R13B, UC_X86_REG_R13, 8, 0 }, { X86_REG_R14B, UC_X86_REG_R14, 8, 0 }, { X86_REG_R15B, UC_X86_REG_R15, 8, 0 },
				{ X86_REG_AH, UC_X86_REG_RAX, 8, 8 }, { X86_REG_CH, UC_X86_REG_RCX, 8, 8 }, { X86_REG_DH, UC_X86_REG_RDX, 8, 8 }, { X86_REG_BH, UC_X86_REG_RBX, 8, 8 },
			};
			for ( const auto& e : t )
				if ( e.r == r )
				{
					int k = 0;
					while ( k < 16 && gpr_ids[ k ] != e.uc ) k++;
					if ( k == 16 ) return false;
					v = G[ k ] >> e.shift;
					if ( e.bits < 64 ) v &= ( 1ull << e.bits ) - 1;
					return true;
				}
			return false;
		}

		// the effective address of a memory operand (base + index * scale + disp; RIP-relative from the next instruction; FS / GS base)
		bool cs_mem_address( uc_engine* uc, const uint64_t* G, const x86_op_mem& m, uint64_t next, uint64_t& ea )
		{
			uint64_t base = 0, index = 0;
			if ( m.base == X86_REG_RIP ) base = next;
			else if ( m.base != X86_REG_INVALID && !cs_gpr_value( G, m.base, base ) ) return false;
			if ( m.index != X86_REG_INVALID && !cs_gpr_value( G, m.index, index ) ) return false;
			ea = base + index * uint64_t( m.scale ) + uint64_t( m.disp );
			if ( m.segment == X86_REG_FS ) ea += rreg( uc, UC_X86_REG_FS_BASE );
			if ( m.segment == X86_REG_GS ) ea += rreg( uc, UC_X86_REG_GS_BASE );
			return true;
		}

		// #GP at CPL 3 because the instruction is privileged (Windows: STATUS_PRIVILEGED_INSTRUCTION) - the SDM's instructions with "#GP(0) If
		// the current privilege level is not 0" (or IOPL < CPL for the I/O and interrupt-flag instructions, IOPL 0 here); measured members:
		// file header item 4
		bool privileged_insn( const decoded& d )
		{
			switch ( d.id )
			{
				case X86_INS_HLT: case X86_INS_CLI: case X86_INS_STI: case X86_INS_IN: case X86_INS_OUT: case X86_INS_INSB: case X86_INS_INSW: case X86_INS_INSD:
				case X86_INS_OUTSB: case X86_INS_OUTSW: case X86_INS_OUTSD: case X86_INS_RDMSR: case X86_INS_WRMSR: case X86_INS_WBINVD: case X86_INS_INVD:
				case X86_INS_LGDT: case X86_INS_LIDT: case X86_INS_LLDT: case X86_INS_LTR: case X86_INS_LMSW: case X86_INS_CLTS: case X86_INS_INVLPG:
				case X86_INS_INVPCID: case X86_INS_SWAPGS: case X86_INS_SYSEXIT: case X86_INS_RDPMC: case X86_INS_XSETBV: case X86_INS_MONITOR: case X86_INS_MWAIT:
					return true;
				case X86_INS_MOV:
					for ( uint8_t i = 0; i < d.x.op_count; i++ )
						if ( d.x.operands[ i ].type == X86_OP_REG )
						{
							const x86_reg r = d.x.operands[ i ].reg;
							if ( ( r >= X86_REG_CR0 && r <= X86_REG_CR15 ) || ( r >= X86_REG_DR0 && r <= X86_REG_DR15 ) ) return true;
						}
					return false;
				default:
					return false;
			}
		}
	}

	// ------------------------------------------------------------------------------------------------ U2250: the IDT, the TSS, the stubs
	bool process::build_exceptions( std::vector<std::string>& lines, std::string& error )
	{
		uc_engine* uc = e_->handle();
		std::string err;
		// the IDT page (256 16-byte gates) after the kernel's stack page; the TSS in the GDT page at + 100H (Vol3A 9.7 64-bit TSS: RSP0 at + 4)
		idt_va_ = guest_kernel_stack_va + page_size;
		tss_va_ = guest_kernel_gdt_va + 0x100;
		ksave_va_ = guest_kernel_stack_va;   // the save area: + 0 the FXSAVE64 image (200H), + 400H the XSAVEC / XRSTOR area (its header at + 600H)
		idt_stub_va_ = guest_kernel_entry_va + 0x800;
		save_stub_ = guest_kernel_entry_va + 0xC00;
		if ( !e_->map( idt_va_, page_size, UC_PROT_READ, "emulator kernel: IDT (P49)", err ) )
		{
			error = err;
			return false;
		}
		const uint64_t rsp0 = guest_kernel_stack_va + page_size;
		uint8_t tss[ 0x68 ] = {};
		std::memcpy( tss + 4, &rsp0, 8 );
		const uint16_t iomap = 0x68;   // the I/O permission bit map base at the limit + 1: no map (Vol3A 21.5.2)
		std::memcpy( tss + 0x66, &iomap, 2 );
		if ( !e_->write( tss_va_, tss, sizeof( tss ), err ) )
		{
			error = "TSS: " + err;
			return false;
		}
		std::vector<uint8_t> idt( page_size, 0 );
		for ( unsigned v = 0; v < 256; v++ )
		{
			const uint64_t off = idt_stub_va_ + 4 * v;
			uint8_t* g = idt.data() + 16 * v;
			const uint16_t lo = uint16_t( off ), sel = uint16_t( layout::gdt_r0_code );
			const uint16_t mid = uint16_t( off >> 16 );
			const uint32_t hi = uint32_t( off >> 32 );
			std::memcpy( g, &lo, 2 );
			std::memcpy( g + 2, &sel, 2 );
			g[ 4 ] = 0;                                              // IST 0
			g[ 5 ] = uint8_t( 0x8E | ( user_gate( v ) ? 0x60 : 0 ) );   // P, DPL, type 0EH (64-bit interrupt gate, Vol3A 7.14.1 Figure 7-8)
			std::memcpy( g + 6, &mid, 2 );
			std::memcpy( g + 8, &hi, 4 );
		}
		std::vector<uint8_t> stubs( 0x400, 0xF4 );   // HLT x 4 per vector: the code hook runs the kernel before it executes
		// the save stub: mov rcx, ksave; fxsave64 [rcx]; mov eax, mask lo; mov edx, mask hi; xsavec64 [rcx + 400H]; hlt
		std::vector<uint8_t> save = { 0x48, 0xB9 };
		for ( int k = 0; k < 8; k++ ) save.push_back( uint8_t( ksave_va_ >> ( 8 * k ) ) );
		save.insert( save.end(), { 0x48, 0x0F, 0xAE, 0x01 } );   // FXSAVE64 [RCX] (REX.W 0F AE /0, 253667-093 Vol2A FXSAVE)
		// the user components: XCR0 without x87 / SSE (their state is FltSave)
		uint64_t xcr0 = 0;
		uc_reg_read( uc, UC_X86_REG_XCR0, &xcr0 );
		xstate_user_mask_ = xcr0 & ~3ull;
		save.push_back( 0xB8 );
		for ( int k = 0; k < 4; k++ ) save.push_back( uint8_t( xstate_user_mask_ >> ( 8 * k ) ) );
		save.push_back( 0xBA );
		for ( int k = 0; k < 4; k++ ) save.push_back( uint8_t( xstate_user_mask_ >> ( 32 + 8 * k ) ) );
		// the XSAVE layout from the CPU profile's leaf 0DH (Vol1 13.2: sub-leaf i EAX = size, EBX = standard offset, ECX[1] = 64-byte
		// alignment in the compacted form; sub-leaf 1 EAX[1] = XSAVEC); the compacted size of the user components (Vol1 13.4.3)
		auto leaf = [ & ]( uint32_t sub, uint32_t* r4 ) {
			for ( const auto& l : e_->profile().leaves )
				if ( l.leaf == 0xD && l.subleaf == sub )
				{
					r4[ 0 ] = l.eax;
					r4[ 1 ] = l.ebx;
					r4[ 2 ] = l.ecx;
					r4[ 3 ] = l.edx;
					return true;
				}
			return false;
		};
		uint32_t s1[ 4 ] = {};
		xstate_ok_ = e_->has_profile() && leaf( 1, s1 ) && ( s1[ 0 ] & 2 );
		uint64_t size = 0x40;   // the header
		for ( unsigned i = 2; xstate_ok_ && i < 63; i++ )
		{
			if ( !( xstate_user_mask_ >> i & 1 ) ) continue;
			uint32_t r[ 4 ] = {};
			if ( !leaf( i, r ) || !r[ 0 ] )
			{
				xstate_ok_ = false;
				break;
			}
			if ( r[ 2 ] & 2 ) size = ( size + 63 ) & ~63ull;
			size += r[ 0 ];
		}
		xstate_user_size_ = ( size + 63 ) & ~63ull;
		if ( xstate_ok_ )
		{
			save.insert( save.end(), { 0x48, 0x0F, 0xC7, 0xA1, 0x00, 0x04, 0x00, 0x00 } );   // XSAVEC64 [RCX + 400H] (REX.W 0F C7 /4, Vol2D XSAVEC): its header at + 600H
		}
		save_stub_end_ = save_stub_ + save.size();
		save.push_back( 0xF4 );   // HLT: the code hook continues (never executed)
		// the restore stub (NtContinue with CONTEXT_XSTATE, the end of an --HLE dispatch): mov rcx, ksave; fxrstor64 [rcx]; xrstor64 [rcx + 200H]
		// (EDX:EAX set by the kernel: the components to load); hlt - xrstor64 [rcx + 400H]
		restore_stub_ = save_stub_ + 0x100;
		std::vector<uint8_t> rst = { 0x48, 0xB9 };
		for ( int k = 0; k < 8; k++ ) rst.push_back( uint8_t( ksave_va_ >> ( 8 * k ) ) );
		rst.insert( rst.end(), { 0x48, 0x0F, 0xAE, 0x09 } );   // FXRSTOR64 [RCX] (REX.W 0F AE /1)
		if ( xstate_ok_ ) rst.insert( rst.end(), { 0x48, 0x0F, 0xAE, 0xA9, 0x00, 0x04, 0x00, 0x00 } );   // XRSTOR64 [RCX + 400H] (REX.W 0F AE /5)
		restore_stub_end_ = restore_stub_ + rst.size();
		rst.push_back( 0xF4 );
		if ( !e_->write( idt_va_, idt.data(), idt.size(), err ) || !e_->write( idt_stub_va_, stubs.data(), stubs.size(), err ) ||
			 !e_->write( save_stub_, save.data(), save.size(), err ) || !e_->write( restore_stub_, rst.data(), rst.size(), err ) )
		{
			error = "the exception stubs: " + err;
			return false;
		}
		uc_x86_mmr idtr{ 0, idt_va_, uint32_t( page_size - 1 ), 0 };
		uc_x86_mmr tr{ uint16_t( layout::gdt_sys_tss ), tss_va_, 0x67, 0x8900 };   // KGDT64_SYS_TSS 40H (ksamd64.inc), an available 64-bit TSS (type 9, P; the fork's descriptor-flags form)
		if ( uc_reg_write( uc, UC_X86_REG_IDTR, &idtr ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_TR, &tr ) != UC_ERR_OK )
		{
			error = "IDTR / TR write failed";
			return false;
		}
		if ( uc_ctl_set_x86_event_delivery( uc, UC_X86_DELIVER_EXC_IDT ) != UC_ERR_OK )
		{
			error = "UC_CTL_X86_EVENT_DELIVERY (UC_X86_DELIVER_EXC_IDT) refused";
			return false;
		}
		lines.push_back( "exceptions (P49, U2250): IDT at " + hx( idt_va_ ) + " (DPL 3 gates 3, 4, 29H, 2CH, 2DH, 2EH), TSS " + hx( tss_va_ ) + " (TR 40H, RSP0 " + hx( rsp0 ) +
						 "), events delivered by the CPU through the IDT (fork U963); user XSAVE components " + hx( xstate_user_mask_ ) + ", frame XSAVE size " +
						 hx( xstate_user_size_ ) + ( xstate_ok_ ? "" : " (no XSAVEC / no leaf 0DH in the profile: an exception frame stops loudly)" ) );
		// KUSER_SHARED_DATA.XState (+3D8H, _XSTATE_CONFIGURATION of the guest ntdll.pdb), from the profile's leaf 0DH (TX4 kuser.xstate.all,
		// measured): EnabledFeatures / EnabledVolatileFeatures = XCR0; Size = CPUID.(0DH, 0).EBX (the size for XCR0); ControlFlags
		// OptimizedSave = XSAVEOPT (sub-leaf 1 EAX[0]), CompactionEnabled = XSAVEC (EAX[1]); Features[0] {0, A0H}, [1] {A0H, 100H}, [i >= 2]
		// {EBX, EAX} of every user component the CPU supports (CPUID.(0DH, 0) EDX:EAX - measured: PKRU listed although XCR0 lacks it);
		// EnabledSupervisorFeatures = the supported ones of PT (bit 8) and CET_U (bit 11), EnabledUserVisibleSupervisorFeatures = CET_U only (sub-leaf 1
		// EDX:ECX; Windows 10's choice, MEASURED in both environments by TX4 kuser.xstate: the VMware guest - sub-leaf 1 ECX 1800H, no PT -
		// "supervisor=800"; the bare host - ECX 19900H: PT, CET_U, CET_S, bits 15 / 16 - "supervisor=900", host_reg_ref_v2 2026-10-30;
		// P57 queued task, U2380); AllFeatures[i] = the size of every supported user or supervisor component; AllFeatureSize = the
		// compacted size of the enabled user + supervisor components; AlignedFeatures = the enabled ones with sub-leaf ECX[1]
		uint32_t s0[ 4 ] = {};
		if ( e_->has_profile() && leaf( 0, s0 ) && leaf( 1, s1 ) && kuser_page_ )
		{
			uint8_t x[ 0x338 ] = {};
			auto p32 = [ & ]( size_t o, uint32_t v ) { std::memcpy( x + o, &v, 4 ); };
			auto p64 = [ & ]( size_t o, uint64_t v ) { std::memcpy( x + o, &v, 8 ); };
			const uint64_t user_sup = uint64_t( s0[ 3 ] ) << 32 | s0[ 0 ], super_sup = uint64_t( s1[ 3 ] ) << 32 | s1[ 2 ];
			const uint64_t super_en = super_sup & 0x900;   // PT | CET_U (measured: TX4 guest 800H, bare host 900H)
			p64( 0x0, xcr0 );
			p64( 0x8, xcr0 );
			p32( 0x10, s0[ 1 ] );
			p32( 0x14, ( s1[ 0 ] & 1 ) | ( s1[ 0 ] & 2 ) );
			uint64_t all = 0x240, aligned = 0;
			for ( unsigned i = 0; i < 64; i++ )
			{
				uint32_t r[ 4 ] = {};
				uint32_t off = 0, sz = 0;
				if ( i == 0 ) { off = 0; sz = 0xA0; }
				else if ( i == 1 ) { off = 0xA0; sz = 0x100; }
				else if ( ( user_sup | super_sup ) >> i & 1 && leaf( i, r ) )
				{
					sz = r[ 0 ];
					off = ( user_sup >> i & 1 ) ? r[ 1 ] : 0;
					if ( ( ( xcr0 | super_en ) >> i & 1 ) )
					{
						if ( r[ 2 ] & 2 )
						{
							all = ( all + 63 ) & ~63ull;
							aligned |= 1ull << i;
						}
						all += sz;
					}
				}
				if ( i < 64 && ( user_sup >> i & 1 || i < 2 ) )
				{
					p32( 0x18 + 8 * i, off );
					p32( 0x18 + 8 * i + 4, sz );
				}
				p32( 0x22C + 4 * i, sz );
			}
			p64( 0x218, super_en );
			p64( 0x220, aligned );
			p32( 0x228, uint32_t( all ) );
			p64( 0x330, super_sup & 0x800 );   // EnabledUserVisibleSupervisorFeatures: CET_U only (measured: TX4 kuser.xstate.all, bare host 800H with 900H enabled)
			std::memcpy( kuser_page_.get() + 0x3D8, x, sizeof( x ) );
			env_lines_.push_back( "KUSER XState (3D8H, P49): EnabledFeatures " + hx( xcr0 ) + ", Size " + hx( s0[ 1 ] ) + ", ControlFlags " + hx( ( s1[ 0 ] & 3 ) ) +
								  ", EnabledSupervisorFeatures " + hx( super_en ) + ", AllFeatureSize " + hx( all ) + " (from the profile's leaf 0DH; TX4 kuser.xstate.all)" );
		}
		else
			env_lines_.push_back( "KUSER XState (3D8H, P49): 0 - no CPUID profile rows for leaf 0DH (--max)" );
		return true;
	}

	// ------------------------------------------------------------------------------------------------ U2250: an IDT gate reached
	bool process::exception_gate( uc_engine* uc, hook_ctx& c, unsigned vector, std::string& why )
	{
		auto& ev = c.ev;
		if ( ev.active )
		{
			why = "vector " + std::to_string( vector ) + " delivered while the kernel handled vector " + std::to_string( ev.vector ) + " (a fault in the exception path) - not modelled";
			return false;
		}
		const uint64_t ksp = rreg( uc, UC_X86_REG_RSP );
		// the frame (Vol3A 7.14.2 Figure 7-9): [error code], RIP, CS, RFLAGS, RSP, SS - an error code for 8, 10-14, 17, 21 (not for INT n:
		// the fork's record says)
		uc_x86_exception x{};
		const bool rec_ok = uc_ctl_get_x86_exception( uc, &x ) == UC_ERR_OK && x.vector == int( vector );
		const bool has_ec = rec_ok && x.has_error_code;
		uint64_t f[ 6 ] = {};
		std::string err;
		if ( !e_->read( ksp, f, has_ec ? 48 : 40, err ) )
		{
			why = "the interrupt frame at " + hx( ksp ) + " is not readable: " + err;
			return false;
		}
		const uint64_t* p = has_ec ? f + 1 : f;
		ev = hook_ctx::cpu_event{};
		ev.vector = vector;
		ev.has_error_code = has_ec;
		ev.error_code = has_ec ? uint32_t( f[ 0 ] ) : 0;
		ev.rip = p[ 0 ];
		ev.cs = p[ 1 ];
		ev.rflags = p[ 2 ];
		ev.rsp = p[ 3 ];
		ev.ss = p[ 4 ];
		if ( ( ev.cs & 3 ) != 3 )
		{
			why = "vector " + std::to_string( vector ) + " at CPL 0 (CS " + hx( ev.cs ) + ", RIP " + hx( ev.rip ) + "): a fault inside the emulator kernel - not modelled";
			return false;
		}
		for ( int i = 0; i < 16; i++ ) ev.gpr[ i ] = i == 4 ? ev.rsp : rreg( uc, gpr_ids[ i ] );
		ev.active = true;
		// to the save stub (CPL 0, the kernel stack below the frame)
		const uint64_t rip = save_stub_;
		if ( uc_reg_write( uc, UC_X86_REG_RIP, &rip ) != UC_ERR_OK )
		{
			why = "RIP write failed";
			return false;
		}
		return true;
	}

	// U2250: a #PF (the memory hook stopped the run at the faulting instruction): the same path, the save stub run at the fault's CPL with TF,
	// AC, RF cleared (the event keeps them)
	bool process::page_fault( uc_engine* uc, hook_ctx& c, uint64_t rip, uint64_t rflags, uint64_t address, int access, std::string& why )
	{
		auto& ev = c.ev;
		if ( ev.active )
		{
			why = "#PF at " + hx( address ) + " while the kernel handled vector " + std::to_string( ev.vector ) + " - not modelled";
			return false;
		}
		ev = hook_ctx::cpu_event{};
		ev.vector = 14;
		ev.page_fault = true;
		ev.has_error_code = true;
		ev.pf_address = address;
		ev.pf_access = access;
		ev.rip = rip;
		ev.cs = layout::gdt_r3_code | layout::rpl3;
		ev.ss = layout::gdt_r3_data | layout::rpl3;
		ev.rflags = rflags | rf_flag;   // a fault: RF = 1 in the pushed image (253669-093 Vol3B 20.3.1.1)
		for ( int i = 0; i < 16; i++ ) ev.gpr[ i ] = rreg( uc, gpr_ids[ i ] );
		ev.rsp = ev.gpr[ 4 ];
		ev.active = true;
		const uint64_t fl = ( rflags & ~( tf_flag | ac_flag | rf_flag ) ) | 2, ksp = guest_kernel_stack_va + page_size - 0x40;
		if ( uc_reg_write( uc, UC_X86_REG_RFLAGS, &fl ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_RSP, &ksp ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_RIP, &save_stub_ ) != UC_ERR_OK )
		{
			why = "register write failed";
			return false;
		}
		return true;
	}

	namespace
	{
		struct mapped
		{
			bool silent = false;   // INT 2DH: no exception
			uint32_t code = 0, parameters = 0;
			uint64_t address = 0, ctx_rip = 0, info[ 2 ] = {};
			uint64_t ctx_eflags = 0;
		};
	}

	// U2253: the event -> Windows' exception record (file header item 4)
	static bool windows_record( uc_engine* uc, const process::hook_ctx& c, mapped& m, std::string& why )
	{
		const auto& ev = c.ev;
		m = mapped{};
		m.address = m.ctx_rip = ev.rip;
		m.ctx_eflags = ev.rflags;
		switch ( ev.vector )
		{
			case 0:
			{
				const decoded d = decode_at( uc, ev.rip );
				if ( !d.ok || ( d.id != X86_INS_DIV && d.id != X86_INS_IDIV ) || d.x.op_count < 1 )
				{
					why = "#DE at " + hx( ev.rip ) + ": not a DIV / IDIV the decoder reads - not modelled";
					return false;
				}
				const cs_x86_op& op = d.x.operands[ 0 ];
				uint64_t v = 0;
				if ( op.type == X86_OP_REG )
				{
					if ( !cs_gpr_value( ev.gpr, op.reg, v ) )
					{
						why = "#DE: the divisor register is not modelled";
						return false;
					}
				}
				else if ( op.type == X86_OP_MEM )
				{
					uint64_t ea = 0;
					if ( !cs_mem_address( uc, ev.gpr, op.mem, d.next, ea ) || op.size > 8 || uc_mem_read( uc, ea, &v, op.size ) != UC_ERR_OK )
					{
						why = "#DE: the divisor in memory is not readable";
						return false;
					}
				}
				m.code = v == 0 ? st_integer_divide_by_zero : st_integer_overflow;
				return true;
			}
			case 1:
				m.code = st_single_step;
				m.ctx_eflags &= ~tf_flag;   // measured TX3: the CONTEXT of a single-step event has TF clear
				return true;
			case 3:
				m.code = st_breakpoint;
				m.address = m.ctx_rip = ev.rip - 1;
				m.parameters = 1;   // Information[0] = BREAKPOINT_BREAK (0)
				return true;
			case 4:
				m.code = st_integer_overflow;
				m.address = ev.rip - 1;
				return true;
			case 6:
				m.code = st_illegal_instruction;
				return true;
			case 13:
			{
				const decoded d = decode_at( uc, ev.rip );
				if ( d.ok && privileged_insn( d ) )
				{
					m.code = st_privileged_instruction;
					return true;
				}
				bool mem = false;
				for ( uint8_t i = 0; d.ok && i < d.x.op_count; i++ ) mem = mem || d.x.operands[ i ].type == X86_OP_MEM;
				if ( d.ok && ( d.id == X86_INS_INT || d.id == X86_INS_INT1 || mem ) )
				{
					m.code = st_access_violation;
					m.parameters = 2;
					m.info[ 0 ] = 0;
					m.info[ 1 ] = ~0ull;
					return true;
				}
				why = "#GP(" + hx( ev.error_code ) + ") at " + hx( ev.rip ) + " (" + ( d.ok ? std::to_string( d.id ) : std::string( "not decoded" ) ) +
					  "): neither a privileged instruction, INT n nor a memory operand - not measured";
				return false;
			}
			case 14:
				m.code = st_access_violation;
				m.parameters = 2;
				m.info[ 0 ] = uint64_t( ev.pf_access );
				m.info[ 1 ] = ev.pf_address;
				return true;
			case 17:
				m.code = st_datatype_misalignment;
				return true;
			case 0x2C:
				m.code = st_assertion_failure;
				m.address = m.ctx_rip = ev.rip - 2;
				return true;
			case 0x2D:
			{
				const uint64_t service = ev.gpr[ 0 ];
				if ( service == 1 || service == 3 || service == 4 || service == 5 )
				{
					m.silent = true;
					m.ctx_rip = ev.rip + 1;
					return true;
				}
				m.code = st_breakpoint;
				m.address = ev.rip + 1;
				m.ctx_rip = ev.rip;
				m.parameters = 1;
				m.info[ 0 ] = service;
				return true;
			}
			case 0x2E:
				why = "a system call through the IDT (INT 2EH): not a path of this environment (SYSCALL is) - stopped, never forwarded";
				return false;
			default:
				why = "vector " + std::to_string( ev.vector ) + ( ev.has_error_code ? " error code " + hx( ev.error_code ) : std::string() ) +
					  ": Windows' answer is not measured (P49: 0, 1, 3, 4, 6, 13, 14, 17, 2CH, 2DH are) - stopped, never guessed";
				return false;
		}
	}

	// ------------------------------------------------------------------------------------------------ U2251 / U2252: the save stub ended
	// a stop names the event first ("#UD at rip X", "#PF write access to A at rip X", "INT 2EH at rip X")
	bool process::exception_saved( uc_engine* uc, hook_ctx& c, std::string& why )
	{
		static const char* const names[ 32 ] = { "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM", "#DF", "vector 9", "#TS", "#NP", "#SS", "#GP", "#PF", "vector 15",
												  "#MF", "#AC", "#MC", "#XM", "#VE", "#CP" };
		const auto& ev = c.ev;
		char nb[ 16 ];
		std::snprintf( nb, sizeof( nb ), "INT %02XH", ev.vector );
		std::string evt = ev.vector < 32 && names[ ev.vector ] ? std::string( names[ ev.vector ] ) : std::string( nb );
		if ( ev.page_fault ) evt += std::string( ev.pf_access == 8 ? " execute" : ev.pf_access == 1 ? " write" : " read" ) + " access to " + hx( ev.pf_address );
		evt += " at rip " + hx( ev.rip );
		if ( exception_saved_body( uc, c, why ) ) return true;
		why = evt + ": " + why;
		return false;
	}

	bool process::exception_saved_body( uc_engine* uc, hook_ctx& c, std::string& why )
	{
		auto& ev = c.ev;
		if ( !ev.active )
		{
			why = "the save stub ran without an event";
			return false;
		}
		ev.active = false;
		auto& r = *c.r;
		exception_event rec;
		rec.index = ++c.exceptions_seen;
		rec.insn = r.insns;
		rec.vector = int( ev.vector );
		rec.error_code = ev.error_code;
		rec.has_error_code = ev.has_error_code;
		rec.frame_rip = ev.rip;
		rec.frame_rflags = ev.rflags;
		rec.frame_rsp = ev.rsp;
		r.fault_vector = int( ev.vector );
		r.fault_error_code = ev.has_error_code ? int64_t( ev.error_code ) : -1;
		r.fault_address = ev.page_fault ? ev.pf_address : ev.rip;
		if ( r.probes.enabled ) r.probes.exceptions[ int( ev.vector ) ]++;
		mapped m;
		if ( !windows_record( uc, c, m, why ) )
		{
			rec.how = "stopped: " + why;
			r.exceptions.push_back( rec );
			return false;
		}
		auto iret_to = [ & ]( uint64_t rip, uint64_t rsp, uint64_t rflags ) {
			const uint64_t frame[ 5 ] = { rip, uint64_t( layout::gdt_r3_code | layout::rpl3 ), ( rflags & eflags_user_sanitize ) | 0x202, rsp, uint64_t( layout::gdt_r3_data | layout::rpl3 ) };
			const uint64_t fva = guest_kernel_stack_va + page_size - sizeof( frame );
			std::string err;
			if ( !e_->write( fva, frame, sizeof( frame ), err ) )
			{
				why = err;
				return false;
			}
			if ( uc_reg_write( uc, UC_X86_REG_RSP, &fva ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_RIP, &iretq_stub_ ) != UC_ERR_OK )
			{
				why = "register write failed";
				return false;
			}
			return true;
		};
		auto restore_gprs = [ & ]() {
			for ( int i = 0; i < 16; i++ )
				if ( i != 4 ) uc_reg_write( uc, gpr_ids[ i ], &ev.gpr[ i ] );
		};
		if ( m.silent )
		{
			rec.how = "silent: INT 2DH debug service " + hx( ev.gpr[ 0 ] ) + " without a kernel debugger - the thread continues at " + hx( m.ctx_rip ) + " (measured TX1)";
			r.exceptions.push_back( rec );
			restore_gprs();
			return iret_to( m.ctx_rip, ev.rsp, ev.rflags & ~rf_flag );
		}
		rec.code = m.code;
		rec.address = m.address;
		rec.context_rip = m.ctx_rip;
		rec.parameters = m.parameters;
		rec.info[ 0 ] = m.info[ 0 ];
		rec.info[ 1 ] = m.info[ 1 ];
		if ( !xstate_ok_ )
		{
			why = "an exception frame needs the XSAVE layout of the CPU profile (leaf 0DH, XSAVEC) - not available (--max)";
			rec.how = "stopped: " + why;
			r.exceptions.push_back( rec );
			return false;
		}
		// the frame (file header item 3)
		const uint64_t S = ev.rsp, ctx = ( S & ~63ull ) - xsave_off - xstate_user_size_;
		const uint64_t frame_size = S - ctx;
		if ( S < xsave_off + xstate_user_size_ + 64 || !kern().user_buffer( ctx, frame_size, UC_PROT_READ | UC_PROT_WRITE ) )
		{
			why = "the exception frame [" + hx( ctx ) + ", " + hx( S ) + ") is not writable user stack (Windows: a stack overflow / a failed delivery - not measured)";
			rec.how = "stopped: " + why;
			r.exceptions.push_back( rec );
			return false;
		}
		std::string err;
		uint8_t fx[ 0x200 ] = {};
		std::vector<uint8_t> xs( size_t( xstate_user_size_ ), 0 );
		if ( !e_->read( ksave_va_, fx, sizeof( fx ), err ) || !e_->read( ksave_va_ + 0x600, xs.data(), xs.size(), err ) )
		{
			why = "the save area: " + err;
			return false;
		}
		auto w = [ & ]( uint64_t va, const void* d, uint64_t n ) {
			if ( !e_->write( va, d, n, err ) )
			{
				why = "the exception frame: " + err;
				return false;
			}
			return true;
		};
		auto w64 = [ & ]( uint64_t va, uint64_t v ) { return w( va, &v, 8 ); };
		auto w32 = [ & ]( uint64_t va, uint32_t v ) { return w( va, &v, 4 ); };
		const uint16_t sel[ 6 ] = { uint16_t( layout::gdt_r3_code | layout::rpl3 ), 0x2B, 0x2B, 0x53, 0x2B, 0x2B };   // Cs, Ds, Es, Fs, Gs, Ss
		uint32_t mxcsr = 0;
		std::memcpy( &mxcsr, fx + 0x18, 4 );
		uint64_t gpr[ 16 ];
		std::memcpy( gpr, ev.gpr, sizeof( gpr ) );
		gpr[ 4 ] = S;
		const uint64_t zero[ 6 ] = {};
		const uint8_t zero32[ 0x20 ] = {};
		if ( !w64( ctx + c_p3home, 0 ) || !w32( ctx + c_flags, context_all_xstate ) || !w32( ctx + c_mxcsr, mxcsr ) || !w( ctx + c_segcs, sel, sizeof( sel ) ) ||
			 !w32( ctx + c_eflags, uint32_t( m.ctx_eflags ) ) || !w( ctx + c_dr0, zero, sizeof( zero ) ) || !w( ctx + c_rax, gpr, sizeof( gpr ) ) ||
			 !w64( ctx + c_rip, m.ctx_rip ) || !w( ctx + c_fltsave, fx, 0x1A0 ) || !w( ctx + c_lastbranch, zero32, sizeof( zero32 ) ) )
			return false;
		// CONTEXT_EX {All, Legacy, XState} (LONG Offset, ULONG Length each), offsets from the CONTEXT_EX
		const int32_t ex[ 6 ] = { -int32_t( ctx_ex_off ), int32_t( frame_size ), -int32_t( ctx_ex_off ), int32_t( ctx_size ), int32_t( xsave_off - ctx_ex_off ),
								  int32_t( S - ( ctx + xsave_off ) ) };
		if ( !w( ctx + ctx_ex_off, ex, sizeof( ex ) ) ) return false;
		// EXCEPTION_RECORD (98H): code, flags 0, nested record 0, address, NumberParameters, Information[15]
		uint8_t er[ rec_size ] = {};
		std::memcpy( er, &m.code, 4 );
		std::memcpy( er + 0x10, &m.address, 8 );
		std::memcpy( er + 0x18, &m.parameters, 4 );
		std::memcpy( er + 0x20, m.info, 16 );
		if ( !w( ctx + rec_off, er, sizeof( er ) ) || !w64( ctx + mframe_rip_off, m.ctx_rip ) || !w64( ctx + mframe_rsp_off, S ) ) return false;
		// the XSAVE area: the header (40H), then each component whose XSTATE_BV bit is set (compacted offsets, Vol1 13.4.3)
		if ( !w( ctx + xsave_off, xs.data(), 0x40 ) ) return false;
		uint64_t bv = 0;
		std::memcpy( &bv, xs.data(), 8 );
		{
			uint64_t off = 0x40;
			for ( unsigned i = 2; i < 63; i++ )
			{
				if ( !( xstate_user_mask_ >> i & 1 ) ) continue;
				uint32_t sz = 0, al = 0;
				for ( const auto& l : e_->profile().leaves )
					if ( l.leaf == 0xD && l.subleaf == i )
					{
						sz = l.eax;
						al = l.ecx & 2;
					}
				if ( al ) off = ( off + 63 ) & ~63ull;
				if ( ( bv >> i & 1 ) && off + sz <= xs.size() && !w( ctx + xsave_off + off, xs.data() + off, sz ) ) return false;
				off += sz;
			}
		}
		const uint64_t rec_va = ctx + rec_off;
		rec.how = "dispatched, the CONTEXT at " + hx( ctx );   // the same in --Sys (ntdll!KiUserExceptionDispatcher) and --HLE (the dispatcher here)
		r.exceptions.push_back( rec );
		const uint64_t disp_flags = ev.rflags & ~( tf_flag | df_flag | ac_flag );
		if ( loader_start_ )
		{
			if ( !ki_user_exception_dispatcher_ )
				for ( const auto& md : modules_ )
					if ( md.name == "ntdll.dll" )
						for ( const auto& [ rva, name ] : md.exports_by_rva )
							if ( name == "KiUserExceptionDispatcher" ) ki_user_exception_dispatcher_ = md.base + rva;
			if ( !ki_user_exception_dispatcher_ )
			{
				why = "ntdll.dll exports no KiUserExceptionDispatcher";
				return false;
			}
			restore_gprs();
			uc_reg_write( uc, UC_X86_REG_RCX, &rec_va );
			uc_reg_write( uc, UC_X86_REG_RDX, &ctx );
			return iret_to( ki_user_exception_dispatcher_, ctx, disp_flags );
		}
		if ( po_.boundary != env_boundary::hle )
		{
			why = "an exception under --Sys without the --loader start: no ntdll dispatcher runs (the direct start of milestone M4) - use --Sys --loader";
			return false;
		}
		// --HLE: the dispatcher (file header item 7)
		if ( !hle().mapped() )
		{
			why = "--HLE: an exception in an image without imports (no HLE page: no dispatcher return point) - not modelled";
			return false;
		}
		auto& hd = c.hd;
		hd = hook_ctx::hle_dispatch{};
		hd.active = true;
		hd.rec = rec_va;
		hd.ctx = ctx;
		hd.ctx_copy = ctx - 0x6D0;
		hd.dc = hd.ctx_copy - 0x60;
		hd.ptrs = hd.dc - 0x10;
		hd.call_rsp = ( ( ctx - 0x800 ) & ~0xFull ) - 8;
		hd.rflags = disp_flags;
		if ( !kern().user_buffer( hd.call_rsp - 0x100, ctx - ( hd.call_rsp - 0x100 ), UC_PROT_READ | UC_PROT_WRITE ) )
		{
			why = "the --HLE dispatcher's stack below the CONTEXT is not writable";
			return false;
		}
		const uint64_t ptrs[ 2 ] = { rec_va, ctx };
		if ( !w( hd.ptrs, ptrs, sizeof( ptrs ) ) ) return false;
		restore_gprs();
		return hle_dispatch_return( uc, c, why );   // the first step (no call returned yet)
	}

	// ------------------------------------------------------------------------------------------------ U2256: the --HLE dispatcher
	namespace
	{
		struct unwind_result
		{
			bool ok = false;
			uint64_t handler = 0, handler_data = 0, establisher = 0, function = 0;
		};
	}

	bool process::hle_dispatch_return( uc_engine* uc, hook_ctx& c, std::string& why )
	{
		auto& hd = c.hd;
		if ( !hd.active )
		{
			why = "the --HLE dispatch return point was reached without a dispatch";
			return false;
		}
		std::string err;
		const bool first = hd.last.empty();
		const int32_t ret = int32_t( rreg( uc, UC_X86_REG_RAX ) );
		auto call = [ & ]( uint64_t target, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, const std::string& what ) {
			const uint64_t ra = hle::dispatch_return_va;
			if ( !e_->write( hd.call_rsp, &ra, 8, err ) )
			{
				why = "the --HLE dispatcher's call frame: " + err;
				return false;
			}
			const uint64_t a[ 4 ] = { a1, a2, a3, a4 };
			const int ids[ 4 ] = { UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9 };
			for ( int i = 0; i < 4; i++ ) uc_reg_write( uc, ids[ i ], &a[ i ] );
			hd.last = what;
			if ( first )
			{
				// from the kernel: IRETQ to the handler
				const uint64_t frame[ 5 ] = { target, uint64_t( layout::gdt_r3_code | layout::rpl3 ), ( hd.rflags & eflags_user_sanitize ) | 0x202, hd.call_rsp,
											  uint64_t( layout::gdt_r3_data | layout::rpl3 ) };
				const uint64_t fva = guest_kernel_stack_va + page_size - sizeof( frame );
				if ( !e_->write( fva, frame, sizeof( frame ), err ) )
				{
					why = err;
					return false;
				}
				return uc_reg_write( uc, UC_X86_REG_RSP, &fva ) == UC_ERR_OK && uc_reg_write( uc, UC_X86_REG_RIP, &iretq_stub_ ) == UC_ERR_OK;
			}
			return uc_reg_write( uc, UC_X86_REG_RSP, &hd.call_rsp ) == UC_ERR_OK && uc_reg_write( uc, UC_X86_REG_RIP, &target ) == UC_ERR_OK;
		};
		auto finish = [ & ]() {
			hd.active = false;
			hd.last.clear();
			return restore_full_context( uc, c, hd.ctx, why );
		};
		// the result of the previous call
		if ( !first )
		{
			if ( hd.phase == 0 && ret == -1 )   // a vectored handler: EXCEPTION_CONTINUE_EXECUTION
			{
				hd.phase = 2;
				hd.next = 0;
			}
			else if ( hd.phase == 1 )
			{
				if ( ret == 0 )   // ExceptionContinueExecution
				{
					uint32_t flags = 0;
					e_->read( hd.rec + 4, &flags, 4, err );
					if ( flags & 1 )
					{
						why = "a frame handler continued a non-continuable exception (Windows raises STATUS_NONCONTINUABLE_EXCEPTION) - not modelled";
						return false;
					}
					hd.phase = 2;
					hd.next = 0;
				}
				else if ( ret != 1 )
				{
					why = "the frame handler " + hd.last + " returned disposition " + std::to_string( ret ) + " (nested / collided unwind) - not modelled";
					return false;
				}
			}
			else if ( hd.phase == 2 && ret == -1 )
				return finish();   // a continue handler continued: the rest is not called
		}
		const auto& veh = hle().vectored_handlers(), &vch = hle().continue_handlers();
		if ( hd.phase == 0 )
		{
			if ( hd.next < veh.size() )
			{
				const auto h = veh[ hd.next++ ];
				return call( h.handler, hd.ptrs, 0, 0, 0, "vectored handler " + hx( h.handler ) );
			}
			hd.phase = 1;
			hd.next = 0;
			// the walk starts at the CONTEXT
			uint64_t g[ 16 ] = {};
			if ( !e_->read( hd.ctx + c_rax, g, sizeof( g ), err ) || !e_->read( hd.ctx + c_rip, &hd.uw_rip, 8, err ) )
			{
				why = "the CONTEXT: " + err;
				return false;
			}
			std::memcpy( hd.uw, g, sizeof( g ) );
		}
		if ( hd.phase == 1 )
		{
			// frames until a handler takes it (file header item 7)
			for ( int depth = 0; depth < 256; depth++ )
			{
				const module_image* img = nullptr;
				for ( const auto& md : modules_ )
					if ( hd.uw_rip >= md.base && hd.uw_rip - md.base < md.size ) img = &md;
				const uint64_t sp = hd.uw[ 4 ];
				uint64_t stack_base = 0, stack_limit = 0;
				e_->read( guest_teb_va + layout::teb_stack_base, &stack_base, 8, err );
				e_->read( guest_teb_va + layout::teb_stack_limit, &stack_limit, 8, err );
				if ( !img || hd.uw_rip == guest_return_sentinel )
				{
					why = "unhandled exception: no frame handler took it (the walk reached " + hx( hd.uw_rip ) + ") - Windows' unhandled-exception path is not measured";
					return false;
				}
				const auto& ed = img->img.dirs[ 3 ];   // IMAGE_DIRECTORY_ENTRY_EXCEPTION (winnt.h)
				uint64_t fe = 0;
				uint32_t rf[ 3 ] = {};
				const uint32_t rva = uint32_t( hd.uw_rip - img->base );
				for ( uint32_t lo = 0, hi = ed.size / 12; lo < hi; )
				{
					const uint32_t mid = ( lo + hi ) / 2;
					if ( !e_->read( img->base + ed.rva + 12ull * mid, rf, 12, err ) ) break;
					if ( rva < rf[ 0 ] ) hi = mid;
					else if ( rva >= rf[ 1 ] ) lo = mid + 1;
					else
					{
						fe = img->base + ed.rva + 12ull * mid;
						break;
					}
				}
				if ( !fe )
				{
					// a leaf: RIP = [RSP], RSP + 8
					uint64_t ra = 0;
					if ( !e_->read( sp, &ra, 8, err ) )
					{
						why = "the walk: [RSP] " + hx( sp ) + " not readable";
						return false;
					}
					hd.uw_rip = ra;
					hd.uw[ 4 ] = sp + 8;
					continue;
				}
				const uint64_t control_pc = hd.uw_rip, image_base = img->base;
				unwind_result ur;
				if ( !virtual_unwind( image_base, control_pc, fe, hd.uw, hd.uw_rip, ur.handler, ur.handler_data, ur.establisher, why ) ) return false;
				if ( ur.establisher < stack_limit || ur.establisher >= stack_base || ( ur.establisher & 7 ) )
				{
					why = "the walk: EstablisherFrame " + hx( ur.establisher ) + " outside the stack [" + hx( stack_limit ) + ", " + hx( stack_base ) +
						  ") or misaligned (Windows: EXCEPTION_STACK_INVALID) - not modelled";
					return false;
				}
				if ( !ur.handler ) continue;
				// the DISPATCHER_CONTEXT (winnt.h _AMD64_: ControlPc, ImageBase, FunctionEntry, EstablisherFrame, TargetIp, ContextRecord,
				// LanguageHandler, HandlerData, HistoryTable, ScopeIndex, Fill0) and the unwound CONTEXT copy it names
				std::vector<uint8_t> cc( size_t( ctx_size ), 0 );
				if ( !e_->read( hd.ctx, cc.data(), cc.size(), err ) ) return false;
				std::memcpy( cc.data() + c_rax, hd.uw, sizeof( hd.uw ) );
				std::memcpy( cc.data() + c_rip, &hd.uw_rip, 8 );
				uint8_t dc[ 0x50 ] = {};
				const uint64_t dcv[ 8 ] = { control_pc, image_base, fe, ur.establisher, 0, hd.ctx_copy, ur.handler, ur.handler_data };
				std::memcpy( dc, dcv, sizeof( dcv ) );
				if ( !e_->write( hd.ctx_copy, cc.data(), cc.size(), err ) || !e_->write( hd.dc, dc, sizeof( dc ), err ) )
				{
					why = "the dispatcher context: " + err;
					return false;
				}
				hd.establisher = ur.establisher;
				return call( ur.handler, hd.rec, ur.establisher, hd.ctx, hd.dc, "frame handler " + hx( ur.handler ) + " (frame " + hx( ur.establisher ) + ")" );
			}
			why = "the walk exceeded 256 frames";
			return false;
		}
		if ( hd.phase == 2 )
		{
			if ( hd.next < vch.size() )
			{
				const auto h = vch[ hd.next++ ];
				return call( h.handler, hd.ptrs, 0, 0, 0, "continue handler " + hx( h.handler ) );
			}
			return finish();
		}
		why = "the --HLE dispatcher: an unknown phase";
		return false;
	}

	// The x64 unwind of one frame (Microsoft Learn "x64 exception handling": struct UNWIND_INFO {Version : 3, Flags : 5, SizeOfProlog,
	// CountOfCodes, FrameRegister : 4, FrameOffset : 4, UNWIND_CODE[]; handler RVA + data or a chained RUNTIME_FUNCTION}; the unwind
	// operation codes; the unwind procedure: an epilog is simulated forward, else the codes whose prolog offset was reached are undone in
	// order, then RIP = [RSP], RSP + 8 unless a machine frame was popped). G = RAX .. R15 (in / out), RIP (in: the control PC, out: the
	// caller's). The handler (UNW_FLAG_EHANDLER 1) is returned only outside the prolog and an epilog.
	bool process::virtual_unwind( uint64_t image_base, uint64_t control_pc, uint64_t function_entry, uint64_t* g, uint64_t& rip, uint64_t& handler,
								  uint64_t& handler_data, uint64_t& establisher, std::string& why )
	{
		std::string err;
		handler = handler_data = 0;
		uint32_t rf[ 3 ] = {};
		if ( !e_->read( function_entry, rf, 12, err ) )
		{
			why = "RUNTIME_FUNCTION " + hx( function_entry ) + ": " + err;
			return false;
		}
		const uint64_t begin = image_base + rf[ 0 ];
		const uint64_t offset = control_pc - begin;
		bool machine_frame = false;
		auto rd64 = [ & ]( uint64_t a, uint64_t& v ) {
			if ( !e_->read( a, &v, 8, err ) )
			{
				why = "the unwind: " + hx( a ) + " not readable";
				return false;
			}
			return true;
		};
		// the epilog test (the instruction at the control PC): ADD RSP, imm / LEA RSP, [FP + disp], then POP r64 ..., then RET (C3 / F3 C3)
		// or a JMP (E9 rel32 / FF 25) - simulated forward
		{
			uint8_t b[ 64 ] = {};
			for ( size_t k = 0; k < sizeof( b ); k++ )
				if ( !e_->read( control_pc + k, b + k, 1, err ) ) break;
			size_t i = 0;
			uint64_t s = g[ 4 ];
			bool adj = false;
			if ( b[ 0 ] == 0x48 && b[ 1 ] == 0x83 && b[ 2 ] == 0xC4 ) { s += int8_t( b[ 3 ] ); i = 4; adj = true; }
			else if ( b[ 0 ] == 0x48 && b[ 1 ] == 0x81 && b[ 2 ] == 0xC4 ) { int32_t d; std::memcpy( &d, b + 3, 4 ); s += int64_t( d ); i = 7; adj = true; }
			uint64_t regs[ 16 ];
			std::memcpy( regs, g, sizeof( regs ) );
			bool epilog = false;
			for ( int n = 0; n < 16 && i < 60; n++ )
			{
				unsigned rx = 0;
				size_t j = i;
				if ( b[ j ] >= 0x40 && b[ j ] <= 0x4F ) rx = b[ j++ ] & 1;
				if ( b[ j ] >= 0x58 && b[ j ] <= 0x5F )
				{
					uint64_t v = 0;
					if ( !rd64( s, v ) ) return false;
					regs[ ( b[ j ] - 0x58 ) + 8 * rx ] = v;
					s += 8;
					i = j + 1;
					continue;
				}
				if ( b[ j ] == 0xC3 || ( b[ j ] == 0xF3 && b[ j + 1 ] == 0xC3 ) || b[ j ] == 0xE9 || ( b[ j ] == 0xFF && b[ j + 1 ] == 0x25 ) ) epilog = true;
				break;
			}
			if ( epilog && ( adj || i > 0 || b[ 0 ] == 0xC3 || ( b[ 0 ] == 0xF3 && b[ 1 ] == 0xC3 ) ) )
			{
				std::memcpy( g, regs, sizeof( regs ) );
				establisher = g[ 4 ];
				uint64_t ra = 0;
				if ( !rd64( s, ra ) ) return false;
				rip = ra;
				g[ 4 ] = s + 8;
				return true;
			}
		}
		uint64_t entry = function_entry;
		bool primary = true;
		uint64_t frame_base = g[ 4 ];
		for ( int chain = 0; chain < 32; chain++ )
		{
			if ( !e_->read( entry, rf, 12, err ) )
			{
				why = "RUNTIME_FUNCTION " + hx( entry ) + ": " + err;
				return false;
			}
			const uint64_t ui = image_base + ( rf[ 2 ] & ~1u );
			uint8_t h[ 4 ] = {};
			if ( !e_->read( ui, h, 4, err ) )
			{
				why = "UNWIND_INFO " + hx( ui ) + ": " + err;
				return false;
			}
			const unsigned version = h[ 0 ] & 7, flags = h[ 0 ] >> 3, prolog = h[ 1 ], count = h[ 2 ], fpreg = h[ 3 ] & 15, fpoff = h[ 3 ] >> 4;
			if ( version != 1 && version != 2 )
			{
				why = "UNWIND_INFO version " + std::to_string( version ) + " at " + hx( ui ) + " - not modelled";
				return false;
			}
			std::vector<uint16_t> codes( count );
			if ( count && !e_->read( ui + 4, codes.data(), 2ull * count, err ) )
			{
				why = "UNWIND_CODE[] " + hx( ui + 4 ) + ": " + err;
				return false;
			}
			const uint64_t fn_begin = image_base + rf[ 0 ];
			const uint64_t off = primary ? control_pc - fn_begin : ~0ull;   // a chained entry: its whole prolog ran
			if ( primary )
			{
				// the establisher frame: the frame register's base when the frame register is set up (UWOP_SET_FPREG executed), else RSP
				frame_base = g[ 4 ];
				if ( fpreg )
				{
					bool fp_set = false;
					for ( unsigned k = 0; k < count; k++ )
						if ( ( ( codes[ k ] >> 8 ) & 15 ) == 3 && ( codes[ k ] & 0xFF ) <= off ) fp_set = true;
					if ( fp_set ) frame_base = g[ fpreg ] - 16ull * fpoff;
				}
				establisher = frame_base;
			}
			for ( unsigned k = 0; k < count; )
			{
				const unsigned code_off = codes[ k ] & 0xFF, op = ( codes[ k ] >> 8 ) & 15, info = codes[ k ] >> 12;
				unsigned slots = 1;
				switch ( op )
				{
					case 0: case 2: case 3: case 10: slots = 1; break;
					case 1: slots = info ? 3 : 2; break;
					case 4: case 8: slots = 2; break;
					case 5: case 9: slots = 3; break;
					case 6:
						if ( version == 2 ) { slots = 2; break; }
						[[fallthrough]];
					default:
						why = "UNWIND_CODE op " + std::to_string( op ) + " at " + hx( ui ) + " - not modelled";
						return false;
				}
				if ( k + slots > count )
				{
					why = "UNWIND_CODE[] truncated at " + hx( ui );
					return false;
				}
				const bool done = op == 6 || code_off <= off;   // the prolog reached this operation (op 6: version-2 epilog records are skipped)
				if ( done && op != 6 )
				{
					uint64_t v = 0;
					switch ( op )
					{
						case 0:   // UWOP_PUSH_NONVOL
							if ( !rd64( g[ 4 ], v ) ) return false;
							g[ info ] = v;
							g[ 4 ] += 8;
							break;
						case 1:   // UWOP_ALLOC_LARGE
							g[ 4 ] += info ? ( uint64_t( codes[ k + 1 ] ) | uint64_t( codes[ k + 2 ] ) << 16 ) : uint64_t( codes[ k + 1 ] ) * 8;
							break;
						case 2:   // UWOP_ALLOC_SMALL
							g[ 4 ] += uint64_t( info ) * 8 + 8;
							break;
						case 3:   // UWOP_SET_FPREG
							g[ 4 ] = g[ fpreg ] - 16ull * fpoff;
							break;
						case 4:   // UWOP_SAVE_NONVOL
							if ( !rd64( frame_base + uint64_t( codes[ k + 1 ] ) * 8, v ) ) return false;
							g[ info ] = v;
							break;
						case 5:   // UWOP_SAVE_NONVOL_FAR
							if ( !rd64( frame_base + ( uint64_t( codes[ k + 1 ] ) | uint64_t( codes[ k + 2 ] ) << 16 ), v ) ) return false;
							g[ info ] = v;
							break;
						case 8: case 9:   // UWOP_SAVE_XMM128 (_FAR): the XMM registers are not part of this walk's state
							break;
						case 10:   // UWOP_PUSH_MACHFRAME: RIP and RSP from the machine frame (+ 8 with an error code)
						{
							const uint64_t mf = g[ 4 ] + ( info ? 8 : 0 );
							uint64_t mrsp = 0;
							if ( !rd64( mf, v ) || !rd64( mf + 24, mrsp ) ) return false;
							rip = v;
							g[ 4 ] = mrsp;
							machine_frame = true;
							break;
						}
					}
				}
				k += slots;
			}
			if ( primary && ( flags & 1 ) && off >= prolog )   // UNW_FLAG_EHANDLER outside the prolog
			{
				uint32_t hr = 0;
				const uint64_t ha = ui + 4 + 2ull * ( ( count + 1 ) & ~1u );
				if ( !e_->read( ha, &hr, 4, err ) )
				{
					why = "the exception handler RVA at " + hx( ha ) + ": " + err;
					return false;
				}
				handler = image_base + hr;
				handler_data = ha + 4;
			}
			if ( flags & 4 )   // UNW_FLAG_CHAININFO: the chained RUNTIME_FUNCTION after the codes
			{
				entry = ui + 4 + 2ull * ( ( count + 1 ) & ~1u );
				primary = false;
				continue;
			}
			break;
		}
		if ( !machine_frame )
		{
			uint64_t ra = 0;
			if ( !rd64( g[ 4 ], ra ) ) return false;
			rip = ra;
			g[ 4 ] += 8;
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------------ U2255: NtContinue with CONTEXT_XSTATE
	bool process::restore_full_context( uc_engine* uc, hook_ctx& c, uint64_t ctx, std::string& why )
	{
		std::vector<uint8_t> cc( size_t( ctx_size ) + 0x20 );
		std::string err;
		if ( !e_->read( ctx, cc.data(), cc.size(), err ) )
		{
			why = "CONTEXT " + hx( ctx ) + " (+ CONTEXT_EX) not readable: " + err;
			return false;
		}
		auto g = [ & ]( uint32_t off, unsigned n ) {
			uint64_t v = 0;
			std::memcpy( &v, cc.data() + off, n );
			return v;
		};
		const uint32_t flags = uint32_t( g( c_flags, 4 ) );
		if ( ( flags & 0x10001F ) != 0x10001F && ( flags & 0x10000B ) != 0x10000B )
		{
			why = "CONTEXT " + hx( ctx ) + " ContextFlags " + hx( flags ) + ": CONTEXT_XSTATE without CONTROL / INTEGER / FLOATING_POINT - not modelled";
			return false;
		}
		if ( flags & ~uint32_t( 0x10005F ) )
		{
			why = "CONTEXT " + hx( ctx ) + " ContextFlags " + hx( flags ) + ": bits beyond CONTEXT_ALL | CONTEXT_XSTATE - not modelled";
			return false;
		}
		if ( ( flags & 0x100010 ) == 0x100010 )
			for ( uint32_t o = c_dr0; o <= c_dr7; o += 8 )
				if ( o != 0x68 && g( o, 8 ) )   // Dr6 is a status register; Dr0-3 / Dr7 set = hardware breakpoints
				{
					why = "CONTEXT " + hx( ctx ) + " sets debug registers (CONTEXT_DEBUG_REGISTERS, " + hx( g( o, 8 ) ) + " at +" + hx( o ) + "): hardware breakpoints are not modelled";
					return false;
				}
		// the legacy image: FltSave with MXCSR := CONTEXT.MxCsr AND MXCSR_MASK (FltSave + 1CH; 0 = FFBFH, Vol1 11.6.6)
		uint8_t fx[ 0x200 ];
		std::memcpy( fx, cc.data() + c_fltsave, sizeof( fx ) );
		uint32_t mask = 0;
		std::memcpy( &mask, fx + 0x1C, 4 );
		if ( !mask ) mask = 0xFFBF;
		const uint32_t mx = uint32_t( g( c_mxcsr, 4 ) ) & mask;
		std::memcpy( fx + 0x18, &mx, 4 );
		uint8_t xs_hdr[ 0x40 ] = {};
		uint64_t xmask = 0;
		std::vector<uint8_t> xs;
		if ( xstate_ok_ && xstate_user_mask_ )
		{
			// the CONTEXT_EX's XState chunk: the header and the components
			int32_t xoff = 0;
			uint32_t xlen = 0;
			std::memcpy( &xoff, cc.data() + ctx_ex_off + 0x10, 4 );
			std::memcpy( &xlen, cc.data() + ctx_ex_off + 0x14, 4 );
			const uint64_t xa = ctx + ctx_ex_off + int64_t( xoff );
			if ( xlen < 0x40 || xlen > 0x10000 || !kern().user_buffer( xa, xlen, UC_PROT_READ ) )
			{
				why = "CONTEXT_EX XState {" + hx( uint32_t( xoff ) ) + ", " + hx( xlen ) + "} of CONTEXT " + hx( ctx ) + ": not a readable XSAVE area - not modelled";
				return false;
			}
			xs.resize( std::min<uint64_t>( xlen, xstate_user_size_ ) );
			if ( !e_->read( xa, xs.data(), xs.size(), err ) ) return false;
			std::memcpy( xs_hdr, xs.data(), 0x40 );
			uint64_t bv = 0, comp = 0;
			std::memcpy( &bv, xs_hdr, 8 );
			std::memcpy( &comp, xs_hdr + 8, 8 );
			bool reserved_zero = true;
			for ( int k = 16; k < 64; k++ ) reserved_zero = reserved_zero && !xs_hdr[ k ];
			if ( comp != ( ( 1ull << 63 ) | xstate_user_mask_ ) || ( bv & ~xstate_user_mask_ ) || !reserved_zero )
			{
				why = "the XSAVE header of CONTEXT " + hx( ctx ) + " (XSTATE_BV " + hx( bv ) + ", XCOMP_BV " + hx( comp ) +
					  ") is not the compacted user form Windows writes - Windows' answer to it is not measured";
				return false;
			}
			xmask = ( flags & 0x100040 ) == 0x100040 ? xstate_user_mask_ : 0;
		}
		else
		{
			// no XSAVE components: an empty compacted header (XRSTOR with RFBM 0 restores nothing)
			const uint64_t comp = 1ull << 63;
			std::memcpy( xs_hdr + 8, &comp, 8 );
			xs.assign( xs_hdr, xs_hdr + 0x40 );
		}
		// the XRSTOR area at + 400H: its legacy region = the same image (XRSTOR loads MXCSR from it when RFBM[2] = 1, Vol1 13.8), the header at + 600H
		if ( !e_->write( ksave_va_, fx, sizeof( fx ), err ) || !e_->write( ksave_va_ + 0x400, fx, sizeof( fx ), err ) ||
			 !e_->write( ksave_va_ + 0x600, xs.data(), xs.size(), err ) )
		{
			why = "the restore area: " + err;
			return false;
		}
		auto& rs = c.rs;
		rs = hook_ctx::pending_restore{};
		rs.active = true;
		rs.ctx = ctx;
		std::memcpy( rs.gpr, cc.data() + c_rax, sizeof( rs.gpr ) );
		rs.rip = g( c_rip, 8 );
		rs.rsp = g( c_rsp, 8 );
		rs.rflags = ( g( c_eflags, 4 ) & eflags_user_sanitize ) | 0x202;
		// RFLAGS 202H while the stub runs: at CPL 3 (the --HLE dispatcher's end) IRETQ cannot change IF (253667-093 Vol2A IRET: IF is
		// updated only if CPL <= IOPL), so the user's IF = 1 is kept; at CPL 0 IRETQ loads it from the frame
		const uint64_t a = xmask & 0xFFFFFFFF, d = xmask >> 32, ksp = guest_kernel_stack_va + page_size - 0x40, kfl = 0x202;
		if ( uc_reg_write( uc, UC_X86_REG_RAX, &a ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_RDX, &d ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_RSP, &ksp ) != UC_ERR_OK ||
			 uc_reg_write( uc, UC_X86_REG_RFLAGS, &kfl ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_RIP, &restore_stub_ ) != UC_ERR_OK )
		{
			why = "register write failed";
			return false;
		}
		return true;
	}

	bool process::context_restored( uc_engine* uc, hook_ctx& c, std::string& why )
	{
		auto& rs = c.rs;
		if ( !rs.active )
		{
			why = "the restore stub ran without a CONTEXT";
			return false;
		}
		rs.active = false;
		for ( int i = 0; i < 16; i++ )
			if ( i != 4 ) uc_reg_write( uc, gpr_ids[ i ], &rs.gpr[ i ] );
		const uint64_t frame[ 5 ] = { rs.rip, uint64_t( layout::gdt_r3_code | layout::rpl3 ), rs.rflags, rs.rsp, uint64_t( layout::gdt_r3_data | layout::rpl3 ) };
		const uint64_t fva = guest_kernel_stack_va + page_size - sizeof( frame );
		std::string err;
		if ( !e_->write( fva, frame, sizeof( frame ), err ) )
		{
			why = err;
			return false;
		}
		return uc_reg_write( uc, UC_X86_REG_RSP, &fva ) == UC_ERR_OK && uc_reg_write( uc, UC_X86_REG_RIP, &iretq_stub_ ) == UC_ERR_OK;
	}
}
