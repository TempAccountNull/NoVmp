# test_runner.ps1 - the suite runner behind test.cmd (plan 1.H.3, ledger U541-U543).
#
#   test.cmd [--release | --debug] [-j N | --jobs N | --serial] [-v | --verbose] [list | NAME ...]
#
#   NAME        a suite id or a group name ("test.cmd list" prints both); several names at once;
#               no NAME = every suite (the full gate, as before)
#   -j N        run up to N suites at the same time (default: NUMBER_OF_PROCESSORS);
#               --serial = -j 1
#   -v          print every selected suite's whole log after the run
#
# Every suite writes its own log, build\x64\<config>\tests\logs\<id>.log, and is judged by the rules
# test.cmd used before (U541): an exit status of 0 (unit tests, expected-value files, python
# checks), "differing: 0" + exit 0 + a strict CPUID profile (hardware files, formerly :hw_zero),
# "cases: 7, differing: 7" + exit 1 (the deliberately wrong expectations, formerly :expect_bad).
# The suites are independent processes; hardware suites compare architectural results (never
# timing), so running them side by side cannot change a result. Longest suites start first (the
# previous run's times, logs\times.tsv). cases_sse_exc runs as 8 shards (emu-alltest --shard K/8,
# U543) and emu-uc72-risk as three parts (--only, U542); hwcheck_gate1 and cases_quirks run alone
# first (a tagged case whose CPU result depends on the load beside it, see Add-Hw).
# Only self-contained test code runs: Unicorn's unit tests emulate their own snippets and
# emu-alltest runs only self-generated snippets natively; nothing from the sample is ever executed.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

