// Plan step 1.6 backport tests (TEMPORARY - removed once every backport is proven; see bp_harness.hpp).
//   emu-backports [filter]     runs every case whose commit or name contains `filter`
#include "bp_harness.hpp"

namespace bp
{
	// Harness self-check: behaviour both engines already agree on. A failure here is a harness bug.
	static std::vector<test_case> harness_cases()
	{
		std::vector<test_case> v;
		v.push_back( { "harness", "add/sub/adc flags", "add rax, rbx\nsub rcx, rdx\nadc r8, r9\nsbb r10, r11\n" } );
		v.push_back( { "harness", "pushfq / push / pop on the test stack", "pushfq\npush rax\npop rbx\npush 0x1234\n", {},
					   [ ]( state& s ) { s.rflags = 0x2C3; } } );
		{
			test_case t{ "harness", "#UD fault (ud2) resumes at the epilogue", "mov rax, 5\nud2\nmov rax, 6\n" };
			v.push_back( t );
		}
		{
			test_case t{ "harness", "misaligned movaps -> #GP", "", {} };
			t.asm_text = "mov rax, " + hx( SCRATCH + 8 ) + "\nmovaps xmm1, xmmword ptr [rax]\n";
			v.push_back( t );
		}
		{
			test_case t{ "harness", "x87 fld/fadd/fstp + memory", "" };
			t.asm_text = "mov rax, " + hx( SCRATCH ) + "\nfld qword ptr [rax]\nfld qword ptr [rax + 8]\nfaddp st(1), st(0)\nfstp qword ptr [rax + 16]\n";
			t.compare = C_ALL | C_FTW_FULL;
			t.init = [ ]( state& s ) { double a = 1.5, b = 2.25; std::memcpy( s.scratch, &a, 8 ); std::memcpy( s.scratch + 8, &b, 8 ); };
			v.push_back( t );
		}
		v.push_back( { "harness", "AVX2 vpaddd ymm", "vpaddd ymm1, ymm2, ymm3\nvpshufd ymm4, ymm1, 0x1b\n" } );
		{
			test_case t{ "harness", "negative control: cpuid leaf 0 differs (host vs emulated model)", "xor eax, eax\nxor ecx, ecx\ncpuid\n" };
			t.expect_mismatch = true;
			v.push_back( t );
		}
		{
			test_case t{ "harness", "store to scratch", "" };
			t.asm_text = "mov rax, " + hx( SCRATCH ) + "\nmov qword ptr [rax + 0x40], rbx\nmov dword ptr [rax + 0x48], 0xdeadbeef\n";
			v.push_back( t );
		}
		return v;
	}

	std::vector<test_case> isa_cases();
	std::vector<test_case> integer_cases();
	std::vector<test_case> avx_cases();
	std::vector<test_case> x87_cases();
	std::vector<test_case> exception_cases();
}

int main( int argc, char** argv )
{
	using namespace bp;
	const char* filter = argc > 1 ? argv[ 1 ] : nullptr;
	unsigned maj = 0, min = 0;
	uc_version( &maj, &min );
	std::printf( "emu-backports: Unicorn %u.%u (QEMU 7.2.22 branch + ledgered backports)%s%s\n",
				 maj, min, filter ? "  filter: " : "", filter ? filter : "" );

	struct group { const char* name; std::vector<test_case> cases; };
	std::vector<group> groups = {
		{ "harness self-check", harness_cases() },
		{ "ISA additions", isa_cases() },
		{ "integer flags / decoding", integer_cases() },
		{ "AVX / SSE decoding", avx_cases() },
		{ "x87 / MXCSR / softfloat", x87_cases() },
		{ "exceptions / RF / TF", exception_cases() },
	};
	summary sum;
	for ( auto& g : groups )
	{
		bool header = false;
		for ( auto& tc : g.cases )
		{
			if ( filter && !std::strstr( tc.commit, filter ) && !std::strstr( tc.name, filter ) ) continue;
			if ( !header ) { std::printf( "\n== %s\n", g.name ); header = true; }
			run_case( tc, sum );
		}
	}
	std::printf( "\n%d passed, %d failed, %d errors\n", sum.pass, sum.fail, sum.error );
	for ( auto& f : sum.failed ) std::printf( "  failed: %s\n", f.c_str() );
	return ( sum.fail || sum.error ) ? 1 : 0;
}
