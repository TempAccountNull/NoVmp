// emu-alltest --bench: a reproducible Unicorn performance benchmark (ledger U500).
//
//   emu-alltest --bench [--reps N] [--filter S] [--scale F] [--bench-cpu C] [--csv FILE]
//
// Every workload is our own self-generated code (Keystone), run in Unicorn only (UC_CPU_X86_MAX,
// the same model emu-alltest uses); nothing runs natively. Each repetition opens a fresh engine,
// so the "cold" run includes translation; a second "warm" run on the same engine (state reset,
// translation cache kept) isolates execution. Both are timed around uc_emu_start only.
// The guest-visible end state (GPRs, RFLAGS, RIP, YMM0-15, x87 ST0-7/FPSW/FPCW/FPTAG, MXCSR and
// every data region) is hashed (FNV-1a 64): the "state" column must be identical across builds,
// i.e. a performance change may never alter a result. Instruction counts are calibrated with a
// UC_HOOK_CODE counter at two small iteration counts (each workload is linear in N).
// Report: median of --reps (default 5) wall times, Minsn/s = instructions / median.
// --profile N: sample the benchmark thread (~1 kHz) and print the N hottest functions per workload.
#pragma once
#include "at_engine.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include <dbghelp.h>
#pragma comment( lib, "dbghelp.lib" )
#include <timeapi.h>
#pragma comment( lib, "winmm.lib" )

namespace at
{
	namespace bench
	{
		constexpr uint64_t B_CODE = 0x10000000, B_CODE_SIZE = 0x10000;   // loop code
		constexpr uint64_t B_FN = 0x10100000, B_FN_SIZE = 0x1000;        // self-modified function
		constexpr uint64_t B_BLK = 0x10200000, B_BLK_SIZE = 0x10000;     // 4096 tiny call targets
		constexpr uint64_t B_DATA = 0x20000000, B_DATA_SIZE = 0x100000;  // arrays, tables, stack
		constexpr uint64_t B_BIG = 0x40000000, B_BIG_SIZE = 0x1000000;   // 16 MiB random-access region (4096 pages, fits the host L3)
		constexpr uint64_t B_STACK_TOP = B_DATA + B_DATA_SIZE - 0x100;
		constexpr uint64_t B_A = B_DATA + 0x00000, B_B = B_DATA + 0x10000, B_OUT = B_DATA + 0x20000;
		constexpr uint64_t B_TAB = B_DATA + 0x30000, B_SRC = B_DATA + 0x40000, B_DST = B_DATA + 0x60000;

		enum hook_mode { H_NONE, H_CODE_ALL, H_MEM_RW, H_COUNT_LIMIT };

		struct workload
		{
			const char* name;
			const char* what;
			std::string text;           // loop; rcx = N on entry; ends at label "done"
			uint64_t n;                 // default iteration count
			bool big = false;           // map the 16 MiB region
			hook_mode hooks = H_NONE;
			int starts = 0;             // >0: many short uc_emu_start calls (N = number of starts)
			int opens = 0;              // >0: uc_open/uc_close churn (N = number of engines)
		};

		inline uint64_t fnv( uint64_t h, const void* p, size_t n )
		{
			const uint8_t* b = ( const uint8_t* ) p;
			for ( size_t i = 0; i < n; ++i ) { h ^= b[ i ]; h *= 0x100000001B3ull; }
			return h;
		}

		// deterministic data shared by every workload
		inline void fill_data( std::vector<uint8_t>& d )
		{
			d.assign( B_DATA_SIZE, 0 );
			uint64_t s = 0x243F6A8885A308D3ull;
			auto nx = [ & ]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
			float* a = ( float* ) &d[ B_A - B_DATA ];
			float* b = ( float* ) &d[ B_B - B_DATA ];
			for ( int i = 0; i < 4096; ++i )
			{
				a[ i ] = 0.5f + float( nx() % 1000 ) / 1000.0f;   // [0.5, 1.5)
				b[ i ] = 0.5f + float( nx() % 1000 ) / 1000.0f;
			}
			double* ad = ( double* ) &d[ B_A - B_DATA + 0x8000 ];
			for ( int i = 0; i < 512; ++i ) ad[ i ] = 0.25 + double( nx() % 4096 ) / 8192.0;
			uint64_t* tab = ( uint64_t* ) &d[ B_TAB - B_DATA ];
			for ( int i = 0; i < 4096; ++i ) tab[ i ] = B_BLK + uint64_t( i ) * 16;
			for ( size_t i = 0; i < 0x20000; ++i ) d[ B_SRC - B_DATA + i ] = uint8_t( nx() );
		}

