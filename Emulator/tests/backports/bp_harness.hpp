// Plan step 1.6: test harness for the QEMU 11.1 -> Unicorn (QEMU 7.2.22) backports.
// TEMPORARY: per the user's decision these tests are deleted once every backport is proven; the
// proof (output + commit) is kept in CHANGES_LEDGER.md and git history.
//
// One self-generated thunk runs byte-identically in two engines at the same fixed addresses:
//   * the host CPU (hardware reference - only self-generated test code ever runs natively)
//   * Unicorn (our QEMU 7.2.22 branch)
// The thunk loads a full input state (GPRs, RFLAGS, FXSAVE image = x87 + MXCSR + XMM, YMM upper
// halves, a test stack and a scratch memory block), runs the snippet, and saves the full output
// state. Faults are recorded (native: vectored exception handler, Unicorn: UC_HOOK_INTR) and both
// engines resume at the same save-state epilogue, so faulting cases are compared too.
#pragma once
#include <unicorn/unicorn.h>
#include <keystone/keystone.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace bp
{
	// ── fixed layout (identical in both engines) ─────────────────────────────────────────────
	constexpr uint64_t CODE = 0x30000000, CODE_SIZE = 0x10000;
	constexpr uint64_t DATA = 0x30010000, DATA_SIZE = 0x10000;
	constexpr uint64_t IN_BLK = DATA + 0x0000, OUT_BLK = DATA + 0x1000, HOST = DATA + 0x2000;
	constexpr uint64_t STACK_LO = DATA + 0x3000, STACK_TOP = DATA + 0x3F00;
	constexpr uint64_t SCRATCH = DATA + 0x4000, SCRATCH_SIZE = 0x400;
	constexpr uint64_t EPI_STACK = DATA + 0x6000;

	// state block (IN_BLK and OUT_BLK use the same layout)
	constexpr uint64_t S_GPR = 0x000;     // 16 x u64, hardware register order (rax rcx rdx rbx rsp rbp rsi rdi r8..r15)
	constexpr uint64_t S_RFLAGS = 0x080;
	constexpr uint64_t S_FX = 0x100;      // FXSAVE64 image, 512 bytes, 16-aligned
	constexpr uint64_t S_YMMH = 0x300;    // 16 x 16 bytes: upper halves of ymm0..15
	constexpr uint64_t S_ENV = 0x400;     // FNSTENV image (28 bytes): full tag word etc.

	struct state
	{
		uint64_t gpr[ 16 ];
		uint64_t rflags;
		uint8_t fx[ 512 ];
		uint8_t ymmh[ 16 ][ 16 ];
		uint8_t env[ 28 ];
		uint8_t stack[ STACK_TOP - STACK_LO ];
		uint8_t scratch[ SCRATCH_SIZE ];

		uint16_t fcw() const { uint16_t v; std::memcpy( &v, fx + 0, 2 ); return v; }
		uint16_t fsw() const { uint16_t v; std::memcpy( &v, fx + 2, 2 ); return v; }
		uint8_t ftw_abridged() const { return fx[ 4 ]; }
		uint16_t ftw_full() const { uint16_t v; std::memcpy( &v, env + 8, 2 ); return v; }
		uint32_t mxcsr() const { uint32_t v; std::memcpy( &v, fx + 24, 4 ); return v; }
		const uint8_t* st( int i ) const { return fx + 32 + i * 16; }      // 10 meaningful bytes
		const uint8_t* xmm( int i ) const { return fx + 160 + i * 16; }
		void set_fcw( uint16_t v ) { std::memcpy( fx + 0, &v, 2 ); }
		void set_mxcsr( uint32_t v ) { std::memcpy( fx + 24, &v, 4 ); }
		void set_xmm( int i, const void* p ) { std::memcpy( fx + 160 + i * 16, p, 16 ); }
	};

	enum reg { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
	inline const char* reg_name( int i )
	{
		static const char* n[] = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15" };
		return n[ i ];
	}

	// what to compare
	enum cmp : unsigned
	{
		C_GPR = 1, C_FLAGS = 2, C_X87 = 4, C_MXCSR = 8, C_XMM = 16, C_YMMH = 32, C_MEM = 64, C_STACK = 128, C_FAULT = 256, C_FTW_FULL = 512,
		C_ALL = C_GPR | C_FLAGS | C_X87 | C_MXCSR | C_XMM | C_YMMH | C_MEM | C_STACK | C_FAULT
	};

	struct result
	{
		state s{};
		bool ran = false;
		bool faulted = false;
		int vector = -1;           // x86 vector (Unicorn exact; native mapped from the NTSTATUS)
		uint64_t fault_rip = 0;
		std::string err;
	};

	struct test_case
	{
		const char* commit;          // QEMU commit being backported
		const char* name;
		std::string asm_text;        // snippet, Intel syntax (Keystone); or
		std::vector<uint8_t> bytes;  // raw snippet bytes when Keystone can't encode it
		std::function<void( state& )> init;
		unsigned compare = C_ALL;
		uint64_t flag_mask = 0x8D5;  // CF PF AF ZF SF OF by default
		bool hardware = true;        // false: host CPU lacks it -> check Unicorn against expect()
		uc_cpu_x86 model = UC_CPU_X86_HASWELL;
		uc_mode mode = UC_MODE_64;   // UC_MODE_16/32 cases are SDM-checked only (Unicorn-only run)
		std::function<bool( const result& uc, std::string& why )> expect;   // SDM expectation
		bool expect_mismatch = false;   // negative control: passes only if the engines differ
		// checked separately on each engine's result (for values that legitimately differ between
		// engines, e.g. anything derived from the TEB address)
		std::function<bool( const result&, std::string& why )> check_each;
		std::function<void( uc_engine* )> uc_setup;                       // extra Unicorn setup (e.g. GS base)
		std::function<std::vector<uint8_t>( uint64_t addr )> gen_bytes;   // position-dependent snippet bytes
	};

	// ── assembling ──────────────────────────────────────────────────────────────────────────
	inline std::vector<uint8_t> assemble( const std::string& text, uint64_t addr, std::string* err = nullptr, ks_mode mode = KS_MODE_64 )
	{
		ks_engine* ks = nullptr;
		std::vector<uint8_t> out;
		if ( ks_open( KS_ARCH_X86, mode, &ks ) != KS_ERR_OK ) { if ( err ) *err = "ks_open"; return out; }
		unsigned char* enc = nullptr;
		size_t size = 0, count = 0;
		if ( ks_asm( ks, text.c_str(), addr, &enc, &size, &count ) != KS_ERR_OK )
		{
			if ( err ) *err = std::string( "keystone: " ) + ks_strerror( ks_errno( ks ) );
		}
		else
			out.assign( enc, enc + size );
		if ( enc ) ks_free( enc );
		ks_close( ks );
		return out;
	}

	inline std::string hx( uint64_t v ) { char b[ 24 ]; std::snprintf( b, sizeof( b ), "0x%llX", ( unsigned long long ) v ); return b; }

	// Memory operands go through a base register: Keystone encodes an absolute `[imm]` operand in
	// 64-bit mode as RIP-relative, so every state access is `[rax + offset-from-DATA]`.
	inline std::string at( uint64_t addr ) { return "[rax + " + hx( addr - DATA ) + "]"; }

	// prologue: save host ABI state, load the input state (rax is the base; loaded last)
	inline std::string prologue()
	{
		std::string s = "mov rax, " + hx( DATA ) + "\n";
		const char* hs[] = { "rbx", "rbp", "rdi", "rsi", "r12", "r13", "r14", "r15" };
		for ( int i = 0; i < 8; ++i ) s += "mov qword ptr " + at( HOST + i * 8 ) + ", " + hs[ i ] + "\n";
		s += "mov qword ptr " + at( HOST + 0x40 ) + ", rsp\n";
		s += "fnstcw word ptr " + at( HOST + 0x48 ) + "\n";
		s += "stmxcsr dword ptr " + at( HOST + 0x4C ) + "\n";
		for ( int i = 6; i < 16; ++i ) s += "movdqu xmmword ptr " + at( HOST + 0x80 + ( i - 6 ) * 16 ) + ", xmm" + std::to_string( i ) + "\n";
		s += "mov rsp, qword ptr " + at( IN_BLK + S_GPR + RSP * 8 ) + "\n";
		s += "fxrstor64 " + at( IN_BLK + S_FX ) + "\n";
		for ( int i = 0; i < 16; ++i )
			s += "vinserti128 ymm" + std::to_string( i ) + ", ymm" + std::to_string( i ) + ", xmmword ptr " + at( IN_BLK + S_YMMH + i * 16 ) + ", 1\n";
		s += "push qword ptr " + at( IN_BLK + S_RFLAGS ) + "\npopfq\n";
		for ( int i = 1; i < 16; ++i )
			if ( i != RSP ) s += std::string( "mov " ) + reg_name( i ) + ", qword ptr " + at( IN_BLK + S_GPR + i * 8 ) + "\n";
		s += "mov rax, qword ptr " + at( IN_BLK + S_GPR + RAX * 8 ) + "\n";
		return s;
	}

	// epilogue part 1 is raw bytes (see build): 48 A3 imm64 = mov [moffs64], rax - a true absolute store.
	// epilogue part 2: save the rest of the output state, restore host ABI state, return
	inline std::string epilogue()
	{
		std::string s = "mov rax, " + hx( DATA ) + "\n";
		for ( int i = 1; i < 16; ++i )
			s += "mov qword ptr " + at( OUT_BLK + S_GPR + i * 8 ) + ", " + reg_name( i ) + "\n";
		s += "mov rsp, " + hx( EPI_STACK ) + "\n";
		s += "pushfq\npop qword ptr " + at( OUT_BLK + S_RFLAGS ) + "\n";
		s += "push 0x202\npopfq\n";                                   // clear TF/AC etc. before touching anything else
		s += "fxsave64 " + at( OUT_BLK + S_FX ) + "\n";
		for ( int i = 0; i < 16; ++i )
			s += "vextracti128 xmmword ptr " + at( OUT_BLK + S_YMMH + i * 16 ) + ", ymm" + std::to_string( i ) + ", 1\n";
		s += "fnstenv " + at( OUT_BLK + S_ENV ) + "\n";
		s += "fninit\n";
		s += "fldcw word ptr " + at( HOST + 0x48 ) + "\n";
		s += "ldmxcsr dword ptr " + at( HOST + 0x4C ) + "\n";
		const char* hs[] = { "rbx", "rbp", "rdi", "rsi", "r12", "r13", "r14", "r15" };
		for ( int i = 0; i < 8; ++i ) s += std::string( "mov " ) + hs[ i ] + ", qword ptr " + at( HOST + i * 8 ) + "\n";
		for ( int i = 6; i < 16; ++i ) s += "movdqu xmm" + std::to_string( i ) + ", xmmword ptr " + at( HOST + 0x80 + ( i - 6 ) * 16 ) + "\n";
		s += "mov rsp, qword ptr " + at( HOST + 0x40 ) + "\n";
		s += "vzeroupper\nret\n";
		return s;
	}

	struct program
	{
		std::vector<uint8_t> code;
		uint64_t snippet_begin = 0, snippet_end = 0, epilogue_at = 0, ret_at = 0;
		std::string err;
	};

	inline program build( const test_case& tc )
	{
		program p;
		std::vector<uint8_t> pro = assemble( prologue(), CODE, &p.err );
		if ( pro.empty() ) return p;
		std::vector<uint8_t> snip = tc.gen_bytes ? tc.gen_bytes( CODE + pro.size() ) : tc.bytes;
		if ( snip.empty() && !tc.asm_text.empty() )
		{
			snip = assemble( tc.asm_text, CODE + pro.size(), &p.err );
			if ( snip.empty() ) { p.err = "snippet: " + p.err; return p; }
		}
		uint64_t epi_addr = CODE + pro.size() + snip.size();
		std::vector<uint8_t> epi = { 0x48, 0xA3 };                    // mov [moffs64], rax  (rax -> OUT gpr[0])
		for ( int i = 0; i < 8; ++i ) epi.push_back( uint8_t( ( OUT_BLK + S_GPR ) >> ( 8 * i ) ) );
		std::vector<uint8_t> rest = assemble( epilogue(), epi_addr + epi.size(), &p.err );
		if ( rest.empty() ) return p;
		epi.insert( epi.end(), rest.begin(), rest.end() );
		p.code = pro;
		p.code.insert( p.code.end(), snip.begin(), snip.end() );
		p.code.insert( p.code.end(), epi.begin(), epi.end() );
		p.snippet_begin = CODE + pro.size();
		p.snippet_end = epi_addr;
		p.epilogue_at = epi_addr;
		p.ret_at = CODE + p.code.size() - 1;
		return p;
	}

	inline void default_state( state& s )
	{
		std::memset( &s, 0, sizeof( s ) );
		for ( int i = 0; i < 16; ++i ) s.gpr[ i ] = 0x1111111111111111ull * uint64_t( i + 1 );
		s.gpr[ RSP ] = STACK_TOP;
		s.rflags = 0x202;
		s.set_fcw( 0x037F );
		uint16_t fsw = 0; std::memcpy( s.fx + 2, &fsw, 2 );
		s.fx[ 4 ] = 0;                     // abridged tag: all empty
		s.set_mxcsr( 0x1F80 );
		uint32_t mask = 0xFFFF; std::memcpy( s.fx + 28, &mask, 4 );
		for ( int i = 0; i < 16; ++i ) for ( int b = 0; b < 16; ++b ) s.fx[ 160 + i * 16 + b ] = uint8_t( 0x10 * i + b );
		for ( int i = 0; i < 16; ++i ) for ( int b = 0; b < 16; ++b ) s.ymmh[ i ][ b ] = uint8_t( 0x80 + 0x10 * i + b );
		for ( size_t i = 0; i < sizeof( s.scratch ); ++i ) s.scratch[ i ] = uint8_t( i * 37 + 11 );
	}

	inline void write_state_blocks( uint8_t* data, const state& s )
	{
		uint8_t* in = data + ( IN_BLK - DATA );
		std::memset( in, 0, 0x1000 );
		std::memcpy( in + S_GPR, s.gpr, sizeof( s.gpr ) );
		std::memcpy( in + S_RFLAGS, &s.rflags, 8 );
		std::memcpy( in + S_FX, s.fx, 512 );
		std::memcpy( in + S_YMMH, s.ymmh, sizeof( s.ymmh ) );
		std::memcpy( data + ( STACK_LO - DATA ), s.stack, sizeof( s.stack ) );
		std::memcpy( data + ( SCRATCH - DATA ), s.scratch, sizeof( s.scratch ) );
	}

	inline void read_state_blocks( const uint8_t* data, state& s )
	{
		const uint8_t* out = data + ( OUT_BLK - DATA );
		std::memcpy( s.gpr, out + S_GPR, sizeof( s.gpr ) );
		std::memcpy( &s.rflags, out + S_RFLAGS, 8 );
		std::memcpy( s.fx, out + S_FX, 512 );
		std::memcpy( s.ymmh, out + S_YMMH, sizeof( s.ymmh ) );
		std::memcpy( s.env, out + S_ENV, sizeof( s.env ) );
		std::memcpy( s.stack, data + ( STACK_LO - DATA ), sizeof( s.stack ) );
		std::memcpy( s.scratch, data + ( SCRATCH - DATA ), sizeof( s.scratch ) );
	}

	// ── native engine (self-generated code only) ─────────────────────────────────────────────
	namespace native_detail
	{
		inline uint64_t g_lo, g_hi, g_resume;
		inline DWORD g_code;
		inline uint64_t g_rip;
		inline bool g_faulted;
		inline LONG CALLBACK veh( EXCEPTION_POINTERS* ep )
		{
			uint64_t rip = ep->ContextRecord->Rip;
			if ( rip < g_lo || rip >= g_hi ) return EXCEPTION_CONTINUE_SEARCH;
			g_faulted = true;
			g_code = ep->ExceptionRecord->ExceptionCode;
			g_rip = rip;
			ep->ContextRecord->Rip = g_resume;
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		inline int vector_of( DWORD code )
		{
			switch ( code )
			{
				case STATUS_INTEGER_DIVIDE_BY_ZERO: case STATUS_INTEGER_OVERFLOW: return 0;
				case STATUS_SINGLE_STEP: return 1;
				case STATUS_BREAKPOINT: return 3;
				case STATUS_ILLEGAL_INSTRUCTION: return 6;
				case STATUS_PRIVILEGED_INSTRUCTION: return 13;
				case STATUS_ACCESS_VIOLATION: return 13;          // #GP and #PF both surface as AV
				case STATUS_DATATYPE_MISALIGNMENT: return 17;
				case STATUS_FLOAT_MULTIPLE_FAULTS: case STATUS_FLOAT_MULTIPLE_TRAPS: return 19;
				case STATUS_FLOAT_INVALID_OPERATION: case STATUS_FLOAT_DIVIDE_BY_ZERO: case STATUS_FLOAT_OVERFLOW:
				case STATUS_FLOAT_UNDERFLOW: case STATUS_FLOAT_INEXACT_RESULT: case STATUS_FLOAT_DENORMAL_OPERAND:
				case STATUS_FLOAT_STACK_CHECK: return 16;
				default: return -2;
			}
		}
	}

	inline result run_native( const program& p, const state& in )
	{
		using namespace native_detail;
		result r;
		uint8_t* code = ( uint8_t* ) VirtualAlloc( ( void* ) CODE, CODE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE );
		uint8_t* data = ( uint8_t* ) VirtualAlloc( ( void* ) DATA, DATA_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
		if ( code != ( void* ) CODE || data != ( void* ) DATA )
		{
			r.err = "VirtualAlloc at the fixed addresses failed";
			if ( code ) VirtualFree( code, 0, MEM_RELEASE );
			if ( data ) VirtualFree( data, 0, MEM_RELEASE );
			return r;
		}
		std::memset( code, 0xCC, CODE_SIZE );
		std::memcpy( code, p.code.data(), p.code.size() );
		FlushInstructionCache( GetCurrentProcess(), code, CODE_SIZE );
		std::memset( data, 0, DATA_SIZE );
		write_state_blocks( data, in );
		g_lo = p.snippet_begin; g_hi = p.snippet_end; g_resume = p.epilogue_at; g_faulted = false; g_code = 0; g_rip = 0;
		PVOID h = AddVectoredExceptionHandler( 1, veh );
		( ( void( * )() ) code )();
		RemoveVectoredExceptionHandler( h );
		r.ran = true;
		r.faulted = g_faulted;
		r.vector = g_faulted ? vector_of( g_code ) : -1;
		r.fault_rip = g_rip;
		read_state_blocks( data, r.s );
		VirtualFree( code, 0, MEM_RELEASE );
		VirtualFree( data, 0, MEM_RELEASE );
		return r;
	}

	// ── Unicorn engine ─────────────────────────────────────────────────────────────────────
	struct uc_ctx { const program* p; result* r; };

	inline void on_intr( uc_engine* uc, uint32_t intno, void* user )
	{
		auto* c = ( uc_ctx* ) user;
		uint64_t rip = 0;
		uc_reg_read( uc, UC_X86_REG_RIP, &rip );
		if ( !c->r->faulted )
		{
			c->r->faulted = true;
			c->r->vector = int( intno );
			c->r->fault_rip = rip;
		}
		if ( rip >= c->p->snippet_begin && rip < c->p->snippet_end )
		{
			uint64_t resume = c->p->epilogue_at;
			uc_reg_write( uc, UC_X86_REG_RIP, &resume );
		}
		else
			uc_emu_stop( uc );
	}

	// Real-mode (UC_MODE_16) runner for SDM-checked cases: the snippet runs at 0000:1000 with the GPRs
	// (low 32 bits) and RFLAGS from the state; the scratch block is mapped at linear 0x2000.
	constexpr uint64_t R16_CODE = 0x1000, R16_SCRATCH = 0x2000;
	inline result run_unicorn16( const std::vector<uint8_t>& code, const state& in, uc_cpu_x86 model )
	{
		result r;
		uc_engine* uc = nullptr;
		uc_err e = uc_open( UC_ARCH_X86, UC_MODE_16, &uc );
		if ( e ) { r.err = std::string( "uc_open(16): " ) + uc_strerror( e ); return r; }
		uc_ctl_set_cpu_model( uc, int( model ) );
		uc_mem_map( uc, 0, 0x100000, UC_PROT_ALL );
		uc_mem_write( uc, R16_CODE, code.data(), code.size() );
		uc_mem_write( uc, R16_SCRATCH, in.scratch, sizeof( in.scratch ) );
		const int ids[ 8 ] = { UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI };
		for ( int i = 0; i < 8; ++i ) { uint64_t v = in.gpr[ i ] & 0xFFFFFFFF; uc_reg_write( uc, ids[ i ], &v ); }
		uint64_t fl = in.rflags; uc_reg_write( uc, UC_X86_REG_EFLAGS, &fl );
		uc_ctx ctx{ nullptr, &r };
		uc_hook h;
		uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) +[]( uc_engine* u, uint32_t intno, void* user ) {
			auto* c = ( uc_ctx* ) user;
			if ( !c->r->faulted ) { c->r->faulted = true; c->r->vector = int( intno ); }
			uc_emu_stop( u );
		}, &ctx, 1, 0 );
		e = uc_emu_start( uc, R16_CODE, R16_CODE + code.size(), 0, 0 );
		if ( e ) r.err = std::string( "uc_emu_start(16): " ) + uc_strerror( e );
		for ( int i = 0; i < 8; ++i ) { uint64_t v = 0; uc_reg_read( uc, ids[ i ], &v ); r.s.gpr[ i ] = v & 0xFFFFFFFF; }
		uint64_t f = 0; uc_reg_read( uc, UC_X86_REG_EFLAGS, &f ); r.s.rflags = f;
		uc_mem_read( uc, R16_SCRATCH, r.s.scratch, sizeof( r.s.scratch ) );
		r.ran = !e;
		uc_close( uc );
		return r;
	}

	inline result run_unicorn( const program& p, const state& in, uc_cpu_x86 model, uc_mode mode = UC_MODE_64,
							   const std::function<void( uc_engine* )>& setup = {} )
	{
		result r;
		uc_engine* uc = nullptr;
		uc_err e = uc_open( UC_ARCH_X86, mode, &uc );
		if ( e ) { r.err = std::string( "uc_open: " ) + uc_strerror( e ); return r; }
		e = uc_ctl_set_cpu_model( uc, int( model ) );
		if ( e ) { r.err = std::string( "cpu model: " ) + uc_strerror( e ); uc_close( uc ); return r; }
		uc_mem_map( uc, CODE, CODE_SIZE, UC_PROT_ALL );
		uc_mem_map( uc, DATA, DATA_SIZE, UC_PROT_READ | UC_PROT_WRITE );
		std::vector<uint8_t> data( DATA_SIZE, 0 );
		write_state_blocks( data.data(), in );
		uc_mem_write( uc, CODE, p.code.data(), p.code.size() );
		uc_mem_write( uc, DATA, data.data(), data.size() );
		uint64_t rsp = HOST + 0x800;     // the thunk switches stacks itself; this only covers the final ret
		uc_reg_write( uc, UC_X86_REG_RSP, &rsp );
		if ( setup ) setup( uc );
		uc_ctx ctx{ &p, &r };
		uc_hook h, hm;
		uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &ctx, 1, 0 );
		static thread_local std::string unmapped;
		unmapped.clear();
		auto on_unmapped = +[]( uc_engine* u, uc_mem_type type, uint64_t addr, int size, int64_t, void* ) -> bool {
			uint64_t rip = 0;
			uc_reg_read( u, UC_X86_REG_RIP, &rip );
			unmapped = "unmapped " + std::string( type == UC_MEM_FETCH_UNMAPPED ? "fetch" : type == UC_MEM_WRITE_UNMAPPED ? "write" : "read" ) +
				" of " + std::to_string( size ) + " bytes at " + hx( addr ) + " (rip " + hx( rip ) + ")";
			return false;
		};
		uc_hook_add( uc, &hm, UC_HOOK_MEM_UNMAPPED, ( void* ) on_unmapped, nullptr, 1, 0 );
		// opcodes QEMU rejects arrive here instead of UC_HOOK_INTR: record #UD and resume at the epilogue
		auto on_invalid = +[]( uc_engine* u, void* user ) -> bool {
			auto* c = ( uc_ctx* ) user;
			uint64_t rip = 0;
			uc_reg_read( u, UC_X86_REG_RIP, &rip );
			if ( !c->r->faulted ) { c->r->faulted = true; c->r->vector = 6; c->r->fault_rip = rip; }
			if ( rip < c->p->snippet_begin || rip >= c->p->snippet_end ) return false;
			uint64_t resume = c->p->epilogue_at;
			uc_reg_write( u, UC_X86_REG_RIP, &resume );
			return true;
		};
		uc_hook hi;
		uc_hook_add( uc, &hi, UC_HOOK_INSN_INVALID, ( void* ) on_invalid, &ctx, 1, 0 );
		e = uc_emu_start( uc, CODE, p.ret_at, 0, 0 );
		if ( e ) r.err = std::string( "uc_emu_start: " ) + uc_strerror( e ) + ( unmapped.empty() ? "" : " - " + unmapped );
		uc_mem_read( uc, DATA, data.data(), data.size() );
		read_state_blocks( data.data(), r.s );
		r.ran = !e;
		uc_close( uc );
		return r;
	}

	// ── comparison / reporting ──────────────────────────────────────────────────────────────
	inline void dump_bytes( const char* tag, const uint8_t* p, size_t n )
	{
		std::printf( "%s", tag );
		for ( size_t i = n; i-- > 0; ) std::printf( "%02x", p[ i ] );
		std::printf( "\n" );
	}

	inline bool compare( const test_case& tc, const result& hw, const result& uc, std::vector<std::string>& diffs )
	{
		auto add = [ & ]( const std::string& s ) { diffs.push_back( s ); };
		unsigned c = tc.compare;
		if ( c & C_FAULT )
		{
			if ( hw.faulted != uc.faulted ) add( std::string( "fault: hw " ) + ( hw.faulted ? "vector " + std::to_string( hw.vector ) : "none" ) +
											 ", uc " + ( uc.faulted ? "vector " + std::to_string( uc.vector ) : "none" ) );
			else if ( hw.faulted )
			{
				bool same = hw.vector == uc.vector || ( hw.vector == 13 && uc.vector == 14 );
				if ( !same ) add( "fault vector: hw " + std::to_string( hw.vector ) + ", uc " + std::to_string( uc.vector ) );
				if ( hw.fault_rip != uc.fault_rip ) add( "fault rip: hw " + hx( hw.fault_rip ) + ", uc " + hx( uc.fault_rip ) );
			}
		}
		if ( c & C_GPR )
			for ( int i = 0; i < 16; ++i )
				if ( hw.s.gpr[ i ] != uc.s.gpr[ i ] ) add( std::string( reg_name( i ) ) + ": hw " + hx( hw.s.gpr[ i ] ) + ", uc " + hx( uc.s.gpr[ i ] ) );
		if ( ( c & C_FLAGS ) && ( ( hw.s.rflags ^ uc.s.rflags ) & tc.flag_mask ) )
			add( "rflags (mask " + hx( tc.flag_mask ) + "): hw " + hx( hw.s.rflags ) + ", uc " + hx( uc.s.rflags ) );
		if ( c & C_X87 )
		{
			if ( hw.s.fcw() != uc.s.fcw() ) add( "fcw: hw " + hx( hw.s.fcw() ) + ", uc " + hx( uc.s.fcw() ) );
			if ( hw.s.fsw() != uc.s.fsw() ) add( "fsw: hw " + hx( hw.s.fsw() ) + ", uc " + hx( uc.s.fsw() ) );
			if ( hw.s.ftw_abridged() != uc.s.ftw_abridged() ) add( "ftw(abridged): hw " + hx( hw.s.ftw_abridged() ) + ", uc " + hx( uc.s.ftw_abridged() ) );
			for ( int i = 0; i < 8; ++i )
				if ( std::memcmp( hw.s.st( i ), uc.s.st( i ), 10 ) ) add( "st(" + std::to_string( i ) + ") differs" );
		}
		if ( ( c & C_FTW_FULL ) && hw.s.ftw_full() != uc.s.ftw_full() )
			add( "ftw(full): hw " + hx( hw.s.ftw_full() ) + ", uc " + hx( uc.s.ftw_full() ) );
		if ( ( c & C_MXCSR ) && hw.s.mxcsr() != uc.s.mxcsr() ) add( "mxcsr: hw " + hx( hw.s.mxcsr() ) + ", uc " + hx( uc.s.mxcsr() ) );
		if ( c & C_XMM )
			for ( int i = 0; i < 16; ++i )
				if ( std::memcmp( hw.s.xmm( i ), uc.s.xmm( i ), 16 ) ) add( "xmm" + std::to_string( i ) + " differs" );
		if ( c & C_YMMH )
			for ( int i = 0; i < 16; ++i )
				if ( std::memcmp( hw.s.ymmh[ i ], uc.s.ymmh[ i ], 16 ) ) add( "ymm" + std::to_string( i ) + "[255:128] differs" );
		if ( ( c & C_MEM ) && std::memcmp( hw.s.scratch, uc.s.scratch, sizeof( hw.s.scratch ) ) ) add( "scratch memory differs" );
		if ( c & C_STACK )
		{
			// Only [final rsp, STACK_TOP) is architecturally defined: below rsp the OS builds its
			// exception-dispatch frame when the native run faults.
			uint64_t rsp = uc.s.gpr[ RSP ];
			size_t from = ( rsp >= STACK_LO && rsp <= STACK_TOP ) ? size_t( rsp - STACK_LO ) : 0;
			for ( size_t i = from; i < sizeof( hw.s.stack ); ++i )
				if ( hw.s.stack[ i ] != uc.s.stack[ i ] ) { add( "test stack differs at " + hx( STACK_LO + i ) ); break; }
		}
		return diffs.empty();
	}

	struct summary { int pass = 0, fail = 0, error = 0; std::vector<std::string> failed; };

	inline void run_case( const test_case& tc, summary& sum )
	{
		std::printf( "  [%s] %s\n", tc.commit, tc.name );
		state in{};
		default_state( in );
		if ( tc.init ) tc.init( in );
		if ( tc.mode == UC_MODE_16 )
		{
			std::vector<uint8_t> code = tc.bytes;
			std::string err;
			if ( code.empty() ) code = assemble( tc.asm_text, R16_CODE, &err, KS_MODE_16 );
			result uc16 = run_unicorn16( code, in, tc.model );
			std::string why;
			bool ok16 = uc16.ran && tc.expect && tc.expect( uc16, why );
			std::printf( "    SDM-checked, Unicorn real mode: %s%s\n", uc16.faulted ? ( "fault vector " + std::to_string( uc16.vector ) ).c_str() : "completed",
						 uc16.err.empty() ? "" : ( " (" + uc16.err + ")" ).c_str() );
			if ( !ok16 ) std::printf( "    DIFF %s\n", why.empty() ? "expectation not met" : why.c_str() );
			std::printf( "    -> %s\n", ok16 ? "PASS" : "FAIL" );
			if ( ok16 ) ++sum.pass; else { ++sum.fail; sum.failed.push_back( tc.name ); }
			return;
		}
		program p = build( tc );
		if ( p.code.empty() ) { std::printf( "    ERROR building thunk: %s\n", p.err.c_str() ); ++sum.error; sum.failed.push_back( tc.name ); return; }
		result uc = run_unicorn( p, in, tc.model, tc.mode, tc.uc_setup );
		if ( !uc.err.empty() && !uc.ran ) { std::printf( "    ERROR unicorn: %s\n", uc.err.c_str() ); ++sum.error; sum.failed.push_back( tc.name ); return; }
		std::vector<std::string> diffs;
		bool ok;
		if ( tc.hardware )
		{
			result hw = run_native( p, in );
			if ( !hw.ran ) { std::printf( "    ERROR native: %s\n", hw.err.c_str() ); ++sum.error; sum.failed.push_back( tc.name ); return; }
			std::printf( "    hw: %s   uc: %s\n",
						 hw.faulted ? ( "fault vector " + std::to_string( hw.vector ) ).c_str() : "completed",
						 uc.faulted ? ( "fault vector " + std::to_string( uc.vector ) ).c_str() : "completed" );
			ok = compare( tc, hw, uc, diffs );
			if ( tc.check_each )
			{
				std::string why;
				if ( !tc.check_each( hw, why ) ) { ok = false; diffs.push_back( "hw: " + why ); }
				why.clear();
				if ( !tc.check_each( uc, why ) ) { ok = false; diffs.push_back( "uc: " + why ); }
			}
			if ( tc.expect_mismatch )
			{
				std::printf( "    negative control: %zu difference(s) detected (expected > 0)\n", diffs.size() );
				ok = !ok;
				diffs.clear();
				if ( !ok ) diffs.push_back( "negative control saw no difference - the comparison is not working" );
			}
		}
		else
		{
			std::string why;
			ok = tc.expect && tc.expect( uc, why );
			std::printf( "    SDM-checked (host CPU lacks it / Unicorn-only mode): uc %s\n", uc.faulted ? ( "fault vector " + std::to_string( uc.vector ) ).c_str() : "completed" );
			if ( !ok ) diffs.push_back( why.empty() ? "expectation not met" : why );
		}
		for ( auto& d : diffs ) std::printf( "    DIFF %s\n", d.c_str() );
		std::printf( "    -> %s\n", ok ? "PASS" : "FAIL" );
		if ( ok ) ++sum.pass; else { ++sum.fail; sum.failed.push_back( tc.name ); }
	}
}