if ( $args.Count -lt 1 ) { Write-Output '[test] test_runner.ps1: missing root argument (run test.cmd)'; exit 2 }
$Root = [IO.Path]::GetFullPath( [string]$args[ 0 ] ).TrimEnd( '\' )
$argv = @()
if ( $args.Count -gt 1 ) { $argv = @( $args[ 1..( $args.Count - 1 ) ] | ForEach-Object { [string]$_ } ) }

$Usage = 'usage: test.cmd [--release ^| --debug] [-j N ^| --jobs N ^| --serial] [-v ^| --verbose] [list ^| NAME ...]' -replace '\^', ''

# ---------------------------------------------------------------------------------------- options
$Config = 'Release'
$Jobs = 0
$Verbose = $false
$List = $false
$Names = New-Object System.Collections.Generic.List[string]
for ( $i = 0; $i -lt $argv.Count; $i++ )
{
	$a = $argv[ $i ]
	switch -regex ( $a )
	{
		'^--release$' { $Config = 'Release'; continue }
		'^--debug$' { $Config = 'Debug'; continue }
		'^(-j|--jobs)$'
		{
			if ( $i + 1 -ge $argv.Count -or $argv[ $i + 1 ] -notmatch '^[0-9]+$' -or [int]$argv[ $i + 1 ] -lt 1 )
			{ Write-Output "[test] $a needs a job count (1 or more)"; Write-Output $Usage; exit 2 }
			$i++; $Jobs = [int]$argv[ $i ]; continue
		}
		'^-j([0-9]+)$' { $Jobs = [int]$Matches[ 1 ]; if ( $Jobs -lt 1 ) { Write-Output $Usage; exit 2 }; continue }
		'^--serial$' { $Jobs = 1; continue }
		'^(-v|--verbose)$' { $Verbose = $true; continue }
		'^(list|--list)$' { $List = $true; continue }
		'^-' { Write-Output "[test] unknown option ""$a"""; Write-Output $Usage; exit 2 }
		default { $Names.Add( $a.ToLowerInvariant() ); continue }
	}
}
if ( $Jobs -lt 1 ) { $Jobs = [Math]::Max( 1, [int]$env:NUMBER_OF_PROCESSORS ) }

$T = Join-Path $Root "build\x64\$Config\tests"
$D = Join-Path $Root 'Emulator\data'
$Cpuid = Join-Path $D 'cpuid_i5-13600k.txt'

# ---------------------------------------------------------------------------------------- suites
# Kind: exit = pass on exit status 0 | hw = hardware file (differing: 0, exit 0, strict profile) |
#       bad = expect_selftest_bad (7 of 7 reported, exit 1) | py = python check (exit status 0) |
#       herr = harness_selftest_bad (every case line a harness error, "cases: 0", exit 1; U1841).
#       nonly = harness_selftest_native (U1871: its one "# native only:" measurement line counted as "native only: 1",
#       every other line run; no native-only line is allowed in any other suite).
#       tagsel = harness_selftest_tags (U1877: the exact stated differences under --tag-env guest / hostE; the summary and
#       the not-observed count must be exactly the suite's expected ones, exit 0).
#       snip / snipbad / snipcli1 = the emu-snippet replay suites and the --expect negative control (U1978, Add-Snip).
# U1841: every emu-alltest --cases suite (except herr) must also report "cases: N" with N = the number of case lines this
# runner counts in the file itself (Get-ExpectedCases).
$Suites = New-Object System.Collections.Generic.List[object]
function Add-Suite( [string]$Id, [string]$Groups, [string]$Kind, [string]$Exe, [string[]]$Arguments, [string]$Note = '', [bool]$Exclusive = $false )
{
	$Suites.Add( [pscustomobject]@{
			Id = $Id; Groups = @( $Groups -split ' ' | Where-Object { $_ } ); Kind = $Kind; Exe = $Exe; Args = @( $Arguments ); Note = $Note
			Log = ''; Proc = $null; Watch = $null; Seconds = 0.0; Exit = $null; Pass = $false; Reason = ''; Lines = @(); Started = $false; Exclusive = $Exclusive; Parent = ''
		} )
}
function CaseFile( [string]$Name ) { return ( Join-Path $D "$Name.txt" ) }
# a hardware file: host CPU vs Unicorn with the i5-13600K CPUID profile. U540: XCR0 (host XGETBV) and
# CR0 (Windows 33h) are automatic for hardware cases; extra arguments only where a file needs more.
# -Exclusive: the file has a tagged case whose CPU result depends on what runs beside it (REP LODS
# with 67h and ECX = 0 takes the SDM path in ~4 % of the runs alone, ~13 % beside 8 parallel runs,
# docs\quirks.md); such a file runs alone, before the parallel suites, as under the serial test.cmd.
# -Shards N (U543): N suites NAME-1 .. NAME-N, each running one contiguous block of the case lines
# (emu-alltest --shard K/N); together exactly the file. "test.cmd NAME" selects all N; the summary adds
# a total row.
function Add-Hw( [string]$Name, [string]$Groups, [string[]]$Extra = @(), [switch]$Exclusive, [int]$Shards = 0 )
{
	if ( $Shards -lt 2 )
	{
		Add-Suite $Name $Groups 'hw' 'emu-alltest' ( @( '--cases', ( CaseFile $Name ), '--cpuid', $Cpuid ) + $Extra ) '' $Exclusive.IsPresent
		return
	}
	for ( $k = 1; $k -le $Shards; $k++ )
	{
		Add-Suite "$Name-$k" $Groups 'hw' 'emu-alltest' ( @( '--cases', ( CaseFile $Name ), '--cpuid', $Cpuid ) + $Extra + @( '--shard', "$k/$Shards" ) ) '' $Exclusive.IsPresent
		$Suites[ $Suites.Count - 1 ].Parent = $Name
	}
}
# an expected-value file: Unicorn only vs the SDM model (U540: Unicorn's reset XCR0, from the opt-ins)
# U1978 (plan 3.M3): the run also writes every case as an emu-snippet spec (emu-alltest --snippet-export, U1977), and the suite
# "<Id>-snip" replays them on the Emulator lib (Add-Snip)
function Add-Exp( [string]$Id, [string]$File, [string]$Groups, [string[]]$Extra = @() )
{
	Add-Suite $Id $Groups 'exit' 'emu-alltest' ( @( '--cases', ( CaseFile $File ) ) + $Extra + @( '--expect-only' ) + ( SnipExport $Id ) )
	Add-Snip $Id $Groups
}
# U1978 (plan 3.M3 "the expected-value case files replayed through emu-snippet, 0 differing"): emu-snippet --batch replays the
# specs suite $Of exported, after $Of has finished. Verdict (kind snip): every expected-value case line of $Of replayed (the
# "snippets:" count = Get-ExpectedCases of $Of), "expect differing: 0", "errors: 0", and the Unicorn engine state of the line
# ("reference") equal except the lines $SnipReferenceKnown names for $Of, by exact count: values that come from the host
# differ between two engines by design: since U2096 (plan 3.M6) only --HostSeed RDRAND / RDSEED (the host DRNG) - RDTSC / the TSC
# MSRs run on the one virtual time base in both engines (emu-alltest exports the clock at the snippet start, config.tsc_start) and
# PBNDKB's random IV / key come from the seeded CTR_DRBG (U2091); until M6 those 38 lines were pinned too (tscadj 5 x 7, cases_sysmsr 1,
# cases_sysins 2). Kind snipbad (expect_selftest_bad): every one of its 7 wrong expectations is a
# DIFF in emu-snippet too. A passing suite removes its spec file (build\x64\<config>\tests\snippet_replay\<id>.jsonl).
function SnipExport( [string]$Id ) { return @( '--snippet-export', ( Join-Path $T "snippet_replay\$Id.jsonl" ) ) }
function Add-Snip( [string]$Of, [string]$Groups, [string]$Kind = 'snip' )
{
	Add-Suite "$Of-snip" ( "$Groups snippet" ) $Kind 'emu-snippet' @( '--batch', ( Join-Path $T "snippet_replay\$Of.jsonl" ), '--quiet' )
	$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName After -NotePropertyValue $Of
	$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName SnipOf -NotePropertyValue $Of
}
$SnipReferenceKnown = @{
	'cases_rdrand_host' = 3   # --HostSeed: the host DRNG (by design; U2096 resolved the 38 time / PBNDKB lines)
}
# a model's self-test (python MODEL --selftest, exit status 0)
function Add-PySelftest( [string]$Id, [string]$Groups, [string]$Model )
{
	Add-Suite $Id $Groups 'py' ( Join-Path $Root "Emulator\tools\isa\$Model" ) @( '--selftest' )
}
# a model's hardware cross-check (python MODEL --hwcmp LOG, exit status 0; formerly :py_hwcmp): it reads
# the log of the hardware suite HwId, so it starts only after HwId has finished, and selecting it
# selects HwId too
function Add-HwCmp( [string]$Id, [string]$Groups, [string]$Model, [string]$HwId )
{
	Add-Suite $Id $Groups 'py' ( Join-Path $Root "Emulator\tools\isa\$Model" ) @( '--hwcmp', ( Join-Path $T "logs\$HwId.log" ) )
	$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName After -NotePropertyValue $HwId
}
function AfterOf( $s ) { if ( $s.PSObject.Properties[ 'After' ] ) { return $s.After } return '' }

# Unicorn's own unit tests (emulate their own snippets)
Add-Suite 'unicorn-test_x86' 'unit x87' 'exit' 'unicorn-test_x86' @()
Add-Suite 'unicorn-test_ctl' 'unit' 'exit' 'unicorn-test_ctl' @()
# test_mem_read_and_write_large_memory_block opens UC_ARCH_ARM64; our unicorn.lib is built for the
# x86_64 target only, so that one upstream test cannot apply (skipped by name).
Add-Suite 'unicorn-test_mem' 'unit' 'exit' 'unicorn-test_mem' @( '--skip', 'test_mem_read_and_write_large_memory_block' ) `
	'skip unicorn-test_mem:test_mem_read_and_write_large_memory_block (needs UC_ARCH_ARM64; x86_64-only build)'
# plan 1.5: QEMU 7.2 branch risks (long AVX2/FMA/VSIB blocks vs hardware, uc_context round trip,
# old_exception / spurious #DF). Hardware reference runs self-generated code only.
# U542: its sections R17 (x87 transcendentals) and R18 (x87 arithmetic) take most of the time; they
# run as their own suites (emu-uc72-risk --only N); "test.cmd emu-uc72-risk" selects all three.
Add-Suite 'emu-uc72-risk' 'unit hw' 'exit' 'emu-uc72-risk' @( '--only', '1-16' )
Add-Suite 'emu-uc72-risk-r17' 'unit hw x87' 'exit' 'emu-uc72-risk' @( '--only', '17' )
Add-Suite 'emu-uc72-risk-r18' 'unit hw x87' 'exit' 'emu-uc72-risk' @( '--only', '18' )
foreach ( $s in $Suites ) { if ( $s.Id -like 'emu-uc72-risk*' ) { $s.Parent = 'emu-uc72-risk' } }
# plan 1.9: emu-alltest quick run (every 7th form of the full x86-64 universe, host CPU vs Unicorn
# UC_CPU_X86_MAX). Differences are the Phase 4/5 work list, reported in build\x64\Release\tests\alltest;
# only harness errors fail. Full run: emu-alltest --full (baseline in Emulator\data\alltest_baseline).
Add-Suite 'emu-alltest-quick' 'sweep hw' 'exit' 'emu-alltest' @()
# U1813: the report's "Known deviations:" line uses neutral wording (the hardware result differs from the SDM-modelled
# emulator result, documented in docs\quirks.md; the list also holds the VMware-guest PTWRITE artifact). Checked in the
# quick run's report (so it starts after emu-alltest-quick) and in the stored --full baseline report.
Add-Suite 'alltest_report_wording' 'sweep tools' 'py' ( Join-Path $Root 'Emulator\tools\check_alltest_report_wording.py' ) @( ( Join-Path $T 'alltest\alltest_report.md' ), ( Join-Path $D 'alltest_baseline\alltest_report.md' ) )
$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName After -NotePropertyValue 'emu-alltest-quick'
# plan 1.15a: expected-value ("SDM vector") cases - Unicorn only, compared to hand-derived SDM results;
# any difference or unparsable line fails. The deliberately wrong file must be caught (7 of 7, exit 1; U772: + a wrong error code).
# U539: the emulator implements the Intel SDM only (no quirk switch). Hardware files tag the cases
# where the i5-13600K deviates from the SDM ("# known deviation: NAME", docs\quirks.md); the same tag
# marks the VMware-guest artifacts PTWRITE / HRESET "hidden by the guest CPUID" (not CPU deviations).
Add-Exp 'expect_selftest' 'expect_selftest' 'selftest expect'
Add-Suite 'expect_selftest_bad' 'selftest expect' 'bad' 'emu-alltest' ( @( '--cases', ( CaseFile 'expect_selftest_bad' ), '--expect-only' ) + ( SnipExport 'expect_selftest_bad' ) )
Add-Snip 'expect_selftest_bad' 'selftest expect' 'snipbad'   # U1978: the 7 wrong expectations are wrong in emu-snippet too
# U1840 / U1841 (plan 1.F.13.b A): a case line that cannot be parsed, assembled, loaded or run natively is a harness error
# (exit 1, never counted in "cases:"); every line of harness_selftest_bad.txt is broken on purpose (kind herr: all of them
# reported as harness errors, "cases: 0", exit 1). No line of it reaches the native engine or runs on Unicorn.
# U1861 (plan 1.F.13.c, G25 gap 3): build-failed and engine-load lines too; the profile without CET_SS
# (cpuid_selftest_nocet.txt) makes "cpl=3 cr4.cet=1" impossible to load.
Add-Suite 'harness_selftest_bad' 'selftest' 'herr' 'emu-alltest' @( '--cases', ( CaseFile 'harness_selftest_bad' ), '--cpuid', ( Join-Path $D 'cpuid_selftest_nocet.txt' ) )
# U1872 (plan 1.F.13.f): the native-only measurement tag - its line runs natively only and is counted as "native only: 1"
# (case lines = run + native only), the other line runs on both engines; exit 0
Add-Suite 'harness_selftest_native' 'selftest' 'nonly' 'emu-alltest' @( '--cases', ( CaseFile 'harness_selftest_native' ), '--cpuid', $Cpuid )
# U1892 (plan 1.F.13.g d): a native #DB without a hardware DR6 is a harness error (harness_selftest_bad.txt line 40) unless a
# measurement line states "nodr6ok": that line runs (DR6 not compared) and is counted in the debug-register summary
Add-Suite 'harness_selftest_dbg' 'selftest' 'tagsel' 'emu-alltest' @( '--cases', ( CaseFile 'harness_selftest_dbg' ), '--cpuid', $Cpuid )
$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName Expect -NotePropertyValue @( 'cases: 2, differing: 0, known deviations: 0, host state: 0', 'debug-register cases: 1 (not run natively: 0, #DB without a hardware DR6 from Windows: 1, harness errors / notes: 0)' )
# U1877 (plan 1.F.13.d): the exact stated differences (U1875) - every alternative must EQUAL the observed difference, the
# statement of the run's environment applies (--tag-env guest / hostE here; the file's header lists each line's verdict)
# U1897 (plan 1.F.13.g a): + 4 value lines (another Unicorn value, another hardware value, classes inside / outside)
# U1904-U1905 (plan 1.F.13.h, G28 A6): + 5 class-boundary lines (a value one above a range on either engine, one mask bit -
# bit 0 / bit 63 - outside, both range ends inside): a matcher off by one at a range end or a mask bit changes the counts
# U1914 (plan 1.F.13.i, G29-2): + 8 lines outside a mask in one middle bit each (bits 4, 12, 20, 28, 36, 44, 52, 60: one bit
# in every byte, alternately the hardware RSI and the Unicorn RCX): a matcher that drops a middle mask bit changes the counts
Add-Suite 'harness_selftest_tags_guest' 'selftest' 'tagsel' 'emu-alltest' @( '--cases', ( CaseFile 'harness_selftest_tags' ), '--cpuid', $Cpuid, '--tag-env', 'guest' )
$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName Expect -NotePropertyValue @( 'cases: 27, differing: 21, known deviations: 5, host state: 0', 'tagged but not observed (matched the hardware): 1' )
Add-Suite 'harness_selftest_tags_hostE' 'selftest' 'tagsel' 'emu-alltest' @( '--cases', ( CaseFile 'harness_selftest_tags' ), '--cpuid', $Cpuid, '--tag-env', 'hostE' )
$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName Expect -NotePropertyValue @( 'cases: 27, differing: 20, known deviations: 5, host state: 0', 'tagged but not observed (matched the hardware): 2' )
# U1893 (plan 1.F.13.g b): the environment from positive evidence only - without --tag-env this VMware guest is recognised by its
# captured profile (cpuid_i5-13600k.txt, the guest verdicts); with --tag-profiles none
# there is no evidence: the 3 per-environment lines (6, 7, 10) are harness errors, the other 24 run (kind tagerr: exit 1)
Add-Suite 'harness_selftest_tags_detect' 'selftest' 'tagsel' 'emu-alltest' @( '--cases', ( CaseFile 'harness_selftest_tags' ), '--cpuid', $Cpuid )
$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName Expect -NotePropertyValue @( 'cases: 27, differing: 21, known deviations: 5, host state: 0', 'tagged but not observed (matched the hardware): 1' )
Add-Suite 'harness_selftest_tags_unknown' 'selftest' 'tagerr' 'emu-alltest' @( '--cases', ( CaseFile 'harness_selftest_tags' ), '--cpuid', $Cpuid, '--tag-profiles', 'none' )
$Suites[ $Suites.Count - 1 ] | Add-Member -NotePropertyName Expect -NotePropertyValue @( 'cases: 24, differing: 19, known deviations: 4, host state: 0', 'case lines: 27, run: 24, harness errors: 3 (hardware 3, expected-value 0), skipped: 0' )
# plan 1.15d milestone K: VEX opmask instructions vs the SDM model (ref_opmask.py), Unicorn only with
# the AVX-512 opt-in (the host has no AVX-512); every line is an expected-value case.
Add-Exp 'cases_opmask' 'cases_opmask' 'evex expect' @( '--avx512' )
# Intel AMX (ledger U170-U180) vs the SDM model (ref_amx.py), Unicorn only with the AMX opt-in (the
# host has no AMX); tile programs store their results to memory.
Add-Exp 'cases_amx' 'cases_amx' 'amx expect' @( '--amx' )
# ISE 319433-062 AMX families (ledger U720-U727): AMX-MOVRS (VEX + APX-promoted EVEX), AMX-FP8,
# AMX-AVX512 (needs the AVX-512 state), XSAVES / XRSTORS of the AMX components, IA32_XSS - vs
# ref_amx.py --cases2 (independent model of the ISE / SDM text), Unicorn only at CPL0 (cpl=3 lines
# for the XSAVES / XRSTORS #GP); the model's hand-derived self-test runs as well.
Add-Exp 'cases_amx2' 'cases_amx2' 'amx expect' @( '--amx', '--avx512', '--apx' )
Add-PySelftest 'ref_amx_selftest' 'amx tools' 'ref_amx.py'
# U1150: AMX-TF32 TMMULTF32PS (ISE 319433-060 3.7, withdrawn in -062; explicit opt-in --amx-tf32,
# UNVERIFIED: self-contradictory pseudocode, the Operation's DAZ = FTZ = 1 reading, docs/quirks.md) vs
# ref_amx_tf32.py --cases (independent model of the ISE text); Unicorn only (no CPU, no SDE support).
Add-Exp 'cases_amx_tf32' 'cases_amx_tf32' 'amx expect' @( '--amx', '--amx-tf32' )
Add-PySelftest 'ref_amx_tf32_selftest' 'amx tools' 'ref_amx_tf32.py'
# plan 1.15d milestone M1: EVEX instructions (moves, integer/logic, FP with {er}, compares into k,
# broadcasts; masking, {1toN}, disp8*N, fault suppression, #UD) vs the SDM model ref_evex_m1.py.
Add-Exp 'cases_evex_m1' 'cases_evex_m1' 'evex expect' @( '--avx512' )
# ledger U96/U97: NaN propagation (SDM Vol1 4.8.3.5 Table 4-8). EVEX VADD/VSUB/VMUL/VDIV/VMIN/VMAX/
# VSQRT PS/PD with NaNs in both sources vs the SDM model (gen_cases_nan.py --evex).
Add-Exp 'cases_nan_evex' 'cases_nan_evex' 'evex nan expect' @( '--avx512' )
# SDM-model expected-value lines of the Key Locker/RAO/UINTR, SHA512/SM3/SM4 and VNNI/IFMA files
# (ledger U82-U105).
Add-Exp 'cases_keylocker' 'cases_keylocker' 'ext expect'
Add-Exp 'cases_sha_sm' 'cases_sha_sm' 'ext expect'
Add-Exp 'cases_vnni_ifma' 'cases_vnni_ifma' 'ext expect'
# ledger U98: DPPS/DPPD with two or more NaN products vs the SDM pseudocode (gen_cases_nan.py
# --dp-sdm), the first NaN product in the order p0..p3 lands in every selected element.
Add-Exp 'cases_dp_nan_sdm' 'cases_dp_nan_sdm' 'sse nan expect'
# The same rules on the host CPU: legacy SSE, VEX AVX/FMA and x87 hardware cases (self-generated
# snippets, gen_cases_nan.py --hw) must all match the i5-13600K.
Add-Hw 'cases_nan' 'hw sse x87 nan'
# plan 1.15d milestone M2 (ledger U250-U251): EVEX gathers/scatters (every form x VL, partial
# completion at an unmapped element, overlapping scatter indices, E12 #UD) vs ref_evex_m2_gather.py.
Add-Exp 'cases_evex_m2_gather' 'cases_evex_m2_gather' 'evex expect' @( '--avx512' )
# plan 1.15d milestone M2 engine (ledger U190-U201): EVEX scalar FP (SS/SD arithmetic, VMOVSS/SD,
# (U)COMISS/SD), VCMPPS/PD/SS/SD into k, FMA (dest as source under masking), VPBLENDMx/VBLENDMPx
# vs the SDM model ref_evex_m2_engine.py.
Add-Exp 'cases_evex_m2_engine' 'cases_evex_m2_engine' 'evex expect' @( '--avx512' )
# EVEX milestone M2 permutes / moves (ledger U210-U215: unpack/shuffle/permute, VPMOVZX/SX and
# narrowing VPMOV*, compress/expand, VMOVNT/DUP/MOVD/MOVQ/MOVHPS.., insert/extract/broadcast x2..x8)
# vs the SDM model ref_evex_m2_perm.py.
Add-Exp 'cases_evex_m2_perm' 'cases_evex_m2_perm' 'evex expect' @( '--avx512' )
# plan 1.15d milestone M3 BW (ledger U260-U269): AVX512BW byte/word instructions (64-bit writemasks,
# masking / fault suppression per byte and word, narrowing stores, disp8*N of every byte/word
# tuple, #UD) vs the SDM model ref_evex_m3_bw.py.
Add-Exp 'cases_evex_m3_bw' 'cases_evex_m3_bw' 'evex expect' @( '--avx512' )
# plan 1.15d milestone M3 (AVX512DQ, ledger U290-U296): VPMULLQ, VANDPS..VXORPD, VFPCLASS, VRANGE,
# VREDUCE vs the SDM model ref_evex_m3_dq.py.
Add-Exp 'cases_evex_m3_dq' 'cases_evex_m3_dq' 'evex expect' @( '--avx512' )
# AVX512CD (ledger U320-U321): VPCONFLICTD/Q, VPLZCNTD/Q, VPBROADCASTMB2Q/MW2D vs ref_evex_m3_cd.py.
Add-Exp 'cases_evex_m3_cd' 'cases_evex_m3_cd' 'evex expect' @( '--avx512' )
# EVEX milestone M4 (ledger U570-U575): AVX512_VP2INTERSECT (k-pair destination) and the EVEX forms of
# GFNI, VAES and VPCLMULQDQ (masking, {1toN}, disp8*N, E4/E4NF memory, #UD) vs the independent SDM model
# ref_evex_m4b.py, Unicorn only with the AVX-512 opt-in (incl. UC_X86_AVX512_VP2INTERSECT).
Add-Exp 'cases_evex_m4b' 'cases_evex_m4b' 'evex expect' @( '--avx512' )
Add-PySelftest 'ref_evex_m4b_selftest' 'evex tools' 'ref_evex_m4b.py'
# EVEX milestone M2 conversions / FP specials / shifts (ledger U230-U241): VCVT* (incl. the AVX512DQ
# QQ forms), VRCP14/VRSQRT14, VGETEXP, VGETMANT, VSCALEF, VFIXUPIMM, VRNDSCALE, VPSLL/VPSRL/VPSRA by
# xmm, VPROLV/VPRORV vs the SDM model ref_evex_m2_cvt.py.
Add-Exp 'cases_evex_m2_cvt' 'cases_evex_m2_cvt' 'evex expect' @( '--avx512' )
# AVX512DQ forms that need the integrated engine (ledger U290-U296 with U192, U210/U215, U230/U233):
# masked VRANGESS/SD, VREDUCESS/SD with DEST != SRC1, DQ insert/extract/broadcast, QQ conversions.
Add-Exp 'cases_evex_m3_dq_post' 'cases_evex_m3_dq_post' 'evex expect' @( '--avx512' )
# AVX512-FP16 (ledger U330-U339): EVEX maps 5/6 and the FP16 forms of map 3 vs the independent SDM
# model ref_evex_fp16.py (the AVX-512 opt-in includes UC_X86_AVX512_FP16).
Add-Exp 'cases_evex_fp16' 'cases_evex_fp16' 'evex fp16 expect' @( '--avx512' )
# AVX512_VBMI2, VPMULTISHIFTQB (AVX512_VBMI), AVX512_VNNI, AVX512_BF16 (ledger U550-U557) vs the independent
# SDM model ref_evex_m4a.py, Unicorn only: with every UC_X86_AVX512_* bit, and with AVX10.1 alone (every
# form is "<feature> OR AVX10.1", U371).
Add-Exp 'cases_evex_m4a' 'cases_evex_m4a' 'evex expect' @( '--avx512' )
Add-Exp 'cases_evex_m4a_avx10_1' 'cases_evex_m4a' 'avx10 evex expect' @( '--avx10', '1' )
Add-PySelftest 'ref_evex_m4a_selftest' 'evex tools' 'ref_evex_m4a.py'
# Intel AVX10 (ledger U370-U376): AVX10.2 alone (no AVX512* CPUID bits) runs the AVX-512 forms and the
# AVX10.2 BF16 / MINMAX / VCOMX / saturating-conversion instructions vs the spec model ref_avx10_a.py
# (AVX10.2 spec 361050-007). AVX10.1 alone (U371) runs the M1 EVEX and VEX opmask files at every VL.
Add-Exp 'cases_avx10_a' 'cases_avx10_a' 'avx10 expect' @( '--avx10', '2' )
Add-Exp 'cases_evex_m1_avx10_1' 'cases_evex_m1' 'avx10 expect' @( '--avx10', '1' )
Add-Exp 'cases_opmask_avx10_1' 'cases_opmask' 'avx10 expect' @( '--avx10', '1' )
# AVX10.2 (avx10_b, ledger U400-U412): FP8 conversions, VCVT2PS2PHX, EVEX VNNI-INT8/INT16, VDPPHPS,
# VMPSADBW, VMOVRS*, zero-extending VMOVD/VMOVW, EVEX SM4 vs the independent model ref_avx10_b.py.
Add-Exp 'cases_avx10_b' 'cases_avx10_b' 'avx10 expect' @( '--avx10', '2' )
# AVX10_V2_AUX (ACE 1.15 chapter 9, ledger U1095): FP32 -> FP8 (RTNE / RTO / bias), FP8 -> FP32, FP8 <->
# FP4 / FP6, VPMOVSSDB, VUNPACKB vs the independent model ref_ace_aux.py (AVX10.2 + UC_X86_AVX10_V2_AUX).
Add-Exp 'cases_avx10_v2aux' 'cases_avx10_v2aux' 'avx10 expect' @( '--avx10', '0x202' )
Add-PySelftest 'ref_ace_aux_selftest' 'avx10 tools' 'ref_ace_aux.py'
# U445 x U147 / U236 / U373 / U404: MXCSR.UM / OM = 0 must not reach the forms that behave as if every
# MXCSR exception were masked ({er} / {sae}, VRCP14, BF16, VCVT2PS2PHX); hand-derived cases.
Add-Exp 'cases_evex_sae_unmasked' 'cases_evex_sae_unmasked' 'avx10 evex expect' @( '--avx10', '2' )
# decision A9 (U940-U959): approximate reciprocals VRCP14* / VRSQRT14* / VRCPPH / VRSQRTPH / VRCPSH / VRSQRTSH
# (every VL, masking, memory, {1toN}, nine MXCSR settings, special values and boundaries; with AVX-512
# and with AVX10.1 alone), every FP16 encoding, VRCPBF16 / VRSQRTBF16 (every BF16 encoding, AVX10.2)
# vs the independent model ref_rcp.py (documented stand-in, docs\reciprocal.md); the legacy RCPPS /
# RSQRTPS / RCPSS / RSQRTSS (+ VEX) as hardware cases against the i5-13600K (0 differing).
Add-Exp 'cases_rcp' 'cases_rcp' 'evex fp16 rcp expect' @( '--avx512' )
Add-Exp 'cases_rcp_avx10_1' 'cases_rcp' 'avx10 evex rcp expect' @( '--avx10', '1' )
Add-Exp 'cases_rcp_f16_all' 'cases_rcp_f16_all' 'evex fp16 rcp expect' @( '--avx512' )
Add-Exp 'cases_rcp_bf16' 'cases_rcp_bf16' 'avx10 rcp expect' @( '--avx10', '2' )
Add-PySelftest 'ref_rcp_selftest' 'rcp tools' 'ref_rcp.py'
Add-Hw 'cases_rcp_hw' 'hw sse rcp'
# AMB-2 (plan 1.J.32, ledger U1240): every reciprocal form's documented special-case rows and threshold
# boundaries (7 MXCSR settings, scalar merging, VL128/256 masks) and the SDE labelled-reference input sample
# vs the analytic model of ref_amb2.py, whose selftest checks the model against the documented requirements
# (bound, special cases, DAZ/FTZ) with an independent checker and cross-checks ref_rcp.py.
Add-Exp 'cases_amb2' 'cases_amb2' 'avx10 evex fp16 rcp expect' @( '--avx10', '2' )
Add-PySelftest 'ref_amb2_selftest' 'rcp tools' 'ref_amb2.py'
# ledger U440-U442: F16C VCVTPS2PH/VCVTPH2PS hardware cases (gen_cases_f16c.py): every rounding source
# (imm8 / MXCSR.RC), FTZ/DAZ, denormal/tiny/overflow/NaN/inf, both VL, register and memory, unmasked
# exceptions; must be 0 differing against the i5-13600K.
Add-Hw 'cases_f16c' 'hw sse'
# ledger U98/U535: DPPD with two NaN products on the host CPU (gen_cases_nan.py --dp-hw): the emulator
# implements the SDM; the cases where the i5-13600K's element order gives another NaN are tagged
# "# known deviation: DPPD two NaN products" (docs\quirks.md), all others must match.
Add-Hw 'cases_dp_nan' 'hw sse nan quirks'
# ledger U434/U539: one or more hardware cases per documented i5-13600K deviation from the SDM
# (cases_quirks.txt, docs\quirks.md): every case is tagged "# known deviation: NAME" (the two PTWRITE
# cases: the VMware-guest artifact "PTWRITE hidden by the guest CPUID", not a CPU deviation).
Add-Hw 'cases_quirks' 'hw quirks x87' -Exclusive
# ledger U861: x87 compares / FTST / FXAM with unmasked #IS / #D / #IA (gen_cases_x87_cmp.py): TOP, tags,
# C1 and the condition codes / ZF PF CF on the host CPU; the unmasked-#IA condition codes are the documented
# deviation (tagged), everything else must match the i5-13600K.
Add-Hw 'cases_x87_cmp_exc' 'hw x87'
# Decoder tables: no X86OpEntry table of decode-new.c.inc (with its included decode*.c.inc) names an
# element twice ([0x42] = A, ..., [0x42] = B compiles silently, the later one wins) in either build
# (__Use_Original_Qemu = 0 and = 1): Emulator\tools\check_decode_dups.py.
Add-Suite 'check_decode_dups' 'tools' 'py' ( Join-Path $Root 'Emulator\tools\check_decode_dups.py' ) @( ( Join-Path $Root 'unicorn\qemu\target\i386' ) )
# ledger U445-U447: SSE/AVX/FMA post-computation exceptions (gen_cases_sse_exc.py): ADD/SUB/MUL/DIV/
# SQRT, HADD/HSUB/ADDSUB, DPPS/DPPD, all 60 FMA3 forms, CVT*, ROUND, RCP/RSQRT, MIN/MAX/CMP x {masked,
# OM=0, UM=0, PM=0, DM=0, all unmasked} x RC x FTZ/DAZ: exact/inexact tiny, rounds-to-normal, overflow
# per RC, exact overflow, denormal sources; must be 0 differing against the i5-13600K.
# U538: the emulator uses the SDM DPPS step order; the cases where the i5-13600K's grouping gives
# another outcome are tagged "# known deviation: DPPS exception step grouping" (dpps_steps.py).
Add-Hw 'cases_sse_exc' 'hw sse' -Shards 8
# U539: the Gate-1, reach, TSX/CET and fixes hardware files; every difference is tagged (known
# deviation, or host state: RDRAND/RDSEED values, CPUID initial APIC ID, WRUSS under host CR4.CET = 1).
# cases_reach needs CR0.NE = 1 (pending x87 exceptions raise #MF): the U540 default CR0 33h.
Add-Hw 'hwcheck_gate1' 'hw x87' -Exclusive
Add-Hw 'cases_reach' 'hw x87'
# U1831: cases_tsx_cet also runs WRUSS at CPL3 on both engines ("cpl=3 cr4.cet=1", U1830: Windows' CR4.CET = 1):
# #GP(0) on both, untagged (its two CPL0 WRUSS lines stay "# host state").
Add-Hw 'cases_tsx_cet' 'hw'
# U879: the TSX / WAITPKG / ENQCMD / CET / SMX / PCONFIG expected values of U110-U116 on UC_CPU_X86_MAX (no
# CPUID profile: the i5-13600K's hides these features; cases_tsx_cet.txt above is the hardware side).
Add-Exp 'cases_tsx_cet_max' 'cases_tsx_cet_max' 'expect'
# U1601-U1602 (agent waitpkg, plan 1.J.55): WAITPKG per platform switch - --host (bare host, CPUID.07H.0:ECX[5] = 1)
# executes UMONITOR / UMWAIT / TPAUSE and has IA32_UMWAIT_CONTROL, --guest / --vm (VMware guest, 0) #UD / #GP(0);
# cases_waitpkg_hw.txt is the hardware side (#UD on both in the VMware guest; bare-host steps in its header).
# U1620-U1621 (agent tpause, plan 1.J.55.a): the host file also checks the TSC advance of the wait (TSC windows).
Add-Exp 'waitpkg_host_p' 'cases_waitpkg_host' 'waitpkg cpuid expect' @( '--host' )
Add-Exp 'waitpkg_host_e' 'cases_waitpkg_host' 'waitpkg cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'waitpkg_guest' 'cases_waitpkg_guest' 'waitpkg cpuid expect' @( '--guest' )
Add-Exp 'waitpkg_vm' 'cases_waitpkg_guest' 'waitpkg cpuid expect' @( '--vm' )
Add-Hw 'cases_waitpkg_hw' 'hw waitpkg'
# U1670-U1672 (agent tscadj, plan 1.J.60): IA32_TSC_ADJUST (3BH) - every platform switch enumerates it (CPUID.07H.0:EBX[1] = 1:
# --host P / E, --guest / --vm) and so does the MAX model without a profile (U1670); cases_tscadj_off.txt runs a strict
# profile without the bit (cpuid_tscadj_off.txt: the gate profile with EBX[1] = 0): #GP(0).
Add-Exp 'tscadj_host_p' 'cases_tscadj' 'tscadj cpuid expect' @( '--host' )
Add-Exp 'tscadj_host_e' 'cases_tscadj' 'tscadj cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'tscadj_guest' 'cases_tscadj' 'tscadj cpuid expect' @( '--guest' )
Add-Exp 'tscadj_vm' 'cases_tscadj' 'tscadj cpuid expect' @( '--vm' )
Add-Exp 'tscadj_max' 'cases_tscadj' 'tscadj expect'
Add-Exp 'tscadj_off' 'cases_tscadj_off' 'tscadj cpuid expect' @( '--cpuid', ( Join-Path $D 'cpuid_tscadj_off.txt' ) )
# U1692 (agent pkru, reduced Gate 1 item 11): RDPKRU / WRPKRU with CR4.PKE = 1 and PKRU as XSAVE component 9 - every
# platform switch enumerates PKU (CPUID.07H.0:ECX[3] = 1) and so does the MAX model; MSR 35H (MSR_CORE_THREAD_COUNT) is not
# in the SDM 093 Vol4 tables of 06_B7H: #GP(0) everywhere. The paging side (AD / WD #PF, PK error bit, MOV to CR4, XRSTOR
# access rights) is in unicorn-test_x86 (test_x86_pk_*).
Add-Exp 'pkru_host_p' 'cases_pkru' 'pkru cpuid expect' @( '--host' )
Add-Exp 'pkru_host_e' 'cases_pkru' 'pkru cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'pkru_guest' 'cases_pkru' 'pkru cpuid expect' @( '--guest' )
Add-Exp 'pkru_vm' 'cases_pkru' 'pkru cpuid expect' @( '--vm' )
Add-Exp 'pkru_max' 'cases_pkru' 'pkru expect'
Add-Exp 'msr35_host_p' 'cases_msr35' 'pkru msr cpuid expect' @( '--host' )
Add-Exp 'msr35_host_e' 'cases_msr35' 'pkru msr cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'msr35_guest' 'cases_msr35' 'pkru msr cpuid expect' @( '--guest' )
Add-Exp 'msr35_vm' 'cases_msr35' 'pkru msr cpuid expect' @( '--vm' )
Add-Exp 'msr35_max' 'cases_msr35' 'pkru msr expect'
# U1712 (agent ptwrite, Gate 1 item P18): PTWRITE per platform switch - --host P / E (CPUID.(14H,0):EBX[4] = 1) executes it
# (operand read and its #GP / #SS / #PF / #AC, LOCK / 66H #UD, Intel PT tracing not modelled = no-op), --guest / --vm and no
# profile (not enumerated) #UD; CR0 40033H (AM = 1) for the #AC lines. The hardware side is cases_quirks.txt.
Add-Exp 'ptwrite_host_p' 'cases_ptwrite_host' 'ptwrite cpuid expect' @( '--host', '--cr0', '0x40033' )
Add-Exp 'ptwrite_host_e' 'cases_ptwrite_host' 'ptwrite cpuid expect' @( '--host', '--host-core', 'E', '--cr0', '0x40033' )
Add-Exp 'ptwrite_guest' 'cases_ptwrite_guest' 'ptwrite cpuid expect' @( '--guest', '--cr0', '0x40033' )
Add-Exp 'ptwrite_vm' 'cases_ptwrite_guest' 'ptwrite cpuid expect' @( '--vm', '--cr0', '0x40033' )
Add-Exp 'ptwrite_max' 'cases_ptwrite_guest' 'ptwrite expect' @( '--cr0', '0x40033' )
# U750-U753: CPL3 far CALL / RET far / IRETQ (heaven's gate included) with CET off on both engines,
# the transfers the CET paths of U750-U752 run through (cases_cet2_hw.txt, 0 differing).
Add-Hw 'cases_cet2_hw' 'hw cet'
Add-Hw 'cases_fixes' 'hw'
# ledger U475-U499: Tier-2/3 upstream QEMU backports and the PUSHF / LFENCE fixes, CPL3-reachable
# behaviour (cases_backport_t2.txt, self-generated snippets): must be 0 differing against the i5-13600K.
Add-Hw 'cases_backport_t2' 'hw backport x87'
# The CPL0-only parts of the same backports as expected-value cases from the SDM text (the file has
# only "=>" lines; it runs without --expect-only as before).
Add-Suite 'cases_backport_t2_sdm' 'backport expect' 'exit' 'emu-alltest' ( @( '--cases', ( CaseFile 'cases_backport_t2_sdm' ) ) + ( SnipExport 'cases_backport_t2_sdm' ) )
Add-Snip 'cases_backport_t2_sdm' 'backport expect'   # U1978
# U468: Tier 1 upstream QEMU backports (U453-U467): hardware lines (cpl=3 = Unicorn at CPL3 with the
# Windows GDT, U452; compatibility mode via CS 23h) and SDM expected values (STI/LSS + TF, MOV DR,
# compatibility-mode SYSCALL); known deviations LOCK PREFETCHW, SYSRET at CPL3 in compatibility mode.
Add-Hw 'cases_backport_t1' 'hw backport'
# ledger U572-U575: the GFNI / VAES / VPCLMULQDQ operation of every EVEX case of cases_evex_m4b.txt on the
# host's VEX.256 / VEX.128 / legacy forms with the same data (cases_evex_m4b_hw.txt, ref_evex_m4b.py --hwgen):
# Unicorn must match the i5-13600K (0 differing), and the CPU's results must match the model (--hwcmp).
Add-Hw 'cases_evex_m4b_hw' 'hw evex'
Add-HwCmp 'ref_evex_m4b_hwcmp' 'hw evex' 'ref_evex_m4b.py' 'cases_evex_m4b_hw'
# ledger U555/U552/U553/U554/U557: the parts of the AVX512_VNNI / VBMI2 / VBMI / BF16 model the i5-13600K
# can run (cases_evex_m4a_hw.txt, ref_evex_m4a.py --hwgen): VEX AVX-VNNI, SHLD/SHRD r16/r32/r64, ROR r64,
# VFMADD231SS with MXCSR 9FC0h (one VDPBF16PS step); Unicorn == CPU (0 differing) and CPU == model (--hwcmp).
Add-Hw 'cases_evex_m4a_hw' 'hw evex'
Add-HwCmp 'ref_evex_m4a_hwcmp' 'hw evex' 'ref_evex_m4a.py' 'cases_evex_m4a_hw'
# U590-U609 (plan 1.F.7): LSS/LFS/LGS m16:64, 64-bit stack width of IRET/RETF/far CALL, page-crossing
# stores, MAXPHYADDR, SYSCALL and EFER.SCE, RDPMC; hardware lines at CPL3 + SDM expected values.
Add-Hw 'cases_fixes2' 'hw'
# U1652 (agent hostfix): the MAXPHYADDR boundary of MOV CR3 per platform switch - --guest / --vm (VMware guest,
# 80000008H EAX 302DH: bit 45 #GP(0)), --host / --host --host-core E (bare host, 302EH: bit 45 an address bit,
# bit 46 #GP(0)); cases_fixes2.txt keeps only the lines that hold under both (it also runs in the bare-host package).
Add-Exp 'maxphy_guest' 'cases_maxphy_guest' 'cpuid expect' @( '--guest' )
Add-Exp 'maxphy_vm' 'cases_maxphy_guest' 'cpuid expect' @( '--vm' )
Add-Exp 'maxphy_host_p' 'cases_maxphy_host' 'cpuid expect' @( '--host' )
Add-Exp 'maxphy_host_e' 'cases_maxphy_host' 'cpuid expect' @( '--host', '--host-core', 'E' )
# U700-U719: the state at a fault (flags, destinations, partial stores) vs the i5-13600K, plus
# expected values against the SDM where the host cannot show it.
Add-Hw 'cases_fix3' 'hw'
# U770-U789: exception error codes in expected-value cases ("#GP(0)", UC_CTL_X86_EXCEPTION), flags and
# partial stores at a fault or a memory hook, LOCK 0F 0D; hardware lines vs the i5-13600K.
Add-Hw 'cases_fix4' 'hw'
# U932: BSF/BSR with a zero source leave the whole destination unchanged (32-bit too) vs the i5-13600K.
Add-Hw 'cases_bsf_zero_hw' 'hw'
# U1121: BSF/BSR flags with a non-zero source (SDM 092: PF from the source's bit count, CF/OF/SF/AF
# cleared) vs the i5-13600K (P- and E-core runs agree; PF from the result: known deviation).
Add-Hw 'cases_bsf_flags_hw' 'hw'
# U1750-U1754 (Gate 1 "Fix D1-D4", notes\eread\P11_probe_fixes.md): LOCK MOV DR at CPL3 #GP, LAR bits 19:16,
# MOV SS / POP SS chain with TF, PCMPESTRx REX.W / VEX.W1 64-bit lengths vs the i5-13600K (cases_probefix_hw.txt).
Add-Hw 'cases_probefix_hw' 'hw'
# U1760-U1767 (Gate 1 step 23, notes\eread\P12_debug_fault_fixes.md): MOV SS / POP SS and instruction / data
# breakpoints, single-step DR6, RF for one instruction, #PF W = 1 of a read-modify-write vs the i5-13600K (debug
# registers through the U1740 tokens; cases_dbgfix_hw.txt).
Add-Hw 'cases_dbgfix_hw' 'hw'
# U1850-U1851 (H22 item 3, Gate 1 D1b, notes\eread\P21_hostfix.md): LOCK on the 13 CPL0-only instructions measured
# at CPL3 raises #GP(0) before the LOCK #UD, as the i5-13600K (bare host P / E, VMware guest; cases_lockpriv_hw.txt).
Add-Hw 'cases_lockpriv_hw' 'hw'
# U1856-U1857 (H22 item 7, BDF97 "Follow SDM"): VCVTPS2PH m64 with a faulting store leaves MXCSR unchanged, #PF before
# #XM; the P-core / VMware-guest values are the tagged known deviation (cases_bdf97_hw.txt).
Add-Hw 'cases_bdf97_hw' 'hw sse'
# U2043 (P37, the sample's #GP): the lazy flags of XOR / the previous instruction survive when the next instruction crosses
# into the next page (the translator restart in a new TB) - our own routine written to CODE + 0FF7H, vs the i5-13600K
# (cases_xpage_hw.txt).
Add-Hw 'cases_xpage_hw' 'hw'
# U870-U899: element-size memory operands, the MMX state at a fault, IMUL/MUL flags, ... vs the i5-13600K
# (cases_fix5.txt, hardware lines plus SDM expected values where the host cannot show it).
Add-Hw 'cases_fix5' 'hw'
# U870/U871 audit: every EVEX form whose memory operand at VL 128 / LIG is smaller than 16 bytes, ending at the
# last mapped byte: no fault (only the element is accessed); expected values, Unicorn only.
Add-Exp 'cases_fix5_evex' 'cases_fix5_evex' 'evex avx10 expect' @( '--avx512', '--avx10', '2' )
# U610-U616 (plan 1.15e, Intel APX part 1): EGPRs R16-R31 through REX2, REX2 decode / #UD rules, APX
# state (XSAVE component 19), CPUID, the APX extension of EVEX instructions; expected values from the
# U750-U760: CET on far CALL / RET far / IRETQ (call gates, supervisor tokens), SYSRET/SYSEXIT SSP,
# WRMSR / XSAVES / XRSTORS of the CET MSRs (CET_U / CET_S) and ENDBR64 with REX2, as CPL0 sequences in
# Unicorn only; expected values from the independent model ref_cet2.py (CR0.WP for CR4.CET, APX for REX2).
Add-Exp 'cases_cet2' 'cases_cet2' 'cet expect' @( '--apx', '--cr0', '0x10011' )
Add-PySelftest 'ref_cet2_selftest' 'cet tools' 'ref_cet2.py'
# U850-U852: the emu-alltest sweep forms that stay out of the native run (WRFSBASE/WRGSBASE, SYSENTER's
# entry check, LFS/LGS: host FS/GS or a kernel entry natively); expected values from the independent
# SDM model ref_sweep_sdm.py.
Add-Exp 'cases_sweep_sdm' 'cases_sweep_sdm' 'expect'
Add-PySelftest 'ref_sweep_sdm_selftest' 'tools' 'ref_sweep_sdm.py'
# independent model ref_apx_core.py, Unicorn only with the APX opt-in (the host has no APX).
Add-Exp 'cases_apx_core' 'cases_apx_core' 'apx expect' @( '--apx' )
# U616: the APX extension of EVEX instructions (EVEX.B4 / X4 = ~U / R4 for GPRs), with AVX-512.
Add-Exp 'cases_apx_evex' 'cases_apx_evex' 'apx evex expect' @( '--apx', '--avx512' )
# The same instructions with R0-R15 only: legacy encoding on the i5-13600K vs the REX2 encoding on
# Unicorn ("<legacy> ~~ <REX2>" pairs; --apx and NO CPUID profile, which would hide APX), 0 differing;
# the CPU's results must also match the model (--hwcmp).
Add-Suite 'cases_apx_core_hw' 'hw apx' 'hwnp' 'emu-alltest' @( '--cases', ( CaseFile 'cases_apx_core_hw' ), '--apx' )
Add-HwCmp 'ref_apx_core_hwcmp' 'hw apx' 'ref_apx_core.py' 'cases_apx_core_hw'
# U640-U689 (Intel APX parts 2/3): EVEX map 4 (promoted legacy instructions with NDD / NF / ZU), the
# APX conditional / PUSH2 / POP2 / JMPABS instructions, promoted map 2/3 and VEX instructions;
# expected values from the independent model ref_apx_map4.py (Unicorn only, APX opt-in).
Add-Exp 'cases_apx_map4' 'cases_apx_map4' 'apx expect' @( '--apx' )
Add-PySelftest 'ref_apx_map4_selftest' 'apx tools' 'ref_apx_map4.py'
# EVEX map 4 decode sweep: every opcode x pp x W x ND x NF x mod (x reg / V), #UD or fault-free, from
# the APX spec table cross-checked against Intel XED (gen_apx_map4_sweep.py).
Add-Exp 'cases_apx_map4_sweep' 'cases_apx_map4_sweep' 'apx expect' @( '--apx' )
# U646: the APX-promoted KMOV* and AMX forms need AVX-512 and AMX as well.
Add-Exp 'cases_apx_map4_ext' 'cases_apx_map4_ext' 'apx evex amx expect' @( '--apx', '--avx512', '--amx' )
# U790 (plan 1.F.13): the ten APX forms without a case before - JMPABS inside the snippet, EVEX ENQCMD/ENQCMDS,
# URDMSR/UWRMSR (IA32_USER_MSR_CTL), WRSSD/Q + WRUSSD/Q (CR0.WP + CR4.CET + IA32_S_CET set inline at CPL0),
# TILELOADDT1; expected values from the independent model ref_apx_cases.py (Unicorn only).
Add-Exp 'cases_apx_sys' 'cases_apx_sys' 'apx amx cet expect' @( '--apx', '--amx' )
Add-PySelftest 'ref_apx_cases_selftest' 'apx tools' 'ref_apx_cases.py'
# The same instructions with R0-R15 only: the i5-13600K runs the legacy equivalent (ND = 1: MOV + op +
# MOVZX; NF = 1: inside PUSHFQ ... POPFQ), Unicorn the EVEX encoding ("~~" pairs, no CPUID profile),
# 0 differing; the CPU's results must also match the model (--hwcmp).
Add-Suite 'cases_apx_map4_hw' 'hw apx' 'hwnp' 'emu-alltest' @( '--cases', ( CaseFile 'cases_apx_map4_hw' ), '--apx' )
Add-HwCmp 'ref_apx_map4_hwcmp' 'hw apx' 'ref_apx_map4.py' 'cases_apx_map4_hw'
# U835 (decision D8): RDRAND/RDSEED source. The default seeded model (since U2091 a CTR_DRBG of NIST SP 800-90A, AES-128, seed 0;
# values from the independent reference ref_ctr_drbg.py, P42 - U835 used SplitMix64), an explicit seed (--rdrand-seed), and the host
# DRNG (--HostSeed: flags only, the values are the host's).
Add-Exp 'cases_rdrand' 'cases_rdrand' 'rdrand expect'
Add-Exp 'cases_rdrand_seed' 'cases_rdrand_seed' 'rdrand expect' @( '--rdrand-seed', '0x1234' )
Add-Exp 'cases_rdrand_host' 'cases_rdrand_host' 'rdrand expect' @( '--HostSeed' )
# U834: alignment check (#AC) at CPL3 vs the i5-13600K (Windows: CR0.AM = 1; RFLAGS.AC set by the
# case), and expected values for AVX-512 / APX / CPL0 (CR0.AM = 1 through --cr0).
Add-Hw 'cases_ac_hw' 'hw ac'
Add-Exp 'cases_ac' 'cases_ac' 'ac expect' @( '--avx512', '--apx', '--cr0', '0x40011' )
# U800-U829 (agent sysins): AVX512DQ VPMOVD2M/Q2M/M2D/M2Q with AVX-512 and with AVX10.1 alone;
# expected values from the independent model ref_sysins.py (Unicorn only, the host has no AVX-512).
Add-Exp 'cases_sysins_dq' 'cases_sysins_dq' 'evex sysins expect' @( '--avx512' )
Add-Exp 'cases_sysins_dq_avx10_1' 'cases_sysins_dq' 'avx10 evex sysins expect' @( '--avx10', '1' )
Add-PySelftest 'ref_sysins_selftest' 'sysins tools' 'ref_sysins.py'
# U801-U829: the CPL0 system instructions (WRMSRNS, ...) at CPL0 and CPL3 (cpl=3), MAX model, expected
# values from ref_sysins.py; and at CPL3 against the i5-13600K with its strict profile (hardware).
Add-Exp 'cases_sysins' 'cases_sysins' 'sysins expect'
# U803: their Intel APX EVEX forms (MSR-IMM EVEX map 7, ...), with the APX opt-in
Add-Exp 'cases_sysins_apx' 'cases_sysins_apx' 'sysins apx expect' @( '--apx' )
Add-Hw 'cases_sysins_hw' 'hw sysins'
# U1020-U1022 (agent pconfig): PCONFIG MKTME_KEY_PROGRAM / TSE leaves and the TME / TME-MK MSRs, MAX
# model at CPL0, expected values from the independent model ref_pconfig.py; at CPL3 against the
# i5-13600K with its strict profile (no PCONFIG / TME_EN there: #UD / #GP on both).
Add-PySelftest 'ref_pconfig_selftest' 'pconfig tools' 'ref_pconfig.py'
Add-Exp 'cases_pconfig' 'cases_pconfig' 'pconfig expect'
Add-Hw 'cases_pconfig_hw' 'hw pconfig'
# U900-U929 (agent sysmsr): the SYSCALL / SYSENTER / SYSRET / SYSEXIT transitions and the MSR model,
# MAX model, expected values from the independent model ref_sysmsr.py (Unicorn only: the transitions
# would enter the host kernel, and WRMSR/RDMSR are CPL0 instructions).
Add-Exp 'cases_sysmsr' 'cases_sysmsr' 'sysmsr expect'
Add-PySelftest 'ref_sysmsr_selftest' 'sysmsr tools' 'ref_sysmsr.py'
# U1823 (agent g1rest, plan 5.2 sub-step 8): SYSCALL / SYSENTER from 64-bit CPL3 to CPL0 with the Windows x64 GDT
# selectors (IA32_STAR 0023_0010_0000_0000H; IA32_FMASK 4700H / 47700H chosen, not captured), expected values from
# ref_sysmsr.py, under the
# strict --host and --guest profiles and the MAX model.
Add-Exp 'cases_sysmsr_win_host' 'cases_sysmsr_win' 'sysmsr cpuid expect' @( '--host' )
Add-Exp 'cases_sysmsr_win_guest' 'cases_sysmsr_win' 'sysmsr cpuid expect' @( '--guest' )
Add-Exp 'cases_sysmsr_win_max' 'cases_sysmsr_win' 'sysmsr expect'
# U1170-U1199 (fredlam): FRED frame / ERETS / ERETU and LAM reference model (specification-validated)
Add-PySelftest 'ref_fred_lam_selftest' 'fred lam tools' 'ref_fred_lam.py'
Add-Exp 'cases_lam' 'cases_lam' 'lam expect' @( '--x86-features', '8' )
# U1260-U1299 (agent sgx): the SGX reference model (MRENCLAVE, SIGSTRUCT RSA-3072, launch token, key
# derivation placeholder; specification-validated) and the consistency of x86_sgx_vectors.inc
Add-PySelftest 'ref_sgx_selftest' 'sgx tools' 'ref_sgx.py'
# U1268 (agent sgxforms): the SGX / SMX opt-in gates (CPUID 12H, ENCLS / ENCLU / GETSEC faults), SDM expectations
Add-Exp 'cases_sgx' 'cases_sgx' 'sgx expect' @( '--x86-features', '48' )
# U1300-U1399 (vmx): VMX reference model (field encodings, capability classes, exit qualifications,
# bitmaps, EPT walk, virtual APIC; specification-validated) - the values of the vmx_ unit tests
Add-PySelftest 'ref_vmx_selftest' 'vmx tools' 'ref_vmx.py'
# U907: the architectural PMU MSRs as storage, with a CPUID profile that enumerates a version-4 PMU
# (cpuid_sysmsr_pmu.txt: the i5-13600K capture with leaf 0AH replaced), non-strict (MAX features)
Add-Exp 'cases_sysmsr_pmu' 'cases_sysmsr_pmu' 'sysmsr expect' @( '--cpuid', ( Join-Path $D 'cpuid_sysmsr_pmu.txt' ), '--no-strict' )
# U990-U995 (agent xeonphi): the Intel Xeon Phi-only families (AVX512_4VNNIW, AVX512_4FMAPS, AVX512ER,
# AVX512PF, PREFETCHWT1) with --xeonphi; expected values from the independent model ref_xeonphi.py
# (Unicorn only, the host has no AVX-512); cases_xeonphi_off: every form #UD with --avx512 alone.
Add-Exp 'cases_xeonphi' 'cases_xeonphi' 'evex xeonphi expect' @( '--xeonphi' )
Add-Exp 'cases_xeonphi_off' 'cases_xeonphi_off' 'evex xeonphi expect' @( '--avx512' )
Add-PySelftest 'ref_xeonphi_selftest' 'xeonphi tools' 'ref_xeonphi.py'
# U1200-U1219 (agent xstatefix): XSAVE / XRSTOR / XSAVEOPT with CR0.TS = 1 raise #NM (U1201, SDM 092 Vol2D
# 6-52 / 6-59 / 6-65); CR0.TS is set for the snippet only (U1200). Control: the same snippets with TS = 0.
Add-Exp 'cases_xstate_ts' 'cases_xstate_ts' 'xstate expect' @( '--cr0', '0x10019' )
Add-Exp 'cases_xstate_ts_ctl' 'cases_xstate_ts_ctl' 'xstate expect' @( '--cr0', '0x10011' )
# U1202: CPUID 40000000H-4FFFFFFFH on the model path = the highest basic leaf (SDM 092 Vol1 p. 21-2), with
# and without the AVX-512 opt-in
Add-Exp 'cases_cpuid_hv' 'cases_cpuid_hv' 'xstate cpuid expect'
Add-Exp 'cases_cpuid_hv_avx512' 'cases_cpuid_hv' 'xstate cpuid evex expect' @( '--avx512' )
# U1203: XSETBV / XGETBV #GP(0) conditions (T-4); XGETBV ECX = 1 with a strict profile without XGETBV1
# (cpuid_xgetbv1_off.txt: the gate profile with CPUID.(0DH,1):EAX[2] = 0)
Add-Exp 'cases_xcr' 'cases_xcr' 'xstate expect'
Add-Exp 'cases_xcr_nox1' 'cases_xcr_nox1' 'xstate cpuid expect' @( '--cpuid', ( Join-Path $D 'cpuid_xgetbv1_off.txt' ) )
# U1480-U1481 (agent xinusemodel, docs/xinuse.md): the XINUSE models (UC_CTL_X86_XINUSE_MODEL) as emu-alltest selects
# them - --guest / --vm P-core, --host the verified / profile core type (else a logged P-core fallback), --AMD always in
# use, --xinuse-model p-core|e-core overrides. One expected-value file per model (the measured i5-13600K P-core / E-core
# outcomes); xu_cli runs every switch combination against all three files (the wrong ones must differ) and checks the
# log lines and usage errors.
Add-Exp 'xu_pcore_guest' 'cases_xu_pcore' 'xstate xinuse expect' @( '--guest', '--xcr0', '0x7' )
Add-Exp 'xu_ecore_host' 'cases_xu_ecore' 'xstate xinuse cpuid expect' @( '--host', '--cpuid', ( Join-Path $D 'cpuid_xu_hybrid_ecore.txt' ), '--xcr0', '0x7' )
Add-Exp 'xu_always_amd' 'cases_xu_always' 'xstate xinuse expect' @( '--AMD', '--xcr0', '0x7' )
Add-Suite 'xu_cli' 'xstate xinuse tools' 'py' ( Join-Path $Root 'Emulator\tools\xinuse_cli_check.py' ) @( '--cli', ( Join-Path $T 'emu-alltest.exe' ) )
# U1204: the 15-byte instruction length limit, #GP(0) (T-3; the hardware lines are in the NON-gate file
# cases_insn_len_hw.txt, checked by hand with --hw-repeat); CPUID 0BH / 1FH unlisted sub-leaves with the gate
# profile (T-5)
Add-Exp 'cases_insn_len' 'cases_insn_len' 'xstate evex expect' @( '--avx512' )
Add-Exp 'cases_cpuid_topo' 'cases_cpuid_topo' 'xstate cpuid expect' @( '--cpuid', $Cpuid )
# U1220-U1239 (agent sdetests, plan 1.J.36): the SDE / Pin-kit test leads T-6..T-14 (sdeida report section 12, SDM 093):
# T-6 XRSTOR header #GP(0) (XCR0 = 7); T-7 / T-8 group-11 and reserved VEX maps #UD, T-9 SYSCALL / SYSRET round trip
# (also with the APX / AVX-512 / AMX opt-ins, which must not give these encodings a meaning); T-12 the x87 tag word;
# T-13 MOVDIRI / MOVDIR64B each gated by its own CPUID bit (two profiles derived from the gate profile); T-14 XSAVEC
# compacted layout / XSTATE_BV / header writes; cases_st_hw: the hardware side of T-6/T-7/T-8/T-12/T-14 (one outcome
# on P- and E-cores, --hw-repeat)
Add-Exp 'cases_st_xrstor' 'cases_st_xrstor' 'xstate expect' @( '--xcr0', '0x7' )
Add-Exp 'cases_st_decode' 'cases_st_decode' 'expect'
Add-Exp 'cases_st_decode_ext' 'cases_st_decode' 'apx evex amx expect' @( '--apx', '--avx512', '--amx' )
Add-Exp 'cases_st_x87tag' 'cases_st_x87tag' 'x87 expect'
Add-Exp 'cases_st_movdiri_only' 'cases_st_movdiri_only' 'cpuid expect' @( '--cpuid', ( Join-Path $D 'cpuid_st_movdiri_only.txt' ) )
Add-Exp 'cases_st_movdir64b_only' 'cases_st_movdir64b_only' 'cpuid expect' @( '--cpuid', ( Join-Path $D 'cpuid_st_movdir64b_only.txt' ) )
Add-Exp 'cases_st_xsavec' 'cases_st_xsavec' 'xstate evex expect' @( '--avx512' )
Add-Exp 'cases_st_xsavec_pad' 'cases_st_xsavec_pad' 'xstate apx amx expect' @( '--apx', '--amx' )
Add-Hw 'cases_st_hw' 'hw xstate x87'
# U1221: the compacted form of XRSTOR #GP(0) with a strict profile without CPUID.(0DH,1):EAX.XSAVEC (T-6)
Add-Exp 'cases_st_xrstor_noxsavec' 'cases_st_xrstor_noxsavec' 'xstate cpuid expect' @( '--cpuid', ( Join-Path $D 'cpuid_st_noxsavec.txt' ) )
# U1474 (agent ucode): the gate profile's microcode identity ("#@ microcode" line, UC_CTL_X86_MICROCODE): the
# IA32_BIOS_SIGN_ID reporting sequence, IA32_PLATFORM_ID, IA32_BIOS_UPDT_TRIG accepted at CPL0 (docs/quirks.md)
Add-Exp 'cases_ucode' 'cases_ucode' 'sysmsr cpuid expect' @( '--cpuid', $Cpuid )
# U1490 (agent bareprofile): the same profile ("#@ microcode" line) with a platform switch. The SMM revision write
# initialises the engine, so it must follow the pre-init controls; before U1490 every case of these runs failed with
# "uc_ctl_set_x86_microcode failed"
Add-Exp 'cases_ucode_host' 'cases_ucode' 'sysmsr cpuid expect' @( '--cpuid', $Cpuid, '--host' )
Add-Exp 'cases_ucode_guest' 'cases_ucode' 'sysmsr cpuid expect' @( '--cpuid', $Cpuid, '--guest' )
# U1491-U1493 (agent bareprofile, plan 1.J.52): the platform switch selects the CPUID profile - --host the bare host
# (cpuid_i5-13600k_bare_host.txt, P-core; --host-core E: _bare_host_ecore.txt), --guest / --vm the VMware guest (the gate
# profile); --cpuid FILE overrides. Expected values from the profile rows (the captures); bp2_profile_check re-derives both
# bare-host files from the captures (Emulator\tools\bare_host_cpuid.py --check, byte for byte); bp2_cli checks the switch
# exclusions, the log lines and the negative controls (the wrong environment's expectations fail).
Add-Exp 'bp2_host_p' 'cases_bp2_host_p' 'cpuid expect bp2' @( '--host' )
Add-Exp 'bp2_host_e' 'cases_bp2_host_e' 'cpuid expect bp2' @( '--host', '--host-core', 'E' )
Add-Exp 'bp2_guest' 'cases_bp2_guest' 'cpuid expect bp2' @( '--guest' )
Add-Exp 'bp2_vm' 'cases_bp2_guest' 'cpuid expect bp2' @( '--vm' )
Add-Exp 'bp2_cpuid_override' 'cases_bp2_guest' 'cpuid expect bp2' @( '--host', '--cpuid', $Cpuid )
# U1533 (agent xcr0fix, notes\eread\X10_xcr0_ospke_fix.md): the XSTATE configuration at ENGINE RESET per platform switch
# (no --xcr0: XGETBV(0) 7H, CPUID 01H / 07H.0 / 0DH.0 / 0DH.1, CR4.PKE / OSPKE 0, IA32_XSS, RDPKRU / WRPKRU #UD, XGETBV(1)
# sequences per XINUSE model); expected values = the clean captures (--host 2026-10-16, --guest 2026-10-15)
Add-Exp 'xz_host_p' 'cases_xz_host_p' 'xstate cpuid expect xz' @( '--host' )
Add-Exp 'xz_host_e' 'cases_xz_host_e' 'xstate cpuid expect xz' @( '--host', '--host-core', 'E' )
Add-Exp 'xz_guest' 'cases_xz_guest' 'xstate cpuid expect xz' @( '--guest' )
Add-Exp 'xz_vm' 'cases_xz_guest' 'xstate cpuid expect xz' @( '--vm' )
# U1630-U1632 (agent gating, reduced Gate 1 item 4, your decision 2026-10-17 (G10); notes\eread\G13_gating_only.md): the
# GATING ONLY features on every profile - each cited CPUID bit absent and every instruction form #UD (MPX BND*: NOP;
# CR4.LA57: #GP(0)); one file for all profiles (the bits are 0 on all three CPUID profiles), the CET_U / CET_S lines
# (1.J.37.c) per profile, and the hardware side under the guest profile. The four files are generated by
# Emulator\tools\isa\gen_gating_cases.py; gating_cases_check regenerates them in memory and compares (--check).
Add-Exp 'gating_host_p' 'cases_gating' 'gating cpuid expect' @( '--host' )
Add-Exp 'gating_host_e' 'cases_gating' 'gating cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'gating_guest' 'cases_gating' 'gating cpuid expect' @( '--guest' )
Add-Exp 'gating_vm' 'cases_gating' 'gating cpuid expect' @( '--vm' )
Add-Exp 'gating_cet_host_p' 'cases_gating_host' 'gating cet cpuid expect' @( '--host' )
Add-Exp 'gating_cet_host_e' 'cases_gating_host' 'gating cet cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'gating_cet_guest' 'cases_gating_guest' 'gating cet cpuid expect' @( '--guest' )
Add-Exp 'gating_cet_vm' 'cases_gating_guest' 'gating cet cpuid expect' @( '--vm' )
Add-Hw 'cases_gating_hw' 'hw gating'
# U1660-U1662 (agent vmxprof, plan 1.J.59, your decision "Profiles enable VMX" 2026-10-19; notes\eread\V5_vmx_profiles.md):
# a strict CPUID profile that reports VMX turns the VMX model on (U1660) and --host / --guest / --vm start with the platform's
# IA32_FEATURE_CONTROL = 5 (U1661): CPL0 MOV CR4.VMXE -> VMXON -> VMPTRLD / VMPTRST / VMCLEAR -> VMXOFF, the #GP / #UD /
# VMfailInvalid paths and the capability MSRs per platform switch; the same profiles through --cpuid (API path, FEATURE_CONTROL
# reset 0: the lock / bit-2 rules); VMX stays off without a profile and with a non-strict one.
Add-Exp 'vmx_prof_host_p' 'cases_vmx_profile_host' 'vmx cpuid expect' @( '--host' )
Add-Exp 'vmx_prof_host_e' 'cases_vmx_profile_host' 'vmx cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'vmx_prof_guest' 'cases_vmx_profile_guest' 'vmx cpuid expect' @( '--guest' )
Add-Exp 'vmx_prof_vm' 'cases_vmx_profile_guest' 'vmx cpuid expect' @( '--vm' )
Add-Exp 'vmx_prof_api_host_p' 'cases_vmx_profile_api' 'vmx cpuid expect' @( '--cpuid', ( Join-Path $D 'cpuid_i5-13600k_bare_host.txt' ) )
Add-Exp 'vmx_prof_api_host_e' 'cases_vmx_profile_api' 'vmx cpuid expect' @( '--cpuid', ( Join-Path $D 'cpuid_i5-13600k_bare_host_ecore.txt' ) )
Add-Exp 'vmx_prof_api_guest' 'cases_vmx_profile_api' 'vmx cpuid expect' @( '--cpuid', $Cpuid )
Add-Exp 'vmx_prof_off' 'cases_vmx_noprofile' 'vmx expect'
Add-Exp 'vmx_prof_nonstrict' 'cases_vmx_noprofile' 'vmx cpuid expect' @( '--cpuid', $Cpuid, '--no-strict' )
# U1680-U1682 (agent cr4prof, plan 1.J.61; notes\eread\V6_cr4_profile.md): the CR4 bits a strict CPUID profile hides are
# reserved (MOV CR4 #GP(0), absent from IA32_VMX_CR4_FIXED1 and from the reset CR4) per platform switch; without a profile and
# with a non-strict one the CPU model decides as before.
Add-Exp 'cr4_prof_host_p' 'cases_cr4_profile_host' 'cr4 vmx cpuid expect' @( '--host' )
Add-Exp 'cr4_prof_host_e' 'cases_cr4_profile_host' 'cr4 vmx cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'cr4_prof_guest' 'cases_cr4_profile_guest' 'cr4 vmx cpuid expect' @( '--guest' )
Add-Exp 'cr4_prof_vm' 'cases_cr4_profile_guest' 'cr4 vmx cpuid expect' @( '--vm' )
Add-Exp 'cr4_prof_off' 'cases_cr4_noprofile' 'cr4 expect'
Add-Exp 'cr4_prof_nonstrict' 'cases_cr4_noprofile' 'cr4 cpuid expect' @( '--cpuid', $Cpuid, '--no-strict' )
# U1780-U1781 (agent efer, plan 1.J.63; notes\eread\P14_efer_profile.md): the IA32_EFER bits a strict CPUID profile does not
# enumerate (SVME with 80000001H:ECX.SVM, FFXSR, SCE / NXE with their EDX bits) are reserved: WRMSR #GP(0) per platform switch,
# also with --apx (no VMX model there, so the MAX model's SVM bit used to make SVME writable); without a profile and with a
# non-strict one the CPU model decides as before.
Add-Exp 'efer_prof_host_p' 'cases_efer_profile' 'efer cpuid expect' @( '--host' )
Add-Exp 'efer_prof_host_e' 'cases_efer_profile' 'efer cpuid expect' @( '--host', '--host-core', 'E' )
Add-Exp 'efer_prof_guest' 'cases_efer_profile' 'efer cpuid expect' @( '--guest' )
Add-Exp 'efer_prof_vm' 'cases_efer_profile' 'efer cpuid expect' @( '--vm' )
Add-Exp 'efer_prof_host_apx' 'cases_efer_profile' 'efer cpuid apx expect' @( '--host', '--apx' )
Add-Exp 'efer_prof_guest_apx' 'cases_efer_profile' 'efer cpuid apx expect' @( '--guest', '--apx' )
Add-Exp 'efer_prof_off' 'cases_efer_noprofile' 'efer expect'
Add-Exp 'efer_prof_nonstrict' 'cases_efer_noprofile' 'efer cpuid expect' @( '--cpuid', $Cpuid, '--no-strict' )
Add-Suite 'gating_cases_check' 'gating tools' 'py' ( Join-Path $Root 'Emulator\tools\isa\gen_gating_cases.py' ) @( '--check' )
# U1790-U1791 (agent tablechk, plan 1.J.57.a; notes\eread\P15_1J57a_table_check.md): the per-profile labels of the instruction
# table's cross section (U1632, plan 1.J.57). Emulator\tools\isa\check_instruction_table.py runs gen_instruction_table.py on its
# inputs into a temporary folder and checks every cross-row reason against the two CPUID profiles (--host bare host, --guest
# VMware guest) with its own family -> CPUID bit list; the "enumerated on --host" count is a cross-check only; three in-memory
# negative controls must fail. Writes nothing in the repository.
Add-Suite 'instruction_table_check' 'table cpuid tools' 'py' ( Join-Path $Root 'Emulator\tools\isa\check_instruction_table.py' ) @()
# U1922 (Gate 1 fix G30-1, plan step 16; notes\eread\P27_gate1fix.md): SDM 093 citations name the single-volume document
# numbers (AGENT_RULES citation rule, U1915). Emulator\tools\check_sdm_citations.py refuses a Vol. 2 / Vol. 3 SET number
# (325383 / 325384-093) followed by a single-volume label in Emulator\data (*.tsv, *.txt), every C / C++ file and
# Emulator\tools (*.py, *.ps1); in-memory negative controls must fail; writes nothing.
Add-Suite 'sdm_citation_check' 'docs tools' 'py' ( Join-Path $Root 'Emulator\tools\check_sdm_citations.py' ) @( $Root )
Add-Suite 'bp2_profile_check' 'cpuid tools bp2' 'py' ( Join-Path $Root 'Emulator\tools\bare_host_cpuid.py' ) @( '--check' )
Add-Suite 'bp2_cli' 'cpuid tools bp2' 'py' ( Join-Path $Root 'Emulator\tools\bare_host_cpuid.py' ) @( '--cli', ( Join-Path $T 'emu-alltest.exe' ) )
# U1510-U1514 (agent platform, plan 1.J.49): ProfileGenerator, the platform identity profiles (Emulator\data\platform).
# pg_unit: the pg_ unit tests on synthetic fixtures (emu-profilegen-tests; never a real capture). pg_profile_check_*: each
# synthetic profile is rebuilt from its saved BuildConfig + identity map (applied, no randomness) and compared byte for byte (emu-profilegen --config ... --check;
# needs the pinned clean captures under the capture root, as bp2_profile_check; writes nothing). Every file of the profile
# is compared, so the synthetic text views views\<fwid file> (U1518) are covered as well, and an extra or missing file fails.
Add-Suite 'pg_unit' 'unit tools platform' 'exit' 'emu-profilegen-tests' @()
Add-Suite 'pg_profile_check_guest' 'tools platform' 'exit' 'emu-profilegen' @( '--config', ( Join-Path $D 'platform\vmware_guest\build_config.txt' ), '--check' )
Add-Suite 'pg_profile_check_host' 'tools platform' 'exit' 'emu-profilegen' @( '--config', ( Join-Path $D 'platform\bare_host\build_config.txt' ), '--check' )
# U1940-U1941 (agent p3m1, plan 3.M1; notes\eread\P30_phase3_m1.md): the Phase 3 Emulator projects (Emulator lib, emu-oep,
# emu-snippet, emu-selftest; solution folder "Emulator"). emu_selftest: the lib self-tests (Gate 2 = G3.A), including the runtime
# no-native case no_native_runtime. emu_oep_options / emu_snippet_options: the M1 skeleton tools parse and print their options
# and stop - INPUT / --image / --out-dir are recorded, never opened or created (the paths below do not exist).
Add-Suite 'emu_selftest' 'emulator unit' 'exit' 'emu-selftest' @()
# U1984 (plan 3.M4): emu-oep now runs its INPUT, so this options suite passes --dry-run (the M1 behaviour: nothing opened)
Add-Suite 'emu_oep_options' 'emulator' 'exit' 'emu-oep' @( '--dry-run', '--guest', '--HLE', '--HostSeed', '--max-insns', '0x100000000', '--out-dir', ( Join-Path $T 'emu_oep_out_not_created' ), ( Join-Path $T 'emu_oep_input_not_opened.exe' ) )
Add-Suite 'emu_snippet_options' 'emulator' 'exit' 'emu-snippet' @( '--hex', '48 31 c0 c3', '--stop-ret-depth', '1', '--max-insns', '16', '--expect', 'rax=0' )
# U1942-U1943 (agent p3m1, plan 3.M1): no_native_check = Emulator\tools\check_no_native.py - no native execution of guest code
# and no host-kernel pass-through (AGENT_RULES "Host safety"): S1 emulator source scan, S2 NoVmp's native executor (allow-listed
# in Emulator\data\no_native_allowlist.tsv, PENDING until M8 / 3.C), S3 project graph, S4 imports of the built emu-* executables,
# R emu-selftest no_native_runtime; in-memory negative controls; reads only.
Add-Suite 'no_native_check' 'emulator safety tools' 'py' ( Join-Path $Root 'Emulator\tools\check_no_native.py' ) @( $Root, '--build', $T )
# U2045-U2046 (agent sjaudit, notes\eread\P38_setjmp_audit.md; the bug class of U2043): setjmp_slots = Emulator\tools\
# check_setjmp_slots.py - every function of the unicorn objects that calls _setjmp_wrapper (the opaque MSVC sigsetjmp) is
# disassembled (dumpbin, read only); on the paths that run after siglongjmp no stack slot may be read at its setjmp-time
# value when the first return can have rewritten it (MSVC reuses slots of locals dead in its single-return view); the
# site set (cpu_exec, uc_invalidate_tb, setjmp_gen_code, disas_insn) is fixed; a built-in selftest of 9 synthetic listings first.
Add-Suite 'setjmp_slots' 'emulator safety tools' 'py' ( Join-Path $Root 'Emulator\tools\check_setjmp_slots.py' ) @( ( Join-Path $Root "build\x64\$Config\obj\unicorn" ) )
# U1975-U1978 (agent p3m3, plan 3.M3, notes\eread\P33_phase3_m3.md): emu-snippet v1. emu_snippet_options (above) now runs its snippet
# (xor rax, rax; ret) and checks --expect rax=0; emu_snippet_expect_bad = the negative control: a wrong --expect must be a DIFF
# (exit 1). The *-snip suites (Add-Snip) replay every expected-value file through emu-snippet. emu_snippet_hw_sample: a fixed
# sample of untagged hardware case lines (>= 200, every family of the hardware files), under the captured VMware-guest and
# bare-host profiles: emu-snippet's final state must equal emu-alltest's Unicorn engine state on every line (reference SAME).
Add-Suite 'emu_snippet_expect_bad' 'emulator snippet' 'snipcli1' 'emu-snippet' @( '--hex', '48 c7 c0 05 00 00 00', '--expect', 'rax=6', '--quiet' )
Add-Suite 'emu_snippet_hw_sample' 'emulator snippet hw' 'py' ( Join-Path $Root 'Emulator\tools\snippet_hw_sample.py' ) @( $Root, $T )
# U1980-U1989 (agent p3m4, plan 3.M4, notes\eread\P34_phase3_m4.md): win_env minimum for a PE. win_env_t1 = the test PE T1
# (Emulator\tests\pe\t1_exit, self-written guest code: one direct SYSCALL NtTerminateProcess( NtCurrentProcess(), 2AH )) with the
# service number of the VMware-guest environment's ntdll capture (decision 3.0.8; capture_sysdlls.py, local only): emu-oep --Sys
# and --HLE exit status 2AH with identical observable results (also relocated), --host refused while no bare-host capture exists,
# loud stops with state dumps, and the native reference of decision 3.0.7 (only when the live ntdll equals the capture).
# win_env_layout = Emulator\tools\check_win_layout.py: every PEB / TEB / KUSER offset, GDT selector and the KUSER address against
# the SDK / WDK headers (ksamd64.inc, ntddk.h) and the Pin kit's PEB. no_host_passthrough = Emulator\tools\check_no_passthrough.py:
# the static "no host pass-through" scan of the emulator code + the runtime proof that an emulated system call never reaches
# the host.
Add-Suite 'win_env_t1' 'emulator win_env' 'py' ( Join-Path $Root 'Emulator\tools\win_env_t1.py' ) @( $Root, $T )
Add-Suite 'win_env_layout' 'emulator win_env tools' 'py' ( Join-Path $Root 'Emulator\tools\check_win_layout.py' ) @( $Root )
Add-Suite 'no_host_passthrough' 'emulator safety tools win_env' 'py' ( Join-Path $Root 'Emulator\tools\check_no_passthrough.py' ) @( $Root, '--build', $T )
# U2016 (agent p3m5, plan 3.M5, notes\eread\P35_phase3_m5.md): the test PEs T2 (kernel32.dll imports), T3 (a TLS callback) and T4
# (direct SYSCALLs from its own code and from allocated memory, EAX bits 31:16 set) with the service numbers of the VMware-guest
# ntdll capture: T4 exits 2DH in --Sys and --HLE with identical observable results; T2 (2BH) and T3 (2EH) run under --HLE and are
# refused loudly under --Sys (the real LdrInitializeThunk start is the open item of P35); the native references of decision 3.0.7.
Add-Suite 'win_env_m5' 'emulator win_env' 'py' ( Join-Path $Root 'Emulator\tools\win_env_m5.py' ) @( $Root, $T )
# U2023 (agent p3m5b, plan 3.M5 part 2, notes\eread\P36_phase3_m5b.md): emu-oep --counters / --trace on the test PE tp_probe (a fixed
# probe sequence: CPUID 0 / 1 / 7.0 / 40000000H, RDTSC x2, RDTSCP, TEB / PEB / KUSER member reads, one direct SYSCALL): every counter
# exact in --Sys and --HLE, the trace one line per instruction, the observable lines unchanged by the instrumentation, the native
# reference 2FH (decision 3.0.7).
Add-Suite 'win_env_probe' 'emulator win_env' 'py' ( Join-Path $Root 'Emulator\tools\win_env_probe.py' ) @( $Root, $T, '--scratch', ( Join-Path $Root 'build\win_env_probe' ) )
# U2029 (agent p3m5b, plan 3.M5 part 2): the process state (system DLL images of the capture as data, PEB loader lists, process
# parameters, process heap, NtGlobalFlag) checked by the test PE tl_ldr: exit 30H natively (labelled reference) and in --HLE / --Sys;
# the capture holds the core DLLs and NLS files of capture_sysdlls.py --dll / --nls (U2024).
Add-Suite 'win_env_process' 'emulator win_env' 'py' ( Join-Path $Root 'Emulator\tools\win_env_process.py' ) @( $Root, $T )
# U2039 (agent p3m5b, plan 3.M5 part 2): the opt-in --Sys --loader start through the environment's real ntdll!LdrInitializeThunk up to
# its frontier - every service before it answered in order, a loud stop at the first service without a source (P36); T2 --Sys
# without --loader refused as in M5.
Add-Suite 'win_env_sys_loader' 'emulator win_env' 'py' ( Join-Path $Root 'Emulator\tools\win_env_sys_loader.py' ) @( $Root, $T )
# U2051 (coordinator p3sys, plan 3.M5 --Sys, notes\eread\P39_sys_loader.md): the native measurements of the undocumented services of
# the --Sys loader (Emulator\data\nt_measured_vmware_guest.txt, our own test PE tm_nt; USER DECISION 2026-10-25: phnt prototypes,
# behaviour measured natively in the VMware guest, decision 3.0.7): the file's shape and identifier screen, and - when the live ntdll is
# the guest capture - a fresh native tm_nt run (one process per case) equal to the recorded lines.
Add-Suite 'nt_measured' 'emulator win_env' 'py' ( Join-Path $Root 'Emulator\tools\nt_measured.py' ) @( $Root, $T )
# U1990-U1991 (agent p3m4, user decision 2026-10-25, notes\eread\P34_phase3_m4.md): capture_sysdlls.py labels a capture only on
# the positive evidence of U1893's environment detection (emu-alltest "tag environment:", run with an empty case file); the
# self-test: mocked / forced (--tag-env) wrong environments and unknown (--tag-profiles none) refuse without writing, the live
# detection is positive (the VMware guest here).
Add-Suite 'capture_sysdlls_selftest' 'emulator win_env tools' 'py' ( Join-Path $Root 'Emulator\tools\capture_sysdlls.py' ) @( '--selftest', '--alltest', ( Join-Path $T 'emu-alltest.exe' ) )
# U2003-U2005 (agent p3m5, plan 1.K.8 = P12, decision 3.0.10; notes\eread\P35_phase3_m5.md): CPUID self-consistency per profile,
# each against its own environment. Emulator\tools\cpuid_consistency.py applies the SDM rules (x2APIC ID vs 01H EBX[31:24],
# 0BH / 1FH structure, leaf 4 enumeration and sharing, leaf 2 descriptors vs leaves 4 / 18H, 1AH, brand string, 0DH, 80000006H vs
# leaf 4, out-of-range leaves, TSC_AUX) to the three committed profiles, to every LP of the bare-host and stock VMware-guest raw
# captures (+ the OS topology: packages, cores, caches, core types; local captures under the capture root, never committed)
# and to the EMULATED CPU of --host / --guest / --max (emu-snippet). Captured inconsistencies are recorded in
# Emulator\data\cpuid_consistency_expected.tsv and never edited; every other result must be as listed there.
Add-Suite 'cpuid_consistency' 'cpuid tools emulator' 'py' ( Join-Path $Root 'Emulator\tools\cpuid_consistency.py' ) @( '--build', $T )
Add-Suite 'cpuid_consistency_selftest' 'cpuid tools' 'py' ( Join-Path $Root 'Emulator\tools\cpuid_consistency.py' ) @( '--selftest' )

# ---------------------------------------------------------------------------------------- selection
$Groups = [ordered]@{}
foreach ( $s in $Suites ) { foreach ( $g in $s.Groups ) { if ( -not $Groups.Contains( $g ) ) { $Groups[ $g ] = New-Object System.Collections.Generic.List[string] }; $Groups[ $g ].Add( $s.Id ) } }

if ( $List )
{
	Write-Output "[test] groups (test.cmd NAME [NAME ...]; no NAME = every suite):"
	Write-Output ( '  {0,-10} {1}' -f 'all', "every suite ($($Suites.Count))" )
	foreach ( $g in $Groups.Keys ) { Write-Output ( '  {0,-10} {1}' -f $g, ( $Groups[ $g ] -join ' ' ) ) }
	Write-Output "[test] suites (id, kind, command; exit = exit status 0, hw = hardware file: differing 0, bad = 7 of 7 wrong expectations reported, herr = every line a harness error, py = python check; every --cases suite: cases = the file's case lines):"
	foreach ( $s in $Suites )
	{
		$shown = @( $s.Args | ForEach-Object { if ( $_ -eq $Root ) { '.' } elseif ( $_.StartsWith( $Root ) ) { $_.Substring( $Root.Length + 1 ) } else { $_ } } ) -join ' '   # U1950: an argument equal to the root (no_native_check) is shown as '.'
		$exe = if ( $s.Kind -eq 'py' ) { 'python -I ' + $s.Exe.Substring( $Root.Length + 1 ) } else { $s.Exe }
		Write-Output ( '  {0,-26} {1,-4} {2} {3}' -f $s.Id, ( $s.Kind + $( if ( $s.Exclusive ) { '*' } else { '' } ) ), $exe, $shown )
	}
	Write-Output '  (* = runs alone, before the parallel suites)'
	exit 0
}

$selected = New-Object System.Collections.Generic.HashSet[string]
if ( $Names.Count -eq 0 -or $Names.Contains( 'all' ) ) { foreach ( $s in $Suites ) { [void]$selected.Add( $s.Id ) } }
else
{
	foreach ( $n in $Names )
	{
		$hit = $false
		if ( $Groups.Contains( $n ) ) { foreach ( $id in $Groups[ $n ] ) { [void]$selected.Add( $id ) }; $hit = $true }
		foreach ( $s in $Suites ) { if ( $s.Id.ToLowerInvariant() -eq $n -or $s.Parent.ToLowerInvariant() -eq $n -or ( $s.PSObject.Properties[ 'SnipOf' ] -and $s.SnipOf.ToLowerInvariant() -eq $n ) ) { [void]$selected.Add( $s.Id ); $hit = $true } }   # U1978: NAME also selects NAME-snip
		if ( -not $hit ) { Write-Output "[test] unknown suite or group ""$n"" (test.cmd list)"; Write-Output $Usage; exit 2 }
	}
}
# a suite that reads another suite's log (Add-HwCmp) needs that suite in the run
foreach ( $s in $Suites.ToArray() ) { if ( $selected.Contains( $s.Id ) -and ( AfterOf $s ) ) { [void]$selected.Add( ( AfterOf $s ) ) } }
$Run = @( $Suites | Where-Object { $selected.Contains( $_.Id ) } )

if ( -not ( Test-Path -LiteralPath $T -PathType Container ) ) { Write-Output "[test] no tests at ""$T\"" - run build.cmd first"; exit 1 }
$LogDir = Join-Path $T 'logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$TimesFile = Join-Path $LogDir 'times.tsv'
$Times = @{}
if ( Test-Path -LiteralPath $TimesFile )
{
	foreach ( $l in [IO.File]::ReadAllLines( $TimesFile ) ) { $f = $l -split "`t"; if ( $f.Count -eq 2 ) { $Times[ $f[ 0 ] ] = [double]$f[ 1 ] } }
}