		inline std::vector<workload> workloads()
		{
			std::vector<workload> w;
			// (a) integer / branch heavy: LCG mix, both branch arms the same length
			const char* int_loop =
				"mov rax, 0x123456789\nxor rbx, rbx\nmov rdx, 0x5851F42D4C957F2D\nxor r8, r8\n"
				"L1: imul rax, rdx\nadd rax, rcx\nmov r9, rax\nshr r9, 29\nxor rax, r9\ntest al, 1\njz L2\n"
				"add rbx, rax\nror rbx, 3\njmp L3\n"
				"L2: sub rbx, r9\nrol rbx, 7\nnop\n"
				"L3: cmp r9d, 0x40000000\nadc r8, 0\nmov r10, rax\nor r10, 1\nbsf r11, r10\nadd r8, r11\n"
				"popcnt r11, rax\nadd r8, r11\nlea r12, [r8 + rbx*2 + 7]\nxor r12, rax\ncmovs r8, r12\n"
				"setnz r13b\nmovzx r13d, r13b\nadd r8, r13\ndec rcx\njnz L1\ndone: nop\n";
			w.push_back( { "int", "integer/branch LCG loop (imul, shifts, adc, bsf, popcnt, cmov, setcc)", int_loop, 40000000 } );
			// (b) SSE/AVX FP kernel over two 16 KiB float arrays
			w.push_back( { "avx_fp", "AVX/FMA/SSE FP over 2x4096 floats (vmulps/vaddps/vfmadd/vsqrtps/divps/mulsd)",
				"O1: mov rsi, 0x20000000\nmov rdi, 0x20010000\nmov edx, 512\nvxorps ymm2, ymm2, ymm2\nvxorps ymm3, ymm3, ymm3\n"
				"I1: vmovups ymm0, ymmword ptr [rsi]\nvmulps ymm1, ymm0, ymmword ptr [rdi]\nvaddps ymm2, ymm2, ymm1\n"
				"vfmadd231ps ymm3, ymm0, ymm1\nvsqrtps ymm4, ymm0\nmovups xmm5, xmmword ptr [rsi]\ndivps xmm5, xmm4\n"
				"addps xmm6, xmm5\nmulsd xmm7, xmm7\naddsd xmm7, xmm5\nadd rsi, 32\nadd rdi, 32\ndec edx\njnz I1\n"
				"vmovups ymmword ptr [0x20020000], ymm2\nvmovups ymmword ptr [0x20020020], ymm3\ndec rcx\njnz O1\ndone: nop\n",
				400 } );
			// (b2) SSE integer / shuffle kernel
			w.push_back( { "sse_int", "SSE2/SSSE3/SSE4 integer SIMD (pcmpeqb, pmovmskb, pshufb, paddd, pmaddwd, pminub, ptest)",
				"pcmpeqb xmm15, xmm15\nO2: mov rsi, 0x20040000\nmov edx, 1024\n"
				"I2: movdqu xmm0, xmmword ptr [rsi]\nmovdqa xmm1, xmm0\npcmpeqb xmm1, xmm14\npmovmskb eax, xmm1\nadd ebx, eax\n"
				"pshufb xmm0, xmm13\npaddd xmm12, xmm0\npmaddwd xmm0, xmm11\npaddd xmm10, xmm0\npminub xmm9, xmm0\n"
				"pxor xmm13, xmm0\nptest xmm0, xmm15\nadc ebx, 0\nadd rsi, 16\ndec edx\njnz I2\ndec rcx\njnz O2\ndone: nop\n",
				10000 } );
			// (b3) x87 kernel: sum of squares, sqrt, one FPATAN per outer iteration
			w.push_back( { "x87", "x87 fld/fmul/faddp over 512 doubles + fsqrt + fpatan",
				"O3: mov rsi, 0x20008000\nmov edx, 512\nfldz\n"
				"I3: fld qword ptr [rsi]\nfmul st(0), st(0)\nfaddp st(1), st(0)\nadd rsi, 8\ndec edx\njnz I3\n"
				"fsqrt\nfld1\nfpatan\nfstp qword ptr [0x20020000]\ndec rcx\njnz O3\ndone: nop\n",
				2000 } );
			// (c) string ops: REP MOVSB 64 KiB + REP STOSQ 8 KiB + REPE CMPSB
			w.push_back( { "rep_str", "REP MOVSB 128 KiB, REPE CMPSB 128 KiB (equal), REP STOSQ 32 KiB per iteration",
				"O4: mov r8, rcx\nmov rsi, 0x20040000\nmov rdi, 0x20060000\nmov ecx, 0x20000\ncld\nrep movsb\n"
				"mov rsi, 0x20040000\nmov rdi, 0x20060000\nmov ecx, 0x20000\nrepe cmpsb\n"
				"mov rdi, 0x20060000\nmov ecx, 0x1000\nmov rax, r8\nrep stosq\n"
				"mov rcx, r8\ndec rcx\njnz O4\ndone: nop\n",
				200 } );
			// (c2) TLB heavy: random 8-byte loads and stores over 16 MiB (4096 pages)
			const char* rnd_loop =
				"mov rax, 0x9E3779B97F4A7C15\nmov rdx, 0x5851F42D4C957F2D\nxor rbx, rbx\nmov r11, 0x40000000\n"
				"L5: imul rax, rdx\ninc rax\nmov r9, rax\nshr r9, 38\nand r9, 0xFFFFF8\nmov r10, qword ptr [r11 + r9]\n"
				"add rbx, r10\nmov qword ptr [r11 + r9], rax\ndec rcx\njnz L5\ndone: nop\n";
			w.push_back( { "tlb_rand", "random 8-byte load+store over 16 MiB = 4096 pages (TLB miss heavy)", rnd_loop, 2000000, true } );
			// (d) self-modifying code: patch an imm32 in a function on another page, then call it
			w.push_back( { "smc", "patch imm32 of a function on another code page, call it (1 retranslation per iteration)",
				"mov r12, 0x10100000\nxor rbx, rbx\n"
				"L6: mov dword ptr [r12 + 1], ecx\ncall r12\nadd rbx, rax\ndec rcx\njnz L6\ndone: nop\n",
				100000 } );
			// (d2) many short TBs: indirect calls into 4096 tiny blocks (add rax, imm32; ret)
			w.push_back( { "short_tb", "indirect call into 4096 tiny blocks + ret (TB lookup / jump cache / chaining)",
				"mov rax, 1\nmov rdx, 0x5851F42D4C957F2D\nmov r11, 0x20030000\nmov r8, 0x9E3779B97F4A7C15\n"
				"L7: imul r8, rdx\ninc r8\nmov r9, r8\nshr r9, 52\ncall qword ptr [r11 + r9*8]\ndec rcx\njnz L7\ndone: nop\n",
				2000000 } );
			// (e) Unicorn hook overhead on the integer loop
			{
				workload h = w[ 0 ]; h.name = "int+codehook"; h.what = "int loop with UC_HOOK_CODE on every address (empty callback)"; h.n = 2000000; h.hooks = H_CODE_ALL;
				w.push_back( h );
				workload c = w[ 0 ]; c.name = "int+count"; c.what = "int loop via uc_emu_start(count = 2^40) (Unicorn's internal count hook)"; c.n = 2000000; c.hooks = H_COUNT_LIMIT;
				w.push_back( c );
				workload m = w[ 5 ]; m.name = "tlb+memhook"; m.what = "tlb_rand with UC_HOOK_MEM_READ|WRITE (empty callback, slow path)"; m.n = 1000000; m.hooks = H_MEM_RW;
				w.push_back( m );
			}
			// (f) many short uc_emu_start calls / engine churn
			w.push_back( { "emu_start", "50k x uc_emu_start on a 6-instruction snippet (same engine)",
				"mov rcx, 3\nL8: add rax, rcx\nxor rbx, rax\ndec rcx\njnz L8\ndone: nop\n", 50000, false, H_NONE, 1 } );
			w.push_back( { "uc_open", "uc_open + map 2 regions + 6-instruction run + uc_close (emu-alltest pattern)", "mov rcx, 3\nL9: add rax, rcx\nxor rbx, rax\ndec rcx\njnz L9\ndone: nop\n",
				200, false, H_NONE, 0, 1 } );
			return w;
		}

