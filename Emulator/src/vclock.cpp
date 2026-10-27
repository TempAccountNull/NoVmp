// emu::vclock (ledger U2092): see emu/vclock.hpp.
#include <emu/vclock.hpp>

#include <intrin.h>

namespace emu::vclock
{
	uint64_t mul_div( uint64_t a, uint64_t b, uint64_t c )
	{
		uint64_t hi = 0;
		const uint64_t lo = _umul128( a, b, &hi );
		if ( hi >= c ) return ~0ull;   // the quotient does not fit (never for the values used here)
		uint64_t rem = 0;
		return _udiv128( hi, lo, c, &rem );
	}

	bool tsc_rate_of( const cpuid_profile& p, bool guest_profile, tsc_rate& r, std::string& why )
	{
		const uc_x86_cpuid* l15 = nullptr;
		const uc_x86_cpuid* l16 = nullptr;
		for ( const auto& row : p.leaves )
		{
			if ( row.leaf == 0x15 && row.subleaf == 0 ) l15 = &row;
			if ( row.leaf == 0x16 && row.subleaf == 0 ) l16 = &row;
		}
		if ( l15 && l15->eax && l15->ebx && l15->ecx )
		{
			r.hz = mul_div( l15->ecx, l15->ebx, l15->eax );
			r.source = "CPUID.15H: ( ECX " + std::to_string( l15->ecx ) + " * EBX " + std::to_string( l15->ebx ) + " ) / EAX " + std::to_string( l15->eax ) +
					   " (253669-093 Vol3B 22.7.3 p. 22-149, nominal TSC frequency)";
			return true;
		}
		if ( l15 && l15->eax && l15->ebx && !l15->ecx )
		{
			why = "CPUID.15H enumerates the TSC / crystal ratio but not the crystal frequency (ECX 0): the SDM's Table 22-95 lookup by model is not modelled";
			return false;
		}
		// CPUID.16H is NOT a TSC rate: "The returned information should not be used for any other purpose as the returned information does
		// not accurately correlate to information / counters returned by other processor interfaces" (ISE 319433-062 CPUID leaf 16H NOTES,
		// p. 1-22 / 1-23; the manual wins) - the bare host shows it: 16H base 3500 MHz, 15H nominal TSC 3494.4 MHz
		(void)l16;
		if ( guest_profile )
		{
			r.hz = guest_tsc_hz;
			r.source = "the VMware guest's measured rate 3494.4 MHz (its profile reports no CPUID.15H / 16H; tm_nt time.tsc: QpcFrequency * 2^64 / "
					   "QpcMultiplier of the guest's hypervisor page, RDTSC / QPC agree within 0.1 %)";
			return true;
		}
		why = "the CPU profile reports no nominal TSC frequency (CPUID.15H EAX / EBX / ECX not all non-zero; 16H is no TSC rate) and is not the "
			  "VMware-guest profile - no rate for the virtual time base";
		return false;
	}

	uint64_t tsc_at_start( uint64_t hz ) { return mul_div( uptime_at_start_hns + interrupt_time_lag_hns, hz, hns_per_second ); }

	bool start( uc_engine* uc, uint64_t hz, std::string& why )
	{
		if ( !hz )
		{
			why = "no TSC frequency";
			return false;
		}
		const uc_err e = uc_ctl_set_x86_vclock( uc, UC_X86_VCLOCK_VIRTUAL, tsc_at_start( hz ), ticks_per_insn );
		if ( e != UC_ERR_OK )
		{
			why = std::string( "UC_CTL_X86_VCLOCK: " ) + uc_strerror( e );
			return false;
		}
		return true;
	}

	reading derive( uint64_t tsc, uint64_t hz )
	{
		reading r;
		r.tsc = tsc;
		r.qpc = mul_div( tsc, qpc_frequency, hz );
		const uint64_t hns = mul_div( tsc, hns_per_second, hz );
		r.interrupt_time = hns > interrupt_time_lag_hns ? hns - interrupt_time_lag_hns : 0;
		r.system_time = system_time_at_interrupt_zero + r.interrupt_time;
		r.tick_count = r.interrupt_time / maximum_time_increment;
		return r;
	}

	bool read( uc_engine* uc, uint64_t hz, reading& r, std::string& why )
	{
		int mode = -1;
		uint64_t now = 0, n = 0;
		const uc_err e = uc_ctl_get_x86_vclock( uc, &mode, &now, &n );
		if ( e != UC_ERR_OK || mode != UC_X86_VCLOCK_VIRTUAL || !hz )
		{
			why = e != UC_ERR_OK ? std::string( "UC_CTL_X86_VCLOCK: " ) + uc_strerror( e ) : "the virtual time base is not running";
			return false;
		}
		r = derive( now, hz );
		r.insns = n;
		return true;
	}

	bool advance_hns( uc_engine* uc, uint64_t hz, uint64_t hns, std::string& why )
	{
		uint64_t ticks = mul_div( hns, hz, hns_per_second );
		if ( mul_div( ticks, hns_per_second, hz ) < hns ) ticks++;   // ceil: the wait lasts at least HNS
		const uc_err e = uc_ctl_x86_vclock_advance( uc, ticks );
		if ( e != UC_ERR_OK )
		{
			why = std::string( "UC_CTL_X86_VCLOCK_ADVANCE: " ) + uc_strerror( e );
			return false;
		}
		return true;
	}
}