$Jobs = [Math]::Min( $Jobs, [Math]::Max( 1, $Run.Count ) )
Write-Output "[test] configuration : $Config | x64"
Write-Output "[test] tests         : $T\"
Write-Output "[test] logs          : $LogDir\<suite>.log"
Write-Output ( "[test] suites        : {0} of {1}{2}" -f $Run.Count, $Suites.Count, $( if ( $Names.Count ) { ' (' + ( $Names -join ' ' ) + ')' } else { ' (all)' } ) )
Write-Output "[test] parallel jobs : $Jobs (NUMBER_OF_PROCESSORS = $env:NUMBER_OF_PROCESSORS)"

# ---------------------------------------------------------------------------------------- running
function Quote( [string]$s ) { if ( $s -match '[\s&|<>^()"]' ) { return '"' + $s + '"' } return $s }

$Python = $null
$cmdPy = Get-Command python -ErrorAction SilentlyContinue
if ( $cmdPy ) { $Python = $cmdPy.Source }

function Start-Suite( $s )
{
	$s.Log = Join-Path $LogDir ( $s.Id + '.log' )
	if ( Test-Path -LiteralPath $s.Log ) { Remove-Item -LiteralPath $s.Log -Force }
	$s.Watch = [Diagnostics.Stopwatch]::StartNew()
	$s.Started = $true
	if ( $s.Kind -eq 'py' )
	{
		if ( -not $Python ) { $s.Reason = "python not found - $( Split-Path -Leaf $s.Exe ) cannot run"; return }
		$line = ( Quote $Python ) + ' -I ' + ( Quote $s.Exe ) + ' ' + ( ( $s.Args | ForEach-Object { Quote $_ } ) -join ' ' )
	}
	else
	{
		$exe = Join-Path $T ( $s.Exe + '.exe' )
		if ( -not ( Test-Path -LiteralPath $exe ) ) { $s.Reason = "missing $exe - run build.cmd first"; return }
		$line = ( Quote $exe ) + ' ' + ( ( $s.Args | ForEach-Object { Quote $_ } ) -join ' ' )
	}
	$s.Lines = @( "command: $line" )
	$psi = New-Object Diagnostics.ProcessStartInfo
	$psi.FileName = $env:ComSpec
	# cmd /s strips the outer quotes: "exe" args > "log" 2>&1 (stdout and stderr interleaved, as before)
	$psi.Arguments = '/d /s /c "' + $line + ' > ' + ( Quote $s.Log ) + ' 2>&1"'
	$psi.UseShellExecute = $false
	$psi.CreateNoWindow = $false   # same console: Ctrl+C reaches every suite
	$psi.WorkingDirectory = ( Get-Location ).Path
	$s.Proc = [Diagnostics.Process]::Start( $psi )
}