		struct compiled
		{
			std::vector<uint8_t> code;
			uint64_t done = 0;
		};

		inline bool compile( const workload& w, compiled& c, std::string& err )
		{
			// "done" is the last instruction (a 1-byte NOP): run until its address
			c.code = assemble( w.text, B_CODE, &err );
			if ( c.code.empty() ) return false;
			c.done = B_CODE + c.code.size() - 1;
			if ( c.code.back() != 0x90 ) { err = "workload must end with 'done: nop'"; return false; }
			return true;
		}

		struct engine
		{
			uc_engine* uc = nullptr;
			uint64_t count = 0;
			static void on_code( uc_engine*, uint64_t, uint32_t, void* u ) { ++( ( engine* ) u )->count; }
			static void on_mem( uc_engine*, uc_mem_type, uint64_t, int, int64_t, void* u ) { ++( ( engine* ) u )->count; }
			~engine() { if ( uc ) uc_close( uc ); }
			bool open( const workload& w, const compiled& c, std::string& err )
			{
				uc_err e = uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
				if ( e ) { err = std::string( "uc_open: " ) + uc_strerror( e ); return false; }
				uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
				uc_mem_map( uc, B_CODE, B_CODE_SIZE, UC_PROT_ALL );
				uc_mem_map( uc, B_FN, B_FN_SIZE, UC_PROT_ALL );
				uc_mem_map( uc, B_BLK, B_BLK_SIZE, UC_PROT_ALL );
				uc_mem_map( uc, B_DATA, B_DATA_SIZE, UC_PROT_READ | UC_PROT_WRITE );
				if ( w.big ) uc_mem_map( uc, B_BIG, B_BIG_SIZE, UC_PROT_READ | UC_PROT_WRITE );
				uc_mem_write( uc, B_CODE, c.code.data(), c.code.size() );
				uc_hook h;
				if ( w.hooks == H_CODE_ALL ) uc_hook_add( uc, &h, UC_HOOK_CODE, ( void* ) on_code, this, 1, 0 );
				if ( w.hooks == H_MEM_RW ) uc_hook_add( uc, &h, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, ( void* ) on_mem, this, 1, 0 );
				return true;
			}
			// reset the guest state (registers, data, function and block pages)
			void reset( const workload& w, uint64_t n, const std::vector<uint8_t>& data )
			{
				uc_mem_write( uc, B_DATA, data.data(), data.size() );
				if ( w.big )
				{
					static std::vector<uint8_t> zero( B_BIG_SIZE, 0 );
					uc_mem_write( uc, B_BIG, zero.data(), zero.size() );
				}
				const uint8_t fn[] = { 0xB8, 0, 0, 0, 0, 0xC3 };   // mov eax, imm32; ret
				uc_mem_write( uc, B_FN, fn, sizeof( fn ) );
				static std::vector<uint8_t> blk;
				if ( blk.empty() )
				{
					blk.assign( B_BLK_SIZE, 0xCC );
					for ( uint32_t i = 0; i < 4096; ++i )
					{
						uint8_t* p = &blk[ i * 16 ];
						uint32_t imm = i * 2654435761u;
						p[ 0 ] = 0x48; p[ 1 ] = 0x05; std::memcpy( p + 2, &imm, 4 ); p[ 6 ] = 0xC3;   // add rax, imm32; ret
					}
				}
				uc_mem_write( uc, B_BLK, blk.data(), blk.size() );
				static const int gprs[] = { UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RDX, UC_X86_REG_RSI, UC_X86_REG_RDI, UC_X86_REG_RBP,
											UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11, UC_X86_REG_R12, UC_X86_REG_R13,
											UC_X86_REG_R14, UC_X86_REG_R15 };
				uint64_t z = 0;
				for ( int r : gprs ) uc_reg_write( uc, r, &z );
				uint64_t fl = 0x202; uc_reg_write( uc, UC_X86_REG_RFLAGS, &fl );
				uint8_t y[ 32 ] = {};
				for ( int i = 0; i < 16; ++i ) { std::memset( y, 0x11 * i, sizeof( y ) ); uc_reg_write( uc, UC_X86_REG_YMM0 + i, y ); }
				uint64_t sp = B_STACK_TOP; uc_reg_write( uc, UC_X86_REG_RSP, &sp );
				uc_reg_write( uc, UC_X86_REG_RCX, &n );
				count = 0;
			}
			uc_err run( const workload& w, const compiled& c )
			{
				return uc_emu_start( uc, B_CODE, c.done, 0, w.hooks == H_COUNT_LIMIT ? ( 1ull << 40 ) : 0 );
			}
			uint64_t state_hash( const workload& w )
			{
				uint64_t h = 0xCBF29CE484222325ull;
				static const int regs[] = { UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RSI, UC_X86_REG_RDI,
											UC_X86_REG_RBP, UC_X86_REG_RSP, UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11,
											UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15, UC_X86_REG_RIP, UC_X86_REG_RFLAGS };
				for ( int r : regs ) { uint64_t v = 0; uc_reg_read( uc, r, &v ); h = fnv( h, &v, 8 ); }
				for ( int i = 0; i < 16; ++i ) { uint8_t y[ 32 ] = {}; uc_reg_read( uc, UC_X86_REG_YMM0 + i, y ); h = fnv( h, y, 32 ); }
				for ( int i = 0; i < 8; ++i ) { uint8_t st[ 10 ] = {}; uc_reg_read( uc, UC_X86_REG_ST0 + i, st ); h = fnv( h, st, 10 ); }
				for ( int r : { UC_X86_REG_FPSW, UC_X86_REG_FPCW, UC_X86_REG_FPTAG, UC_X86_REG_MXCSR } ) { uint64_t v = 0; uc_reg_read( uc, r, &v ); h = fnv( h, &v, 4 ); }
				std::vector<uint8_t> m( B_DATA_SIZE );
				uc_mem_read( uc, B_DATA, m.data(), m.size() ); h = fnv( h, m.data(), m.size() );
				m.resize( B_FN_SIZE ); uc_mem_read( uc, B_FN, m.data(), m.size() ); h = fnv( h, m.data(), m.size() );
				if ( w.big ) { m.resize( B_BIG_SIZE ); uc_mem_read( uc, B_BIG, m.data(), m.size() ); h = fnv( h, m.data(), m.size() ); }
				return h;
			}
		};

