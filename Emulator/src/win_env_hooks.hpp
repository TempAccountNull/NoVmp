// process::hook_ctx (plan 3.M5 / P41 / P44 / P45; P49 U2250: moved here from win_env.cpp so that win_exception.cpp shares it) - the state of
// the engine hooks of one process run. Internal to the Emulator library (src/), never installed.
#pragma once

#include <emu/win_env.hpp>
#include <emu/assemble.hpp>
#include <emu/output_file.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace emu::win
{
	struct process::hook_ctx
	{
		process* p = nullptr;
		run_result* r = nullptr;
		uint64_t last_insn = 0;
		bool stopped = false;
		// U2020: instrumentation (process_options::counters / trace_path)
		bool counting = false;
		bool cpuid_pending = false;
		std::pair<uint32_t, uint32_t> cpuid_key{};
		size_t tail_next = 0;
		std::unique_ptr<output_file> trace;   // the audited writer (no_native_check)
		std::unique_ptr<disassembler> dis;
		// U2040: register-level trace lines (process_options::trace_regs) and the ring of the last lines (trace_last)
		bool regs = false;
		uint64_t last = 0;
		std::vector<std::string> ring;
		size_t ring_next = 0;
		std::string pending;          // the current instruction's line: written when the next instruction starts / the run stops
		bool have_pending = false;
		bool pending_mem = false;
		// P41 (U2084): the console capture - the --Sys entry hooks (address -> export), the calls waiting for their return, the file
		std::map<uint64_t, std::string> con_hooks;
		struct con_wait
		{
			uint64_t ret = 0, rsp = 0, pwritten = 0;
			console_attempt a;
		};
		std::vector<con_wait> con_waits;
		std::unique_ptr<output_file> con;
		// P44 (U2122): the guest bytes written inside watched regions (address -> 1); a read of a watched byte neither known nor written stops
		std::set<uint64_t> watch_written;
		// P44 (U2118): the user state of each service SYSCALL interrupted by a kernel -> user callback (innermost last)
		struct cb_saved
		{
			uint64_t gpr[ 16 ] = {};   // RAX .. R15 at the SYSCALL (RSP = the user RSP; RCX = the return RIP, R11 = RFLAGS)
			std::string service;
		};
		std::vector<cb_saved> cb_stack;
		std::string con_dir;
		uint64_t con_max = 0;
		bool con_failed = false;
		// P45 (U2128): the file-system access log <out dir>\fs_access.txt (the console capture's folder) - created at the first access
		std::unique_ptr<output_file> fs;
		size_t fs_written = 0;
		bool fs_failed = false;

		// P49 (plan 3.M7, U2250-U2256, win_exception.cpp): the exception path
		struct cpu_event
		{
			bool active = false;
			unsigned vector = 0;
			bool has_error_code = false, page_fault = false;
			uint32_t error_code = 0;
			uint64_t rip = 0, cs = 0, rflags = 0, rsp = 0, ss = 0;   // the frame the CPU pushed (the #PF path: the state at the fault, RF = 1)
			uint64_t pf_address = 0;
			int pf_access = 0;                                         // ExceptionInformation[0]: 0 read, 1 write, 8 execute
			uint64_t gpr[ 16 ] = {};                                   // RAX .. R15 at the event (RSP = the frame's)
		} ev;
		struct pending_restore
		{
			bool active = false;
			uint64_t ctx = 0;
			uint64_t gpr[ 16 ] = {};
			uint64_t rip = 0, rsp = 0, rflags = 0;
		} rs;
		// --HLE: the dispatcher of win_exception.cpp (our documented implementation of RtlDispatchException), one exception at a time
		struct hle_dispatch
		{
			bool active = false;
			int phase = 0;                      // 0 vectored handlers, 1 frame handlers (SEH), 2 continue handlers, 3 restore
			size_t next = 0;                    // the next list entry of the phase
			uint64_t rec = 0, ctx = 0, ptrs = 0, dc = 0, ctx_copy = 0, call_rsp = 0, rflags = 0;
			uint64_t uw[ 16 ] = {};             // the SEH walk's unwound GPRs (RSP = uw[ 4 ])
			uint64_t uw_rip = 0;
			uint64_t establisher = 0;
			std::string last;                   // what the last call was (for the stop text)
		} hd;
		uint64_t exceptions_seen = 0;
		// a #PF the memory hook saw (CPL 3): process::run continues in the exception path
		bool pf_pending = false;
		uint64_t pf_rip = 0, pf_rflags = 0, pf_address = 0;
		int pf_access = 0;

		// P57 (U2373): the OEP stop and the dump's records (process_options::stop_written_exec / stop_at / dump_dir) - observation only
		bool track_writes = false;                  // the write hook is on (stop_written_exec or dump_dir)
		bool stop_written_exec = false, stop_at_on = false;
		uint64_t stop_at = 0;
		struct page_write
		{
			uint64_t first_insn = 0, first_rip = 0, count = 0;
		};
		std::unordered_map<uint64_t, page_write> written;   // page number -> its first guest write, the write count
		uint64_t last_exec_page = ~0ull;                    // the page of the previous instruction (checked once per page change)
		uint64_t img_lo = 0, img_hi = 0;                    // the process image's range
		struct ptr_write
		{
			uint64_t value = 0, insn = 0, rip = 0, count = 0;
		};
		std::map<uint64_t, ptr_write> image_ptr_writes;     // 8-byte guest writes into the image whose value lies in the module area (last)
		// the loader look-ups (--Sys --loader): entry hooks on ntdll's LdrGetProcedureAddressForCaller / LdrGetDllHandleEx / LdrLoadDll,
		// the result read at the return (address = [RSP] at the entry, RSP = the entry RSP + 8)
		std::map<uint64_t, std::string> res_hooks;
		struct res_wait
		{
			uint64_t ret = 0, rsp = 0, out = 0, insn = 0, caller = 0;
			std::string api, arg;
		};
		std::vector<res_wait> res_waits;
		std::vector<std::string> res_records;
		std::vector<std::pair<uint64_t, uint64_t>> watch;   // P58 (U2383): the watched ranges [at, +len)
		uint64_t trace_from = 0;                            // P58 (U2383): trace lines only from this instruction count on
		bool watch_value_on = false;                        // P58 (U2383): --watch-value
		uint64_t watch_value = 0;

		void stop( stop_kind k, const std::string& detail )
		{
			if ( stopped ) return;
			stopped = true;
			r->stop = k;
			r->detail = detail;
		}
	};
}