# the lines a verdict and the summary need: the whole log when small, else its first 64 KiB and
# last 4 MiB (emu-alltest prints the state first and its summary last)
function Read-LogLines( [string]$path )
{
	if ( -not ( Test-Path -LiteralPath $path ) ) { return @() }
	$len = ( Get-Item -LiteralPath $path ).Length
	if ( $len -le 8MB ) { return @( [IO.File]::ReadAllLines( $path ) ) }
	$fs = [IO.File]::Open( $path, 'Open', 'Read', 'ReadWrite' )
	try
	{
		$buf = New-Object byte[] 65536
		$n = $fs.Read( $buf, 0, $buf.Length )
		$head = [Text.Encoding]::ASCII.GetString( $buf, 0, $n ) -split "`r?`n"
		$tailLen = 4MB
		[void]$fs.Seek( $len - $tailLen, 'Begin' )
		$buf = New-Object byte[] $tailLen
		$n = $fs.Read( $buf, 0, $tailLen )
		$tail = [Text.Encoding]::ASCII.GetString( $buf, 0, $n ) -split "`r?`n"
		return @( $head[ 0..( $head.Count - 2 ) ] ) + @( '[... log middle not read ...]' ) + @( $tail[ 1..( $tail.Count - 1 ) ] )
	}
	finally { $fs.Dispose() }
}