		using clk = std::chrono::steady_clock;
		inline double secs( clk::time_point a, clk::time_point b ) { return std::chrono::duration<double>( b - a ).count(); }

		struct sample { double cold = 0, warm = 0; uint64_t hash = 0; bool ok = true; std::string err; };

		// one repetition: fresh engine, cold run, reset, warm run
		inline sample run_once( const workload& w, const compiled& c, uint64_t n, const std::vector<uint8_t>& data )
		{
			sample s;
			if ( w.opens )
			{
				auto t0 = clk::now();
				uint64_t h = 0;
				// like emu-alltest's per-form engine: open, model, map, write a few bytes, run, close
				for ( uint64_t i = 0; i < n; ++i )
				{
					uc_engine* uc = nullptr;
					if ( uc_open( UC_ARCH_X86, UC_MODE_64, &uc ) ) { s.ok = false; s.err = "uc_open failed"; return s; }
					uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
					uc_mem_map( uc, B_CODE, B_CODE_SIZE, UC_PROT_ALL );
					uc_mem_map( uc, B_DATA, 0x20000, UC_PROT_READ | UC_PROT_WRITE );
					uc_mem_write( uc, B_CODE, c.code.data(), c.code.size() );
					uint64_t z = 0, sp = B_DATA + 0x10000;
					for ( int r : { UC_X86_REG_RAX, UC_X86_REG_RBX } ) uc_reg_write( uc, r, &z );
					uc_reg_write( uc, UC_X86_REG_RSP, &sp );
					uc_err e = uc_emu_start( uc, B_CODE, c.done, 0, 0 );
					if ( i == n - 1 )
					{
						h = 0xCBF29CE484222325ull;
						for ( int r : { UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RCX, UC_X86_REG_RIP, UC_X86_REG_RFLAGS } ) { uint64_t v = 0; uc_reg_read( uc, r, &v ); h = fnv( h, &v, 8 ); }
					}
					uc_close( uc );
					if ( e ) { s.ok = false; s.err = "uc_emu_start failed"; return s; }
				}
				s.cold = s.warm = secs( t0, clk::now() );
				s.hash = h;
				return s;
			}
			engine e;
			if ( !e.open( w, c, s.err ) ) { s.ok = false; return s; }
			for ( int pass = 0; pass < 2; ++pass )
			{
				e.reset( w, w.starts ? 3 : n, data );
				auto t0 = clk::now();
				uc_err r = UC_ERR_OK;
				if ( w.starts )
					for ( uint64_t i = 0; i < n && !r; ++i ) { uint64_t three = 3; uc_reg_write( e.uc, UC_X86_REG_RCX, &three ); r = e.run( w, c ); }
				else r = e.run( w, c );
				double t = secs( t0, clk::now() );
				if ( r ) { s.ok = false; s.err = std::string( "uc_emu_start: " ) + uc_strerror( r ); return s; }
				uint64_t h = e.state_hash( w );
				if ( pass == 0 ) { s.cold = t; s.hash = h; }
				else
				{
					s.warm = t;
					if ( h != s.hash ) { s.ok = false; s.err = "warm run state differs from cold run"; }
				}
			}
			return s;
		}

