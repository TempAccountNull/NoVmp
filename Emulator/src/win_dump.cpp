// P57 (U2373, plan 5.C 8.4 / 8.5): the OEP dump of an emu-oep run - process::call_stack and process::write_dump (see include/emu/win_env.hpp).
// Kept out of win_env.cpp (a file that serves guest requests: check_no_passthrough GUEST_SERVING): this code runs once at the stop, never
// on behalf of the guest. It writes only through the audited writer emu::output_file, into the existing folder of --dump-dir. A sample's
// dump is a derivative of the sample: it stays local (scratch / a local folder outside git) and is only read as data - never loaded,
// mapped executable or run, never opened in a debugger.
#include "win_env_hooks.hpp"

#include <emu/output_file.hpp>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace emu::win
{
	namespace
	{
		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		uint64_t read_reg( uc_engine* uc, int id )
		{
			uint64_t v = 0;
			uc_reg_read( uc, id, &v );
			return v;
		}
	}

	// ------------------------------------------------------------------------------------------------ P57 (U2373): the OEP dump
	// "name+off" of ADDRESS by the nearest export at or below it in its module ("" outside the modules / before the first export)
	namespace
	{
		// the modules for naming / unwinding: the process' list (the image + the system images the environment mapped) and every image view
		// the --Sys loader mapped later (an engine region "<name>.dll headers" - NtMapViewOfSection of a SEC_IMAGE section; not in that list),
		// parsed from the capture's file of that name (the .pdata RVA, the SizeOfImage, the named exports)
		std::vector<module_image> all_modules( const std::vector<module_image>& mods, const engine& e, const environment_facts& f, std::vector<std::string>* notes )
		{
			std::vector<module_image> out = mods;
			for ( const auto& rg : e.regions() )
			{
				const auto k = rg.name.rfind( " headers" );
				if ( k == std::string::npos || k + 8 != rg.name.size() ) continue;
				std::string n = rg.name.substr( 0, k );
				for ( auto& ch : n ) ch = char( std::tolower( uint8_t( ch ) ) );
				bool have = false;
				for ( const auto& m : out ) have = have || m.base == rg.base;
				if ( have || !f.files || !f.files->count( n ) ) continue;
				const auto& file = f.files->at( n );
				module_image m;
				std::string perr;
				if ( !pe::parse( file, m.img, perr ) )
				{
					if ( notes ) notes->push_back( "module " + n + " at " + hex( rg.base ) + ": the capture's file does not parse (" + perr + ")" );
					continue;
				}
				m.name = n;
				m.base = rg.base;
				m.size = ( uint64_t( m.img.size_of_image ) + 0xFFF ) & ~0xFFFull;
				m.original_base = m.img.image_base;
				m.entry = m.img.entry_rva ? m.base + m.img.entry_rva : 0;
				m.is_dll = true;
				std::vector<pe::export_entry> ex;
				if ( pe::exports( file, m.img, ex, perr ) )
					for ( const auto& x : ex )
						if ( !x.forwarder && !x.name.empty() ) m.exports_by_rva.emplace( x.rva, x.name );
				out.push_back( std::move( m ) );
			}
			return out;
		}

		std::string export_near( const std::vector<module_image>& mods, uint64_t a )
		{
			for ( const auto& m : mods )
				if ( a >= m.base && a - m.base < m.size && !m.exports_by_rva.empty() )
				{
					const uint32_t rva = uint32_t( a - m.base );
					auto it = m.exports_by_rva.upper_bound( rva );
					if ( it == m.exports_by_rva.begin() ) return "";
					--it;
					return m.name + "!" + it->second + ( rva != it->first ? "+" + hex( rva - it->first ) : std::string() );
				}
			return "";
		}
	}

	// The call stack at the stop: the x64 unwind (virtual_unwind, win_exception.cpp: UNWIND_INFO of the module's IMAGE_DIRECTORY_ENTRY_EXCEPTION,
	// Microsoft Learn "x64 exception handling") from the final registers - the image's .pdata as its FILE header names it (a packed image may
	// not describe its code: the walk then ends there); a function without an entry is a leaf (RIP = [RSP], RSP + 8). Then a scan of the
	// stack's first 1000H bytes for return addresses: a qword into a module whose preceding bytes are a CALL (E8 rel32; FF /2: FF 15 disp32,
	// FF D0+r, 41 FF D0+r, FF 50+r disp8, FF 90+r disp32 - 253666-093 Vol2A CALL). Text only.
	std::vector<std::string> process::call_stack( const run_result& r )
	{
		std::vector<std::string> o;
		if ( !r.have_final )
		{
			o.push_back( "call stack: no final register state" );
			return o;
		}
		uc_engine* uc = e_->handle();
		const std::vector<module_image> mods = all_modules( modules_, *e_, facts_, &o );
		static const int ids[ 16 ] = { UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX, UC_X86_REG_RSP, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
									   UC_X86_REG_R8,  UC_X86_REG_R9,  UC_X86_REG_R10, UC_X86_REG_R11, UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15 };
		uint64_t g[ 16 ] = {};
		for ( int i = 0; i < 16; i++ ) g[ i ] = read_reg( uc, ids[ i ] );
		uint64_t rip = read_reg( uc, UC_X86_REG_RIP ), stack_base = 0, stack_limit = 0;
		const uint64_t rsp0 = g[ 4 ];
		std::string err;
		e_->read( guest_teb_va + layout::teb_stack_base, &stack_base, 8, err );
		e_->read( guest_teb_va + layout::teb_stack_limit, &stack_limit, 8, err );
		o.push_back( "call stack (x64 unwind through each module's .pdata; a function without an entry = a leaf: RIP = [RSP]); the stack [" + hex( stack_limit ) + ", " +
					 hex( stack_base ) + ")" );
		for ( int depth = 0; depth < 64; depth++ )
		{
			const std::string ex = export_near( mods, rip );
			o.push_back( "  #" + std::to_string( depth ) + " rip " + hex( rip ) + " " + where( rip ) + ( ex.empty() ? "" : " (" + ex + ")" ) + ", rsp " + hex( g[ 4 ] ) );
			if ( rip == guest_return_sentinel )
			{
				o.push_back( "  the walk ends: the return sentinel (the bottom of the thread)" );
				break;
			}
			const module_image* img = nullptr;
			for ( const auto& md : mods )
				if ( rip >= md.base && rip - md.base < md.size ) img = &md;
			if ( !img )
			{
				o.push_back( "  the walk ends: rip outside every module" );
				break;
			}
			const auto& ed = img->img.dirs[ 3 ];   // IMAGE_DIRECTORY_ENTRY_EXCEPTION (winnt.h)
			uint64_t fe = 0;
			uint32_t rf[ 3 ] = {};
			const uint32_t rva = uint32_t( rip - img->base );
			for ( uint32_t lo = 0, hi = ed.size / 12; lo < hi; )
			{
				const uint32_t mid = ( lo + hi ) / 2;
				if ( !e_->read( img->base + ed.rva + 12ull * mid, rf, 12, err ) ) break;
				if ( rva < rf[ 0 ] ) hi = mid;
				else if ( rva >= rf[ 1 ] ) lo = mid + 1;
				else
				{
					fe = img->base + ed.rva + 12ull * mid;
					break;
				}
			}
			if ( !fe )
			{
				uint64_t ra = 0;
				if ( !e_->read( g[ 4 ], &ra, 8, err ) )
				{
					o.push_back( "  the walk ends: [RSP] " + hex( g[ 4 ] ) + " not readable" );
					break;
				}
				o.push_back( "    (no RUNTIME_FUNCTION for rva " + hex( rva ) + " in " + img->name + ": a leaf, RIP = [RSP])" );
				rip = ra;
				g[ 4 ] += 8;
				continue;
			}
			uint64_t handler = 0, hdata = 0, est = 0;
			std::string why;
			if ( !virtual_unwind( img->base, rip, fe, g, rip, handler, hdata, est, why ) )
			{
				o.push_back( "  the walk ends: the unwind of " + img->name + " rva " + hex( rva ) + ": " + why );
				break;
			}
			if ( g[ 4 ] < stack_limit || g[ 4 ] > stack_base )
			{
				o.push_back( "  the walk ends: RSP " + hex( g[ 4 ] ) + " outside the stack" );
				break;
			}
		}
		o.push_back( "stack scan [" + hex( rsp0 ) + ", +1000H): qwords into a module right after a CALL" );
		for ( auto& l : return_scan( rsp0, 0x1000, 64 ) ) o.push_back( l );
		return o;
	}

	// P58 (U2383): the return addresses on the stack [RSP, +SPAN) (below the TEB's StackBase): qwords into a module whose preceding bytes are
	// a CALL (E8 rel32; FF /2 forms - 253666-093 Vol2A CALL); the module list (all_modules) is cached until the region table changes
	std::vector<std::string> process::return_scan( uint64_t rsp0, uint64_t span, size_t max_lines )
	{
		std::vector<std::string> o;
		std::string err;
		if ( scan_regions_ != e_->regions().size() || scan_mods_.empty() )
		{
			scan_mods_ = all_modules( modules_, *e_, facts_, nullptr );
			scan_regions_ = e_->regions().size();
		}
		const auto& mods = scan_mods_;
		uint64_t stack_base = 0;
		e_->read( guest_teb_va + layout::teb_stack_base, &stack_base, 8, err );
		size_t found = 0;
		for ( uint64_t a = rsp0; a < rsp0 + span && a + 8 <= stack_base; a += 8 )
		{
			uint64_t v = 0;
			if ( !e_->read( a, &v, 8, err ) ) break;
			bool in_mod = false;
			for ( const auto& md : mods )
				if ( v >= md.base + 7 && v - md.base < md.size ) in_mod = true;
			if ( !in_mod ) continue;
			uint8_t b[ 7 ] = {};
			if ( !e_->read( v - 7, b, 7, err ) ) continue;   // b[ 6 ] = the byte before V
			std::string call;
			if ( b[ 2 ] == 0xE8 ) call = "E8 rel32";
			else if ( b[ 1 ] == 0xFF && b[ 2 ] == 0x15 ) call = "FF 15 disp32";
			else if ( b[ 5 ] == 0xFF && ( b[ 6 ] & 0xF8 ) == 0xD0 ) call = "FF D0+r";
			else if ( b[ 4 ] == 0x41 && b[ 5 ] == 0xFF && ( b[ 6 ] & 0xF8 ) == 0xD0 ) call = "41 FF D0+r";
			else if ( b[ 4 ] == 0xFF && ( b[ 5 ] & 0xF8 ) == 0x50 ) call = "FF 50+r disp8";
			else if ( b[ 1 ] == 0xFF && ( b[ 2 ] & 0xF8 ) == 0x90 ) call = "FF 90+r disp32";
			if ( call.empty() ) continue;
			const std::string ex = export_near( mods, v );
			o.push_back( "  [rsp+" + hex( a - rsp0 ) + "] " + hex( v ) + " " + where( v ) + ( ex.empty() ? "" : " (" + ex + ")" ) + " after " + call );
			if ( ++found >= max_lines ) break;
		}
		if ( !found ) o.push_back( "  (none)" );
		return o;
	}

	// process_options::dump_dir: the dump at the stop, as DATA files in the existing folder (nothing is created but these files, through the
	// audited writer emu::output_file; the folder must exist). A sample's dump is a derivative of the sample: it stays local (scratch / a
	// local folder outside git) and is only read as data - never loaded, mapped executable or run, never opened in a debugger.
	//   oep_summary.txt          the stop, the instruction counts, the image's place, the written pages per region
	//   registers.txt            the full register file (reg::format: one "name = value" line per field; RFLAGS decoded)
	//   call_stack.txt           process::call_stack
	//   memory_map.txt           the engine's regions + the kernel layer's views / private allocations
	//   image.bin                [ImageBase, + SizeOfImage) as in memory (an unreadable page = zeros, listed in oep_summary.txt)
	//   other_memory.bin / .txt  the private memory (private allocations, the stack, PEB, TEB) concatenated + its index (base, size, offset)
	//   modules.txt              the modules (name, base, size, the image's original base, the export count)
	//   image_pointer_writes.txt the image's 8-byte guest writes of module-area values (the last per address; with the export named)
	//   loader_lookups.txt       the LdrGetProcedureAddressForCaller / LdrGetDllHandleEx / LdrLoadDll calls with their results
	bool process::write_dump( const run_result& r, std::vector<std::string>& lines, std::string& error )
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		if ( po_.dump_dir.empty() || !fs::is_directory( fs::path( po_.dump_dir ), ec ) )
		{
			error = "--dump-dir " + po_.dump_dir + ": not an existing folder (emu-oep creates no folder)";
			return false;
		}
		auto put = [ & ]( const std::string& name, const std::string& data ) -> bool {
			output_file f;
			std::string e2;
			const std::string path = ( fs::path( po_.dump_dir ) / name ).string();
			if ( !f.create( path, po_.dump_max_bytes, e2 ) )
			{
				error = e2;
				return false;
			}
			const bool ok = f.write( data );
			f.close();
			lines.push_back( "dump: " + path + " (" + std::to_string( data.size() ) + " bytes" + ( ok ? "" : ", TRUNCATED at the size bound" ) + ")" );
			return true;
		};
		auto join = []( const std::vector<std::string>& v ) {
			std::string s;
			for ( const auto& l : v ) s += l + "\n";
			return s;
		};
		std::string err;
		std::vector<std::string> mnotes;
		const std::vector<module_image> mods = all_modules( modules_, *e_, facts_, &mnotes );
		// the image as in memory
		std::string image( size_t( map_.size ), '\0' );
		std::vector<std::string> unreadable;
		for ( uint64_t off = 0; off < map_.size; off += page_size )
			if ( !e_->read( map_.base + off, image.data() + off, page_size, err ) ) unreadable.push_back( hex( map_.base + off ) );
		// the summary
		std::vector<std::string> s;
		s.push_back( std::string( "stop: " ) + name_of( r.stop ) + " - " + r.detail );
		s.push_back( "instructions counted: " + std::to_string( r.insns ) + ( r.stop == stop_kind::oep_stop ? " (the last one = the OEP instruction, not executed)" : "" ) );
		if ( r.stop == stop_kind::oep_stop )
		{
			s.push_back( "OEP: rip " + hex( r.oep_rip ) + " " + where( r.oep_rip ) + " = image RVA " + hex( r.oep_rip - map_.base ) + " (" + r.oep_how + ")" );
			if ( r.oep_page_writes )
				s.push_back( "OEP page: first guest write at instruction " + std::to_string( r.oep_page_first_insn ) + " (rip " + hex( r.oep_page_first_rip ) + " " +
							 where( r.oep_page_first_rip ) + "), " + std::to_string( r.oep_page_writes ) + " writes" );
		}
		s.push_back( "system calls handled: " + std::to_string( r.syscalls_handled ) + ", SYSCALL instructions: " + std::to_string( r.syscall_insns ) );
		s.push_back( "image: base " + hex( map_.base ) + ", size " + hex( map_.size ) + " (SizeOfImage rounded to 4 KiB), the file's entry RVA " + hex( img_.entry_rva ) +
					 ", ImageBase " + hex( img_.image_base ) + ( map_.relocated ? " (relocated)" : "" ) );
		s.push_back( "image pages not readable (zeros in image.bin): " + std::to_string( unreadable.size() ) );
		for ( const auto& u : unreadable ) s.push_back( "  " + u );
		s.push_back( "guest-written pages: " + std::to_string( hc_->written.size() ) );
		{
			std::map<std::string, std::tuple<uint64_t, uint64_t, uint64_t>> per;   // region -> pages, writes, the first write's instruction
			for ( const auto& [ pg, w ] : hc_->written )
			{
				std::string rn = "[unmapped]";
				for ( const auto& rg : e_->regions() )
					if ( ( pg << 12 ) >= rg.base && ( pg << 12 ) - rg.base < rg.size ) rn = rg.name;
				auto& t = per[ rn ];
				if ( !std::get<0>( t ) || w.first_insn < std::get<2>( t ) ) std::get<2>( t ) = w.first_insn;
				std::get<0>( t )++;
				std::get<1>( t ) += w.count;
			}
			for ( const auto& [ rn, t ] : per )
				s.push_back( "  " + rn + ": " + std::to_string( std::get<0>( t ) ) + " pages, " + std::to_string( std::get<1>( t ) ) + " writes, first at instruction " +
							 std::to_string( std::get<2>( t ) ) );
		}
		if ( !put( "oep_summary.txt", join( s ) ) ) return false;
		// the registers
		std::vector<std::string> rl;
		if ( r.have_final )
		{
			rl = reg::format( r.final );
			uint64_t fl = 0;
			if ( reg::get_u64( r.final, "rflags", fl ) ) rl.push_back( "; rflags decoded: " + reg::decode_rflags( fl ) );
		}
		else
			rl.push_back( "; no final register state" );
		if ( !put( "registers.txt", join( rl ) ) ) return false;
		if ( !put( "call_stack.txt", join( call_stack( r ) ) ) ) return false;
		std::vector<std::string> mm = e_->describe_memory_map();
		if ( kernel_ )
			for ( auto& l : kernel_->describe() ) mm.push_back( l );
		if ( !put( "memory_map.txt", join( mm ) ) ) return false;
		if ( !put( "image.bin", image ) ) return false;
		image.clear();
		image.shrink_to_fit();
		// the private memory
		std::string other;
		std::vector<std::string> idx = { "; base size offset(in other_memory.bin) protection name" };
		for ( const auto& rg : e_->regions() )
		{
			const bool pick = rg.name.rfind( "private allocation", 0 ) == 0 || rg.name.rfind( "stack", 0 ) == 0 || rg.name == "PEB" || rg.name.rfind( "TEB", 0 ) == 0;
			if ( !pick || rg.base >= hc_->img_lo && rg.base < hc_->img_hi ) continue;
			if ( other.size() + rg.size > po_.dump_max_bytes )
			{
				idx.push_back( "; " + hex( rg.base ) + " " + hex( rg.size ) + " skipped: the file's size bound" );
				continue;
			}
			std::string part( size_t( rg.size ), '\0' );
			if ( !e_->read( rg.base, part.data(), rg.size, err ) )
			{
				idx.push_back( "; " + hex( rg.base ) + " " + hex( rg.size ) + " not readable: " + err );
				continue;
			}
			idx.push_back( hex( rg.base ) + " " + hex( rg.size ) + " " + hex( other.size() ) + " " + std::string( rg.prot & UC_PROT_READ ? "r" : "-" ) +
						   ( rg.prot & UC_PROT_WRITE ? "w" : "-" ) + ( rg.prot & UC_PROT_EXEC ? "x" : "-" ) + " " + rg.name );
			other += part;
		}
		if ( !put( "other_memory.bin", other ) || !put( "other_memory.txt", join( idx ) ) ) return false;
		// the modules
		std::vector<std::string> ml = { "; name base size original_base exports is_dll" };
		for ( const auto& m : mods )
			ml.push_back( m.name + " " + hex( m.base ) + " " + hex( m.size ) + " " + hex( m.original_base ) + " " + std::to_string( m.exports_by_rva.size() ) + " " +
						  ( m.is_dll ? "dll" : "image" ) );
		for ( const auto& l : mnotes ) ml.push_back( "; " + l );
		if ( !put( "modules.txt", join( ml ) ) ) return false;
		// the image's pointer writes
		std::vector<std::string> pw = { "; address (image RVA) value module!export last-write-instruction last-write-rip writes" };
		for ( const auto& [ a, w ] : hc_->image_ptr_writes )
		{
			const std::string ex = export_near( mods, w.value );
			pw.push_back( hex( a ) + " (rva " + hex( a - map_.base ) + ") " + hex( w.value ) + " " + ( ex.empty() ? where( w.value ) : ex ) + " insn " + std::to_string( w.insn ) + " rip " +
						  hex( w.rip ) + " " + where( w.rip ) + " writes " + std::to_string( w.count ) );
		}
		if ( !put( "image_pointer_writes.txt", join( pw ) ) ) return false;
		std::vector<std::string> lk = hc_->res_records;
		lk.insert( lk.begin(), "; " + std::to_string( hc_->res_records.size() ) + " loader look-up(s) (" + std::to_string( hc_->res_hooks.size() ) + " ntdll exports hooked)" +
								   ( hc_->res_waits.empty() ? "" : "; " + std::to_string( hc_->res_waits.size() ) + " call(s) had not returned at the stop" ) );
		if ( !put( "loader_lookups.txt", join( lk ) ) ) return false;
		return true;
	}
}