# U1841 (plan 1.F.13.b A): the number of case lines an emu-alltest --cases suite must report in its "cases: N" line, counted
# here independently of emu-alltest with the rule of the host package's expected_counts.txt (gate1_host_package_v3a, H6):
# every line (split at LF, one trailing CR removed) that is not empty and does not start with '#' is one case line; with
# --expect-only only the case lines containing "=>"; with --shard K/N the K-th contiguous block of the case lines (lines
# T*(K-1)/N .. T*K/N - 1 of T, integer division). The bytes are read as Latin-1 (one character per byte). -1 = no case file.
# A line emu-alltest does not run (harness error, not run natively) makes its N smaller, so the suite fails.
$CaseKinds = @{}
function Get-ExpectedCases( $s )
{
	$a = @( $s.Args )
	$i = [Array]::IndexOf( $a, '--cases' )
	if ( $s.Kind -eq 'py' -or $i -lt 0 -or $i + 1 -ge $a.Count ) { return -1 }
	$file = [string]$a[ $i + 1 ]
	if ( -not $CaseKinds.ContainsKey( $file ) )
	{
		$kinds = New-Object System.Collections.Generic.List[bool]   # per case line: an expected-value line ("=>")
		$text = [Text.Encoding]::GetEncoding( 28591 ).GetString( [IO.File]::ReadAllBytes( $file ) )
		foreach ( $l in $text.Split( [char]10 ) )
		{
			if ( $l.EndsWith( [string][char]13 ) ) { $l = $l.Substring( 0, $l.Length - 1 ) }
			if ( $l.Length -eq 0 -or $l[ 0 ] -eq '#' ) { continue }
			$kinds.Add( $l.Contains( '=>' ) )
		}
		$CaseKinds[ $file ] = $kinds.ToArray()
	}
	$k = $CaseKinds[ $file ]
	[long]$lo = 0; [long]$hi = $k.Count
	$j = [Array]::IndexOf( $a, '--shard' )
	if ( $j -ge 0 -and $j + 1 -lt $a.Count -and [string]$a[ $j + 1 ] -match '^([0-9]+)/([0-9]+)$' -and [int]$Matches[ 2 ] -gt 1 )
	{
		[long]$sk = $Matches[ 1 ]; [long]$sn = $Matches[ 2 ]; [long]$t = $k.Count
		$lo = [Math]::Floor( $t * ( $sk - 1 ) / $sn ); $hi = [Math]::Floor( $t * $sk / $sn )
	}
	$expOnly = $a -contains '--expect-only'
	$c = 0
	for ( [long]$x = $lo; $x -lt $hi; $x++ ) { if ( -not $expOnly -or $k[ $x ] ) { $c++ } }
	return $c
}

