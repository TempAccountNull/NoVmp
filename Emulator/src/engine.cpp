// emu::engine (plan 3.M2, ledger U1952; plan 3.M3, ledger U1973: the emu-alltest machine settings): see include/emu/engine.hpp.
#include <emu/engine.hpp>
#include <emu/reg.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace emu
{
	namespace
	{
		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		std::string prot_text( uint32_t p )
		{
			std::string s;
			s += p & UC_PROT_READ ? 'r' : '-';
			s += p & UC_PROT_WRITE ? 'w' : '-';
			s += p & UC_PROT_EXEC ? 'x' : '-';
			return s;
		}
	}

	engine_config config_from( const run_options& o )
	{
		engine_config c;
		c.profile = o.profile;
		c.rng = o.rng;
		// U1973: emu-snippet's opt-ins, with the masks of the emu-alltest options of the same name (at_main.cpp)
		if ( o.avx512 )
			c.avx512 = UC_X86_AVX512_F | UC_X86_AVX512_DQ | UC_X86_AVX512_BW | UC_X86_AVX512_VL | UC_X86_AVX512_CD | UC_X86_AVX512_IFMA | UC_X86_AVX512_VPOPCNTDQ |
					   UC_X86_AVX512_BITALG | UC_X86_AVX512_VBMI | UC_X86_AVX512_FP16 | UC_X86_AVX512_VP2INTERSECT | UC_X86_AVX512_VBMI2 | UC_X86_AVX512_VNNI |
					   UC_X86_AVX512_BF16;
		if ( o.amx ) c.amx = UC_X86_AMX_ALL;
		if ( o.apx ) c.apx = UC_X86_APX_F;
		c.avx10 = o.avx10;
		return c;
	}

	void engine::log_line( const std::string& line )
	{
		log_.push_back( line );
		if ( cfg_.echo_log ) std::cout << "[engine] " << line << "\n";
	}

	std::unique_ptr<engine> engine::open( const engine_config& cfg, std::string& error )
	{
		std::unique_ptr<engine> e( new engine() );
		e->cfg_ = cfg;
		unsigned maj = 0, min = 0;
		uc_version( &maj, &min );
		e->log_line( "Emulator lib milestone " + std::string( milestone() ) + "; Unicorn " + std::to_string( maj ) + "." +
					 std::to_string( min ) + " (the NoVmp fork), x86-64" );

		// 1. the CPU profile (decision 3.0.3); U1973: an explicit file (emu-alltest --cpuid / --host-core E) overrides the choice's
		e->log_line( std::string( "cpu profile: " ) + name_of( cfg.profile ) + ( cfg.profile == cpu_profile_choice::host ? " (default)" : "" ) +
					 " = " + profile_description( cfg.profile ) );
		if ( cfg.profile == cpu_profile_choice::runtime_cpuid )
		{
			error = "--cpuid-runtime is not implemented (the running machine's CPUID needs an explicit, allow-listed host-CPU path; "
					"decision 3.0.3) - use --host (default), --guest or --max";
			e->log_line( "REFUSED: " + error );
			return nullptr;
		}
		if ( !cfg.cpuid_file.empty() || cfg.profile == cpu_profile_choice::host || cfg.profile == cpu_profile_choice::guest )
		{
			std::string path = cfg.cpuid_file;
			if ( path.empty() )
			{
				std::string dir = cfg.data_dir;
				if ( dir.empty() && !default_data_dir( dir, error ) )
				{
					e->log_line( "REFUSED: " + error );
					return nullptr;
				}
				path = ( std::filesystem::path( dir ) / profile_file_name( cfg.profile ) ).string();
			}
			if ( !load_cpuid_profile_file( path, e->profile_, error ) )
			{
				e->log_line( "REFUSED: " + error );
				return nullptr;
			}
			e->log_line( "cpuid profile file: " + path + ( cfg.cpuid_file.empty() ? "" : " (explicit file, overrides the choice)" ) + " (" +
						 std::to_string( e->profile_.file_bytes ) + " bytes, " + std::to_string( e->profile_.leaves.size() ) + " (leaf, sub-leaf) rows, microcode line " +
						 ( e->profile_.has_microcode ? "present" : "absent" ) + "; values not printed)" );
		}

		// 2. the engine
		uc_err ue = uc_open( UC_ARCH_X86, UC_MODE_64, &e->uc_ );
		if ( ue != UC_ERR_OK )
		{
			e->uc_ = nullptr;
			error = std::string( "uc_open: " ) + uc_strerror( ue );
			e->log_line( "FAILED: " + error );
			return nullptr;
		}
		e->log_line( "uc_open(UC_ARCH_X86, UC_MODE_64) ok" );
		auto step = [ & ]( uc_err r, const std::string& what ) {
			if ( r == UC_ERR_OK )
			{
				e->log_line( what + " ok" );
				return true;
			}
			error = what + ": " + uc_strerror( r );
			e->log_line( "FAILED: " + error );
			return false;
		};
		if ( cfg.cpu_model < 0 || cfg.cpu_model >= UC_CPU_X86_ENDING )
		{
			error = "cpu model " + std::to_string( cfg.cpu_model ) + " is not a built-in UC_CPU_X86_* model";
			e->log_line( "REFUSED: " + error );
			return nullptr;
		}
		if ( !step( uc_ctl_set_cpu_model( e->uc_, cfg.cpu_model ), "cpu model: " + std::string( cfg.cpu_model == UC_CPU_X86_MAX ? "UC_CPU_X86_MAX" : "UC_CPU_X86 model " + std::to_string( cfg.cpu_model ) ) ) )
			return nullptr;

		// U1973: the opt-ins, only before the engine initialises (emu-alltest at_engine.hpp open_configured order)
		if ( cfg.avx512 && !step( uc_ctl_set_x86_avx512( e->uc_, cfg.avx512 ), "UC_CTL_X86_AVX512 " + hex( uint64_t( uint32_t( cfg.avx512 ) ) ) ) ) return nullptr;
		if ( cfg.amx && !step( uc_ctl_set_x86_amx( e->uc_, cfg.amx ), "UC_CTL_X86_AMX " + hex( uint64_t( uint32_t( cfg.amx ) ) ) ) ) return nullptr;
		if ( cfg.avx10 && !step( uc_ctl_set_x86_avx10( e->uc_, cfg.avx10 ), "UC_CTL_X86_AVX10 " + hex( uint64_t( uint32_t( cfg.avx10 ) ) ) ) ) return nullptr;
		if ( cfg.apx && !step( uc_ctl_set_x86_apx( e->uc_, cfg.apx ), "UC_CTL_X86_APX " + hex( uint64_t( uint32_t( cfg.apx ) ) ) ) ) return nullptr;
		if ( cfg.x86_features && !step( uc_ctl_set_x86_features( e->uc_, cfg.x86_features ), "UC_CTL_X86_FEATURES " + hex( uint64_t( uint32_t( cfg.x86_features ) ) ) ) )
			return nullptr;

		// 4. RDRAND / RDSEED source (D8)
		const int mode = cfg.rng == rng_source::host_seed ? UC_X86_RDRAND_HOST : UC_X86_RDRAND_SEEDED;
		ue = uc_ctl_set_x86_rdrand( e->uc_, mode, cfg.rng_seed );
		if ( ue != UC_ERR_OK )
		{
			error = std::string( "UC_CTL_X86_RDRAND: " ) + uc_strerror( ue );
			e->log_line( "FAILED: " + error );
			return nullptr;
		}
		e->log_line( cfg.rng == rng_source::host_seed
						 ? std::string( "rng: --HostSeed (decision D8): RDRAND / RDSEED from the host DRNG, executed by the fork (U835)" )
						 : "rng: --seeded (default, decision D8): deterministic model, seed " + hex( cfg.rng_seed ) +
							   " (fork U835: value n = SplitMix64(seed + n * 9E3779B97F4A7C15H), CF = 1)" );

		// 3. the CPUID profile at uc_open (1.J.47.a), before any memory map / register access
		if ( e->has_profile() )
		{
			ue = uc_ctl_set_x86_cpuid( e->uc_, e->profile_.leaves.data(), e->profile_.leaves.size() );
			if ( ue != UC_ERR_OK )
			{
				error = std::string( "UC_CTL_X86_CPUID: " ) + uc_strerror( ue );
				e->log_line( "FAILED: " + error );
				return nullptr;
			}
			e->log_line( "CPUID profile installed at uc_open (plan 1.J.47.a, fork U1580): " + std::to_string( e->profile_.leaves.size() ) +
						 " rows; " + ( cfg.strict < 0 ? std::string( "UC_CTL_X86_CPUID_STRICT not written = the fork's default (strict while a profile is installed, U435)" )
													   : std::string( "UC_CTL_X86_CPUID_STRICT written below" ) ) );
		}
		else
			e->log_line( "no CPUID profile (--max): CPUID returns the model's values" );
		if ( cfg.strict >= 0 && !step( uc_ctl_set_x86_cpuid_strict( e->uc_, cfg.strict ), "UC_CTL_X86_CPUID_STRICT " + std::to_string( cfg.strict ) ) ) return nullptr;
		if ( e->has_profile() && e->profile_.has_microcode )
		{
			ue = uc_ctl_set_x86_microcode( e->uc_, e->profile_.microcode_rev, e->profile_.microcode_platform_id );
			if ( ue != UC_ERR_OK )
			{
				error = std::string( "UC_CTL_X86_MICROCODE: " ) + uc_strerror( ue );
				e->log_line( "FAILED: " + error );
				return nullptr;
			}
			e->log_line( "microcode revision / platform ID from the profile's \"#@ microcode\" line installed (U1473; value not printed)" );
		}

		// U1973: the platform settings (the engine initialises with the first of them), then XCR0 / CR0 / XINUSE model
		if ( cfg.smm_revision &&
			 !step( uc_ctl( e->uc_, UC_CTL_WRITE( UC_CTL_X86_SMM_REVISION, 1 ), cfg.smm_revision ), "UC_CTL_X86_SMM_REVISION " + hex( cfg.smm_revision ) ) )
			return nullptr;
		if ( cfg.feature_control )
		{
			uc_x86_msr m{ 0x3a, cfg.feature_control };
			if ( !step( uc_reg_write( e->uc_, UC_X86_REG_MSR, &m ), "IA32_FEATURE_CONTROL " + hex( cfg.feature_control ) ) ) return nullptr;
		}
		if ( cfg.xcr0 && !step( uc_reg_write( e->uc_, UC_X86_REG_XCR0, &cfg.xcr0 ), "XCR0 " + hex( cfg.xcr0 ) ) ) return nullptr;
		if ( cfg.cr0 && !step( uc_reg_write( e->uc_, UC_X86_REG_CR0, &cfg.cr0 ), "CR0 " + hex( cfg.cr0 ) + " (U1962: refused where MOV to CR0 would #GP)" ) ) return nullptr;
		if ( cfg.xinuse_model &&
			 !step( uc_ctl( e->uc_, UC_CTL_WRITE( UC_CTL_X86_XINUSE_MODEL, 1 ), cfg.xinuse_model ), "UC_CTL_X86_XINUSE_MODEL " + std::to_string( cfg.xinuse_model ) ) )
			return nullptr;

		// 5. the reset state, read back (the first register access initialises the engine)
		reg::dump d;
		std::string rerr;
		if ( !reg::read( e->uc_, d, rerr ) )
		{
			error = "reset state: " + rerr;
			e->log_line( "FAILED: " + error );
			return nullptr;
		}
		uint64_t rip = 0, rsp = 0, rfl = 0, cr0 = 0, cr4 = 0, xcr0 = 0;
		reg::get_u64( d, "rip", rip );
		reg::get_u64( d, "rsp", rsp );
		reg::get_u64( d, "rflags", rfl );
		reg::get_u64( d, "cr0", cr0 );
		reg::get_u64( d, "cr4", cr4 );
		reg::get_u64( d, "xcr0", xcr0 );
		int strict = -1, xin = -1, rmode = -1;
		size_t rows = 0;
		uint64_t rseed = 0;
		uc_ctl_get_x86_cpuid_strict( e->uc_, &strict );
		uc_ctl_get_x86_cpuid_count( e->uc_, &rows );
		uc_ctl( e->uc_, UC_CTL_READ( UC_CTL_X86_XINUSE_MODEL, 1 ), &xin );
		uc_ctl_get_x86_rdrand( e->uc_, &rmode, &rseed );
		e->log_line( "reset state (fork values): rip " + hex( rip ) + ", rsp " + hex( rsp ) + ", rflags " + hex( rfl ) + " (" +
					 reg::decode_rflags( rfl ) + "), cr0 " + hex( cr0 ) + ", cr4 " + hex( cr4 ) + ", xcr0 " + hex( xcr0 ) );
		e->log_line( "engine controls read back: cpuid rows " + std::to_string( rows ) + ", strict " + std::to_string( strict ) +
					 ", rdrand mode " + std::to_string( rmode ) + " (0 seeded, 1 host) seed " + hex( rseed ) + ", XINUSE model " +
					 std::to_string( xin ) + " (1 P-core, 2 E-core, 3 always)" );
		e->log_line( "engine open: no memory mapped yet; at most " + hex( cfg.max_mapped_bytes ) + " bytes of guest memory, " +
					 hex( max_io_bytes ) + " bytes per read / write; emulate() never uses a host-time timeout" );
		return e;
	}

	engine::~engine()
	{
		if ( uc_ ) uc_close( uc_ );
		uc_ = nullptr;
	}

	uc_err engine::install_cpuid_profile( const std::vector<uc_x86_cpuid>& leaves )
	{
		const uc_err ue = uc_ctl_set_x86_cpuid( uc_, leaves.empty() ? nullptr : leaves.data(), leaves.size() );
		log_line( "install_cpuid_profile(" + std::to_string( leaves.size() ) + " rows) after open, " +
				  ( ran_ ? "after guest code ran" : "before the first instruction" ) + ": " + uc_strerror( ue ) +
				  ( ue == UC_ERR_ARG ? " (a strict profile only at uc_open, plan 1.J.47.a / fork U1580)" : "" ) );
		return ue;
	}

	bool engine::map( uint64_t base, uint64_t size, uint32_t prot, const std::string& name, std::string& error )
	{
		const std::string what = "map " + name + " [" + hex( base ) + ", +" + hex( size ) + ") " + prot_text( prot );
		auto refuse = [ & ]( const std::string& why ) {
			error = what + ": " + why;
			log_line( "REFUSED " + error );
			return false;
		};
		if ( size == 0 ) return refuse( "size 0" );
		if ( ( base | size ) & ( page_size - 1 ) ) return refuse( "base and size must be multiples of 4 KiB" );
		const uint64_t last = base + ( size - 1 );   // inclusive end: a region may end exactly at 2^64
		if ( last < base ) return refuse( "end above 2^64 (overflow)" );
		if ( prot & ~uint32_t( UC_PROT_ALL ) ) return refuse( "unknown protection bits" );
		if ( size > cfg_.max_mapped_bytes || mapped_bytes_ > cfg_.max_mapped_bytes - size )
			return refuse( "more than max_mapped_bytes " + hex( cfg_.max_mapped_bytes ) + " in total" );
		for ( const auto& r : regions_ )
			if ( base <= r.base + ( r.size - 1 ) && r.base <= last ) return refuse( "overlaps " + r.name );
		const uc_err ue = uc_mem_map( uc_, base, size, prot );
		if ( ue != UC_ERR_OK ) return refuse( std::string( "uc_mem_map: " ) + uc_strerror( ue ) );
		mapped_region r{ base, size, prot, name };
		regions_.insert( std::upper_bound( regions_.begin(), regions_.end(), base, []( uint64_t b, const mapped_region& x ) { return b < x.base; } ), r );
		mapped_bytes_ += size;
		log_line( what + " ok" );
		return true;
	}

	bool engine::unmap( uint64_t base, uint64_t size, std::string& error )
	{
		for ( auto it = regions_.begin(); it != regions_.end(); ++it )
			if ( it->base == base && it->size == size )
			{
				const uc_err ue = uc_mem_unmap( uc_, base, size );
				if ( ue != UC_ERR_OK )
				{
					error = "unmap " + it->name + ": uc_mem_unmap: " + uc_strerror( ue );
					log_line( "FAILED " + error );
					return false;
				}
				log_line( "unmap " + it->name + " [" + hex( base ) + ", +" + hex( size ) + ") ok" );
				mapped_bytes_ -= size;
				regions_.erase( it );
				return true;
			}
		error = "unmap [" + hex( base ) + ", +" + hex( size ) + "): not exactly one mapped region";
		log_line( "REFUSED " + error );
		return false;
	}

	// U2006 (plan 3.M5): sub-range protection / removal and the guest-pointer probe of the emulated kernel layer
	bool engine::page_range( uint64_t base, uint64_t size, const char* what, std::string& error ) const
	{
		if ( size == 0 || ( ( base | size ) & ( page_size - 1 ) ) )
		{
			error = std::string( what ) + " [" + hex( base ) + ", +" + hex( size ) + "): base and size must be non-zero multiples of 4 KiB";
			return false;
		}
		if ( base + ( size - 1 ) < base )
		{
			error = std::string( what ) + " [" + hex( base ) + ", +" + hex( size ) + "): wraps around 2^64";
			return false;
		}
		std::string why;
		if ( size > max_io_bytes ? false : !inside_mapped( base, size, why ) )
		{
			error = std::string( what ) + ": " + why;
			return false;
		}
		if ( size > max_io_bytes )   // inside_mapped bounds one I/O call; walk the regions for a large range
		{
			uint64_t cur = base;
			const uint64_t last = base + ( size - 1 );
			for ( ;; )
			{
				const mapped_region* hit = nullptr;
				for ( const auto& r : regions_ )
					if ( cur >= r.base && cur - r.base < r.size ) hit = &r;
				if ( !hit )
				{
					error = std::string( what ) + ": " + hex( cur ) + " is not mapped";
					return false;
				}
				const uint64_t hl = hit->base + ( hit->size - 1 );
				if ( last <= hl ) break;
				cur = hl + 1;
			}
		}
		return true;
	}

	bool engine::protect( uint64_t base, uint64_t size, uint32_t prot, std::string& error )
	{
		if ( prot & ~uint32_t( UC_PROT_ALL ) )
		{
			error = "protect: unknown protection bits";
			return false;
		}
		if ( !page_range( base, size, "protect", error ) ) return false;
		const uc_err ue = uc_mem_protect( uc_, base, size, prot );
		if ( ue != UC_ERR_OK )
		{
			error = std::string( "protect: uc_mem_protect: " ) + uc_strerror( ue );
			log_line( "FAILED " + error );
			return false;
		}
		const uint64_t last = base + ( size - 1 );
		std::vector<mapped_region> out;
		for ( const auto& r : regions_ )
		{
			const uint64_t rl = r.base + ( r.size - 1 );
			if ( rl < base || r.base > last )
			{
				out.push_back( r );
				continue;
			}
			if ( r.base < base ) out.push_back( { r.base, base - r.base, r.prot, r.name } );
			const uint64_t mb = std::max( r.base, base ), ml = std::min( rl, last );
			out.push_back( { mb, ml - mb + 1, prot, r.name } );
			if ( rl > last ) out.push_back( { last + 1, rl - last, r.prot, r.name } );
		}
		regions_ = out;
		log_line( "protect [" + hex( base ) + ", +" + hex( size ) + ") " + prot_text( prot ) + " ok" );
		return true;
	}

	bool engine::unmap_range( uint64_t base, uint64_t size, std::string& error )
	{
		if ( !page_range( base, size, "unmap_range", error ) ) return false;
		const uc_err ue = uc_mem_unmap( uc_, base, size );
		if ( ue != UC_ERR_OK )
		{
			error = std::string( "unmap_range: uc_mem_unmap: " ) + uc_strerror( ue );
			log_line( "FAILED " + error );
			return false;
		}
		const uint64_t last = base + ( size - 1 );
		std::vector<mapped_region> out;
		for ( const auto& r : regions_ )
		{
			const uint64_t rl = r.base + ( r.size - 1 );
			if ( rl < base || r.base > last )
			{
				out.push_back( r );
				continue;
			}
			if ( r.base < base ) out.push_back( { r.base, base - r.base, r.prot, r.name } );
			if ( rl > last ) out.push_back( { last + 1, rl - last, r.prot, r.name } );
		}
		regions_ = out;
		mapped_bytes_ -= size;
		log_line( "unmap_range [" + hex( base ) + ", +" + hex( size ) + ") ok" );
		return true;
	}

	bool engine::accessible( uint64_t address, uint64_t length, uint32_t need ) const
	{
		if ( length == 0 ) return true;
		const uint64_t last = address + ( length - 1 );
		if ( last < address ) return false;
		uint64_t cur = address;
		for ( ;; )
		{
			const mapped_region* hit = nullptr;
			for ( const auto& r : regions_ )
				if ( cur >= r.base && cur - r.base < r.size ) hit = &r;
			if ( !hit || ( hit->prot & need ) != need ) return false;
			const uint64_t hl = hit->base + ( hit->size - 1 );
			if ( last <= hl ) return true;
			cur = hl + 1;
		}
	}

	bool engine::check_regions( std::string& error ) const
	{
		uc_mem_region* r = nullptr;
		uint32_t n = 0;
		const uc_err ue = uc_mem_regions( uc_, &r, &n );
		if ( ue != UC_ERR_OK )
		{
			error = std::string( "uc_mem_regions: " ) + uc_strerror( ue );
			return false;
		}
		std::vector<mapped_region> u;
		for ( uint32_t i = 0; i < n; i++ ) u.push_back( { r[ i ].begin, r[ i ].end - r[ i ].begin + 1, r[ i ].perms, "" } );
		uc_free( r );
		std::sort( u.begin(), u.end(), []( const mapped_region& a, const mapped_region& b ) { return a.base < b.base; } );
		if ( u.size() != regions_.size() )
		{
			error = "Unicorn has " + std::to_string( u.size() ) + " region(s), the engine table " + std::to_string( regions_.size() );
			return false;
		}
		for ( size_t i = 0; i < u.size(); i++ )
			if ( u[ i ].base != regions_[ i ].base || u[ i ].size != regions_[ i ].size || u[ i ].prot != regions_[ i ].prot )
			{
				error = "region " + regions_[ i ].name + " differs from Unicorn's [" + hex( u[ i ].base ) + ", +" + hex( u[ i ].size ) + ") " +
						prot_text( u[ i ].prot );
				return false;
			}
		return true;
	}

	bool engine::inside_mapped( uint64_t address, uint64_t length, std::string& error ) const
	{
		if ( length > max_io_bytes )
		{
			error = "length " + hex( length ) + " above max_io_bytes " + hex( max_io_bytes );
			return false;
		}
		if ( length == 0 ) return true;
		const uint64_t last = address + ( length - 1 );   // inclusive: the last byte may be 2^64 - 1
		if ( last < address )
		{
			error = "[" + hex( address ) + ", +" + hex( length ) + ") wraps around 2^64";
			return false;
		}
		uint64_t cur = address;
		for ( ;; )
		{
			const mapped_region* hit = nullptr;
			for ( const auto& r : regions_ )
				if ( cur >= r.base && cur - r.base < r.size ) hit = &r;
			if ( !hit )
			{
				error = "[" + hex( address ) + ", +" + hex( length ) + "): " + hex( cur ) + " is not mapped";
				return false;
			}
			const uint64_t hit_last = hit->base + ( hit->size - 1 );
			if ( last <= hit_last ) return true;
			cur = hit_last + 1;   // no wrap: hit_last < last <= 2^64 - 1
		}
	}

	bool engine::read( uint64_t address, void* out, uint64_t length, std::string& error ) const
	{
		if ( length == 0 ) return true;
		if ( !out )
		{
			error = "read: no buffer";
			return false;
		}
		if ( !inside_mapped( address, length, error ) ) return false;
		const uc_err ue = uc_mem_read( uc_, address, out, size_t( length ) );
		if ( ue != UC_ERR_OK )
		{
			error = std::string( "uc_mem_read: " ) + uc_strerror( ue );
			return false;
		}
		return true;
	}

	bool engine::write( uint64_t address, const void* in, uint64_t length, std::string& error )
	{
		if ( length == 0 ) return true;
		if ( !in )
		{
			error = "write: no buffer";
			return false;
		}
		if ( !inside_mapped( address, length, error ) ) return false;
		const uc_err ue = uc_mem_write( uc_, address, in, size_t( length ) );
		if ( ue != UC_ERR_OK )
		{
			error = std::string( "uc_mem_write: " ) + uc_strerror( ue );
			return false;
		}
		return true;
	}

	std::vector<std::string> engine::describe_memory_map() const
	{
		std::vector<std::string> out;
		out.push_back( "memory map: " + std::to_string( regions_.size() ) + " region(s), " + hex( mapped_bytes_ ) + " bytes" );
		for ( const auto& r : regions_ )
			out.push_back( "  [" + hex( r.base ) + ", " + hex( r.base + ( r.size - 1 ) ) + "] " + prot_text( r.prot ) + " " + r.name );
		return out;
	}

	uc_err engine::emulate( uint64_t begin, uint64_t until, uint64_t count )
	{
		// timeout 0: never host time (AGENT_RULES "Host safety"); count bounds the run (0 = until / a stop)
		const uc_err ue = uc_emu_start( uc_, begin, until, 0, size_t( count ) );
		ran_ = true;
		uint64_t rip = 0;
		uc_reg_read( uc_, UC_X86_REG_RIP, &rip );
		log_line( "emulate [" + hex( begin ) + " .. " + hex( until ) + "), count " + std::to_string( count ) + ": " + uc_strerror( ue ) +
				  ", rip " + hex( rip ) );
		return ue;
	}
}