		// instructions executed for N iterations: UC_HOOK_CODE count at two small N, linear fit
		inline uint64_t insn_count( const workload& w, const compiled& c, uint64_t n, const std::vector<uint8_t>& data )
		{
			workload k = w; k.hooks = H_CODE_ALL;
			auto cnt = [ & ]( uint64_t m ) -> uint64_t {
				std::string err; engine e;
				if ( !e.open( k, c, err ) ) return 0;
				e.reset( k, w.starts || w.opens ? 3 : m, data );
				e.run( k, c );
				return e.count;
			};
			if ( w.starts || w.opens ) return cnt( 0 ) * n;
			uint64_t n1 = std::max<uint64_t>( 2, n / 1000 ), n2 = 2 * n1;
			uint64_t c1 = cnt( n1 ), c2 = cnt( n2 );
			uint64_t per = ( c2 - c1 ) / ( n2 - n1 );
			uint64_t base = c1 - per * n1;
			return base + per * n;
		}

		// --profile: a tiny in-process sampling profiler (suspend the benchmark thread every ~1 ms,
		// record RIP and the unwound callers, symbolize with dbghelp from the PDBs next to the executable). RIPs outside any
		// module are TCG-generated code ("[jit]").
		struct sampler
		{
			HANDLE target = nullptr, thread = nullptr;
			volatile bool stop_ = false;
			// fixed storage: nothing may allocate while the target thread is suspended (it may hold
			// the heap lock); unwinding uses RtlLookupFunctionEntry/RtlVirtualUnwind (no allocation)
			struct rec { uint64_t pc[ 16 ]; int n; };
			std::vector<rec> recs;
			volatile size_t nrec = 0;
			static void sym_init()
			{
				static bool done = false;
				if ( !done ) { SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS ); SymInitialize( GetCurrentProcess(), nullptr, TRUE ); done = true; }
			}
			static DWORD WINAPI loop( void* p )
			{
				auto* s = ( sampler* ) p;
				SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL );
				while ( !s->stop_ )
				{
					Sleep( 1 );
					if ( s->nrec >= s->recs.size() ) continue;
					if ( SuspendThread( s->target ) == DWORD( -1 ) ) continue;
					CONTEXT ctx = {}; ctx.ContextFlags = CONTEXT_FULL;
					if ( GetThreadContext( s->target, &ctx ) )
					{
						rec& r = s->recs[ s->nrec ];
						r.n = 0;
						r.pc[ r.n++ ] = ctx.Rip;
						while ( r.n < 16 )
						{
							DWORD64 img = 0;
							PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry( ctx.Rip, &img, nullptr );
							if ( !fe ) break;   // TCG code or a leaf without unwind data
							void* hd = nullptr; DWORD64 est = 0;
							RtlVirtualUnwind( UNW_FLAG_NHANDLER, img, ctx.Rip, fe, &ctx, &hd, &est, nullptr );
							if ( !ctx.Rip ) break;
							r.pc[ r.n++ ] = ctx.Rip;
						}
						s->nrec = s->nrec + 1;
					}
					ResumeThread( s->target );
				}
				return 0;
			}
			void start()
			{
				sym_init();
				timeBeginPeriod( 1 );
				recs.assign( 400000, rec{} ); nrec = 0; stop_ = false;
				DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS );
				thread = CreateThread( nullptr, 0, loop, this, 0, nullptr );
			}
			void stop()
			{
				stop_ = true;
				WaitForSingleObject( thread, INFINITE );
				CloseHandle( thread ); CloseHandle( target );
			}
			void report( int top )
			{
				HANDLE proc = GetCurrentProcess();
				std::map<uint64_t, std::string> names;
				auto name = [ & ]( uint64_t a ) -> const std::string& {
					auto it = names.find( a );
					if ( it != names.end() ) return it->second;
					alignas( SYMBOL_INFO ) char buf[ sizeof( SYMBOL_INFO ) + 256 ] = {};
					auto* si = ( SYMBOL_INFO* ) buf; si->SizeOfStruct = sizeof( SYMBOL_INFO ); si->MaxNameLen = 255;
					DWORD64 disp = 0;
					std::string n = SymGetModuleBase64( proc, a ) == 0 ? "[jit]" : SymFromAddr( proc, a, &disp, si ) ? si->Name : "[unknown]";
					return names[ a ] = n;
				};
				std::map<std::string, size_t> self, incl;
				for ( size_t i = 0; i < nrec; ++i )
				{
					self[ name( recs[ i ].pc[ 0 ] ) ]++;
					std::set<std::string> seen;
					for ( int k = 0; k < recs[ i ].n; ++k ) seen.insert( name( recs[ i ].pc[ k ] ) );
					for ( auto& n : seen ) incl[ n ]++;
				}
				auto print = [ & ]( const char* what, std::map<std::string, size_t>& h ) {
					std::vector<std::pair<size_t, std::string>> v;
					for ( auto& [ k, n ] : h ) if ( k.rfind( "at::", 0 ) && k.rfind( "main", 0 ) && k != "BaseThreadInitThunk" && k != "RtlUserThreadStart" && k.find( "scrt_common_main" ) == std::string::npos ) v.push_back( { n, k } );
					std::sort( v.rbegin(), v.rend() );
					std::printf( "    profile %s (%zu samples):\n", what, size_t( nrec ) );
					for ( int i = 0; i < top && i < int( v.size() ); ++i )
						std::printf( "    %6.2f%%  %s\n", 100.0 * v[ i ].first / std::max<size_t>( 1, size_t( nrec ) ), v[ i ].second.c_str() );
				};
				print( "self", self );
				print( "inclusive", incl );
			}
		};

		inline int run( int reps, const std::string& filter, double scale, int cpu, const std::string& csv_path, int profile_top = 0 )
		{
			if ( cpu >= 0 )
			{
				SetThreadAffinityMask( GetCurrentThread(), DWORD_PTR( 1 ) << cpu );
				SetPriorityClass( GetCurrentProcess(), HIGH_PRIORITY_CLASS );
				SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_HIGHEST );
			}
			unsigned maj = 0, min = 0; uc_version( &maj, &min );
			std::printf( "emu-alltest --bench: Unicorn %u.%u UC_CPU_X86_MAX | reps %d (median) | scale %.3g | cpu %d\n", maj, min, reps, scale, cpu );
			std::printf( "%-13s %12s %10s %10s %10s %10s %10s  %-16s\n", "workload", "insns", "cold_ms", "warm_ms", "cold_Mi/s", "warm_Mi/s", "xlat_ms", "state" );
			std::vector<uint8_t> data;
			fill_data( data );
			std::ofstream csv;
			if ( !csv_path.empty() )
			{
				csv.open( csv_path );
				csv << "workload,n,insns,cold_ms_median,warm_ms_median,cold_mips,warm_mips,state,cold_all_ms,warm_all_ms\n";
			}
			int failed = 0;
			for ( auto& w : workloads() )
			{
				if ( !filter.empty() && std::string( w.name ).find( filter ) == std::string::npos ) continue;
				compiled c;
				std::string err;
				if ( !compile( w, c, err ) ) { std::printf( "%-13s compile error: %s\n", w.name, err.c_str() ); ++failed; continue; }
				uint64_t n = std::max<uint64_t>( 1, uint64_t( double( w.n ) * scale ) );
				uint64_t insns = insn_count( w, c, n, data );
				std::vector<double> cold, warm;
				uint64_t hash = 0;
				bool ok = true;
				sampler prof;
				if ( profile_top ) prof.start();
				for ( int r = 0; r < reps; ++r )
				{
					sample s = run_once( w, c, n, data );
					if ( !s.ok ) { std::printf( "%-13s error: %s\n", w.name, s.err.c_str() ); ok = false; break; }
					if ( r && s.hash != hash ) { std::printf( "%-13s error: state hash differs between repetitions\n", w.name ); ok = false; break; }
					hash = s.hash;
					cold.push_back( s.cold * 1e3 ); warm.push_back( s.warm * 1e3 );
				}
				if ( profile_top ) prof.stop();
				if ( !ok ) { ++failed; continue; }
				auto med = []( std::vector<double> v ) { std::sort( v.begin(), v.end() ); size_t k = v.size(); return k % 2 ? v[ k / 2 ] : 0.5 * ( v[ k / 2 - 1 ] + v[ k / 2 ] ); };
				double mc = med( cold ), mw = med( warm );
				std::printf( "%-13s %12llu %10.1f %10.1f %10.1f %10.1f %10.1f  %016llX\n", w.name, ( unsigned long long ) insns, mc, mw,
							 insns / ( mc * 1e3 ), insns / ( mw * 1e3 ), w.opens ? 0.0 : mc - mw, ( unsigned long long ) hash );
				if ( csv )
				{
					csv << w.name << ',' << n << ',' << insns << ',' << mc << ',' << mw << ',' << insns / ( mc * 1e3 ) << ',' << insns / ( mw * 1e3 ) << ','
						<< std::hex << hash << std::dec << ",\"";
					for ( size_t k = 0; k < cold.size(); ++k ) csv << ( k ? " " : "" ) << cold[ k ];
					csv << "\",\"";
					for ( size_t k = 0; k < warm.size(); ++k ) csv << ( k ? " " : "" ) << warm[ k ];
					csv << "\"\n";
				}
				if ( profile_top ) prof.report( profile_top );
			}
			return failed ? 1 : 0;
		}
	}
}
