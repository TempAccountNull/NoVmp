// emu::snippet (plan 3.M3 = emu-snippet v1, plan 3.A 2.7; ledger U1974): run an isolated piece of x86-64 code on one
// emu::engine and report what it did. Built on the M2 lib: the engine (CPU model, CPUID profile at uc_open, opt-ins, RNG
// source) is emu::engine::open, the register dump is emu::reg - nothing here opens Unicorn itself.
//
// One run (run()):
//   1. engine   emu::engine::open( spec.config ) (decision 3.0.3: --host = the captured bare-host profile by default)
//   2. memory   the spec's regions (page aligned, named, with their bytes) and the code; with auto_stack a 64 KiB stack whose
//               top holds the RETURN SENTINEL (a canonical, never mapped address): returning to it ends the run ("returned")
//   3. state    MSRs, GDTR, then the x87 / SSE / AVX state through XRSTOR64 of an XSAVE image the lib builds, executed by the
//               engine in a private loader page that is unmapped again before the snippet starts (as an OS restores a thread's
//               extended state, so XINUSE[2:0] reports the components in use; FXRSTOR64 when the engine has no XSAVE); a CPL3
//               state (state.cpl3) is entered by IRETQ from that loader page with the given CS / SS (a GDT must be mapped).
//               Then every other register through the API (GPRs, RIP, RFLAGS, FS / GS base, ZMM / K / R16-R31, DR0-DR7,
//               FCS / FDS) and last CR0 / CR4 (so CR0.TS acts on the snippet only). Registers the spec does not name keep the
//               engine's reset values. The state at the snippet's first instruction is read back ("start").
//   4. run      until the stop address (default: the end of the code), the instruction bound (default 1000000: every run
//               is bounded, no host time), a fault, a system call, an external transfer, or the ret depth.
//   5. result   the final state, the stop reason, the trace (optional), and the checks: the expectations and, for a replayed
//               emu-alltest line, its Unicorn engine state ("reference").
//
// Stops (the result's reason):
//   end         RIP reached the stop address (default: the first byte after the code)
//   count       the instruction bound was reached
//   returned    control reached the return sentinel (the snippet returned to its caller)
//   ret_depth   --stop-ret-depth N: N RETs more than CALLs executed; the run stops before the instruction RET returned to
//   fault       an exception / interrupt (UC_HOOK_INTR: vector, error code from UC_CTL_X86_EXCEPTION), an invalid instruction
//               (#UD), or a data access outside the mapped memory or against its protection (#PF; the access type and address
//               are reported; there is no paging: unmapped = not present). The state is the state at the event.
//   syscall     (syscall policy "stop", the default) SYSCALL, SYSENTER or INT n is about to execute: the run stops BEFORE it,
//               RIP = the instruction, and reports the kind and RAX (the service number). Nothing is ever forwarded to the
//               host (AGENT_RULES "Host safety"). Policy "cpu": the emulated CPU executes the instruction as the SDM defines
//               it, inside the engine (SYSCALL to the guest's own IA32_LSTAR, INT n raises vector n = a fault stop) - still
//               never the host.
//   external    (external policy "stop", the default) control went to an address outside the executable mapped memory
//               (an external call / jump / return): stopped, the target reported. Policy "fault": the same fetch is a #PF
//               (execute access), as a CPU without that page would raise it (the replay of emu-alltest lines uses this).
//
// Checks:
//   expect      registers (whole or by byte mask), memory bytes, the fault (none / vector / vector + error code). Loose (the
//               default): only what is listed. Strict: every register of the checked set (RAX-R15, R16-R31 with APX, RFLAGS,
//               FCW, FSW, FTW, MXCSR, ST0-ST7, ZMM0-ZMM31, K0-K7) and every checked memory range must equal its START value
//               unless listed (as emu-alltest's "=>" lines).
//   reference   the same comparison against another engine's final state (the replay of emu-alltest lines: its Unicorn
//               engine state), with the field masks the reference names.
//   JSON form of both: {"mode": "strict"|"loose", "fault": null | "#GP(0)" | {"vector", "error_code", "pf_access"},
//               "regs": {NAME: VALUE | {"hex"|"value", "mask"}}, "zero": [NAME, ...] (expected all zero), "mem": [{"address",
//               "hex", "mask"}], "checked_mem": [{"address", "size"}] (strict: start bytes + "mem"), "full_mem": [{"address",
//               "size"}] (exactly the "mem" bytes, zero elsewhere)}
//
// Register names (case-insensitive): rax..r15, r16..r31 (APX), rip, rflags, fs_base, gs_base, cs, ss (CPL3 entry), fpcw,
// fpsw, ftw (the abridged tag byte, bit i = physical register i), fptag (the full tag word, read only), fop, fip, fdp, fcs,
// fds, st0..st7 (10 bytes, ST(i) = stack order as in FXSAVE), mxcsr, xmm0..31 / ymm0..31 / zmm0..31 (16 / 32 / 64 bytes,
// memory byte order, low byte first), k0..k7, dr0..dr3, dr6, dr7, cr0, cr4, gdtr (base + limit). Scalars as numbers; byte
// fields as hex byte strings.
#pragma once