function Complete-Suite( $s )
{
	$s.Watch.Stop()
	$s.Seconds = $s.Watch.Elapsed.TotalSeconds
	if ( $s.Proc ) { $s.Proc.WaitForExit(); $s.Exit = $s.Proc.ExitCode; $s.Proc.Dispose(); $s.Proc = $null }
	if ( $s.Reason ) { $s.Pass = $false; return }   # could not start
	$lines = Read-LogLines $s.Log
	if ( $s.Kind -eq 'py' ) { $s.Lines = @( $s.Lines ) + @( $lines | Select-Object -Last 20 ) }
	$s.Lines = @( $s.Lines ) + @( $lines | Where-Object {
			$_ -match '^(machine state: |cases: |expected-value cases: |case lines: |ERROR: |known deviation: |host state: |tagged but not observed|  not observed |cpuid: |SUCCESS:|FAILED:|  machine state: |  buckets:|  report: |snippet export: |snippets: |\[emu-snippet\] result: |hw sample)' -or
			$_ -match '^    [a-z].{49} +[0-9]+$' } )
	$sum = @( $lines | Where-Object { $_ -match '^cases: [0-9]+, differing: [0-9]+, known deviations: [0-9]+, host state: [0-9]+' } )
	switch ( $s.Kind )
	{
		'hw'
		{
			if ( -not ( $lines | Where-Object { $_ -match '^cases: [0-9]+, differing: 0, known deviations: [0-9]+, host state: [0-9]+' } ) )
			{ $s.Reason = "hardware and Unicorn differ, see $( $s.Log )" }
			elseif ( $s.Exit -ne 0 ) { $s.Reason = "exit status $( $s.Exit )" }
			elseif ( -not ( $lines | Where-Object { $_ -like 'cpuid: profile*' -and $_ -like '*strict on (default with a profile)*' } ) )
			{ $s.Reason = 'the CPUID profile was not strict by default (U435)' }
		}
		'hwnp'
		{
			# U547: hardware pairs run WITHOUT a CPUID profile (it would hide an opt-in feature the host
			# lacks, e.g. APX: "<legacy on the CPU> ~~ <REX2 on Unicorn>"); nothing may differ and no
			# case may need a known-deviation or host-state tag.
			if ( -not ( $lines | Where-Object { $_ -match '^cases: [0-9]+, differing: 0, known deviations: 0, host state: 0' } ) )
			{ $s.Reason = "hardware and Unicorn differ, see $( $s.Log )" }
			elseif ( $s.Exit -ne 0 ) { $s.Reason = "exit status $( $s.Exit )" }
		}
		'bad'
		{
			if ( -not ( $lines | Where-Object { $_.StartsWith( 'cases: 7, differing: 7' ) } ) ) { $s.Reason = 'wrong expectations not all detected' }
			elseif ( $s.Exit -ne 1 ) { $s.Reason = "exit status $( $s.Exit ), expected 1" }
		}
		'herr'
		{
			# U1841: harness_selftest_bad.txt - every case line is broken on purpose: each one must be a harness error
			# (none counted in "cases:", the accounting line names them all) and the exit status 1
			$want = Get-ExpectedCases $s
			$acct = @( $lines | Where-Object { $_ -match '^case lines: ([0-9]+), run: ([0-9]+), harness errors: ([0-9]+) ' } )
			if ( $acct.Count -ne 1 ) { $s.Reason = 'no "case lines:" accounting line (U1840)' }
			elseif ( -not ( $acct[ 0 ] -match "^case lines: $want, run: 0, harness errors: $want " ) ) { $s.Reason = "not every one of the $want broken case lines was reported as a harness error: $( $acct[ 0 ] )" }
			elseif ( -not ( $sum.Count -eq 1 -and $sum[ 0 ] -match '^cases: 0, ' ) ) { $s.Reason = 'a broken case line was counted in "cases:"' }
			elseif ( $s.Exit -ne 1 ) { $s.Reason = "exit status $( $s.Exit ), expected 1" }
		}
		'nonly'
		{
			# U1871 (plan 1.F.13.f): exactly one native-only line (counted, not in "cases:"), every other line run, nothing differing
			$want = Get-ExpectedCases $s
			$acct = @( $lines | Where-Object { $_ -match '^case lines: ' } )
			if ( $acct.Count -ne 1 ) { $s.Reason = 'no "case lines:" accounting line (U1840)' }
			elseif ( -not ( $acct[ 0 ] -match "^case lines: $want, run: $( $want - 1 ), native only: 1, harness errors: 0 " ) ) { $s.Reason = "the native-only line was not counted as native only: $( $acct[ 0 ] )" }
			elseif ( -not ( $lines | Where-Object { $_ -match '^native only: 1 ' } ) ) { $s.Reason = 'no "native only: 1" summary line' }
			elseif ( -not ( $sum.Count -eq 1 -and $sum[ 0 ] -match "^cases: $( $want - 1 ), differing: 0, " ) ) { $s.Reason = "the other line was not run (or differs): $( $sum -join ' / ' )" }
			elseif ( $s.Exit -ne 0 ) { $s.Reason = "exit status $( $s.Exit )" }
		}
		'tagerr'
		{
			# U1893 (plan 1.F.13.g b): the per-environment lines are harness errors (no positive evidence), the rest run as stated
			if ( -not ( $lines | Where-Object { $_ -ceq $s.Expect[ 0 ] } ) ) { $s.Reason = 'expected "' + $s.Expect[ 0 ] + '", got: ' + ( $sum -join ' / ' ) }
			elseif ( -not ( $lines | Where-Object { $_ -ceq $s.Expect[ 1 ] } ) ) { $s.Reason = 'expected "' + $s.Expect[ 1 ] + '"' }
			elseif ( -not ( $lines | Where-Object { $_ -like 'tag environment: unknown - no positive evidence*' } ) ) { $s.Reason = 'the environment was not "unknown"' }
			elseif ( $s.Exit -ne 1 ) { $s.Reason = "exit status $( $s.Exit ), expected 1" }
		}
		'tagsel'
		{
			# U1877 (plan 1.F.13.d): the selftest's verdicts are known per environment (harness_selftest_tags.txt header)
			if ( -not ( $lines | Where-Object { $_ -ceq $s.Expect[ 0 ] } ) ) { $s.Reason = 'expected "' + $s.Expect[ 0 ] + '", got: ' + ( $sum -join ' / ' ) }
			elseif ( -not ( $lines | Where-Object { $_ -ceq $s.Expect[ 1 ] } ) ) { $s.Reason = 'expected "' + $s.Expect[ 1 ] + '"' }
			elseif ( $s.Exit -ne 0 ) { $s.Reason = "exit status $( $s.Exit )" }
		}
		'snip'
		{
			# U1978: every expected-value line of the exporting suite replayed, every expectation met, no error, the reference
			# differences exactly the known host-value lines ($SnipReferenceKnown)
			$want = Get-ExpectedCases ( $Suites | Where-Object { $_.Id -eq $s.SnipOf } | Select-Object -First 1 )
			$known = if ( $SnipReferenceKnown.ContainsKey( $s.SnipOf ) ) { $SnipReferenceKnown[ $s.SnipOf ] } else { 0 }
			$sl = @( $lines | Where-Object { $_ -match '^snippets: ([0-9]+), expect checked: ([0-9]+), expect differing: ([0-9]+), reference checked: ([0-9]+), reference differing: ([0-9]+), errors: ([0-9]+)' } )
			if ( $sl.Count -ne 1 ) { $s.Reason = 'no "snippets:" summary line (emu-snippet --batch)' }
			else
			{
				[void]( $sl[ 0 ] -match '^snippets: ([0-9]+), expect checked: ([0-9]+), expect differing: ([0-9]+), reference checked: ([0-9]+), reference differing: ([0-9]+), errors: ([0-9]+)' )
				$m = $Matches
				if ( [long]$m[ 1 ] -ne $want -or [long]$m[ 2 ] -ne $want ) { $s.Reason = "$( $m[ 1 ] ) snippets / $( $m[ 2 ] ) expectations replayed, the case file has $want expected-value lines" }
				elseif ( [long]$m[ 3 ] -ne 0 ) { $s.Reason = "$( $m[ 3 ] ) expectation(s) differ in emu-snippet" }
				elseif ( [long]$m[ 6 ] -ne 0 ) { $s.Reason = "$( $m[ 6 ] ) error(s)" }
				elseif ( [long]$m[ 5 ] -ne $known ) { $s.Reason = "$( $m[ 5 ] ) line(s) differ from emu-alltest's Unicorn engine state, $known known (host values)" }
				elseif ( $s.Exit -ne $( if ( $known ) { 1 } else { 0 } ) ) { $s.Reason = "exit status $( $s.Exit )" }
			}
		}
		'snipbad'
		{
			if ( -not ( $lines | Where-Object { $_ -match '^snippets: 7, expect checked: 7, expect differing: 7, reference checked: 7, reference differing: 0, errors: 0' } ) ) { $s.Reason = 'the 7 wrong expectations were not all reported by emu-snippet' }
			elseif ( $s.Exit -ne 1 ) { $s.Reason = "exit status $( $s.Exit ), expected 1" }
		}
		'snipcli1'
		{
			if ( -not ( $lines | Where-Object { $_ -match '^\[emu-snippet\] result: DIFF' } ) ) { $s.Reason = 'the wrong --expect was not reported as DIFF' }
			elseif ( $s.Exit -ne 1 ) { $s.Reason = "exit status $( $s.Exit ), expected 1" }
		}
		default { if ( $s.Exit -ne 0 ) { $s.Reason = "exit status $( $s.Exit )" } }
	}
	# U1871 (plan 1.F.13.f): "# native only:" belongs to measurement files - a test.cmd suite compares both engines on
	# every line (except the native-only self-test)
	if ( -not $s.Reason -and $s.Kind -ne 'nonly' -and ( $lines | Where-Object { $_ -match '^native only: [0-9]+' } ) )
	{ $s.Reason = 'a native-only line in a test.cmd suite (U1871: measurement files only)' }
	# U1841 (plan 1.F.13.b A): every emu-alltest --cases suite must report exactly as many cases as its file has case lines
	# (Get-ExpectedCases, counted here): a case line emu-alltest could not parse, assemble, load or run natively is no longer
	# lost while the suite still shows "differing: 0"
	$want = Get-ExpectedCases $s
	if ( -not $s.Reason -and $want -ge 0 -and $s.Kind -ne 'herr' -and $s.Kind -ne 'nonly' -and $s.Kind -ne 'tagerr' )   # U1871: nonly counts above; U1893: tagerr too
	{
		if ( $sum.Count -ne 1 ) { $s.Reason = "$( $sum.Count ) ""cases:"" summary lines, expected exactly 1" }
		elseif ( -not ( $sum[ 0 ] -match '^cases: ([0-9]+),' ) -or [long]$Matches[ 1 ] -ne $want )
		{ $s.Reason = "$( $sum[ 0 ].Split( ',' )[ 0 ] ) but the case file has $want case lines for this run (a line was not run: harness error, see the log)" }
	}
	$s.Pass = -not $s.Reason
	# U1978: a passing replay suite removes the spec file it read (written by its exporting suite; up to ~60 MB each)
	if ( $s.Pass -and $s.Kind -in @( 'snip', 'snipbad' ) )
	{
		$jf = [string]$s.Args[ 1 ]
		if ( $jf.EndsWith( '.jsonl' ) -and ( Test-Path -LiteralPath $jf -PathType Leaf ) ) { Remove-Item -LiteralPath $jf -Force }
	}
	if ( -not $s.Pass )
	{
		# the differing cases and the end of the log
		$s.Lines = @( $s.Lines ) + @( '--- differing cases (first 40):' ) + @( $lines | Where-Object { $_ -match '^\[[0-9]+\] DIFF ' } | Select-Object -First 40 ) +
		@( '--- last 25 lines of the log:' ) + @( $lines | Select-Object -Last 25 )
	}
}

