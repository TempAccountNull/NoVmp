// emu-profilegen (plan 1.J.49, ledger U1510-U1512): writes the platform identity profiles. A thin CLI: it only turns the
// arguments into a BuildConfig, calls ProfileGenerator (profile_generator.hpp / .cpp: the field rules, outputs and
// self-checks; no console I/O inside) as ProfileGen->..., and prints the returned structured log.
//
//   emu-profilegen --guest | --vm | --host | --config FILE  [overrides] [--new-identity] [--check] [--save-config FILE]
//
//   --guest / --vm     the clean VMware guest, Run 3 without VmLoader (pinned capture) -> Emulator\data\platform\vmware_guest
//   --host             the clean bare host (pinned capture)                             -> Emulator\data\platform\bare_host
//   --config FILE      a saved BuildConfig (e.g. Emulator\data\platform\<env>\build_config.txt, seed included) instead of the
//                      defaults; it reproduces that build byte for byte
//   overrides          --capture-root DIR (default: NOVMP_CAPTURE_ROOT, else the first ancestor folder "capture" of the
//                      repository that holds the input), --out DIR (SYNTHETIC profile, default <repo>\Emulator\data\platform\
//                      <env>), --original-out DIR (ORIGINAL profile with the real values, LOCAL ONLY, default <capture root>\
//                      profiles_original\<env>; refused inside any git work tree), --no-original, --seed FILE (the seed store,
//                      default <repo>\Emulator\data\platform\profilegen_seed.txt; created with BCryptGenRandom on the first run),
//                      --repo-root DIR, --rules "1 2 2h 2o 3 4a 4b 4c 4d 6 7 8"
//   --new-identity     ProfileGen->gen_new_seed(): a fresh seed, i.e. a different but equally consistent identity; the seed
//                      store is updated after the build (rebuild the other environment too: the USB mapping is shared)
//   --check            regenerate in memory and compare with the SYNTHETIC output folder byte for byte; writes nothing
//   --save-config FILE write the effective BuildConfig to FILE and stop (no build)
#include "profile_generator.hpp"

#include <cstring>
#include <iostream>
#include <memory>

int main( int argc, char** argv )
{
	using namespace emu::platform;
	auto usage = []() {
		std::cout << "usage: emu-profilegen --guest | --vm | --host | --config FILE [--capture-root DIR] [--out DIR] [--original-out DIR | --no-original]\n"
					 "                      [--seed FILE] [--repo-root DIR] [--rules LIST] [--new-identity] [--check] [--save-config FILE]\n";
		return 2;
	};
	int envs = 0;
	Environment env = Environment::vmware_guest;
	const char* config_file = nullptr;
	const char* save_file = nullptr;
	bool check = false, no_original = false, new_identity = false;
	std::vector<std::pair<std::string, std::string>> over;
	for ( int i = 1; i < argc; i++ )
	{
		const char* a = argv[ i ];
		auto value = [ & ]() -> const char* { return i + 1 < argc ? argv[ ++i ] : nullptr; };
		if ( !strcmp( a, "--guest" ) || !strcmp( a, "--vm" ) ) { env = Environment::vmware_guest; envs++; }
		else if ( !strcmp( a, "--host" ) ) { env = Environment::bare_host; envs++; }
		else if ( !strcmp( a, "--no-original" ) ) no_original = true;
		else if ( !strcmp( a, "--check" ) ) check = true;
		else if ( !strcmp( a, "--new-identity" ) ) new_identity = true;
		else if ( !strcmp( a, "--help" ) || !strcmp( a, "-h" ) ) { usage(); return 0; }
		else if ( !strcmp( a, "--config" ) || !strcmp( a, "--save-config" ) || !strcmp( a, "--capture-root" ) || !strcmp( a, "--out" ) || !strcmp( a, "--original-out" )
				  || !strcmp( a, "--seed" ) || !strcmp( a, "--repo-root" ) || !strcmp( a, "--rules" ) )
		{
			const char* v = value();
			if ( !v ) { std::cout << "[profilegen] " << a << " needs a value\n"; return usage(); }
			if ( !strcmp( a, "--config" ) ) config_file = v;
			else if ( !strcmp( a, "--save-config" ) ) save_file = v;
			else over.emplace_back( a, v );
		}
		else { std::cout << "[profilegen] unknown option \"" << a << "\"\n"; return usage(); }
	}
	if ( envs + ( config_file ? 1 : 0 ) != 1 ) { std::cout << "[profilegen] give exactly one of --guest / --vm / --host / --config FILE\n"; return usage(); }
	if ( check && new_identity ) { std::cout << "[profilegen] --check compares an existing identity: not with --new-identity\n"; return usage(); }

	BuildConfig c;
	try
	{
		c = config_file ? BuildConfig::load_file( config_file ) : BuildConfig::defaults( env );
		for ( auto& [ k, v ] : over )
		{
			if ( k == "--capture-root" ) c.capture_root = v;
			else if ( k == "--out" ) c.out = v;
			else if ( k == "--original-out" ) c.original_out = v;
			else if ( k == "--seed" ) { c.seed_file = v; c.seed.clear(); }
			else if ( k == "--repo-root" ) c.repo_root = v;
			else if ( k == "--rules" ) c.rules = RuleSet::parse( v );
		}
		if ( no_original ) c.original_out = "none";
	}
	catch ( const std::exception& e )
	{
		std::cout << "[profilegen] FAILED: " << e.what() << "\n";
		return 2;
	}
	auto ProfileGen = std::make_unique<ProfileGenerator>( c );
	if ( new_identity ) ProfileGen->gen_new_seed();
	if ( save_file )
	{
		try { ProfileGen->config().save_file( save_file ); }
		catch ( const std::exception& e ) { std::cout << "[profilegen] FAILED: " << e.what() << "\n"; return 2; }
		std::cout << "[profilegen] build config written: " << save_file << "\n";
		return 0;
	}
	GenResult r = ProfileGen->run_build( check );
	std::cout << r.log.text() << ( r.ok ? "[profilegen] OK\n" : "[profilegen] FAILED: " + r.error + "\n" );
	return r.ok ? 0 : 1;
}
