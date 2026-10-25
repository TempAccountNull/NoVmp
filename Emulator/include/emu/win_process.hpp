// emu::win process state (plan 3.M5 part 2, ledgers U2025-U2027; demand items 2 and 4 of oep_progress.md run 1): what a Windows
// x64 process holds when its image's code first runs, built in guest memory from the environment's capture and the public
// symbols' layouts (emu/win_fields.hpp: the guest ntdll.pdb types _PEB, _PEB_LDR_DATA, _LDR_DATA_TABLE_ENTRY,
// _RTL_USER_PROCESS_PARAMETERS, _CURDIR, _UNICODE_STRING, _LIST_ENTRY, _HEAP; offsets cited per field in win_process.cpp).
// Nothing here comes from the running machine; every name / path / environment value is a synthesized placeholder (AGENT_RULES
// "real identifiers": no real user, computer or path of any machine).
//
//   system images  the environment's system DLL images (ntdll.dll, kernel32.dll, kernelbase.dll of the capture) mapped AS DATA
//                  by emu::pe::map (headers R, sections by their characteristics, relocated to the slot chosen below). Windows
//                  places them by ASLR (per boot); the emulator uses fixed slots (deterministic; not modelled: randomization).
//                  Their code is never run natively; under --Sys it may run in emulation, under --HLE reaching it stops loudly.
//   loader data    PEB_LDR_DATA + one LDR_DATA_TABLE_ENTRY per module (the image, ntdll, kernel32, kernelbase) linked into
//                  InLoadOrderModuleList / InMemoryOrderModuleList (image, ntdll, kernel32, kernelbase) and
//                  InInitializationOrderModuleList (ntdll, kernelbase, kernel32 - the dependency order; the image is not in it):
//                  the lists a process walks to find its modules (Windows keeps PEB_LDR_DATA in ntdll's data, public symbol
//                  PebLdr, and the entries in the process heap; here one private allocation "loader data")
//   parameters     RTL_USER_PROCESS_PARAMETERS, normalized (absolute pointers; RTL_USER_PROCESS_PARAMETERS_NORMALIZED 1, ReactOS
//                  0.4.16 sdk/include/ndk/rtltypes.h:41): image path, command line, current directory, window title, desktop,
//                  the environment block (UTF-16 "NAME=value" strings, sorted, double-NUL terminated) - placeholders
//   heap           the process heap of --HLE (and of --Sys until the real loader creates its own, milestone M5 part 2 object C):
//                  one private reservation of HeapSegmentReserve (1 MiB) with HeapSegmentCommit (2 pages) committed and the
//                  _HEAP header fields a program reads: Flags = HEAP_GROWABLE (2, winnt.h :23131; the process heap's flags,
//                  ReactOS dll/ntdll/ldr/ldrinit.c:1902) and ForceFlags 0 - the values without heap-debugging global flags
//                  (NtGlobalFlag 0, no debugger); no allocator behind it yet (HeapAlloc / RtlAllocateHeap under --HLE: stubs)
#pragma once

#include <emu/engine.hpp>
#include <emu/nt_kernel.hpp>
#include <emu/pe_image.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace emu::win
{
	struct environment_facts;

	struct module_image
	{
		std::string name;              // the module's base name ("ntdll.dll" ...; for the process image: its file name)
		std::string path;              // the full path the loader data names (placeholder for the image)
		uint64_t base = 0, size = 0, entry = 0, original_base = 0;
		uint32_t timestamp = 0;
		bool is_dll = false;
		pe::image img;
		std::map<uint32_t, std::string> exports_by_rva;   // the export names (forwarders excluded) - to NAME an address
	};

	// the fixed slot of a system DLL (chosen; Windows: ASLR) - 0 for an unknown name
	uint64_t system_image_slot( const std::string& lower_name );
	inline constexpr const char* system_images_order[] = { "ntdll.dll", "kernel32.dll", "kernelbase.dll" };   // load / memory order

	// map the capture's system DLL images NAMES (those the capture holds; --HLE: system_images_order, --Sys loader start: ntdll.dll -
	// the loader maps the others through their sections) as data; out in the order of NAMES
	bool map_system_images( engine& e, const environment_facts& f, const std::vector<std::string>& names, std::vector<module_image>& out, std::vector<std::string>& log,
							std::string& error );
	// one system image FILE (bytes of the capture) mapped as data at BASE: the module record (exports by RVA, time stamp)
	bool map_one_image( engine& e, const std::string& name, const std::string& system_root, const std::vector<uint8_t>& file, uint64_t base, module_image& m,
						std::vector<std::string>& log, std::string& error );

	struct process_identity   // the synthesized placeholders of one run (no real identifier)
	{
		std::string user = "user", computer = "WIN10-EMU";
		std::string image_dir;        // "<drive>:\Users\user\Desktop"
		std::string image_path;       // image_dir + "\" + image file name
	};
	process_identity make_identity( const environment_facts& f, const std::string& image_file_name );

	struct process_state   // the guest addresses written
	{
		uint64_t parameters = 0, environment = 0, environment_size = 0, ldr = 0, heap = 0;
		std::vector<uint64_t> entries;   // LDR_DATA_TABLE_ENTRY per module (load order)
	};

	// RTL_USER_PROCESS_PARAMETERS + strings + environment block in one private allocation of the kernel layer
	bool build_process_parameters( engine& e, nt::kernel& k, const environment_facts& f, const process_identity& id, process_state& st,
								   std::vector<std::string>& lines, std::string& error );
	// the heap reservation and its _HEAP header fields
	bool build_process_heap( engine& e, nt::kernel& k, process_state& st, std::vector<std::string>& lines, std::string& error );
	// PEB_LDR_DATA + the entries + their name strings in one private allocation; MODULES in load order (the image first)
	bool build_loader_data( engine& e, nt::kernel& k, const std::vector<module_image>& modules, process_state& st, std::vector<std::string>& lines,
							std::string& error );
}
