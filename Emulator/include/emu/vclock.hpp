// emu::vclock (plan 3.M6 "one virtual time base + RNG", 1.K.10 P7; ledger U2092): the ONE emulated clock of a run. Every time a
// guest can observe comes from it - never from the host clock (AGENT_RULES "Host safety": "No host sleep or host time calls"):
//   TSC        the fork's virtual time base (UC_CTL_X86_VCLOCK, fork U2090): RDTSC / RDTSCP / RDMSR IA32_TIME_STAMP_COUNTER /
//              IA32_TSC_ADJUST / TPAUSE / UMWAIT deadlines all read it. It advances by ticks_per_insn TSC ticks per instruction
//              started (the DOCUMENTED RATE: one tick per instruction - a virtual CPU that retires one instruction per TSC tick) and
//              by the environment's waits (advance_hns: a time-out, a delay); TPAUSE / UMWAIT waits (fork U1620) add their ticks.
//   QPC        QueryPerformanceCounter / NtQueryPerformanceCounter = TSC * qpc_frequency / tsc_hz (the counter of the reset-based
//              TSC, frequency 10 MHz as measured: tm_nt time.qpf / time.nqpc)
//   KUSER      InterruptTime = QPC - interrupt_time_lag (measured shape tm_nt time.qpc.it: QPC - InterruptTime is a positive per-boot
//              constant), SystemTime = system_time_at_interrupt_zero + InterruptTime (tm_nt time.st: SystemTime - InterruptTime
//              constant), TickCountQuad = InterruptTime / maximum_time_increment (tm_nt time.tick), TickCountMultiplier and
//              QpcFrequency constants (tm_nt time.res / time.qpf); written by win_env when the guest reads them
//   waits      NtDelayExecution / NtWaitForSingleObject time-outs advance the clock (one thread: nothing else can end the wait)
// The TSC frequency (needed for QPC / KUSER) comes from the CPU profile per the SDM: CPUID.15H "Nominal TSC frequency = ( ECX * EBX
// ) / EAX" (253669-093 Vol3B 22.7.3 p. 22-149). CPUID.16H is NOT used: its NOTES say the data "should not be used for any other
// purpose" (ISE 319433-062 CPUID leaf 16H NOTES, p. 1-22 / 1-23; the bare host: 16H base 3500 MHz, 15H nominal TSC 3494.4 MHz). The VMware-guest
// profile reports no 15H (VMware hides 15H / 16H): its rate is the guest's own, MEASURED by our own test PE (tm_nt time.tsc:
// QpcFrequency * 2^64 / QpcMultiplier of the guest's hypervisor page, cross-checked against RDTSC / QPC over 100 ms) = 3494.4 MHz.
// Deterministic: a pure function of the instruction count (and the waits) - two runs of the same image give the same times under
// --seeded and --HostSeed alike (--HostSeed changes only RDRAND / RDSEED).
#pragma once

#include <emu/profile.hpp>

#include <unicorn/unicorn.h>

#include <cstdint>
#include <string>

namespace emu::vclock
{
	inline constexpr uint64_t ticks_per_insn = 1;                    // the documented rate (P42)
	inline constexpr uint64_t hns_per_second = 10000000;             // Windows time unit: 100 ns
	// Windows constants of the emulated environment, MEASURED natively in the VMware guest (tm_nt case time, P42)
	inline constexpr uint64_t qpc_frequency = 10000000;               // time.qpf: QueryPerformanceFrequency = KUSER QpcFrequency = 989680H
	inline constexpr uint32_t maximum_time_increment = 156250;        // time.res: NtQueryTimerResolution MaximumTime 2625AH (15.625 ms)
	inline constexpr uint32_t tick_count_multiplier = 0x0FA00000;     // time.res: KUSER TickCountMultiplier = (MaximumTime << 24) / 10000
	inline constexpr uint64_t guest_tsc_hz = 3494400000ull;           // time.tsc: the VMware guest's TSC rate (0.1 MHz), no CPUID 15H
	// model choices (P42), documented
	inline constexpr uint64_t uptime_at_start_hns = 36000000000ull;   // InterruptTime when the emulated process starts: 1 h
	inline constexpr uint64_t interrupt_time_lag_hns = 1500000;       // QPC - InterruptTime: 150 ms (time.qpc.it: positive, constant;
	                                                                  // 0.15 s in the measurement - a per-boot value, this one fixed)
	inline constexpr uint64_t system_time_at_interrupt_zero = 134116992000000000ull;   // FILETIME 2026-01-01T00:00:00Z (the emulated boot)

	// floor( a * b / c ) without overflow (c != 0)
	uint64_t mul_div( uint64_t a, uint64_t b, uint64_t c );

	struct tsc_rate
	{
		uint64_t hz = 0;
		std::string source;
	};
	// the TSC frequency of a CPU profile (its leaf 15H row) - false with the reason when it reports none; guest_profile = the profile is
	// the VMware guest's (its measured rate is the fallback)
	bool tsc_rate_of( const cpuid_profile& p, bool guest_profile, tsc_rate& r, std::string& why );

	struct reading
	{
		uint64_t tsc = 0;              // the time base now (TPAUSE / UMWAIT ticks included, IA32_TSC_ADJUST not)
		uint64_t insns = 0;            // instructions started since the clock was started
		uint64_t qpc = 0;              // QueryPerformanceCounter (qpc_frequency)
		uint64_t interrupt_time = 0;   // KUSER InterruptTime (100 ns)
		uint64_t system_time = 0;      // KUSER SystemTime (FILETIME, UTC)
		uint64_t tick_count = 0;       // KUSER TickCountQuad
	};

	// the TSC value at the process start: ( uptime_at_start + interrupt_time_lag ) in TSC ticks
	uint64_t tsc_at_start( uint64_t hz );
	// start the engine's virtual time base (UC_CTL_X86_VCLOCK virtual, start tsc_at_start( hz ), ticks_per_insn)
	bool start( uc_engine* uc, uint64_t hz, std::string& why );
	bool read( uc_engine* uc, uint64_t hz, reading& r, std::string& why );
	// derived values of a TSC value (hz > 0)
	reading derive( uint64_t tsc, uint64_t hz );
	// time passes without instructions (a wait): HNS 100-ns units -> ceil( HNS * hz / 10^7 ) ticks
	bool advance_hns( uc_engine* uc, uint64_t hz, uint64_t hns, std::string& why );
}