$queue = New-Object System.Collections.Generic.List[object]
# exclusive suites first (each alone), then longest first (previous run's times; a suite without a
# time first of all)
foreach ( $s in @( $Run | Sort-Object -Property @{ Expression = { -not $_.Exclusive } }, @{ Expression = { if ( $Times.ContainsKey( $_.Id ) ) { -$Times[ $_.Id ] } else { -1e9 } } }, @{ Expression = { $Suites.IndexOf( $_ ) } } ) ) { $queue.Add( $s ) }
$running = New-Object System.Collections.Generic.List[object]
$doneIds = New-Object System.Collections.Generic.HashSet[string]
$doneCount = 0
$wall = [Diagnostics.Stopwatch]::StartNew()
try
{
	while ( $queue.Count -gt 0 -or $running.Count -gt 0 )
	{
		while ( $running.Count -lt $Jobs -and $queue.Count -gt 0 -and -not @( $running | Where-Object { $_.Exclusive } ).Count )
		{
			# the first queued suite whose "after" suite has finished; an exclusive one only when idle
			$s = $null
			foreach ( $c in $queue ) { if ( -not ( AfterOf $c ) -or $doneIds.Contains( ( AfterOf $c ) ) ) { $s = $c; break } }
			if ( $null -eq $s -or ( $s.Exclusive -and $running.Count -gt 0 ) ) { break }
			[void]$queue.Remove( $s )
			Start-Suite $s
			$running.Add( $s )
		}
		$finished = @( $running | Where-Object { $null -eq $_.Proc -or $_.Proc.HasExited } )
		if ( $finished.Count -eq 0 ) { Start-Sleep -Milliseconds 50; continue }
		foreach ( $s in $finished )
		{
			[void]$running.Remove( $s )
			Complete-Suite $s
			[void]$doneIds.Add( $s.Id )
			$doneCount++
			$state = if ( $s.Pass ) { 'passed' } else { 'FAILED' }
			Write-Output ( '[test] {0,2}/{1} {2,-26} {3} ({4:0.0} s, exit {5}){6}' -f $doneCount, $Run.Count, $s.Id, $state, $s.Seconds,
				$( if ( $null -eq $s.Exit ) { '-' } else { $s.Exit } ), $( if ( $running.Count ) { '  running: ' + ( ( $running | ForEach-Object { $_.Id } ) -join ' ' ) } else { '' } ) )
		}
	}
}
finally
{
	# Ctrl+C or an error: stop the suites this run started (their process trees, by PID)
	foreach ( $s in $running ) { if ( $s.Proc -and -not $s.Proc.HasExited ) { & taskkill.exe /T /F /PID $s.Proc.Id 2>&1 | Out-Null } }
}
$wall.Stop()

