// emu::nt::kernel - P44 (USER DECISION 2026-10-27 STEP 3; ledger U2118-U2123): user32's start in an ordinary GUI program - its CSR
// server connection (the USERSRV request), the win32k services of the start (the second system-service table: the numbers the capture's
// win32u.dll stubs load, U2117), the GUI conversion of the process / thread and the kernel -> user callback it makes.
//
// Method (as P39-P43): the requests and the bytes user32 / gdi32 / ntdll read are learned from the emulated trace of the REAL user32 /
// gdi32full / ntdll code (P44 explorations x1-x11, labelled and removed), the answers are MEASURED from inside our own GUI test PE
// tm_u32 (it links user32 like any GUI program, started normally; Windows' own user32 code makes the connection - no message is hand-
// built, csrss is never opened, NtAlpc* / NtConnectPort / CsrClientCallServer are never called by it; it reads only its own memory after
// its start: Emulator/data/nt_measured_vmware_guest.txt u32.*, notes\eread\P44_user32_csr.md). Every machine / session / user value is
// replaced by a placeholder of the same shape; nothing is forwarded to the host, nothing is sent to csrss; an unmeasured input stops, an
// unmeasured byte of a modelled region stops when the guest reads it (the watch, U2122). Layout names / the callback mechanism / the GUI
// conversion: ReactOS 0.4.16 (reference only, cited per item).
#include <emu/nt_kernel.hpp>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace emu::nt
{
	namespace
	{
		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		reply stop_( const std::string& why )
		{
			reply r;
			r.kind = reply_kind::stop;
			r.text = why;
			return r;
		}

		reply done_( uint32_t st, const std::string& text )
		{
			reply r;
			r.kind = reply_kind::done;
			r.status = st;
			r.text = text;
			return r;
		}

		// ---- the measured shapes (tm_nt u32.*; the guest's user32.dll 10.0.19044.7807, public symbols E:\sources\ms_symbols) ----------
		// placeholder bases of the four views inside the watched window (the real ones are ASLR addresses; tm_u32 prints only relations)
		constexpr uint64_t si_base = win32k_view_window;                  // R1: the user shared-info section view (psi, pDispInfo, abMsgs)
		constexpr uint64_t si_size = 0x8000, si_committed = 0x4000;       // u32.region.R1: 8000H, 4000H committed, PAGE_READONLY, MEM_MAPPED
		constexpr uint64_t he_base = win32k_view_window + 0x100000;       // R2: the user handle table view (aheList)
		constexpr uint64_t he_size = 0x200000, he_committed = 0x23000;    // u32.region.R2
		constexpr uint64_t gdi_base = win32k_view_window + 0x1000000;     // R3: the GDI shared memory (PEB GdiSharedHandleTable)
		constexpr uint64_t gdi_size = 0x181000;                           // u32.region.R3: all committed
		constexpr uint64_t desk_base = win32k_view_window + 0x2000000;    // R4: the desktop heap view (CLIENTINFO pDeskInfo)
		constexpr uint64_t desk_size = 0x1401000, desk_committed = 0x199000;   // u32.region.R4 (the committed size: the first measurement's, a snapshot)
		constexpr uint64_t psi_off = 0x1040, dispinfo_off = 0x2C00;       // u32.si.0: psi = R1+1040H, pDispInfo = R1+2C00H
		constexpr uint32_t he_entry_size = 0x20;                          // u32.si.10: HeEntrySize 20H
		struct wmsg
		{
			uint32_t max, off;   // awmControl[k]: maxMsgs, abMsgs (an R1 offset; 0 = NULL)
		};
		constexpr wmsg awm[ 33 ] = { { 0x318, 0x2DE0 }, { 0, 0 }, { 0x318, 0x2E60 }, { 0x14, 0x2EE0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0x318, 0x2F80 },
									 { 0x318, 0x3080 }, { 0x318, 0x3100 }, { 0x402, 0x2D40 }, { 0x318, 0x3180 }, { 0x318, 0x3000 }, { 0, 0 }, { 0x318, 0x2F00 },
									 { 0x288, 0x3200 }, { 0x82, 0x3270 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
									 { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0x33F, 0x2C40 }, { 0x349, 0x2CC0 } };   // u32.si.wm0 .. wm32
		constexpr uint32_t srvi_flags = 0xD14;                            // u32.psi: psi +0 (dwSRVIFlags; user32+17A2C tests bit 2)
		constexpr uint8_t psi_8bc = 0x04;                                 // u32.psi: psi +8BCH (user32+181FD tests bit 20H)
		constexpr uint32_t gdi_180094 = 1;                                // u32.gdi: +180094H (gdi32full GdiProcessSetup: LPK on)
		constexpr uint8_t stock_type[ 24 ] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x30, 0x30, 0x30, 0, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x08, 0x0A, 0x0A,
											   0x10, 0x30, 0x09, 0x05, 0, 0 };   // u32.gdi: the stock objects' type bytes (0 = no object)
		constexpr uint32_t gdi_batch_limit = 0x14;                        // u32.peb: GdiDCAttributeList (108H) 14H
		constexpr uint64_t ci_desk_info = 0x1040, ci_thread_info = 0xE3430;   // u32.teb.ci: +20H = R4+1040H, +60H = R4+<a heap block> (placeholder E3430H)
		constexpr uint64_t ci_98 = 0x4E4;                                 // u32.teb.ci: +98H 4E4H
		constexpr uint32_t client_thread_setup = 84;                      // user32 apfnDispatch (user32+92070H) entry 84 = __ClientThreadSetup (user32+1DC50H)
		constexpr uint64_t user32_apfn_dispatch = 0x92070;                // u32.peb: KernelCallbackTable = user32+92070H
	}

	bool kernel::map_watched_view( uint64_t base, uint64_t size, uint64_t committed, const std::vector<uint8_t>& content, const std::string& name,
								   const std::vector<std::pair<uint64_t, uint64_t>>& known, std::string& why )
	{
		for ( const auto& r : e_.regions() )
			if ( base < r.base + r.size && r.base < base + size )
			{
				why = name + ": the placeholder range [" + hex( base ) + ", +" + hex( size ) + ") is not free";
				return false;
			}
		if ( content.size() > committed )
		{
			why = name + ": content larger than the committed part";
			return false;
		}
		if ( !e_.map( base, committed, UC_PROT_READ | UC_PROT_WRITE, name, why ) ) return false;
		if ( ( !content.empty() && !e_.write( base, content.data(), content.size(), why ) ) || !e_.protect( base, committed, UC_PROT_READ, why ) ) return false;
		view v;
		v.base = base;
		v.size = size;
		v.type = mem2::mapped;
		v.allocation_protect = page::readonly;
		v.pages.assign( size_t( size / f_.page_size ), 0 );
		for ( uint64_t i = 0; i < committed / f_.page_size; i++ ) v.pages[ size_t( i ) ] = page::readonly;
		v.name = name;
		views_[ base ] = v;
		watch w;
		w.base = base;
		w.size = committed;
		w.name = name;
		w.known = known;
		watches_.push_back( w );
		return true;
	}

	// ------------------------------------------------------------------------------------------------ the CSR USERSRV request (U2119)
	// NtAlpcSendWaitReceivePort( PortHandle, Flags, SendMessage, SendMessageAttributes, ReceiveMessage, *BufferLength,
	// ReceiveMessageAttributes, Timeout ) - phnt ntlpcapi.h NtAlpcSendWaitReceivePort. The caller (P44 exploration x1, system call 281 of
	// T6; sample run 8 final system call 492): user32's _UserClientDllInitialize (user32+1896BH) -> ntdll CsrClientConnectToServer( "\Sessions\
	// <id>\Windows" (or "\Windows"), 3 USERSRV, a zeroed 240H-byte buffer, 240H, &gfServerProcess ) -> CsrpClientConnectToServer ->
	// CsrClientCallServer( the message, a capture buffer with the 240H bytes, ApiNumber 0 (CSRSRV CsrpClientConnect), 18H ): PortHandle =
	// the CSR API port of P40's connection, Flags 20000H (ALPC_MSGFLG_SYNC_REQUEST), SendMessage = ReceiveMessage = the CSR_API_MESSAGE
	// (PORT_MESSAGE DataLength 30H / TotalLength 58H, +28H the capture buffer (server address), +30H ApiNumber, +34H Status, +40H ServerId,
	// +48H the connection information (server address), +50H its size), *BufferLength 3B8H, no attributes, no time-out (ntdll+89A0 ..
	// +8A87, the capture's ntdll). What ntdll / user32 READ of the answer (trace x1): +34H (the CSR status: CsrClientCallServer's result),
	// +28H and the capture buffer's pointer array (converted back by the same delta - csrss replies with the message it got: not written
	// here), the 240H bytes of the connection information (CsrpClientConnectToServer copies them back); user32 copies bytes +8 .. +240H
	// into its gSharedInfo (SHAREDINFO, 238H: user32+189AB .. +18A1A), sets gpsi = gSharedInfo.psi and tests psi +0 bit 2. MEASURED (tm_u32,
	// the SHAREDINFO user32 kept after its own connection - u32.si.*, u32.region.*): psi = R1+1040H, aheList = R2+0, HeEntrySize 20H,
	// pDispInfo = R1+2C00H, +20H = R1's base, awmControl[0..32] = the measured (maxMsgs, R1 offset) pairs; R1 a read-only MEM_MAPPED view
	// of 8000H (4000H committed), R2 one of 200000H (23000H committed) - the views win32k maps into the client during this request
	// (ReactOS 0.4.16 win32ss/user/winsrv/usersrv/init.c UserClientConnect -> NtUserProcessConnect, reference only). Status: STATUS_SUCCESS
	// (user32 fails its start on a negative status - a GUI program that started saw a success). Not written (no reader: ntdll / user32 never
	// read them): the reply's PORT_MESSAGE header, *BufferLength, the connection information's first 8 bytes (USERCONNECT version
	// fields). The CONTENTS of R1 / R2: only the measured bytes user32 reads (psi +0 dwSRVIFlags D14H, psi +8BCH 04H); every other byte is
	// watched (an unmeasured read stops). Other ports / flags / shapes / API numbers / servers: stop.
	reply kernel::svc_alpc_send_wait_receive_port( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtAlpcSendWaitReceivePort: the stack arguments are not readable" );
		const uint64_t port = c.args[ 0 ], msg = c.args[ 2 ], smsga = c.args[ 3 ], rmsg = c.args[ 4 ], plen = c.args[ 5 ], rmsga = c.args[ 6 ], tmo = c.args[ 7 ];
		const uint32_t flags = uint32_t( c.args[ 1 ] );
		std::string what = "NtAlpcSendWaitReceivePort( " + hex( port ) + ", flags " + hex( flags ) + ", SendMessage " + hex( msg ) + ", " + hex( smsga ) + ", ReceiveMessage " +
						   hex( rmsg ) + ", BufferLength " + hex( plen ) + ", " + hex( rmsga ) + ", Timeout " + hex( tmo ) + " )";
		if ( !csr_port_ || port != csr_port_ ) return stop_( what + " - not the CSR API port of the connected-client model: not measured" );
		if ( flags != 0x20000 || msg != rmsg || smsga || rmsga || tmo || !plen ) return stop_( what + " - another shape: not measured (P44 x1: 20000H, SendMessage = ReceiveMessage, no attributes, no time-out)" );
		if ( !user_buffer( msg, 0x58, UC_PROT_READ | UC_PROT_WRITE ) || !user_buffer( plen, 8, UC_PROT_READ ) ) return done_( status::access_violation, what + ": a buffer is not accessible user memory (probe)" );
		std::string err;
		uint32_t len_word = 0, api = 0, server = 0, isize = 0;
		uint64_t blen = 0, iptr = 0, capture = 0;
		e_.read( msg, &len_word, 4, err );
		e_.read( msg + 0x28, &capture, 8, err );
		e_.read( msg + 0x30, &api, 4, err );
		e_.read( msg + 0x40, &server, 4, err );
		e_.read( msg + 0x48, &iptr, 8, err );
		e_.read( msg + 0x50, &isize, 4, err );
		e_.read( plen, &blen, 8, err );
		what += " - CSR message: lengths " + hex( len_word ) + ", ApiNumber " + hex( api ) + ", ServerId " + std::to_string( server ) + ", connection information " + hex( iptr ) + " (" +
				hex( isize ) + " bytes), capture buffer " + hex( capture );
		if ( len_word != 0x580030 || api != 0 || server != 3 || isize != 0x240 || blen != 0x3B8 || !capture )
			return stop_( what + " - not user32's USERSRV connection (P44 x1: lengths 580030H, ApiNumber 0, ServerId 3, 240H bytes, BufferLength 3B8H): not measured" );
		if ( user_connected_ ) return stop_( what + " - a second USERSRV connection: not measured" );
		// the client address of the connection information: the server address - the port memory delta (ViewRemoteBase - ViewBase, P40)
		uint64_t cview = 0;
		for ( const auto& [ b, v ] : views_ )
			if ( v.name == "CSR API port client view" ) cview = b;
		const uint64_t delta = 0x00007DF600000000ull - cview, data = iptr - delta;
		if ( !cview || data < cview || data + 0x240 > cview + 0x10000 ) return stop_( what + " - the connection information is not inside the client port view: not measured" );
		// the two views (the contents user32 reads, measured; the rest watched)
		std::vector<uint8_t> si( size_t( si_committed ), 0 );
		std::memcpy( si.data() + psi_off, &srvi_flags, 4 );
		si[ size_t( psi_off + 0x8BC ) ] = psi_8bc;
		if ( !map_watched_view( si_base, si_size, si_committed, si, "user shared-info view (R1, model of the USERSRV connection)", { { psi_off, psi_off + 4 }, { psi_off + 0x8BC, psi_off + 0x8BD } }, err ) ||
			 !map_watched_view( he_base, he_size, he_committed, {}, "user handle table view (R2, model of the USERSRV connection)", {}, err ) )
			return stop_( what + " - " + err );
		user_si_view_ = si_base;
		user_he_view_ = he_base;
		user_connected_ = true;
		uint8_t sh[ 0x238 ] = {};
		auto p64 = [ & ]( uint32_t o, uint64_t v ) { std::memcpy( sh + o, &v, 8 ); };
		p64( 0x00, si_base + psi_off );
		p64( 0x08, he_base );
		std::memcpy( sh + 0x10, &he_entry_size, 4 );
		p64( 0x18, si_base + dispinfo_off );
		p64( 0x20, si_base );
		for ( int k = 0; k < 33; k++ )
		{
			std::memcpy( sh + 0x28 + 0x10 * k, &awm[ k ].max, 4 );
			p64( 0x28 + 0x10 * k + 8, awm[ k ].off ? si_base + awm[ k ].off : 0 );
		}
		reply r = done_( status::success, what + ": the model of user32's USERSRV connection (measured: tm_nt u32.si.* / u32.region.*) - CSR status STATUS_SUCCESS, SHAREDINFO into the "
												 "connection information (psi " + hex( si_base + psi_off ) + ", aheList " + hex( he_base ) + ", pDispInfo " + hex( si_base + dispinfo_off ) +
												 "), views R1 " + hex( si_base ) + " / R2 " + hex( he_base ) + " (placeholder bases)" );
		const uint32_t ok = 0;
		if ( !put( msg + 0x34, &ok, 4, r ) || !put( data + 8, sh, sizeof( sh ), r ) ) return stop_( "writing the result failed after the probe" );
		return r;
	}

	// ------------------------------------------------------------------------------------------------ NtUnmapViewOfSection (U2121)
	// NtUnmapViewOfSection( ProcessHandle, BaseAddress ) - phnt ntmmapi.h NtUnmapViewOfSection; the WDK documents ZwUnmapViewOfSection
	// ("BaseAddress ... can be any virtual address within the view"; the whole view is unmapped). MEASURED (tm_nt sp.*.map: the view of an
	// image section unmapped by its base = STATUS_SUCCESS). The caller in user32's start (P44 x10): ntdll's LdrpFindLoadedDllByMappingFile
	// unmaps its temporary read-only view of a file. Every view the emulated kernel registered can be unmapped (its committed pages leave
	// the engine, the reserved part with it); another process / an address in no view: stop (Windows' error not measured).
	reply kernel::svc_unmap_view_of_section( const call& c )
	{
		const std::string what = "NtUnmapViewOfSection( " + hex( c.args[ 0 ] ) + ", " + hex( c.args[ 1 ] ) + " )";
		if ( c.args[ 0 ] != ~0ull ) return stop_( what + " - another process: not modelled" );
		auto it = views_.end();
		for ( auto v = views_.begin(); v != views_.end(); ++v )
			if ( c.args[ 1 ] >= v->second.base && c.args[ 1 ] - v->second.base < v->second.size ) it = v;
		if ( it == views_.end() ) return stop_( what + " - the address is in no view of this process: Windows' error is not measured" );
		const view v = it->second;
		std::string err;
		for ( uint64_t i = 0; i < v.pages.size(); )
		{
			if ( !v.pages[ size_t( i ) ] )
			{
				i++;
				continue;
			}
			uint64_t j = i;
			while ( j < v.pages.size() && v.pages[ size_t( j ) ] ) j++;
			if ( !e_.unmap_range( v.base + i * f_.page_size, ( j - i ) * f_.page_size, err ) ) return stop_( what + " - unmapping failed: " + err );
			i = j;
		}
		views_.erase( it );
		for ( auto w = watches_.begin(); w != watches_.end(); )
			if ( w->base == v.base )
				w = watches_.erase( w );
			else
				++w;
		return done_( status::success, what + ": the view " + v.name + " [" + hex( v.base ) + ", +" + hex( v.size ) + ") unmapped (measured: tm_nt sp.*.map NtUnmapViewOfSection 0)" );
	}

	// ------------------------------------------------------------------------------------------------ more services of user32's start (U2121)
	bool kernel::dispatch_user_start( const call& c, reply& r )
	{
		std::string err;
		// NtDuplicateToken( ExistingTokenHandle, DesiredAccess, ObjectAttributes, EffectiveOnly, TokenType, *NewTokenHandle ) - phnt ntseapi.h
		// NtDuplicateToken; kernel32 BasepIsServiceSidBlocked (user32's LoadAppInitDlls) through kernelbase DuplicateToken( SecurityImpersonation ):
		// the process token (opened with 0AH), access 0CH (TOKEN_IMPERSONATE | TOKEN_QUERY), object attributes {30H, no root / name /
		// attributes / SD, a SECURITY_QUALITY_OF_SERVICE {0CH, SecurityImpersonation 2, SECURITY_DYNAMIC_TRACKING 1, EffectiveOnly 0} (um\winnt.h)},
		// EffectiveOnly FALSE, TokenImpersonation (2) - MEASURED tm_nt u32k.svc.dup (DuplicateToken -> TRUE, a handle): STATUS_SUCCESS, a
		// new "token (impersonation)" handle. Other shapes: stop.
		if ( c.name == "NtDuplicateToken" )
		{
			const std::string what = "NtDuplicateToken( " + hex( c.args[ 0 ] ) + ", " + hex( uint32_t( c.args[ 1 ] ) ) + ", OA " + hex( c.args[ 2 ] ) + ", " + hex( uint32_t( c.args[ 3 ] ) ) + ", type " +
									 hex( uint32_t( c.args[ 4 ] ) ) + ", " + hex( c.args[ 5 ] ) + " )";
			const auto it = handles_.find( c.args[ 0 ] );
			uint8_t oa[ 0x30 ] = {}, q[ 12 ] = {};
			uint64_t pq = 0;
			bool ok = c.stack_args && it != handles_.end() && it->second.kind == "token (process)" && ( it->second.value & 2 ) && uint32_t( c.args[ 1 ] ) == 0xC &&
					  ( c.args[ 3 ] & 0xFF ) == 0 && uint32_t( c.args[ 4 ] ) == 2 && user_buffer( c.args[ 2 ], 0x30, UC_PROT_READ );
			if ( ok )
			{
				e_.read( c.args[ 2 ], oa, 0x30, err );
				std::memcpy( &pq, oa + 0x28, 8 );
				uint32_t len = 0;
				std::memcpy( &len, oa, 4 );
				ok = len == 0x30 && pq && user_buffer( pq, 12, UC_PROT_READ );
				for ( int i = 8; ok && i < 0x28; i++ ) ok = ( i >= 0x1C && i < 0x20 ) || oa[ i ] == 0;   // +1CH..+1FH: the padding after Attributes (not read by the kernel)
				if ( ok )
				{
					e_.read( pq, q, 12, err );
					ok = q[ 0 ] == 0x0C && q[ 1 ] == 0 && q[ 2 ] == 0 && q[ 3 ] == 0 && q[ 4 ] == 2 && q[ 5 ] == 0 && q[ 6 ] == 0 && q[ 7 ] == 0 && q[ 8 ] == 1 && q[ 9 ] == 0;
				}
			}
			if ( !ok )
			{
				std::string oh;
				for ( int i = 0; i < 0x30; i++ ) { char t[ 4 ]; std::snprintf( t, sizeof( t ), "%02X", oa[ i ] ); oh += ( i ? " " : "" ) + std::string( t ); }
				std::string qh;
				for ( int i = 0; i < 12; i++ ) { char t[ 4 ]; std::snprintf( t, sizeof( t ), "%02X", q[ i ] ); qh += ( i ? " " : "" ) + std::string( t ); }
				r = stop_( what + " - not measured (tm_nt u32k.svc.dup: the 0AH process token, 0CH, OA {30H, SQOS {0CH, 2, 1, 0}}, FALSE, TokenImpersonation); OA bytes " + oh + "; SQOS bytes " + qh );
				return true;
			}
			if ( !user_buffer( c.args[ 5 ], 8, UC_PROT_WRITE ) )
			{
				r = done_( status::access_violation, what + ": NewTokenHandle not writable" );
				return true;
			}
			object o;
			o.kind = "token (impersonation)";
			o.value = 0xC;
			const uint64_t h = open_handle( o );
			r = done_( status::success, what + ": an impersonation token (SecurityImpersonation) of the process token, handle " + hex( h ) + " (measured: tm_nt u32k.svc.dup)" );
			if ( !put( c.args[ 5 ], &h, 8, r ) ) r = stop_( "writing the handle failed after the probe" );
			return true;
		}
		// NtQueryKey( KeyHandle, 7 KeyHandleTagsInformation, KeyInformation, Length, *ResultLength ) - phnt ntregapi.h NtQueryKey /
		// KEY_INFORMATION_CLASS; kernelbase on its HKEY_LOCAL_MACHINE handle (user32's LoadAppInitDlls, P44 x13). MEASURED tm_nt u32k.hklm.tags
		// / .tags.0 (on \REGISTRY\MACHINE): Length 4 = STATUS_SUCCESS, HandleTags 0, ResultLength 4; Length 0 = STATUS_BUFFER_TOO_SMALL,
		// ResultLength 4, nothing written. Other classes / keys / lengths: stop.
		if ( c.name == "NtQueryKey" )
		{
			const auto it = handles_.find( c.args[ 0 ] );
			const uint32_t cls = uint32_t( c.args[ 1 ] ), len = c.stack_args ? uint32_t( c.args[ 3 ] ) : 0xFFFFFFFF;
			const std::string what = "NtQueryKey( " + hex( c.args[ 0 ] ) + ( it != handles_.end() ? " (" + it->second.kind + " \"" + it->second.name + "\")" : std::string() ) + ", class " + hex( cls ) +
									 ", " + hex( c.args[ 2 ] ) + ", Length " + hex( len ) + " )";
			std::string kn;
			if ( it != handles_.end() )
				for ( char ch : it->second.name ) kn += char( std::tolower( uint8_t( ch ) ) );
			// P46 (U2152): also the root opened with the trailing backslash (tm_nt reg.hklm.tags0: 0 before any set), and the tags this handle
			// P46 (U2157): and the user's hive root \REGISTRY\USER\<the placeholder SID> (tm_nt reg.hkcu.*: the same answers from inside)
			// got from NtSetInformationKey 5 (tm_nt reg.hklm.tags1 / .tags3: the value set is read back)
			if ( it == handles_.end() || it->second.kind != "key" || false /* P46 (U2157): every key handle (tm_nt reg.hklm / reg.hkcu / reg.hkcu.sub) */ || cls != 7 || ( len != 4 && len != 0 ) ||
				 !c.stack_args )
			{
				r = stop_( what + " - not measured (tm_nt u32k.hklm.tags: class 7 on \\REGISTRY\\MACHINE, Length 4 / 0)" );
				return true;
			}
			const uint64_t prl = c.args[ 4 ];
			if ( ( len && !user_buffer( c.args[ 2 ], 4, UC_PROT_WRITE ) ) || ( prl && !user_buffer( prl, 4, UC_PROT_WRITE ) ) )
			{
				r = done_( status::access_violation, what + ": an OUT buffer is not writable" );
				return true;
			}
			const auto tg = key_tags_.find( c.args[ 0 ] );
			const uint32_t tags = tg != key_tags_.end() ? tg->second : 0, four = 4;
			r = done_( len ? status::success : status::buffer_too_small, what + ( len ? ": HandleTags " + hex( tags ) + ", ResultLength 4" : ": STATUS_BUFFER_TOO_SMALL, ResultLength 4" ) +
											 " (measured: tm_nt u32k.hklm.tags / reg.hklm.tags*)" );
			if ( ( len && !put( c.args[ 2 ], &tags, 4, r ) ) || ( prl && !put( prl, &four, 4, r ) ) ) r = stop_( "writing the result failed after the probe" );
			return true;
		}
		// P46 (U2152): NtSetInformationKey( KeyHandle, 5 KeySetHandleTagsInformation, &tags, Length ) - phnt ntregapi.h:778-784, KEY_SET_INFORMATION_CLASS
		// :248-258; kernelbase's predefined HKEY_LOCAL_MACHINE in combase's start (sample run 11 exploration x11 system call 660: the handle of
		// NtOpenKeyEx( "\REGISTRY\MACHINE\", 20119H ), tags 100H). MEASURED tm_nt reg.hklm.*: Length 4 = STATUS_SUCCESS and the handle's tags
		// become the value (100H; 0 again - NtQueryKey 7 reads it back); Length 0 / 8 = STATUS_INFO_LENGTH_MISMATCH, the tags unchanged. Other
		// keys / classes / values: stop. P46 (U2157): also on the user's hive root (tm_nt reg.hkcu.set / .tags1: the same).
		if ( c.name == "NtSetInformationKey" )
		{
			const auto it = handles_.find( c.args[ 0 ] );
			const uint32_t cls = uint32_t( c.args[ 1 ] ), len = uint32_t( c.args[ 3 ] );
			std::string kn;
			if ( it != handles_.end() )
				for ( char ch : it->second.name ) kn += char( std::tolower( uint8_t( ch ) ) );
			const std::string what = "NtSetInformationKey( " + hex( c.args[ 0 ] ) + ( it != handles_.end() ? " (" + it->second.kind + " \"" + it->second.name + "\")" : std::string() ) +
									 ", class " + hex( cls ) + ", " + hex( c.args[ 2 ] ) + ", Length " + hex( len ) + " )";
			if ( it == handles_.end() || it->second.kind != "key" || false /* P46 (U2157): every key handle (tm_nt reg.hklm / reg.hkcu / reg.hkcu.sub) */ || cls != 5 )
			{
				r = stop_( what + " - not measured (tm_nt reg.hklm: class 5 on \\REGISTRY\\MACHINE)" );
				return true;
			}
			if ( len != 4 )
			{
				r = done_( status::info_length_mismatch, what + ": STATUS_INFO_LENGTH_MISMATCH, the tags unchanged (measured: tm_nt reg.hklm.set.0 / .set.8)" );
				return true;
			}
			uint32_t v = 0;
			std::string err;
			if ( !user_buffer( c.args[ 2 ], 4, UC_PROT_READ ) || !e_.read( c.args[ 2 ], &v, 4, err ) )
			{
				r = stop_( what + " - the input is not readable: not measured" );
				return true;
			}
			if ( v != 0x100 && v != 0 )
			{
				r = stop_( what + ": HandleTags " + hex( v ) + " - not measured (100H / 0 are)" );
				return true;
			}
			key_tags_[ c.args[ 0 ] ] = v;
			r = done_( status::success, what + ": HandleTags " + hex( v ) + " set on the handle (measured: tm_nt reg.hklm.set / .set.zero)" );
			return true;
		}
		// P46 (U2155): NtOpenThreadToken( ThreadHandle, DesiredAccess, OpenAsSelf, *TokenHandle ) - phnt ntseapi.h; combase's start (sample run 11
		// exploration x20 system call 670: NtCurrentThread(), 4 TOKEN_IMPERSONATE, OpenAsSelf TRUE). The emulated thread never impersonates.
		// MEASURED tm_nt tok.thr / .thr.noself / .thr.query: STATUS_NO_TOKEN (C000007CH), the handle not written (access 4 / 8, OpenAsSelf 1 / 0).
		// Another thread / access: stop.
		if ( c.name == "NtOpenThreadToken" )
		{
			const uint32_t acc = uint32_t( c.args[ 1 ] );
			const std::string what = "NtOpenThreadToken( " + hex( c.args[ 0 ] ) + ", " + hex( acc ) + ", OpenAsSelf " + hex( c.args[ 2 ] & 0xFF ) + ", " + hex( c.args[ 3 ] ) + " )";
			if ( c.args[ 0 ] != ~0ull - 1 || ( acc != 4 && acc != 8 ) || ( c.args[ 2 ] & 0xFF ) > 1 || !user_buffer( c.args[ 3 ], 8, UC_PROT_WRITE ) )
			{
				r = stop_( what + " - not measured (tm_nt tok.thr*: NtCurrentThread(), access 4 / 8, OpenAsSelf 0 / 1, a writable handle pointer)" );
				return true;
			}
			r = done_( 0xC000007C, what + ": the thread does not impersonate - STATUS_NO_TOKEN, the handle not written (measured: tm_nt tok.thr)" );
			return true;
		}
		// P46 (U2154): NtNotifyChangeKey( KeyHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, CompletionFilter, WatchTree, Buffer,
		// BufferSize, Asynchronous ) - phnt ntregapi.h:1196-1207; kernelbase RegNotifyChangeKeyValue in combase's start (sample run 11
		// exploration x17 system call 669: the HKLM\Software\Microsoft\Ole key, a notification event, no APC, CompletionFilter 10000001H
		// REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_THREAD_AGNOSTIC, WatchTree FALSE, no buffer, Asynchronous TRUE). MEASURED tm_nt reg.notify.*:
		// STATUS_PENDING (103H), the IoStatusBlock not written, the event not signaled (also 200 ms later: nothing changes the key); the key's
		// NtClose then signals the event, the IoStatusBlock still not written. The emulated registry never changes: the notification stays
		// pending until the key's close (svc_close). P46 (U2154): WatchTree TRUE on a key of the user's hive the same (tm_nt reg.notify.tree:
		// STATUS_PENDING, nothing written; the close signals the event). Other argument sets: stop.
		if ( c.name == "NtNotifyChangeKey" )
		{
			const auto it = handles_.find( c.args[ 0 ] );
			const auto ev = handles_.find( c.args[ 1 ] );
			const std::string what = "NtNotifyChangeKey( " + hex( c.args[ 0 ] ) + ( it != handles_.end() ? " (" + it->second.kind + " \"" + it->second.name + "\")" : std::string() ) +
									 ", event " + hex( c.args[ 1 ] ) + ", APC " + hex( c.args[ 2 ] ) + " / " + hex( c.args[ 3 ] ) + ", IoStatusBlock " + hex( c.args[ 4 ] ) +
									 ( c.stack_args ? ", filter " + hex( c.args[ 5 ] ) + ", WatchTree " + hex( c.args[ 6 ] & 0xFF ) + ", Buffer " + hex( c.args[ 7 ] ) + " / " +
														  hex( uint32_t( c.args[ 8 ] ) ) + ", Asynchronous " + hex( c.args[ 9 ] & 0xFF ) : std::string() ) + " )";
			if ( !c.stack_args || it == handles_.end() || it->second.kind != "key" || ev == handles_.end() || ev->second.kind != "event (notification)" || ev->second.value ||
				 c.args[ 2 ] || c.args[ 3 ] || uint32_t( c.args[ 5 ] ) != 0x10000001 || ( c.args[ 6 ] & 0xFF ) > 1 || c.args[ 7 ] || uint32_t( c.args[ 8 ] ) || ( c.args[ 9 ] & 0xFF ) != 1 ||
				 key_notify_.count( c.args[ 0 ] ) || !user_buffer( c.args[ 4 ], 16, UC_PROT_WRITE ) )
			{
				r = stop_( what + " - not measured (tm_nt reg.notify / .notify.tree: a key, a non-signaled notification event, no APC, filter 10000001H, WatchTree FALSE / TRUE, "
								  "no buffer, asynchronous, one notification per key, a writable IoStatusBlock)" );
				return true;
			}
			key_notify_[ c.args[ 0 ] ] = c.args[ 1 ];
			r = done_( 0x103, what + ": STATUS_PENDING - the IoStatusBlock not written, the event not signaled; pending until the key's close (the emulated "
									 "registry does not change) (measured: tm_nt reg.notify)" );
			return true;
		}
		// NtSetInformationThread( NtCurrentThread(), 0AH ThreadZeroTlsCell, &index, 4 ) - phnt ntpsapi.h THREADINFOCLASS ThreadZeroTlsCell
		// ("s: ULONG TlsIndex"); kernelbase TlsFree (gdi32full's detach). MEASURED tm_nt u32k.tls / .tls.8: an index below 64 = STATUS_SUCCESS,
		// the cell (TEB TlsSlots, ntdll.pdb _TEB +1480H) of every thread zeroed (one thread here); Length 8 = STATUS_INFO_LENGTH_MISMATCH.
		// Other threads / classes / indexes >= 64 (the expansion slots): not measured - the generic stop below (dispatch_more).
		if ( c.name == "NtSetInformationThread" && c.args[ 0 ] == ~0ull - 1 && uint32_t( c.args[ 1 ] ) == 0xA )
		{
			const uint32_t len = uint32_t( c.args[ 3 ] );
			uint32_t idx = 0xFFFFFFFF;
			if ( len >= 4 && user_buffer( c.args[ 2 ], 4, UC_PROT_READ ) ) e_.read( c.args[ 2 ], &idx, 4, err );
			const std::string what = "NtSetInformationThread( NtCurrentThread(), 0AH ThreadZeroTlsCell, index " + std::to_string( idx ) + ", Length " + hex( len ) + " )";
			if ( len == 8 )
			{
				r = done_( status::info_length_mismatch, what + ": STATUS_INFO_LENGTH_MISMATCH (measured: tm_nt u32k.tls.8)" );
				return true;
			}
			if ( len != 4 || idx >= 64 )
			{
				r = stop_( what + " - not measured (tm_nt u32k.tls: Length 4, an index below 64)" );
				return true;
			}
			const uint64_t zero = 0;
			if ( !e_.write( f_.teb + 0x1480 + 8ull * idx, &zero, 8, err ) )
			{
				r = stop_( what + " - the TEB write failed: " + err );
				return true;
			}
			r = done_( status::success, what + ": the cell of the (one) thread zeroed (measured: tm_nt u32k.tls)" );
			return true;
		}
		return false;
	}

	// ------------------------------------------------------------------------------------------------ kernel -> user callbacks (U2118)
	// KeUserModeCallback (ReactOS 0.4.16 ntoskrnl/ke/amd64/usercall.c:233-294, reference only): the service in progress calls back into
	// the thread's user mode - ntdll!KiUserCallbackDispatcher calls PEB KernelCallbackTable[ApiNumber]( Buffer, Length ) and ends with
	// NtCallbackReturn( Result, ResultLength, Status ); the service then continues with that status. The process layer saves / restores
	// the user state (win_env.cpp process::callback_transfer); here: which service waits, and how it completes.
	reply kernel::begin_callback( const std::string& service, uint32_t api, const std::string& text )
	{
		pending_callback p;
		p.service = service;
		p.api = api;
		callbacks_.push_back( p );
		reply r;
		r.kind = reply_kind::callback;
		r.callback_api = api;
		r.text = text + " - callback " + std::to_string( api ) + " into user mode (KiUserCallbackDispatcher), the service waits for NtCallbackReturn";
		return r;
	}

	// NtCallbackReturn( OutputBuffer, OutputLength, Status ) - phnt ntkeapi.h NtCallbackReturn; ends the innermost callback
	reply kernel::svc_callback_return( const call& c )
	{
		const std::string what = "NtCallbackReturn( " + hex( c.args[ 0 ] ) + ", " + hex( uint32_t( c.args[ 1 ] ) ) + ", status " + hex( uint32_t( c.args[ 2 ] ) ) + " )";
		if ( callbacks_.empty() ) return stop_( what + " - no callback in progress: Windows' answer (an error status) is not measured" );
		if ( c.args[ 0 ] || uint32_t( c.args[ 1 ] ) ) return stop_( what + " - a callback result buffer: not measured (the callbacks modelled return none)" );
		const pending_callback p = callbacks_.back();
		callbacks_.pop_back();
		reply r = complete_callback( p, uint32_t( c.args[ 2 ] ) );
		if ( r.kind == reply_kind::done )
		{
			r.kind = reply_kind::callback_return;
			r.text = what + ": callback " + std::to_string( p.api ) + " of " + p.service + " ended; " + p.service + " completes: " + r.text;
		}
		return r;
	}

	reply kernel::complete_callback( const pending_callback& p, uint32_t callback_status )
	{
		// NtGdiInit after user32's ClientThreadSetup (callback 84): TRUE (gdi32full GdiDllInitialize continues only on 1 - a GUI program
		// that started saw it: inferred from the native start, tm_u32); a failed ClientThreadSetup (status not 0) is not measured
		if ( p.service == "NtGdiInit" && p.api == client_thread_setup )
		{
			if ( callback_status != status::success ) return stop_( "NtGdiInit: ClientThreadSetup returned " + hex( callback_status ) + " - not measured (a started GUI program saw success)" );
			return done_( 1, "NtGdiInit -> TRUE (the GUI conversion and ClientThreadSetup done)" );
		}
		return stop_( p.service + ": no completion after callback " + std::to_string( p.api ) + " (status " + hex( callback_status ) + ") is modelled" );
	}

	// ------------------------------------------------------------------------------------------------ the GUI conversion (U2120)
	// The thread's FIRST win32k service converts the process (once) and the thread to GUI (PsConvertToGuiThread: ReactOS 0.4.16
	// win32ss/user/ntuser/main.c:240-420 InitProcessCallback / UserProcessCreate, :450-720 InitThreadCallback, win32ss/gdi/ntgdi/init.c
	// GdiProcessCreate - reference only). In T6's start (P44 x7 / x8) that service is NtGdiInit (gdi32full GdiDllInitialize) - no win32k
	// service before it - and user32's ClientThreadSetup ran natively (tm_u32: CLIENTINFO flags 8 and +D0H = user32+B4364H, gfFirstThread
	// 0, gcxGray 8: only its kernel callback writes them) - so NtGdiInit makes the callback (ReactOS main.c:697-707 co_IntClientThreadSetup
	// at the end of InitThreadCallback). MEASURED end state (tm_nt u32.*): PEB GdiSharedHandleTable (F8H) = R3 (181000H, read-only,
	// MEM_MAPPED, all committed) whose +180094H = 1 and the stock-object array +1800B0H (24 x 8 bytes: GDI handles of the measured type
	// bytes, stock bit 23 set, upper half 0 - the index / uniqueness bits are placeholders), PEB GdiDCAttributeList (108H) = 14H; TEB
	// Win32ThreadInfo (78H) = the thread id; CLIENTINFO (TEB+800H): dwExpWinVer (+10H) = the image's subsystem version, the compat flags 0,
	// pDeskInfo (+20H) = R4+1040H, +28H = R4's base, +60H = a block of R4 (placeholder), +98H 4E4H, the rest 0 (CI_flags 0: user32 sets
	// bit 3 itself) - +90H / +100H hold user addresses not mapped in the process (not modelled: watched); R4 = the desktop heap view
	// (1401000H, its first 199000H committed); a window station and a desktop handle in the process (ReactOS main.c:600-660: the startup
	// desktop opened for the process).
	reply kernel::gui_convert( const call& c )
	{
		std::string err;
		if ( !user_connected_ ) return stop_( c.name + ": the GUI conversion before user32's USERSRV connection - not measured" );
		uint64_t kct = 0, u32 = 0;
		e_.read( f_.peb + 0x58, &kct, 8, err );
		for ( const auto& [ b, v ] : views_ )
			if ( v.type == mem2::image && v.name.find( "user32.dll" ) != std::string::npos ) u32 = b;
		if ( !u32 || kct != u32 + user32_apfn_dispatch )
			return stop_( c.name + ": PEB KernelCallbackTable " + hex( kct ) + " is not the measured user32 build's apfnDispatch (user32+92070H) - the callback numbers are those of user32 10.0.19044.7807" );
		if ( !gui_process_ )
		{
			std::vector<uint8_t> g( size_t( gdi_size ), 0 );
			std::memcpy( g.data() + 0x180094, &gdi_180094, 4 );
			for ( uint32_t i = 0; i < 24; i++ )
			{
				if ( !stock_type[ i ] ) continue;
				const uint64_t h = uint64_t( 0x80 + i ) | ( uint64_t( stock_type[ i ] ) << 16 ) | ( 1ull << 23 );
				std::memcpy( g.data() + 0x1800B0 + 8 * i, &h, 8 );
			}
			if ( !map_watched_view( gdi_base, gdi_size, gdi_size, g, "GDI shared memory (R3, model of the process' GUI conversion)", { { 0x180094, 0x180098 }, { 0x1800B0, 0x180170 } }, err ) )
				return stop_( c.name + ": " + err );
			gdi_view_ = gdi_base;
			const uint32_t bl = gdi_batch_limit;
			if ( !e_.write( f_.peb + 0xF8, &gdi_view_, 8, err ) || !e_.write( f_.peb + 0x108, &bl, 4, err ) ) return stop_( c.name + ": the PEB write failed: " + err );
			object ws;
			ws.kind = "window station";
			ws.name = "WinSta0";   // the interactive window station (its object path is not modelled)
			winsta_handle_ = open_handle( ws );
			gui_process_ = true;
		}
		if ( !map_watched_view( desk_base, desk_size, desk_committed, {}, "desktop heap view (R4, model of the thread's GUI conversion)", {}, err ) ) return stop_( c.name + ": " + err );
		desk_view_ = desk_base;
		object dk;
		dk.kind = "desktop";
		dk.name = "Default";
		desktop_handle_ = open_handle( dk );
		// the image's subsystem version (RtlGetExpWinVer: ReactOS main.c:561-563; measured: = the image's)
		uint64_t image = 0;
		uint32_t lfanew = 0;
		uint16_t maj = 0, min = 0;
		e_.read( f_.peb + 0x10, &image, 8, err );
		e_.read( image + 0x3C, &lfanew, 4, err );
		e_.read( image + lfanew + 0x18 + 0x30, &maj, 2, err );
		e_.read( image + lfanew + 0x18 + 0x32, &min, 2, err );
		uint8_t ci[ 0x1F0 ] = {};
		const uint32_t expver = ( uint32_t( maj ) << 8 ) | min;
		const uint64_t di = desk_base + ci_desk_info, db = desk_base, ti = desk_base + ci_thread_info, v98 = ci_98;
		std::memcpy( ci + 0x10, &expver, 4 );
		std::memcpy( ci + 0x20, &di, 8 );
		std::memcpy( ci + 0x28, &db, 8 );
		std::memcpy( ci + 0x60, &ti, 8 );
		std::memcpy( ci + 0x98, &v98, 8 );
		const uint64_t tid = f_.thread_id;
		if ( !e_.write( f_.teb + 0x800, ci, sizeof( ci ), err ) || !e_.write( f_.teb + 0x78, &tid, 8, err ) ) return stop_( c.name + ": the TEB write failed: " + err );
		watch w;
		w.base = f_.teb + 0x800;
		w.size = 0x1F0;
		w.name = "TEB CLIENTINFO (model of the thread's GUI conversion)";
		w.known = { { 0, 0x90 }, { 0x98, 0x100 }, { 0x108, 0x1F0 } };
		watches_.push_back( w );
		gui_thread_ = true;
		return done_( 0, "the GUI conversion (process: GDI shared memory " + hex( gdi_base ) + ", window station " + hex( winsta_handle_ ) + "; thread: Win32ThreadInfo = the thread id, CLIENTINFO, "
						 "desktop heap view " + hex( desk_base ) + ", desktop " + hex( desktop_handle_ ) + "; measured: tm_nt u32.*)" );
	}

	// ------------------------------------------------------------------------------------------------ the win32k services
	bool kernel::dispatch_win32k( const call& c, reply& r )
	{
		if ( c.number < 0x1000 || c.number > 0x1FFF ) return false;
		std::string conv;
		if ( !gui_thread_ )
		{
			reply g = gui_convert( c );
			if ( g.kind != reply_kind::done )
			{
				r = g;
				return true;
			}
			conv = g.text + "; ";
		}
		const std::string what = c.name + "( " + hex( c.args[ 0 ] ) + ", " + hex( c.args[ 1 ] ) + ", " + hex( c.args[ 2 ] ) + ", " + hex( c.args[ 3 ] ) + " )";
		// NtGdiInit() - gdi32full GdiDllInitialize; the thread's first win32k service (the conversion above), then user32's ClientThreadSetup
		if ( c.name == "NtGdiInit" )
		{
			if ( conv.empty() ) { r = stop_( what + " - a second NtGdiInit: not measured" ); return true; }
			r = begin_callback( "NtGdiInit", client_thread_setup, "NtGdiInit(): " + conv + "user32's ClientThreadSetup (apfnDispatch 84 = __ClientThreadSetup)" );
			return true;
		}
		// NtUserCallNoParam( 22H ) - user32 ClientThreadSetup (user32+1DD7C): on 1 it skips InitClientDrawing. MEASURED (tm_u32 u32.g):
		// InitClientDrawing ran natively (gcxGray / gcyGray 8, ghbrWhite / ghbrBlack = the stock brushes) - the result was not 1; the model
		// answers 0 (the value itself is not observable: user32 keeps only the comparison)
		if ( c.name == "NtUserCallNoParam" && c.args[ 0 ] == 0x22 )
		{
			r = done_( 0, conv + what + ": 0 (not 1: InitClientDrawing ran natively - measured: tm_nt u32.g gcxGray 8)" );
			return true;
		}
		// NtUserGetThreadDesktop( dwThreadId ) - user32 ClientThreadSetup (user32+1DDAB): gfSystemInitialized = ( result != 0 ). MEASURED
		// (tm_u32 u32.g): gfSystemInitialized 1 - a desktop handle (the thread's: the conversion's desktop handle)
		if ( c.name == "NtUserGetThreadDesktop" )
		{
			if ( uint32_t( c.args[ 0 ] ) != f_.thread_id ) { r = stop_( what + " - another thread: not measured" ); return true; }
			r = done_( uint32_t( desktop_handle_ ), conv + what + ": the thread's desktop handle " + hex( desktop_handle_ ) + " (non-zero: measured gfSystemInitialized 1, tm_nt u32.g)" );
			return true;
		}
		// P46 (U2160): NtUserRegisterWindowMessage( PUNICODE_STRING MessageName ) - user32 RegisterWindowMessageW (combase / ole32's start,
		// sample run 11 exploration x63: "{FB8F0821-0164-101B-84ED-08002B2EC713}"); the win32k prototype after ReactOS 0.4.16 win32ss/include/
		// ntuser.h NtUserRegisterWindowMessage (reference only). MEASURED (tm_u32 u32.rwm, our own GUI test PE started normally): the result is an
		// atom of the window station's message-name table in C000H..FFFFH (its number depends on the session - the environment answers a
		// PLACEHOLDER from C200H on), the same name again = the same atom; the name is NOT in the global atom table (GlobalFindAtomW 0: a table
		// of its own, not NtAddAtomEx's). A name of printable ASCII, 1..255 characters; two names equal but for case (not measured), an empty
		// name, an unreadable string: stop.
		if ( c.name == "NtUserRegisterWindowMessage" )
		{
			std::string nm, why;
			if ( !guest_ustring( c.args[ 0 ], nm, why ) || nm.empty() || nm.size() > 255 )
			{
				r = stop_( what + " - the name is not a readable 1..255-character string: not measured" + ( why.empty() ? std::string() : " (" + why + ")" ) );
				return true;
			}
			std::string low;
			for ( char ch : nm )
			{
				if ( uint8_t( ch ) < 0x20 || uint8_t( ch ) > 0x7E ) { r = stop_( what + " - a non-printable / non-ASCII name: not measured" ); return true; }
				low += char( std::tolower( uint8_t( ch ) ) );
			}
			for ( const auto& [ k, v ] : wm_names_ )
			{
				std::string kl;
				for ( char ch : k ) kl += char( std::tolower( uint8_t( ch ) ) );
				if ( kl == low && k != nm ) { r = stop_( what + " - \"" + nm + "\" equals \"" + k + "\" but for case: not measured" ); return true; }
			}
			auto it = wm_names_.find( nm );
			const bool again = it != wm_names_.end();
			if ( !again )
			{
				if ( wm_next_ > 0xFFFF ) { r = stop_( what + " - the placeholder message atoms are used up" ); return true; }
				it = wm_names_.emplace( nm, wm_next_++ ).first;
			}
			r = done_( it->second, conv + what + ": \"" + nm + "\" -> message atom " + hex( it->second ) + ( again ? " (registered before: the same)" : " (a placeholder in C000H..FFFFH)" ) +
										" (measured shape: tm_nt u32.rwm)" );
			return true;
		}
		// NtUserCallOneParam( LPK flags, 3DH ) - user32 ClientThreadSetup (user32+1DDD6) when the LPK hooks are set (user32+B43A4H, measured FH
		// natively: tm_u32 u32.g); user32 ignores the result (not observable) - answered 0
		if ( c.name == "NtUserCallOneParam" && c.args[ 1 ] == 0x3D && c.args[ 0 ] == 0xF )
		{
			r = done_( 0, conv + what + ": the LPK flags FH handed to win32k; the result is ignored by user32 (not observable) - 0" );
			return true;
		}
		// NtUserGetProcessUIContextInformation( ProcessHandle, *Information ) - user32 ClientThreadSetup (user32+1DE11) into guiContextInfo
		// (user32+B4730H, 8 bytes): MEASURED (tm_u32 u32.g) 0 / 0 after the start; user32 ignores the result - 8 zero bytes written, TRUE
		if ( c.name == "NtUserGetProcessUIContextInformation" )
		{
			if ( c.args[ 0 ] != ~0ull || !user_buffer( c.args[ 1 ], 8, UC_PROT_WRITE ) ) { r = stop_( what + " - another shape: not measured (NtCurrentProcess(), 8 writable bytes)" ); return true; }
			r = done_( 1, conv + what + ": PROCESS_UICONTEXT_INFORMATION 8 zero bytes (measured: tm_nt u32.g guiContextInfo 0 0), TRUE (ignored by user32)" );
			const uint64_t zero = 0;
			if ( !put( c.args[ 1 ], &zero, 8, r ) ) r = stop_( "writing the result failed after the probe" );
			return true;
		}
		r = stop_( conv + what + " (" + hex( c.number ) + ", win32k) - not measured: no answer of this win32k service is modelled" );
		return true;
	}
}
