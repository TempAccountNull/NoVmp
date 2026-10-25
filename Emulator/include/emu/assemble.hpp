// emu::assemble / emu::disassembler (plan 3.M3, ledger U1974): Keystone input and Capstone trace text for emu-snippet
// (plan 2.7: "an assembly listing assembled with Keystone (for example code copied from IDA)"; "verbose per-instruction trace
// (address, bytes, disassembly, ...)"). Both only turn text into bytes and bytes into text: nothing is executed on the host
// (the bytes go to the engine's guest memory, AGENT_RULES "Host safety").
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emu
{
	// Keystone, x86 64-bit, at ADDRESS (labels / RIP-relative operands resolve against it). Statements separated by ';' or
	// new lines. Keystone leaves MCAsmInfo::Radix uninitialised, so a fresh engine may read a plain immediate as hex (emu-alltest
	// U545: "shrd rax, rcx, 26" became imm8 26h in 8 of 160000 fresh engines): every engine is checked with "mov eax, 10"
	// (B8 0A 00 00 00 only in radix 10) and reopened until it reads decimal, as emu-alltest does (at_engine.hpp ks_open_decimal).
	bool assemble( const std::string& text, uint64_t address, std::vector<uint8_t>& out, std::string& error );

	struct insn_info
	{
		uint64_t address = 0;
		uint32_t size = 0;
		std::string text;          // "mnemonic operands", or "(not decoded by Capstone)"
		bool decoded = false;
		bool is_call = false;      // near / far CALL
		bool is_ret = false;       // near RET (RET imm16 included)
		bool is_system = false;    // SYSCALL, SYSENTER or INT n (CD ib): the user / kernel boundary of a snippet
		std::string system_kind;   // "syscall", "sysenter", "int 0xNN"
	};

	// Capstone, x86 64-bit (the classification also falls back to the raw bytes when Capstone cannot decode the instruction:
	// 0F 05 SYSCALL, 0F 34 SYSENTER, CD ib INT n after legacy / REX prefixes)
	class disassembler
	{
	public:
		disassembler();
		~disassembler();
		disassembler( const disassembler& ) = delete;
		disassembler& operator=( const disassembler& ) = delete;
		bool ok() const { return handle_ != 0; }
		// one instruction from BYTES (at most 15 used) at ADDRESS
		insn_info decode( const uint8_t* bytes, size_t n, uint64_t address ) const;

	private:
		size_t handle_ = 0;
	};
}