#include <emu/engine.hpp>
#include <emu/json.hpp>
#include <emu/reg.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace emu::snippet
{
	enum class syscall_policy { stop, cpu };
	enum class external_policy { stop, fault };
	enum class stop_reason { none, end, count, returned, ret_depth, fault, syscall, external, error };
	const char* name_of( stop_reason r );

	inline constexpr uint64_t default_code_address = 0x140001000ull;   // a Windows x64 image's first section
	inline constexpr uint64_t default_stack_top = 0x0000000000300000ull;
	inline constexpr uint64_t default_stack_size = 0x10000;
	inline constexpr uint64_t return_sentinel = 0x00007FFFDEAD0000ull;  // canonical, never mapped
	inline constexpr uint64_t default_max_insns = 1000000;
	inline constexpr uint64_t max_code_bytes = 1u << 20;

	// a value of one named field: little-endian bytes (scalars: their 1 / 2 / 4 / 8 bytes) and an optional byte mask
	// (empty = every byte); the name is canonical (lower case)
	struct field_value
	{
		std::string name;
		std::vector<uint8_t> bytes;
		std::vector<uint8_t> mask;
	};

	struct mem_bytes
	{
		uint64_t address = 0;
		std::vector<uint8_t> bytes;
		std::vector<uint8_t> mask;   // empty = every byte
	};

	struct region
	{
		uint64_t base = 0, size = 0;
		uint32_t prot = UC_PROT_READ | UC_PROT_WRITE;
		std::string name;
		std::vector<mem_bytes> data;   // initial bytes (absolute addresses; the rest of the region is zero)
	};

	struct range
	{
		uint64_t address = 0, size = 0;
	};

	struct checks
	{
		bool present = false;
		bool strict = false;
		bool fault_given = false;   // false = the fault outcome is not checked
		int vector = -1;            // -1 = "no fault"
		int64_t error_code = -1;    // -1 = not compared
		int pf_access = -1;         // #PF access type to compare (0 read, 1 write, 8 execute), -1 = not compared
		std::vector<field_value> regs;
		std::vector<mem_bytes> mem;
		std::vector<range> checked_mem;   // strict: these ranges equal their start bytes except the listed ones
		std::vector<range> full_mem;      // reference: these ranges equal 'mem' (bytes not listed there = 0)
		std::string label;                // "expect" / "reference (...)"
	};

	struct spec
	{
		std::string name;                  // for messages (file:line, "command line")
		engine_config config;
		std::vector<uint8_t> code;
		uint64_t code_address = default_code_address;
		uint32_t code_prot = UC_PROT_ALL;  // used when the code is not inside a given region
		std::vector<region> regions;
		bool auto_stack = false;           // map the default stack (+ return sentinel) when RSP is not given
		// state
		std::vector<field_value> regs;
		std::vector<std::pair<uint32_t, uint64_t>> msrs;
		bool cpl3 = false;
		// stops
		bool stop_at_given = false;
		uint64_t stop_at = 0;
		uint64_t max_insns = default_max_insns;
		bool ret_depth_given = false;
		uint64_t ret_depth = 0;
		syscall_policy syscall = syscall_policy::stop;
		external_policy external = external_policy::stop;
		// output
		bool trace = false;
		std::vector<range> dump_mem;
		// checks
		checks expect;
		checks reference;
	};

	struct trace_entry
	{
		uint64_t address = 0;
		std::vector<uint8_t> bytes;
		std::string text;
		std::vector<std::string> changes;   // "rax: 0x0 -> 0x5", "rflags: ... (ZF 0->1)"
		std::vector<std::string> mem;       // "read  [0x...] 8 = 0x...", "write [0x...] 4 = 0x..."
	};

	struct result
	{
		bool ran = false;                 // the engine was set up and the code started
		std::string error;                // setup / run problem (not a check difference)
		stop_reason reason = stop_reason::none;
		uint64_t rip = 0;                 // RIP at the stop
		uint64_t insns = 0;               // instructions executed (counted when the per-instruction hook is on)
		bool counted = false;
		// fault
		bool faulted = false;
		int vector = -1;
		int64_t error_code = -1;
		int pf_access = -1;
		uint64_t pf_address = 0;
		// system call (policy stop)
		std::string syscall_kind;
		uint64_t syscall_number = 0;
		// external transfer
		uint64_t external_target = 0;
		// states: field name -> bytes (the snippet field set, see field_names())
		std::map<std::string, std::vector<uint8_t>> start, final;
		std::vector<trace_entry> trace;
		// checks
		bool expect_checked = false, expect_ok = true;
		std::vector<std::string> expect_diffs;
		bool reference_checked = false, reference_ok = true;
		std::vector<std::string> reference_diffs;
		std::vector<std::string> log;     // the engine's verbose log and the loader steps
		std::vector<std::pair<range, std::vector<uint8_t>>> dumps;
	};

	// ---- spec input
	// a register assignment "NAME=VALUE" (scalars: decimal / 0x hex; byte fields: hex bytes, low byte first)
	bool parse_assignment( const std::string& text, field_value& out, std::string& error );
	// "ADDR=HEXBYTES"
	bool parse_mem_assignment( const std::string& text, mem_bytes& out, std::string& error );
	// "ADDR:SIZE[:PROT]" (PROT letters r w x)
	bool parse_map( const std::string& text, region& out, std::string& error );
	// a fault token: "none", "#UD", "#GP(0)", "#PF", "13", ...
	bool parse_fault( const std::string& text, int& vector, int64_t& error_code, std::string& error );
	// a spec from JSON (the format of emu-snippet --state / --batch; see emu_snippet.cpp usage): unknown keys are errors.
	// blobs: named byte strings defined earlier in a batch file ({"blob": NAME, "hex": ...}) that "data" entries may use.
	bool spec_from_json( const json::value& v, spec& out, std::string& error, const std::map<std::string, std::vector<uint8_t>>* blobs = nullptr );

	// ---- running
	bool run( const spec& s, result& r );

	// ---- output
	// the snippet field set in print order, for a run with / without APX state
	std::vector<std::string> field_names( bool apx );
	std::string format_field( const std::string& name, const std::vector<uint8_t>& bytes );
	std::vector<std::string> format_result( const spec& s, const result& r, bool with_states );
}
