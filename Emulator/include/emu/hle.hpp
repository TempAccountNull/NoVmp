// emu::hle (plan 3.M5 = 3.B 6.1 --HLE, ledger U2009): the API-level boundary. An image's imports are bound ("redirected") to
// stubs in one emulator page of guest memory; a call of a stub runs the API's documented semantics here, on the ONE kernel
// layer of emu::nt (D3, plan 6.1: "In --HLE mode the API reimplementations call into that same kernel layer, so there's only
// one implementation of each piece of semantics") - ExitProcess IS the kernel's NtTerminateProcess( NtCurrentProcess() ).
// Nothing is forwarded to the host (AGENT_RULES "Host safety"): the host never calls a system DLL for the guest, and no host
// function name of the Win32 API is used as an identifier here (no_host_passthrough pt_host_query: they appear as strings).
//
// The module page (module_va, read + execute, user mode): one 16-byte stub per bound import, each a single RET (C3H) padded
// with INT3; the emulator's code hook runs the API when the stub's RET is reached, writes RAX (the return value) and lets the
// RET return to the caller (Windows x64 calling convention: arguments RCX, RDX, R8, R9, the return value in RAX). An import
// this milestone does not implement is bound too - CALLING it stops the run loudly (the API named), so a run measures which
// APIs a program really calls. Imports by ordinal are bound the same way (named by the ordinal).
//
// APIs of milestone M5 (kernel32.dll; Windows SDK 10.0.26100.0 um\processthreadsapi.h / um\libloaderapi.h prototypes, the
// semantics from the Microsoft documentation of each function):
//   GetCurrentProcessId()        "Retrieves the process identifier of the calling process." -> the TEB ClientId.UniqueProcess
//   GetCurrentThreadId()         "Retrieves the thread identifier of the calling thread." -> ClientId.UniqueThread
//   ExitProcess( uExitCode )     "Ends the calling process and all its threads." -> the kernel's NtTerminateProcess(
//                                NtCurrentProcess(), uExitCode) (no DLL is loaded under --HLE, so no DLL_PROCESS_DETACH)
//   GetModuleHandleW / A( NULL ) "If this parameter is NULL, GetModuleHandle returns a handle to the file used to create the
//                                calling process (.exe file)." - the handle is the module's base address (Windows Data
//                                Types: HINSTANCE / HMODULE "the base address of the module in memory"); a module name =
//                                stop (no module list in M5)
// P41 (U2083, USER DECISION 2026-10-26 "started without a console"): the APIs a console program uses to print, with the results a process
// WITHOUT a console gets (MEASURED natively by our own test PE tm_nt in a detached process, tm_nt con.*; the documented functions:
// um\processenv.h GetStdHandle "If an application does not have associated standard handles ... the return value is NULL", um\fileapi.h
// WriteFile, um\consoleapi.h WriteConsoleA / W, um\errhandlingapi.h GetLastError):
//   GetStdHandle( STD_INPUT / OUTPUT / ERROR_HANDLE ) = the process parameters' StandardInput / Output / Error (0: no standard handles;
//                                LastError unchanged - tm_nt con.std.*); another value = stop
//   WriteFile( 0, ... )          *lpNumberOfBytesWritten = 0, then the kernel layer's NtWriteFile( 0 ) = STATUS_INVALID_HANDLE ->
//                                FALSE, LastError 6 ERROR_INVALID_HANDLE (tm_nt con.api.wfile / con.ntwf); another handle = stop
//   WriteConsoleA / W( 0, ... )  FALSE, LastError 6, the written count NOT written (tm_nt con.api.wcon); another handle = stop
//   GetLastError()               the TEB's LastErrorValue (68H)
// Each WriteFile / WriteConsole call is an output ATTEMPT that win_env records in the console capture (its text decoded; observation only).
// The TLS start (U2010): under --HLE the loader's part of process start for TLS - the index, the thread's TLS block from the
// template, the callbacks with DLL_PROCESS_ATTACH before the entry point - is done by emu::win (win_env.cpp) with a user-mode
// trampoline in this page (start_trampoline).
#pragma once

#include <emu/engine.hpp>
#include <emu/nt_kernel.hpp>
#include <emu/pe_image.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace emu::hle
{
	inline constexpr uint64_t module_va = 0x7FFA00000000ull;   // the emulator's HLE page (user range; never an image's preferred base here)
	inline constexpr uint64_t module_size = 0x10000;
	inline constexpr uint64_t stub_size = 16;
	inline constexpr uint64_t max_stubs = 0x800;                 // stubs in [module_va, +8000H); the trampoline above
	inline constexpr uint64_t trampoline_va = module_va + 0x8000;

	enum class api { unknown, get_current_process_id, get_current_thread_id, exit_process, get_module_handle_w, get_module_handle_a,
					 get_std_handle, write_file, write_console_a, write_console_w, get_last_error,   // P41 (U2083): the last five
					 char_upper_w };   // P44 (U2123): user32.dll CharUpperW

	struct binding
	{
		std::string dll, name;      // as imported (name "" = by ordinal)
		uint32_t ordinal = 0;
		api id = api::unknown;
		uint64_t stub = 0, iat = 0;
		std::string label() const;  // "kernel32.dll!ExitProcess" / "x.dll!#12"
	};

	enum class result_kind { returned, terminated, stop };
	struct result
	{
		result_kind kind = result_kind::stop;
		uint64_t rax = 0;
		uint32_t exit_status = 0;
		std::string text;
		// P41 (U2083): an output attempt (WriteFile / WriteConsoleA / W) for win_env's console capture
		bool console = false;
		uint64_t con_handle = 0, con_buffer = 0, con_length = 0;
		bool con_wide = false;
		std::string con_result;
	};

	class module
	{
	public:
		// map the page (once) and bind every import of the image mapped at IMAGE_BASE: write the stubs and the IAT slots
		bool bind( engine& e, const std::vector<pe::import_module>& mods, uint64_t image_base, std::vector<std::string>& log, std::string& error );
		// a user-mode start trampoline: the TLS callbacks (DllHandle = image base, DLL_PROCESS_ATTACH, Reserved 0), then a jump to
		// the entry point with the entry's stack unchanged ([RSP] = the return sentinel); returns its address
		bool write_trampoline( engine& e, uint64_t image_base, const std::vector<uint64_t>& callbacks, uint64_t entry, uint64_t& start, std::string& error );
		const std::vector<binding>& bindings() const { return b_; }
		bool mapped() const { return mapped_; }

		// the stub at ADDRESS was reached: run the API (arguments from the engine's registers)
		result call( uint64_t address, uc_engine* uc, nt::kernel& k, uint64_t image_base, std::string& api_label, std::array<uint64_t, 4>& args );

	private:
		bool map_page( engine& e, std::string& error );
		std::vector<binding> b_;
		bool mapped_ = false;
	};
}