# ---------------------------------------------------------------------------------------- report
foreach ( $s in $Run ) { $Times[ $s.Id ] = $s.Seconds }
try { [IO.File]::WriteAllLines( $TimesFile, [string[]]@( $Times.Keys | Sort-Object | ForEach-Object { '{0}{1}{2:0.000}' -f $_, "`t", $Times[ $_ ] } ) ) } catch { }

$failed = 0
foreach ( $s in $Run )
{
	Write-Output ''
	Write-Output "[test] ===== $( $s.Id )"
	if ( $s.Note ) { Write-Output "[test] $( $s.Note )" }
	foreach ( $l in $s.Lines ) { Write-Output $l }
	Write-Output "log: $( $s.Log )"
	if ( $Verbose -and $s.Log -and ( Test-Path -LiteralPath $s.Log ) )
	{
		Write-Output '--- log:'
		Get-Content -LiteralPath $s.Log | Write-Output
	}
	if ( $s.Pass ) { Write-Output "[test] $( $s.Id ) passed" }
	else { Write-Output "[test] $( $s.Id ) FAILED: $( $s.Reason )"; $failed++ }
}

# summary table: cases / differing / known deviations / host state from the emu-alltest summary,
# unit tests counted from the acutest lines, the quick sweep's forms from its buckets
function Get-Stats( $s )
{
	$r = [ordered]@{ cases = '-'; differing = '-'; known = '-'; host = '-'; notobs = '-' }
	if ( -not $s.Log -or -not ( Test-Path -LiteralPath $s.Log ) ) { return $r }
	$lines = $s.Lines
	$c = @( $lines | Where-Object { $_ -match '^cases: ([0-9]+), differing: ([0-9]+), known deviations: ([0-9]+), host state: ([0-9]+)' } | Select-Object -First 1 )
	if ( $c.Count )
	{
		[void]( $c[ 0 ] -match '^cases: ([0-9]+), differing: ([0-9]+), known deviations: ([0-9]+), host state: ([0-9]+)' )
		$r.cases = $Matches[ 1 ]; $r.differing = $Matches[ 2 ]; $r.known = $Matches[ 3 ]; $r.host = $Matches[ 4 ]
		$no = @( $lines | Where-Object { $_ -match '^tagged but not observed.*: ([0-9]+)$' } | Select-Object -First 1 )
		if ( $no.Count ) { [void]( $no[ 0 ] -match ': ([0-9]+)$' ); $r.notobs = $Matches[ 1 ] }
		return $r
	}
	$b = @( $lines | Where-Object { $_ -match '^    [a-z].{49} +[0-9]+$' } )
	if ( $b.Count )
	{
		$total = 0; $diff = 0; $known = 0
		foreach ( $l in $b )
		{
			[void]( $l -match '^    (.{50}) +([0-9]+)$' ); $name = $Matches[ 1 ].Trim(); $v = [int]$Matches[ 2 ]; $total += $v
			if ( $name -eq 'differs' ) { $diff = $v }
			if ( $name -like 'known deviation (*' ) { $known = $v }
		}
		$r.cases = $total; $r.differing = $diff; $r.known = $known
		return $r
	}
	# U1978: an emu-snippet replay: snippets / expect differing / reference differing (shown in the "known" column)
	$sn = @( $lines | Where-Object { $_ -match '^snippets: ([0-9]+), expect checked: [0-9]+, expect differing: ([0-9]+), reference checked: [0-9]+, reference differing: ([0-9]+)' } | Select-Object -First 1 )
	if ( $sn.Count )
	{
		[void]( $sn[ 0 ] -match '^snippets: ([0-9]+), expect checked: [0-9]+, expect differing: ([0-9]+), reference checked: [0-9]+, reference differing: ([0-9]+)' )
		$r.cases = $Matches[ 1 ]; $r.differing = $Matches[ 2 ]; $r.known = $Matches[ 3 ]
		return $r
	}
	$all = @( [IO.File]::ReadAllLines( $s.Log ) | Where-Object { $_ -match '^Test .*\[ *(OK|FAILED|SKIPPED) *\]' } )
	if ( $all.Count )
	{
		$r.cases = @( $all | Where-Object { $_ -match '\[ *OK *\]' } ).Count + @( $all | Where-Object { $_ -match '\[ *FAILED *\]' } ).Count
		$r.differing = @( $all | Where-Object { $_ -match '\[ *FAILED *\]' } ).Count
	}
	return $r
}

Write-Output ''
Write-Output '[test] summary (cases / differing / known deviations / host state / tagged-not-observed per emu-alltest summary;'
Write-Output '[test]          unit tests: acutest tests run / failed; sweep: forms run / "differs" / known-deviation bucket)'
$fmt = '[test] {0,-26} {1,-6} {2,8} {3,9} {4,6} {5,5} {6,7} {7,8}'
Write-Output ( $fmt -f 'suite', 'result', 'cases', 'differing', 'known', 'host', 'not-obs', 'time s' )
$sumTime = 0.0
foreach ( $s in $Run )
{
	$st = Get-Stats $s
	$sumTime += $s.Seconds
	Write-Output ( $fmt -f $s.Id, $( if ( $s.Pass ) { 'ok' } else { 'FAILED' } ), $st.cases, $st.differing, $st.known, $st.host, $st.notobs, ( '{0:0.0}' -f $s.Seconds ) )
	$s | Add-Member -NotePropertyName Stats -NotePropertyValue $st -Force
}
# a file split into shards / a test split into parts: one total row (counts summed, time summed)
$parents = @( $Run | Where-Object { $_.Parent } | ForEach-Object { $_.Parent } | Select-Object -Unique )
foreach ( $par in $parents )
{
	$parts = @( $Run | Where-Object { $_.Parent -eq $par } )
	$tot = [ordered]@{}
	foreach ( $f in 'cases', 'differing', 'known', 'host', 'notobs' )
	{
		$vals = @( $parts | ForEach-Object { $_.Stats[ $f ] } )
		$tot[ $f ] = if ( @( $vals | Where-Object { $_ -ne '-' } ).Count -eq $vals.Count ) { ( $vals | ForEach-Object { [long]$_ } | Measure-Object -Sum ).Sum } else { '-' }
	}
	$ok = -not @( $parts | Where-Object { -not $_.Pass } ).Count
	$t = ( $parts | ForEach-Object { $_.Seconds } | Measure-Object -Sum ).Sum
	Write-Output ( $fmt -f ( "= $par ($( $parts.Count ))" ), $( if ( $ok ) { 'ok' } else { 'FAILED' } ), $tot.cases, $tot.differing, $tot.known, $tot.host, $tot.notobs, ( '{0:0.0}' -f $t ) )
}
Write-Output ( '[test] wall clock {0:0.0} s with {1} parallel job(s); the suites alone {2:0.0} s' -f $wall.Elapsed.TotalSeconds, $Jobs, $sumTime )

Write-Output ''
if ( $failed -ne 0 )
{
	Write-Output "[test] FAILED: $failed suite(s) failed"
	exit 1
}
Write-Output '[test] OK: all suites passed'
exit 0
