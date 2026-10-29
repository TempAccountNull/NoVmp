// emu::nt (P45, ledgers U2126-U2130): the EMULATED FILE SYSTEM of a process - USER DECISION 2026-10-27 "virtual FS, measured misses".
// The file system holds ONLY (a) the process image (the sample or the test PE, as data) at its emulated image path, (b) the environment's
// captured system files (capture\sysdlls\<env>: the DLLs, the NLS tables, the API set schema) at their System32 paths, and the folders on
// the way to them; NOTHING ELSE. Every other path - a file, a folder, an alternate data stream, a device name - is a MISS, answered with the
// status Windows gives for that KIND of miss in that CONTEXT, MEASURED natively by our own test PE tm_nt (case fs, Emulator/data/
// nt_measured_vmware_guest.txt; decision 3.0.7; on its own temporary names only). The answers below are that measurement, one table row per
// measured line (suite nt_measured compares the rows with the recorded lines). A path IN the model with an operation that was not measured,
// a kind of path that was not measured (a UNC path, a path relative to a handle, a wildcard, another volume, a file used as a folder ...)
// and an argument set that was not measured are a LOUD STOP with the decoded call (the process layer adds the state dump). Nothing is ever
// opened on the host for the guest: the model is the image's bytes, the capture's file list and this table (no_host_passthrough rule
// pt_host_file). Every file-system access of the emulated program is recorded (fs_log(); win_env writes <out dir>\fs_access.txt) -
// observation only, the answers do not depend on it.
//
// Kinds (classification of an NT path "\??\X:\..."; case-insensitive like OBJ_CASE_INSENSITIVE):
//   name      the folder exists in the model, the last component does not (tm_nt fs.dir / fs.sys / fs.win)
//   path      a folder on the way does not exist (tm_nt fs.sub - the SystemResources\<image>.mun case of sample run 9 - fs.syssub / fs.winsub)
//   stream    "<missing file>:<stream>" (fs.ads); stream-of-file "<the image>:<missing stream>" (fs.adsimg)
//   device    "\Device\<a name>" - no device is modelled (fs.dev)
//   name/     a missing folder opened as a folder with a trailing backslash (tm_nt of.missing)
//   dir       a folder of the model itself (only the measured opens: of.curdir - nt_kernel_measured.cpp - and fs.dq.open)
// Contexts: image (the image's directory), image-parent (its parent: the .mun search of sample run 9 opens <parent>\SystemResources\
// <image>.mun - tm_nt fs.par / fs.parsub), system (<SystemRoot>\System32), windows (<SystemRoot>), device (\Device). Another folder of
// the model (the drive root, \Users ...) is a context that was not measured: stop.
#include <emu/nt_kernel.hpp>

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

		std::string hx( uint64_t v )   // the measured lines' spelling (upper-case hex, no prefix)
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "%llX", (unsigned long long)v );
			return b;
		}

		std::string lower( std::string s )
		{
			for ( auto& ch : s )
				if ( ch >= 'A' && ch <= 'Z' ) ch = char( ch - 'A' + 'a' );
			return s;
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

		// one measured answer (a line of tm_nt case fs / of): the operation (service + its argument set), the kind of path and its context;
		// what Windows returned - the status, a new handle (only for an existing folder), the IoStatusBlock (written or not, its two fields)
		// and how many bytes of the output buffer were written (always a zero fill from offset 0 when not 0)
		struct vfs_row
		{
			const char* item;      // the tm_nt item ("fs.dir.of1")
			const char* op;        // "NtOpenFile 100001 40 5 60" ...
			const char* kind;
			const char* ctx;
			uint32_t status;
			int handle;            // 0 = not written, 1 = a new handle
			int iosb;              // 0 = not written, 1 = written (the two values below)
			uint32_t iosb_status;
			uint64_t iosb_info;
			uint32_t written;      // output bytes written (zero fill from offset 0)
		};

		// BEGIN MEASURED TABLE (P45, U2127; generated from Emulator/data/nt_measured_vmware_guest.txt by the coordinator's tool, checked by suite
		// nt_measured on every run - one row per recorded line, nothing added)
		const vfs_row vfs_rows[] = {
			{ "of.missing", "NtOpenFile 100020 40 3 21", "name/", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.of1", "NtOpenFile 100001 40 5 60", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.of2", "NtOpenFile 100021 840 5 60", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.of3", "NtOpenFile 100001 840 5 60", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.of4", "NtOpenFile 100020 40 3 21", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.qa1", "NtQueryAttributesFile 40", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.qa2", "NtQueryAttributesFile 840", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.qf1", "NtQueryFullAttributesFile 40", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dir.qf2", "NtQueryFullAttributesFile 840", "name", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.of1", "NtOpenFile 100001 40 5 60", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.of2", "NtOpenFile 100021 840 5 60", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.of3", "NtOpenFile 100001 840 5 60", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.of4", "NtOpenFile 100020 40 3 21", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.qa1", "NtQueryAttributesFile 40", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.qa2", "NtQueryAttributesFile 840", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.qf1", "NtQueryFullAttributesFile 40", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sub.qf2", "NtQueryFullAttributesFile 840", "path", "image", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.of1", "NtOpenFile 100001 40 5 60", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.of2", "NtOpenFile 100021 840 5 60", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.of3", "NtOpenFile 100001 840 5 60", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.of4", "NtOpenFile 100020 40 3 21", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.qa1", "NtQueryAttributesFile 40", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.qa2", "NtQueryAttributesFile 840", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.qf1", "NtQueryFullAttributesFile 40", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.sys.qf2", "NtQueryFullAttributesFile 840", "name", "system", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.of1", "NtOpenFile 100001 40 5 60", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.of2", "NtOpenFile 100021 840 5 60", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.of3", "NtOpenFile 100001 840 5 60", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.of4", "NtOpenFile 100020 40 3 21", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.qa1", "NtQueryAttributesFile 40", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.qa2", "NtQueryAttributesFile 840", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.qf1", "NtQueryFullAttributesFile 40", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.syssub.qf2", "NtQueryFullAttributesFile 840", "path", "system", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.of1", "NtOpenFile 100001 40 5 60", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.of2", "NtOpenFile 100021 840 5 60", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.of3", "NtOpenFile 100001 840 5 60", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.of4", "NtOpenFile 100020 40 3 21", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.qa1", "NtQueryAttributesFile 40", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.qa2", "NtQueryAttributesFile 840", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.qf1", "NtQueryFullAttributesFile 40", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.ads.qf2", "NtQueryFullAttributesFile 840", "stream", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.of1", "NtOpenFile 100001 40 5 60", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.of2", "NtOpenFile 100021 840 5 60", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.of3", "NtOpenFile 100001 840 5 60", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.of4", "NtOpenFile 100020 40 3 21", "stream-of-file", "image", 0xC0000103u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.qa1", "NtQueryAttributesFile 40", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.qa2", "NtQueryAttributesFile 840", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.qf1", "NtQueryFullAttributesFile 40", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.adsimg.qf2", "NtQueryFullAttributesFile 840", "stream-of-file", "image", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.of1", "NtOpenFile 100001 40 5 60", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.of2", "NtOpenFile 100021 840 5 60", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.of3", "NtOpenFile 100001 840 5 60", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.of4", "NtOpenFile 100020 40 3 21", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.qa1", "NtQueryAttributesFile 40", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.qa2", "NtQueryAttributesFile 840", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.qf1", "NtQueryFullAttributesFile 40", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dev.qf2", "NtQueryFullAttributesFile 840", "device", "device", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.of1", "NtOpenFile 100001 40 5 60", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.of2", "NtOpenFile 100021 840 5 60", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.of3", "NtOpenFile 100001 840 5 60", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.of4", "NtOpenFile 100020 40 3 21", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.qa1", "NtQueryAttributesFile 40", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.qa2", "NtQueryAttributesFile 840", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.qf1", "NtQueryFullAttributesFile 40", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.win.qf2", "NtQueryFullAttributesFile 840", "name", "windows", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.of1", "NtOpenFile 100001 40 5 60", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.of2", "NtOpenFile 100021 840 5 60", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.of3", "NtOpenFile 100001 840 5 60", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.of4", "NtOpenFile 100020 40 3 21", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.qa1", "NtQueryAttributesFile 40", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.qa2", "NtQueryAttributesFile 840", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.qf1", "NtQueryFullAttributesFile 40", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.winsub.qf2", "NtQueryFullAttributesFile 840", "path", "windows", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.of1", "NtOpenFile 100001 40 5 60", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.of2", "NtOpenFile 100021 840 5 60", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.of3", "NtOpenFile 100001 840 5 60", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.of4", "NtOpenFile 100020 40 3 21", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.qa1", "NtQueryAttributesFile 40", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.qa2", "NtQueryAttributesFile 840", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.qf1", "NtQueryFullAttributesFile 40", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.par.qf2", "NtQueryFullAttributesFile 840", "name", "image-parent", 0xC0000034u, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.of1", "NtOpenFile 100001 40 5 60", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.of2", "NtOpenFile 100021 840 5 60", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.of3", "NtOpenFile 100001 840 5 60", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.of4", "NtOpenFile 100020 40 3 21", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.cf1", "NtCreateFile 80100080 40 0 80 1 1 60 0", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.cf2", "NtCreateFile 80100080 40 0 80 7 1 60 0", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.qa1", "NtQueryAttributesFile 40", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.qa2", "NtQueryAttributesFile 840", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.qf1", "NtQueryFullAttributesFile 40", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.parsub.qf2", "NtQueryFullAttributesFile 840", "path", "image-parent", 0xC000003Au, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.open", "NtOpenFile 100001 40 7 4021", "dir/", "image", 0x00000000u, 1, 1, 0x00000000u, 0x1ull, 0 },
			{ "fs.dq.c1", "NtQueryDirectoryFile 1 1000 single 1 restart 1", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.c2", "NtQueryDirectoryFile 2 1000 single 1 restart 1", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.c3", "NtQueryDirectoryFile 3 1000 single 1 restart 1", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.c12", "NtQueryDirectoryFile 12 1000 single 1 restart 1", "nomatch", "image", 0xC000000Fu, 0, 1, 0xC000000Fu, 0x0ull, 0 },
			{ "fs.dq.c37", "NtQueryDirectoryFile 37 1000 single 1 restart 1", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.c38", "NtQueryDirectoryFile 38 1000 single 1 restart 1", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.x1", "NtQueryDirectoryFileEx 1 1000 flags 3", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.x2", "NtQueryDirectoryFileEx 2 1000 flags 3", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.x3", "NtQueryDirectoryFileEx 3 1000 flags 3", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.x12", "NtQueryDirectoryFileEx 12 1000 flags 3", "nomatch", "image", 0xC000000Fu, 0, 1, 0xC000000Fu, 0x0ull, 0 },
			{ "fs.dq.x37", "NtQueryDirectoryFileEx 37 1000 flags 3", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
			{ "fs.dq.x38", "NtQueryDirectoryFileEx 38 1000 flags 3", "nomatch", "image", 0xC000000Fu, 0, 0, 0x00000000u, 0x0ull, 0 },
		};
		// END MEASURED TABLE

		const char* const fs_services[] = { "NtOpenFile", "NtCreateFile", "NtQueryAttributesFile", "NtQueryFullAttributesFile", "NtQueryDirectoryFile",
											"NtQueryDirectoryFileEx", "NtQueryInformationFile", "NtSetInformationFile", "NtQueryVolumeInformationFile", "NtReadFile",
											"NtDeleteFile", "NtQueryEaFile", "NtSetEaFile", "NtFsControlFile", "NtDeviceIoControlFile", "NtLockFile", "NtUnlockFile",
											"NtFlushBuffersFile", "NtNotifyChangeDirectoryFile", "NtOpenFileEx" };
	}

	bool kernel::fs_service( const std::string& name )
	{
		for ( const char* s : fs_services )
			if ( name == s ) return true;
		return false;
	}

	// the path as the fs_access log shows it: the image's directory and the user profile replaced by placeholders (they are placeholders of
	// the environment already - the emulated identity - but the log names them by role)
	std::string kernel::fs_shown( const std::string& path ) const
	{
		std::string out = path, lo = lower( path );
		auto repl = [ & ]( const std::string& what, const std::string& by ) {
			if ( what.empty() ) return;
			const std::string lw = lower( what );
			for ( size_t at = lo.find( lw ); at != std::string::npos; at = lo.find( lw, at + by.size() ) )
			{
				out.replace( at, what.size(), by );
				lo.replace( at, lw.size(), by );
			}
		};
		std::string dir = f_.image_dir;
		while ( !dir.empty() && dir.back() == '\\' ) dir.pop_back();
		repl( dir, "<image dir>" );
		const size_t u = lower( dir ).find( "\\users\\" );
		if ( u != std::string::npos )
		{
			const size_t e = dir.find( '\\', u + 7 );
			repl( dir.substr( 0, e == std::string::npos ? dir.size() : e ), "<user profile>" );
		}
		return out;
	}

	// record one file-system access (observation only): time = the instruction count of the call, the service, the path (by OBJECT_ATTRIBUTES
	// or by handle), the access / options, the answer
	void kernel::fs_record( const call& c, const reply& r )
	{
		fs_access a;
		a.index = fs_log_.size() + 1;
		a.insns = c.insns;
		a.service = c.name;
		object_attributes oa;
		std::string why;
		const bool by_oa = c.name == "NtOpenFile" || c.name == "NtCreateFile" || c.name == "NtQueryAttributesFile" || c.name == "NtQueryFullAttributesFile" ||
						   c.name == "NtDeleteFile" || c.name == "NtOpenFileEx";
		const int oa_arg = ( c.name == "NtOpenFile" || c.name == "NtCreateFile" || c.name == "NtOpenFileEx" ) ? 2 : 0;
		if ( by_oa )
		{
			if ( guest_object_attributes( c.args[ size_t( oa_arg ) ], oa, why ) )
				a.path = ( oa.root ? "<relative to handle " + hex( oa.root ) + "> " : std::string() ) + fs_shown( oa.name );
			else
				a.path = "<unreadable: " + why + ">";
			a.args = "attributes " + hx( oa.attributes );
		}
		else
		{
			const auto it = handles_.find( c.args[ 0 ] );
			a.path = it != handles_.end() ? "<handle " + hex( c.args[ 0 ] ) + ": " + it->second.kind + " " + fs_shown( it->second.name ) + ">" : "<handle " + hex( c.args[ 0 ] ) + ">";
		}
		if ( c.name == "NtOpenFile" ) a.args = "access " + hx( uint32_t( c.args[ 1 ] ) ) + "H, " + a.args + "H, share " + hx( uint32_t( c.args[ 4 ] ) ) + "H, options " + hx( uint32_t( c.args[ 5 ] ) ) + "H";
		else if ( c.name == "NtCreateFile" )
			a.args = "access " + hx( uint32_t( c.args[ 1 ] ) ) + "H, " + a.args + "H, file attributes " + hx( uint32_t( c.args[ 5 ] ) ) + "H, share " + hx( uint32_t( c.args[ 6 ] ) ) +
					 "H, disposition " + hx( uint32_t( c.args[ 7 ] ) ) + "H, options " + hx( uint32_t( c.args[ 8 ] ) ) + "H";
		else if ( by_oa ) a.args += "H";
		else if ( c.name == "NtQueryDirectoryFile" || c.name == "NtQueryDirectoryFileEx" )
			a.args = "class " + std::to_string( uint32_t( c.args[ 7 ] ) ) + ", Length " + hx( uint32_t( c.args[ 6 ] ) ) + "H" +
					 ( c.name == "NtQueryDirectoryFileEx" ? ", flags " + hx( uint32_t( c.args[ 8 ] ) ) + "H" : ", single " + std::to_string( uint8_t( c.args[ 8 ] ) ) + ", restart " + std::to_string( uint8_t( c.args[ 10 ] ) ) );
		else if ( c.name == "NtQueryInformationFile" || c.name == "NtSetInformationFile" ) a.args = "class " + std::to_string( uint32_t( c.args[ 4 ] ) ) + ", Length " + hx( uint32_t( c.args[ 3 ] ) ) + "H";
		else if ( c.name == "NtQueryVolumeInformationFile" ) a.args = "class " + std::to_string( uint32_t( c.args[ 4 ] ) ) + ", Length " + hx( uint32_t( c.args[ 3 ] ) ) + "H";
		else if ( c.name == "NtReadFile" ) a.args = "Length " + hx( uint32_t( c.args[ 6 ] ) ) + "H";
		else if ( c.name == "NtDeviceIoControlFile" )   // P47 (U2166): the device request
			a.args = "IoControlCode " + hx( uint32_t( c.args[ 5 ] ) ) + "H, InputBufferLength " + hx( uint32_t( c.args[ 7 ] ) ) + "H, OutputBufferLength " +
					 hx( uint32_t( c.args[ 9 ] ) ) + "H";
		char st[ 16 ];
		std::snprintf( st, sizeof( st ), "%08X", r.status );
		if ( r.kind == reply_kind::done ) a.answer = std::string( "status " ) + st + "H";
		else if ( r.kind == reply_kind::stop ) a.answer = "STOP (not answered: " + r.text.substr( 0, 300 ) + ( r.text.size() > 300 ? "..." : "" ) + ")";
		else a.answer = "reply kind " + std::to_string( int( r.kind ) );
		fs_log_.push_back( std::move( a ) );
	}

	// the kind of an NT path in the model (see the file header); false = a kind of path that was not measured (WHY says which)
	bool kernel::vfs_classify( const object_attributes& oa, std::string& kind, std::string& ctx, std::string& why ) const
	{
		if ( oa.root ) { why = "a path relative to a handle (RootDirectory " + hex( oa.root ) + ")"; return false; }
		const std::string n = lower( oa.name );
		if ( n.compare( 0, 8, "\\device\\" ) == 0 )
		{
			const std::string rest = n.substr( 8 );
			if ( rest.empty() || rest.find( '\\' ) != std::string::npos || rest.find( '?' ) != std::string::npos || rest.find( ':' ) != std::string::npos )
			{
				why = "a device path with more than one name";
				return false;
			}
			kind = "device";
			ctx = "device";
			return true;
		}
		if ( n.size() < 7 || n.compare( 0, 4, "\\??\\" ) != 0 || !( n[ 4 ] >= 'a' && n[ 4 ] <= 'z' ) || n[ 5 ] != ':' || n[ 6 ] != '\\' )
		{
			why = "not a \\??\\<drive>:\\ path (a UNC / GLOBALROOT / volume / other namespace path)";
			return false;
		}
		std::string p = n.substr( 4 );
		bool trailing = false;
		if ( p.size() > 3 && p.back() == '\\' )
		{
			trailing = true;
			p.pop_back();
		}
		if ( p.find_first_of( "*?<>\"|" ) != std::string::npos ) { why = "a wildcard / reserved / non-ASCII character in the path"; return false; }
		std::vector<std::string> comp;
		for ( size_t at = 3, nx; at <= p.size(); at = nx + 1 )
		{
			nx = p.find( '\\', at );
			if ( nx == std::string::npos ) nx = p.size();
			comp.push_back( p.substr( at, nx - at ) );
		}
		for ( size_t i = 0; i < comp.size(); i++ )
		{
			const std::string& s = comp[ i ];
			if ( s.empty() || s == "." || s == ".." || s.back() == ' ' || s.back() == '.' ) { why = "an empty / '.' / '..' / trailing-dot or -space component"; return false; }
			if ( i + 1 < comp.size() && s.find( ':' ) != std::string::npos ) { why = "a ':' in a folder component"; return false; }
		}
		// the model's folders: every folder on the way to the image and to System32; the roles of the measured contexts
		std::string img_dir = lower( f_.image_dir ), sys_root = lower( f_.system_root );
		while ( !img_dir.empty() && img_dir.back() == '\\' ) img_dir.pop_back();
		while ( !sys_root.empty() && sys_root.back() == '\\' ) sys_root.pop_back();
		const std::string sys_dir = sys_root + "\\system32";
		auto is_model_dir = [ & ]( const std::string& d ) {
			for ( const std::string& m : { img_dir, sys_dir } )
				if ( m == d || ( m.size() > d.size() && m.compare( 0, d.size(), d ) == 0 && m[ d.size() ] == '\\' ) ) return true;
			return false;
		};
		auto is_model_file = [ & ]( const std::string& dir, const std::string& leaf ) {
			if ( dir == img_dir && leaf == lower( f_.image_name ) ) return true;
			return dir == sys_dir && f_.files && f_.files->count( leaf ) != 0;
		};
		const size_t cut = img_dir.rfind( '\\' );
		const std::string img_parent = cut != std::string::npos && cut > 2 ? img_dir.substr( 0, cut ) : std::string();
		auto role = [ & ]( const std::string& d ) -> std::string {
			if ( d == img_dir ) return "image";
			if ( d == sys_dir ) return "system";
			if ( d == sys_root ) return "windows";
			if ( !img_parent.empty() && d == img_parent ) return "image-parent";   // the .mun search: <parent>\SystemResources\<image>.mun
			return "";
		};
		std::string cur = p.substr( 0, 2 );
		if ( !is_model_dir( cur ) ) { why = "a volume the model does not hold (" + cur + ")"; return false; }
		for ( size_t i = 0; i + 1 < comp.size(); i++ )
		{
			const std::string next = cur + "\\" + comp[ i ];
			if ( is_model_dir( next ) ) { cur = next; continue; }
			if ( is_model_file( cur, comp[ i ] ) ) { why = "a file of the model used as a folder"; return false; }
			ctx = role( cur );
			if ( ctx.empty() ) { why = "a missing folder below " + cur + " (a folder of the model whose misses were not measured)"; return false; }
			kind = "path";
			return true;
		}
		const std::string& last = comp.back();
		const size_t colon = last.find( ':' );
		const std::string base = last.substr( 0, colon );
		ctx = role( cur );
		if ( colon != std::string::npos )
		{
			if ( trailing || base.empty() || last.find( ':', colon + 1 ) != std::string::npos || colon + 1 >= last.size() ) { why = "a stream name of another form"; return false; }
			if ( is_model_dir( cur + "\\" + base ) ) { why = "a stream of a folder of the model"; return false; }
			kind = is_model_file( cur, base ) ? "stream-of-file" : "stream";
		}
		else if ( is_model_dir( cur + "\\" + base ) )
		{
			kind = trailing ? "dir/" : "dir";
			ctx = role( cur + "\\" + base );   // a folder of the model: its own role
		}
		else if ( is_model_file( cur, base ) ) kind = trailing ? "file/" : "file";
		else kind = trailing ? "name/" : "name";
		if ( ctx.empty() ) { why = "a name in " + cur + " (a folder of the model whose misses were not measured)"; return false; }
		return true;
	}

	// answer a file-system call from the measured table: OP is the call's argument set, PH / IOSB the FileHandle / IoStatusBlock pointers (0 =
	// none), INFO / INFO_LEN the output buffer (0 = none); probes like the kernel (a bad pointer = STATUS_ACCESS_VIOLATION, nothing written)
	reply kernel::vfs_answer( const object_attributes& oa, const std::string& op, uint64_t ph, uint64_t iosb, uint64_t info, uint64_t info_len, const std::string& what )
	{
		std::string kind, ctx, why;
		if ( !vfs_classify( oa, kind, ctx, why ) )
			return stop_( what + " - the emulated file system: " + why + " - this kind of path was not measured (tm_nt fs)" );
		const bool in_model = kind == "file" || kind == "file/" || kind == "dir" || kind == "dir/";
		const vfs_row* row = nullptr;
		for ( const auto& t : vfs_rows )
			if ( op == t.op && kind == t.kind && ctx == t.ctx ) row = &t;
		if ( !row && in_model )
			return stop_( what + " - a path IN the emulated file system (" + kind + ", context '" + ctx + "'): the operation \"" + op + "\" on it was not measured" );
		if ( !row )
			return stop_( what + " - the emulated file system: a miss of kind '" + kind + "' in context '" + ctx + "', but the argument set \"" + op + "\" was not measured (tm_nt fs)" );
		if ( ( ph && !user_buffer( ph, 8, UC_PROT_WRITE ) ) || ( iosb && !user_buffer( iosb, 16, UC_PROT_WRITE ) ) || ( info && !user_buffer( info, info_len, UC_PROT_WRITE ) ) )
			return done_( status::access_violation, what + ": an output pointer is not writable user memory (probe)" );
		if ( row->written > info_len ) return stop_( what + " - the measured answer writes " + std::to_string( row->written ) + " bytes, the buffer has " + std::to_string( info_len ) );
		char st[ 16 ];
		std::snprintf( st, sizeof( st ), "%08X", row->status );
		reply r = done_( row->status, what + ": " + ( in_model ? "a folder of the model" : "a miss of kind '" + kind + "' in context '" + ctx + "'" ) + " - status " + st + "H" +
										  ( row->handle ? ", a handle" : "" ) + ( row->iosb ? ", IoStatus written" : ", IoStatus not written" ) +
										  ( row->written ? ", " + std::to_string( row->written ) + " bytes zero" : std::string( info ? ", nothing written" : "" ) ) + " (measured: tm_nt " + row->item + ")" );
		if ( row->handle )
		{
			object o;
			o.kind = "file (directory)";
			o.name = f_.image_dir;
			o.value = 1;   // opened for a listing (the measured FindFirstFileExW open); 3 once a query was answered
			const uint64_t h = open_handle( o );
			if ( !put( ph, &h, 8, r ) ) return stop_( "writing the handle failed after the probe" );
		}
		if ( row->iosb )
		{
			// the measured IoStatusBlock: Status (4 bytes) and Information (8 bytes) - the upper half of the Status / Pointer union is NOT
			// written (tm_nt fs.dq.*: "upper half not written")
			const uint32_t ios = row->iosb_status;
			const uint64_t ioi = row->iosb_info;
			if ( !put( iosb, &ios, 4, r ) || !put( iosb + 8, &ioi, 8, r ) ) return stop_( "writing the IoStatusBlock failed after the probe" );
		}
		if ( row->written )
		{
			std::vector<uint8_t> z( row->written, 0 );
			if ( !put( info, z.data(), z.size(), r ) ) return stop_( "writing the output failed after the probe" );
		}
		return r;
	}

	// NtOpenFile / NtCreateFile / NtQueryAttributesFile / NtQueryFullAttributesFile of a path the measured handlers of nt_kernel_measured.cpp
	// do not answer (the captured System32 files, the loader's open of the image's directory) - the model above
	reply kernel::vfs_open_file( const call& c, const object_attributes& oa, const std::string& what )
	{
		const std::string op = "NtOpenFile " + hx( uint32_t( c.args[ 1 ] ) ) + " " + hx( oa.attributes ) + " " + hx( uint32_t( c.args[ 4 ] ) ) + " " + hx( uint32_t( c.args[ 5 ] ) );
		return vfs_answer( oa, op, c.args[ 0 ], c.args[ 3 ], 0, 0, what );
	}

	reply kernel::vfs_create_file( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtCreateFile: the stack arguments are not readable" );
		object_attributes oa;
		std::string why;
		if ( !guest_object_attributes( c.args[ 2 ], oa, why ) ) return done_( status::access_violation, why );
		const std::string op = "NtCreateFile " + hx( uint32_t( c.args[ 1 ] ) ) + " " + hx( oa.attributes ) + " " + ( c.args[ 4 ] ? std::string( "alloc" ) : std::string( "0" ) ) + " " +
							   hx( uint32_t( c.args[ 5 ] ) ) + " " + hx( uint32_t( c.args[ 6 ] ) ) + " " + hx( uint32_t( c.args[ 7 ] ) ) + " " + hx( uint32_t( c.args[ 8 ] ) ) + " " +
							   ( c.args[ 9 ] || uint32_t( c.args[ 10 ] ) ? std::string( "ea" ) : std::string( "0" ) );
		const std::string what = "NtCreateFile( access " + hx( uint32_t( c.args[ 1 ] ) ) + "H, \"" + oa.name + "\" " + hx( oa.attributes ) + "H, AllocationSize " + hex( c.args[ 4 ] ) +
								 ", attributes " + hx( uint32_t( c.args[ 5 ] ) ) + "H, share " + hx( uint32_t( c.args[ 6 ] ) ) + "H, disposition " + hx( uint32_t( c.args[ 7 ] ) ) +
								 "H, options " + hx( uint32_t( c.args[ 8 ] ) ) + "H, EaBuffer " + hex( c.args[ 9 ] ) + " / " + hx( uint32_t( c.args[ 10 ] ) ) + "H )";
		{
			reply cr;
			if ( cm_open( c, oa, what, cr ) ) return cr;   // p3cm (U2281): \Device\DeviceApi\CMApi, the configuration-manager device of the environment
		}
		return vfs_answer( oa, op, c.args[ 0 ], c.args[ 3 ], 0, 0, what );
	}

	reply kernel::vfs_query_attributes( const call& c, const object_attributes& oa, bool full, const std::string& what )
	{
		const std::string op = std::string( full ? "NtQueryFullAttributesFile " : "NtQueryAttributesFile " ) + hx( oa.attributes );
		return vfs_answer( oa, op, 0, 0, c.args[ 1 ], full ? 0x38 : 0x28, what );
	}

	reply kernel::svc_query_full_attributes_file( const call& c )
	{
		object_attributes oa;
		std::string why;
		if ( !guest_object_attributes( c.args[ 0 ], oa, why ) ) return done_( status::access_violation, why );
		const std::string what = "NtQueryFullAttributesFile( \"" + oa.name + "\", attributes " + hex( oa.attributes ) + ", FileInformation " + hex( c.args[ 1 ] ) + " )";
		return vfs_query_attributes( c, oa, true, what );
	}

	// NtQueryDirectoryFile( FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass,
	// ReturnSingleEntry, FileName, RestartScan ) - phnt ntioapi.h:2509-2522; NtQueryDirectoryFileEx( ..., FileInformationClass, QueryFlags,
	// FileName ) - :2576-2588. The model: the FIRST query on a handle of the image's directory opened as FindFirstFileExW opens it (tm_nt
	// fs.dq.open), with a FileName that names no entry of that directory (the directory holds only the image) and no wildcard - the measured
	// answer of that class / length / flags (tm_nt fs.dq.c* / fs.dq.x*). A further query on the handle, a name of the image, a wildcard,
	// an event / APC, another directory: not measured - stop.
	reply kernel::svc_query_directory_file( const call& c, bool ex )
	{
		const std::string svc = ex ? "NtQueryDirectoryFileEx" : "NtQueryDirectoryFile";
		if ( !c.stack_args ) return stop_( svc + ": the stack arguments are not readable" );
		const auto it = handles_.find( c.args[ 0 ] );
		std::string mask, why;
		if ( c.args[ 9 ] && !guest_ustring( c.args[ 9 ], mask, why ) ) return done_( status::access_violation, svc + ": FileName " + why );
		const std::string op = ex ? svc + " " + std::to_string( uint32_t( c.args[ 7 ] ) ) + " " + hx( uint32_t( c.args[ 6 ] ) ) + " flags " + hx( uint32_t( c.args[ 8 ] ) )
								  : svc + " " + std::to_string( uint32_t( c.args[ 7 ] ) ) + " " + hx( uint32_t( c.args[ 6 ] ) ) + " single " + std::to_string( uint8_t( c.args[ 8 ] ) ) +
										" restart " + std::to_string( uint8_t( c.args[ 10 ] ) );
		const std::string what = svc + "( " + hex( c.args[ 0 ] ) + ( it != handles_.end() ? " (" + it->second.kind + ")" : std::string() ) + ", class " + std::to_string( uint32_t( c.args[ 7 ] ) ) +
								 ", Length " + hx( uint32_t( c.args[ 6 ] ) ) + "H, FileName \"" + mask + "\" ) [" + op + "]";
		if ( it == handles_.end() || it->second.kind != "file (directory)" || it->second.value != 1 )
			return stop_( what + " - not a handle of the image's directory opened for a listing, or not its first query: not measured (tm_nt fs.dq)" );
		if ( c.args[ 1 ] || c.args[ 2 ] || c.args[ 3 ] ) return stop_( what + " - an event / APC: not measured" );
		if ( !c.args[ 9 ] || mask.empty() || mask.find_first_of( "*?<>\"" ) != std::string::npos || lower( mask ) == lower( f_.image_name ) )
			return stop_( what + " - no FileName, a wildcard or the image's own name: the directory's entries are not measured (only a name nothing matches is)" );
		const vfs_row* row = nullptr;
		for ( const auto& t : vfs_rows )
			if ( op == t.op && std::string( t.kind ) == "nomatch" ) row = &t;
		if ( !row ) return stop_( what + " - this class / length / flag set was not measured (tm_nt fs.dq)" );
		const uint64_t iosb = c.args[ 4 ], info = c.args[ 5 ], len = uint32_t( c.args[ 6 ] );
		if ( !user_buffer( iosb, 16, UC_PROT_WRITE ) || ( len && !user_buffer( info, len, UC_PROT_WRITE ) ) ) return done_( status::access_violation, what + ": an output pointer is not writable (probe)" );
		char st[ 16 ];
		std::snprintf( st, sizeof( st ), "%08X", row->status );
		reply r = done_( row->status, what + ": no entry of the image's directory matches - status " + st + "H (measured: tm_nt " + row->item + ")" );
		it->second.value = 3;
		if ( row->iosb )
		{
			// the measured IoStatusBlock: Status (4 bytes) and Information (8 bytes) - the upper half of the Status / Pointer union is NOT
			// written (tm_nt fs.dq.*: "upper half not written")
			const uint32_t ios = row->iosb_status;
			const uint64_t ioi = row->iosb_info;
			if ( !put( iosb, &ios, 4, r ) || !put( iosb + 8, &ioi, 8, r ) ) return stop_( "writing the IoStatusBlock failed after the probe" );
		}
		if ( row->written )
		{
			if ( row->written > len ) return stop_( what + " - the measured answer writes more than the buffer" );
			std::vector<uint8_t> z( row->written, 0 );
			if ( !put( info, z.data(), z.size(), r ) ) return stop_( "writing the output failed after the probe" );
		}
		return r;
	}

	// ------------------------------------------------------------------------------------------------ \Device\KsecDD (P47)
	// USER DECISION 2026-10-28 (P47 STEP 2, "\Device\KsecDD: MODEL IT AS A DEVICE"): the kernel security device is a DEVICE of the
	// environment - the only device the emulated file system holds. Sample run 11 system call 759: bcrypt.dll's DllMain (bcrypt.dll+3AA4 ..
	// +6490) opens it. Names / codes: phnt ntseapi.h:1458-1488 (KSEC_DEVICE_NAME, IOCTL_KSEC_* = CTL_CODE( FILE_DEVICE_KSEC 39H, function,
	// method, access ) - shared\devioctl.h CTL_CODE: DeviceType << 16 | Access << 14 | Function << 2 | Method); the services' prototypes phnt
	// ntioapi.h NtOpenFile / NtDeviceIoControlFile. Every answer is MEASURED natively from inside our own test PE (tm_nt case ksec, decision
	// 3.0.7): the open with bcrypt's exact arguments (U2165), each device request with the code / lengths bcrypt sends (U2166). A random output
	// (the random-number request) comes from the environment's seeded generator (emu/ctr_drbg.hpp, plan 3.M6: CTR_DRBG of the seed of decision
	// D8, personalization "NoVmp environment RNG device", environment_random - a model choice with the measured shape, never host entropy). A request that was
	// not measured, other open arguments, another device: a loud stop (the process layer adds the state dump). Every access is in fs_access.txt.
	// P47: the second device, \Device\CNG (the CNG driver), is opened by bcryptprimitives.dll after bcrypt's provider resolution (test PE T9,
	// BCryptGenRandom): MEASURED tm_nt ksec.cng.open - NtOpenFile( 100001H, "\Device\CNG" attributes 40H, share 7, options 20H ) = STATUS_SUCCESS, a
	// handle, IoStatus {Status 0 (the upper half not written), Information 0} - the same shape as the kernel security device. Modelled under the
	// same USER DECISION (2026-10-28: the system RNG path's devices; noted in notes/eread/P47_ksecdd_run12.md).
	bool kernel::ksec_open( const call& c, const object_attributes& oa, const std::string& what, reply& r )
	{
		const std::string ln = lower( oa.name );
		if ( oa.root || ( ln != "\\device\\ksecdd" && ln != "\\device\\cng" ) ) return false;
		const bool cng = ln == "\\device\\cng";
		const uint32_t access = uint32_t( c.args[ 1 ] ), share = uint32_t( c.args[ 4 ] ), options = uint32_t( c.args[ 5 ] );
		if ( !cng && ( oa.name != "\\Device\\KsecDD" || access != 0x100003 || oa.attributes != 0 || share != 7 || options != 0x20 ) )
		{
			r = stop_( what + " - the kernel security device with other arguments / another spelling: not measured (tm_nt ksec.open: \"\\Device\\KsecDD\", 100003H, "
							  "attributes 0, share 7, options 20H - bcrypt's DllMain)" );
			return true;
		}
		if ( cng && ( oa.name != "\\Device\\CNG" || access != 0x100001 || oa.attributes != 0x40 || share != 7 || options != 0x20 ) )
		{
			r = stop_( what + " - the CNG device with other arguments / another spelling: not measured (tm_nt ksec.cng.open: \"\\Device\\CNG\", 100001H, attributes 40H, "
							  "share 7, options 20H - bcryptprimitives.dll)" );
			return true;
		}
		const uint64_t ph = c.args[ 0 ], iosb = c.args[ 3 ];
		if ( !user_buffer( ph, 8, UC_PROT_WRITE ) || !user_buffer( iosb, 16, UC_PROT_WRITE ) )
		{
			r = stop_( what + " - FileHandle / IoStatusBlock not writable: not measured for the device" );
			return true;
		}
		object o;
		o.kind = cng ? "device (CNG)" : "device (KsecDD)";
		o.name = oa.name;
		const uint64_t h = open_handle( o );
		r = done_( status::success, what + ": the " + ( cng ? "CNG" : "kernel security" ) + " device, handle " + hex( h ) + ", IoStatus {Status 0 (the upper half not written), "
										  "Information 0} (measured: tm_nt " + ( cng ? "ksec.cng.open" : "ksec.open" ) + ")" );
		// the measured IoStatusBlock: Status (4 bytes) = 0, the upper half of the Status / Pointer union NOT written, Information (8 bytes) = 0
		const uint32_t ios = 0;
		const uint64_t ioi = 0;
		if ( !put( ph, &h, 8, r ) || !put( iosb, &ios, 4, r ) || !put( iosb + 8, &ioi, 8, r ) ) r = stop_( "writing the result failed after the probe" );
		return true;
	}

	// NtDeviceIoControlFile( FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, IoControlCode, InputBuffer, InputBufferLength,
	// OutputBuffer, OutputBufferLength ) - phnt ntioapi.h NtDeviceIoControlFile (the WDK's ZwDeviceIoControlFile documents the call)
	reply kernel::svc_device_io_control_file( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtDeviceIoControlFile: the stack arguments are not readable" );
		const auto it = handles_.find( c.args[ 0 ] );
		const uint32_t code = uint32_t( c.args[ 5 ] ), inlen = uint32_t( c.args[ 7 ] ), outlen = uint32_t( c.args[ 9 ] );
		const uint64_t iosb = c.args[ 4 ], in = c.args[ 6 ], out = c.args[ 8 ];
		const std::string what = "NtDeviceIoControlFile( " + hex( c.args[ 0 ] ) + ( it != handles_.end() ? " (" + it->second.kind + " " + it->second.name + ")" : std::string() ) +
								 ", Event " + hex( c.args[ 1 ] ) + ", ApcRoutine " + hex( c.args[ 2 ] ) + ", ApcContext " + hex( c.args[ 3 ] ) + ", IoStatusBlock " + hex( iosb ) +
								 ", IoControlCode " + hx( code ) + "H (DeviceType " + hx( code >> 16 ) + "H, Access " + std::to_string( ( code >> 14 ) & 3 ) + ", Function " +
								 std::to_string( ( code >> 2 ) & 0xFFF ) + ", Method " + std::to_string( code & 3 ) + "), InputBuffer " + hex( in ) + " / " + hx( inlen ) +
								 "H, OutputBuffer " + hex( out ) + " / " + hx( outlen ) + "H )";
		if ( it == handles_.end() || ( it->second.kind != "device (KsecDD)" && it->second.kind != "device (CNG)" && it->second.kind != "device (CMApi)" ) )   // p3cm (U2281): + CMApi
			return stop_( what + " - not a handle of a device of the environment (\\Device\\KsecDD, \\Device\\CNG, \\Device\\DeviceApi\\CMApi): not modelled" );
		if ( c.args[ 1 ] || c.args[ 2 ] || c.args[ 3 ] ) return stop_( what + " - an event / APC on the device: not measured" );
		std::string inbytes;
		if ( in && inlen && inlen <= 0x100 && user_buffer( in, inlen, UC_PROT_READ ) )
		{
			uint8_t b[ 0x100 ] = {};
			std::string err;
			if ( e_.read( in, b, inlen, err ) )
			{
				inbytes = ", input bytes";
				for ( uint32_t i = 0; i < inlen; i++ )
				{
					char t[ 4 ];
					std::snprintf( t, sizeof( t ), " %02X", b[ i ] );
					inbytes += t;
				}
			}
		}
		reply r;
		if ( it->second.kind == "device (CMApi)" )   // p3cm (U2281): the configuration-manager device - only cfgmgr32's measured start-up requests
		{
			if ( cm_request( c, what, r ) ) return r;
			return stop_( what + inbytes + " - a request on the configuration-manager device OUTSIDE the measured start-up set of cfgmgr32.dll (tm_nt cm): not answered - a "
								  "later request (device-tree content, machine identity) needs the user's decision" );
		}
		if ( ksec_request( c, what, r ) ) return r;
		return stop_( what + inbytes + " - a request on the device that was not measured (tm_nt ksec): not answered" );
	}

	// the measured requests on the kernel security device (tm_nt ksec.*); false = not measured (the caller stops loudly)
	bool kernel::ksec_request( const call& c, const std::string& what, reply& r )
	{
		const uint32_t code = uint32_t( c.args[ 5 ] ), inlen = uint32_t( c.args[ 7 ] ), outlen = uint32_t( c.args[ 9 ] );
		const uint64_t iosb = c.args[ 4 ], in = c.args[ 6 ], out = c.args[ 8 ];
		const auto dv = handles_.find( c.args[ 0 ] );
		if ( dv == handles_.end() ) return false;
		if ( dv->second.kind == "device (CNG)" ) return cng_request( c, what, r );
		if ( code == 0x390400 && inlen == 0x68 )
		{
			// IoControlCode 390400H = CTL_CODE( 39H FILE_DEVICE_KSEC, function 100H, METHOD_BUFFERED, FILE_ANY_ACCESS ) - a function phnt
			// ntseapi.h:1461-1488 does not list (IOCTL_KSEC_* end at function 27) and no public document names: the request as bcrypt.dll's
			// DllMain sends it (test PE T9 exploration x4 system call 220; sample run 12): 68H input bytes {+0 ULONG 1A2B3C4DH, +4 ULONG
			// 00010500H, +8 0, +10H -1, +18H 0, +20H / +28H / +30H -1, +38H 0, +40H / +48H -1, +50H 0, +58H -1, +60H the handle of a
			// synchronization event of the process}, an 8-byte output. MEASURED from inside (tm_nt ksec.cfg, the same bytes with its own
			// event, twice): STATUS_SUCCESS, IoStatus {Status 0 (the upper half not written), Information 0}, the output NOT written, the input
			// unchanged, the event NOT signaled (at once, after 200 ms, after the device handle's close). The event is recorded on the device
			// handle and never signaled here (no change of the emulated environment's configuration exists). Another input / length, an
			// unwritable buffer, a handle that is not a synchronization event: not measured - stop.
			if ( inlen != 0x68 || outlen != 8 || !in || !out ) return false;
			if ( !user_buffer( in, 0x68, UC_PROT_READ ) || !user_buffer( out, 8, UC_PROT_WRITE ) || !user_buffer( iosb, 16, UC_PROT_WRITE ) )
			{
				r = stop_( what + " - an input / output / IoStatusBlock buffer is not accessible: not measured" );
				return true;
			}
			uint8_t b[ 0x68 ] = {};
			std::string err;
			if ( !e_.read( in, b, 0x68, err ) )
			{
				r = stop_( what + " - the input is not readable: " + err );
				return true;
			}
			static const uint8_t pat[ 12 ] = { 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 2 };   // the 8-byte fields from +8: 0 zero, 1 -1, 2 the event handle
			uint32_t magic = 0, ver = 0;
			std::memcpy( &magic, b, 4 );
			std::memcpy( &ver, b + 4, 4 );
			uint64_t ev = 0;
			bool shape = magic == 0x1A2B3C4D && ver == 0x00010500;
			for ( int k = 0; k < 12 && shape; k++ )
			{
				uint64_t v = 0;
				std::memcpy( &v, b + 8 + 8 * k, 8 );
				if ( pat[ k ] == 2 ) ev = v;
				else if ( v != ( pat[ k ] ? ~0ull : 0ull ) ) shape = false;
			}
			const auto evh = handles_.find( ev );
			if ( !shape || evh == handles_.end() || evh->second.kind != "event (synchronization)" )
				return false;
			r = done_( status::success, what + ": the 68H-byte request naming the synchronization event " + hex( ev ) + " - STATUS_SUCCESS, IoStatus {0, Information 0}, the output "
											  "not written, the event not signaled (measured: tm_nt ksec.cfg)" );
			auto& dev = handles_[ c.args[ 0 ] ];
			dev.value = ev;   // the event the device would signal (never: the emulated environment's configuration does not change)
			const uint32_t ios = 0;
			const uint64_t ioi = 0;
			if ( !put( iosb, &ios, 4, r ) || !put( iosb + 8, &ioi, 8, r ) ) r = stop_( "writing the IoStatusBlock failed after the probe" );
			return true;
		}
		// the same IoControlCode with the CNG configuration queries of bcrypt.dll's BCryptGenRandom( NULL, ..., BCRYPT_USE_SYSTEM_PREFERRED_RNG )
		// (test PE T9 explorations x5 / x7): an input {+0 ULONG 1A2B3C4DH, +4 ULONG 00020000H, ...} and a 180H-byte output. MEASURED from inside
		// (tm_nt ksec.<item>.1-3: three requests on one handle, measured twice): STATUS_SUCCESS, IoStatus {Status 0 (the upper half not
		// written), Information = the answer's length}, exactly that many output bytes written, EQUAL in every request - CNG's resolution of the
		// algorithm "RNG": self-relative offsets (no pointer), "Microsoft Primitive Provider", "bcryptprimitives.dll" (the system's
		// configuration: not random, no identifier). Only an input equal to a measured one, byte for byte, with the measured output length is
		// answered; another one, an inaccessible buffer: not measured - stop.
		// BEGIN KSEC QUERY TABLE (P47, U2166; generated by the coordinator's tool from the recorded tm_nt ksec.<item> lines - the dump of each
		// answer, its IoStatus and its "equal in all three requests" shape; checked by emu-selftest nt_measured_ksec)
		// tm_nt ksec.res - T9 exploration x5 system call 228: the resolution of "RNG"
		static const uint8_t ksec_in_res[ 0x30 ] = {
			0x4D, 0x3C, 0x2B, 0x1A, 0x00, 0x00, 0x02, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		};
		static const uint8_t ksec_out_res[ 0xD8 ] = {
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
			0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0x52, 0x00, 0x4E, 0x00, 0x47, 0x00, 0x00, 0x00, 0x4D, 0x00, 0x69, 0x00, 0x63, 0x00, 0x72, 0x00,
			0x6F, 0x00, 0x73, 0x00, 0x6F, 0x00, 0x66, 0x00, 0x74, 0x00, 0x20, 0x00, 0x50, 0x00, 0x72, 0x00,
			0x69, 0x00, 0x6D, 0x00, 0x69, 0x00, 0x74, 0x00, 0x69, 0x00, 0x76, 0x00, 0x65, 0x00, 0x20, 0x00,
			0x50, 0x00, 0x72, 0x00, 0x6F, 0x00, 0x76, 0x00, 0x69, 0x00, 0x64, 0x00, 0x65, 0x00, 0x72, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x00, 0x63, 0x00, 0x72, 0x00, 0x79, 0x00,
			0x70, 0x00, 0x74, 0x00, 0x70, 0x00, 0x72, 0x00, 0x69, 0x00, 0x6D, 0x00, 0x69, 0x00, 0x74, 0x00,
			0x69, 0x00, 0x76, 0x00, 0x65, 0x00, 0x73, 0x00, 0x2E, 0x00, 0x64, 0x00, 0x6C, 0x00, 0x6C, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		};
		// tm_nt ksec.q2 - T9 exploration x7 system call 229: {.., +18H 30H, +28H 1 / 2, +30H L"RNG"}
		static const uint8_t ksec_in_q2[ 0x38 ] = {
			0x4D, 0x3C, 0x2B, 0x1A, 0x00, 0x00, 0x02, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
			0x52, 0x00, 0x4E, 0x00, 0x47, 0x00, 0x00, 0x00
		};
		static const uint8_t ksec_out_q2[ 0xD8 ] = {
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0x52, 0x00, 0x4E, 0x00, 0x47, 0x00, 0x00, 0x00, 0x4D, 0x00, 0x69, 0x00, 0x63, 0x00, 0x72, 0x00,
			0x6F, 0x00, 0x73, 0x00, 0x6F, 0x00, 0x66, 0x00, 0x74, 0x00, 0x20, 0x00, 0x50, 0x00, 0x72, 0x00,
			0x69, 0x00, 0x6D, 0x00, 0x69, 0x00, 0x74, 0x00, 0x69, 0x00, 0x76, 0x00, 0x65, 0x00, 0x20, 0x00,
			0x50, 0x00, 0x72, 0x00, 0x6F, 0x00, 0x76, 0x00, 0x69, 0x00, 0x64, 0x00, 0x65, 0x00, 0x72, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x00, 0x63, 0x00, 0x72, 0x00, 0x79, 0x00,
			0x70, 0x00, 0x74, 0x00, 0x70, 0x00, 0x72, 0x00, 0x69, 0x00, 0x6D, 0x00, 0x69, 0x00, 0x74, 0x00,
			0x69, 0x00, 0x76, 0x00, 0x65, 0x00, 0x73, 0x00, 0x2E, 0x00, 0x64, 0x00, 0x6C, 0x00, 0x6C, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		};
		struct ksec_query { const char* item; const uint8_t* in; uint32_t inlen, outlen; const uint8_t* answer; uint32_t answer_len; };
		static const ksec_query ksec_queries[] = {
			{ "tm_nt ksec.res", ksec_in_res, 0x30, 0x180, ksec_out_res, 0xD8 },
			{ "tm_nt ksec.q2", ksec_in_q2, 0x38, 0x180, ksec_out_q2, 0xD8 }
		};
		// END KSEC QUERY TABLE
		if ( code == 0x390400 && in && out && inlen <= 0x100 )
		{
			uint8_t got[ 0x100 ] = {};
			std::string err;
			if ( !user_buffer( in, inlen, UC_PROT_READ ) || !e_.read( in, got, inlen, err ) ) return false;
			for ( const auto& q : ksec_queries )
			{
				if ( q.inlen != inlen || q.outlen != outlen || std::memcmp( got, q.in, inlen ) != 0 ) continue;
				if ( !user_buffer( out, outlen, UC_PROT_WRITE ) || !user_buffer( iosb, 16, UC_PROT_WRITE ) )
				{
					r = stop_( what + " - the output / IoStatusBlock buffer is not accessible: not measured" );
					return true;
				}
				r = done_( status::success, what + ": a CNG configuration query - the measured answer, " + hx( q.answer_len ) + "H bytes, IoStatus {0, Information " +
												  hx( q.answer_len ) + "H} (resolution of \"RNG\": \"Microsoft Primitive Provider\", \"bcryptprimitives.dll\"; measured: " + q.item + ")" );
				const uint32_t ios = 0;
				const uint64_t ioi = q.answer_len;
				if ( !put( out, q.answer, q.answer_len, r ) || !put( iosb, &ios, 4, r ) || !put( iosb + 8, &ioi, 8, r ) ) r = stop_( "writing the answer failed after the probe" );
				return true;
			}
		}
		return false;
	}

	// the environment's random bytes (USER DECISION 2026-10-28): the seeded CTR_DRBG of plan 3.M6 (emu/ctr_drbg.hpp: NIST SP 800-90A Rev. 1
	// 10.2.1, AES-128, the seed of decision D8) with its own personalization string "NoVmp environment RNG device" (a stream of its own: the
	// process cookie keeps the first value of the "NoVmp environment" stream) - one stream per process, consumed in the order of the requests;
	// never host entropy. Also the --HLE BCryptGenRandom (hle.cpp).
	void kernel::environment_random( uint8_t* p, size_t n )
	{
		if ( !dev_rng_ ) dev_rng_ = std::make_unique<ctr_drbg>( f_.rng_seed, std::string( environment_personalization ) + " RNG device" );
		for ( size_t i = 0; i < n; i += 8 )
		{
			const uint64_t v = dev_rng_->next();
			std::memcpy( p + i, &v, n - i < 8 ? n - i : 8 );
		}
	}

	// the measured requests on the CNG device (tm_nt ksec.cng.*); false = not measured (the caller stops loudly)
	bool kernel::cng_request( const call& c, const std::string& what, reply& r )
	{
		const uint32_t code = uint32_t( c.args[ 5 ] ), inlen = uint32_t( c.args[ 7 ] ), outlen = uint32_t( c.args[ 9 ] );
		const uint64_t iosb = c.args[ 4 ], in = c.args[ 6 ], out = c.args[ 8 ];
		if ( code == 0x390008 )
		{
			// IOCTL_KSEC_RNG_REKEY = CTL_CODE( FILE_DEVICE_KSEC 39H, 2, METHOD_BUFFERED, FILE_ANY_ACCESS ) (phnt ntseapi.h:1463) as
			// bcryptprimitives.dll sends it on \Device\CNG (test PE T9 exploration x13 system call 257: no input, a 30H-byte output - the
			// seed of its process PRNG). MEASURED from inside (tm_nt ksec.cng.rekey.1-3, three requests on one handle, twice): STATUS_SUCCESS,
			// IoStatus {Status 0 (the upper half not written), Information 30H}, exactly 30H bytes written, all 30H bytes different in every
			// request (random; never recorded). The model: 30H bytes of the environment's seeded generator (environment_random). Another
			// length, an input, an inaccessible buffer: not measured - stop.
			if ( in || inlen || outlen != 0x30 || !out ) return false;
			if ( !user_buffer( out, 0x30, UC_PROT_WRITE ) || !user_buffer( iosb, 16, UC_PROT_WRITE ) )
			{
				r = stop_( what + " - the output / IoStatusBlock buffer is not accessible: not measured" );
				return true;
			}
			uint8_t seed[ 0x30 ];
			environment_random( seed, sizeof( seed ) );
			r = done_( status::success, what + ": IOCTL_KSEC_RNG_REKEY - 30H random bytes from the environment's seeded generator (CTR_DRBG, never the host), IoStatus "
											  "{0, Information 30H} (measured shape: tm_nt ksec.cng.rekey)" );
			const uint32_t ios = 0;
			const uint64_t ioi = 0x30;
			if ( !put( out, seed, sizeof( seed ), r ) || !put( iosb, &ios, 4, r ) || !put( iosb + 8, &ioi, 8, r ) ) r = stop_( "writing the answer failed after the probe" );
			return true;
		}
		return false;
	}

	// ------------------------------------------------------------------------------------------------ \Device\DeviceApi\CMApi (p3cm)
	// USER DECISION 2026-10-28 (p3cm STEP 2, "CMApi MINIMAL DEVICE MODEL"): the configuration-manager device that cfgmgr32.dll's DllMain opens
	// (sample run 12 / 13 system call 798: cfgmgr32.dll+1280F -> +12FA4 -> +4C30 -> +4D2B) is a DEVICE of the environment that answers ONLY the
	// requests of cfgmgr32's start-up (its DLL_PROCESS_ATTACH), each MEASURED natively from inside our own test PE (tm_nt case cm, decision
	// 3.0.7) - its SHAPE only (status, IoStatusBlock, output size / layout), never device-tree content. Every later request (CM_* API calls:
	// device lists, properties, the device tree - machine identity) is a LOUD STOP with the decoded request (the user's decision point).
	// Reference for the request formats (read only, no code copied): sogen src/windows-emulator/io_device.cpp:40-88 / :147 (the device name,
	// IoControlCode 470803H = the device-ID-list request and its reply header); the service prototype phnt ntioapi.h NtCreateFile /
	// NtDeviceIoControlFile. Every access is in fs_access.txt (fs_record).
	bool kernel::cm_open( const call& c, const object_attributes& oa, const std::string& what, reply& r )
	{
		if ( oa.root || lower( oa.name ) != "\\device\\deviceapi\\cmapi" ) return false;
		// the argument set of cfgmgr32's DllMain (run 12 system call 798): NtCreateFile( &h, 80000000H GENERIC_READ, attributes 0, &iosb,
		// AllocationSize NULL, FileAttributes 0, ShareAccess 0, FILE_OPEN 1, CreateOptions 0, EaBuffer NULL, 0 )
		if ( oa.name != "\\Device\\DeviceApi\\CMApi" || uint32_t( c.args[ 1 ] ) != 0x80000000u || oa.attributes != 0 || c.args[ 4 ] || uint32_t( c.args[ 5 ] ) ||
			 uint32_t( c.args[ 6 ] ) || uint32_t( c.args[ 7 ] ) != 1 || uint32_t( c.args[ 8 ] ) || c.args[ 9 ] || uint32_t( c.args[ 10 ] ) )
		{
			r = stop_( what + " - the configuration-manager device with other arguments / another spelling: not measured (tm_nt cm.open: \"\\Device\\DeviceApi\\CMApi\", "
							  "80000000H, attributes 0, no AllocationSize, file attributes 0, share 0, FILE_OPEN, options 0, no EA - cfgmgr32's DllMain)" );
			return true;
		}
		const uint64_t ph = c.args[ 0 ], iosb = c.args[ 3 ];
		if ( !user_buffer( ph, 8, UC_PROT_WRITE ) || !user_buffer( iosb, 16, UC_PROT_WRITE ) )
		{
			r = stop_( what + " - FileHandle / IoStatusBlock not writable: not measured for the device" );
			return true;
		}
		object o;
		o.kind = "device (CMApi)";
		o.name = oa.name;
		const uint64_t h = open_handle( o );
		r = done_( status::success, what + ": the configuration-manager device, handle " + hex( h ) + ", IoStatus {Status 0, Information 0} (measured: tm_nt cm.open)" );
		const uint32_t ios = 0;
		const uint64_t ioi = 0;
		if ( !put( ph, &h, 8, r ) || !put( iosb, &ios, 4, r ) || !put( iosb + 8, &ioi, 8, r ) ) r = stop_( "writing the result failed after the probe" );
		return true;
	}

	// the measured start-up requests of cfgmgr32.dll on the configuration-manager device (tm_nt cm.*); false = outside that set (the caller
	// stops loudly with the decoded request)
	bool kernel::cm_request( const call&, const std::string&, reply& )
	{
		return false;
	}
}
