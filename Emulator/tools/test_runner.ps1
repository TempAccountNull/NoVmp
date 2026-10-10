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
#       bad = expect_selftest_bad (7 of 7 reported, exit 1) | py = python check (exit status 0)
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
function Add-Exp( [string]$Id, [string]$File, [string]$Groups, [string[]]$Extra = @() )
{
	Add-Suite $Id $Groups 'exit' 'emu-alltest' ( @( '--cases', ( CaseFile $File ) ) + $Extra + @( '--expect-only' ) )
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
# plan 1.15a: expected-value ("SDM vector") cases - Unicorn only, compared to hand-derived SDM results;
# any difference or unparsable line fails. The deliberately wrong file must be caught (7 of 7, exit 1; U772: + a wrong error code).
# U539: the emulator implements the Intel SDM only (no quirk switch). Hardware files tag the cases
# where the i5-13600K deviates from the SDM ("# known deviation: NAME", docs\quirks.md).
Add-Exp 'expect_selftest' 'expect_selftest' 'selftest expect'
Add-Suite 'expect_selftest_bad' 'selftest expect' 'bad' 'emu-alltest' @( '--cases', ( CaseFile 'expect_selftest_bad' ), '--expect-only' )
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
# U445 x U147 / U236 / U373 / U404: MXCSR.UM / OM = 0 must not reach the forms that behave as if every
# MXCSR exception were masked ({er} / {sae}, VRCP14, BF16, VCVT2PS2PHX); hand-derived cases.
Add-Exp 'cases_evex_sae_unmasked' 'cases_evex_sae_unmasked' 'avx10 evex expect' @( '--avx10', '2' )
# ledger U440-U442: F16C VCVTPS2PH/VCVTPH2PS hardware cases (gen_cases_f16c.py): every rounding source
# (imm8 / MXCSR.RC), FTZ/DAZ, denormal/tiny/overflow/NaN/inf, both VL, register and memory, unmasked
# exceptions; must be 0 differing against the i5-13600K.
Add-Hw 'cases_f16c' 'hw sse'
# ledger U98/U535: DPPD with two NaN products on the host CPU (gen_cases_nan.py --dp-hw): the emulator
# implements the SDM; the cases where the i5-13600K's element order gives another NaN are tagged
# "# known deviation: DPPD two NaN products" (docs\quirks.md), all others must match.
Add-Hw 'cases_dp_nan' 'hw sse nan quirks'
# ledger U434/U539: one or more hardware cases per documented i5-13600K deviation from the SDM
# (cases_quirks.txt, docs\quirks.md): every case is tagged "# known deviation: NAME".
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
Add-Hw 'cases_tsx_cet' 'hw'
# U750-U753: CPL3 far CALL / RET far / IRETQ (heaven's gate included) with CET off on both engines,
# the transfers the CET paths of U750-U752 run through (cases_cet2_hw.txt, 0 differing).
Add-Hw 'cases_cet2_hw' 'hw cet'
Add-Hw 'cases_fixes' 'hw'
# ledger U475-U499: Tier-2/3 upstream QEMU backports and the PUSHF / LFENCE fixes, CPL3-reachable
# behaviour (cases_backport_t2.txt, self-generated snippets): must be 0 differing against the i5-13600K.
Add-Hw 'cases_backport_t2' 'hw backport x87'
# The CPL0-only parts of the same backports as expected-value cases from the SDM text (the file has
# only "=>" lines; it runs without --expect-only as before).
Add-Suite 'cases_backport_t2_sdm' 'backport expect' 'exit' 'emu-alltest' @( '--cases', ( CaseFile 'cases_backport_t2_sdm' ) )
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
# U700-U719: the state at a fault (flags, destinations, partial stores) vs the i5-13600K, plus
# expected values against the SDM where the host cannot show it.
Add-Hw 'cases_fix3' 'hw'
# U770-U789: exception error codes in expected-value cases ("#GP(0)", UC_CTL_X86_EXCEPTION), flags and
# partial stores at a fault or a memory hook, LOCK 0F 0D; hardware lines vs the i5-13600K.
Add-Hw 'cases_fix4' 'hw'
# U932: BSF/BSR with a zero source leave the whole destination unchanged (32-bit too) vs the i5-13600K.
Add-Hw 'cases_bsf_zero_hw' 'hw'
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
# U835 (decision D8): RDRAND/RDSEED source. The default seeded model (SplitMix64, seed 0; values
# from an independent SplitMix64), an explicit seed (--rdrand-seed), and the host DRNG (--HostSeed:
# flags only, the values are the host's).
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

# ---------------------------------------------------------------------------------------- selection
$Groups = [ordered]@{}
foreach ( $s in $Suites ) { foreach ( $g in $s.Groups ) { if ( -not $Groups.Contains( $g ) ) { $Groups[ $g ] = New-Object System.Collections.Generic.List[string] }; $Groups[ $g ].Add( $s.Id ) } }

if ( $List )
{
	Write-Output "[test] groups (test.cmd NAME [NAME ...]; no NAME = every suite):"
	Write-Output ( '  {0,-10} {1}' -f 'all', "every suite ($($Suites.Count))" )
	foreach ( $g in $Groups.Keys ) { Write-Output ( '  {0,-10} {1}' -f $g, ( $Groups[ $g ] -join ' ' ) ) }
	Write-Output "[test] suites (id, kind, command; exit = exit status 0, hw = hardware file: differing 0, bad = 7 of 7 wrong expectations reported, py = python check):"
	foreach ( $s in $Suites )
	{
		$shown = @( $s.Args | ForEach-Object { if ( $_.StartsWith( $Root ) ) { $_.Substring( $Root.Length + 1 ) } else { $_ } } ) -join ' '
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
		foreach ( $s in $Suites ) { if ( $s.Id.ToLowerInvariant() -eq $n -or $s.Parent.ToLowerInvariant() -eq $n ) { [void]$selected.Add( $s.Id ); $hit = $true } }
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

function Complete-Suite( $s )
{
	$s.Watch.Stop()
	$s.Seconds = $s.Watch.Elapsed.TotalSeconds
	if ( $s.Proc ) { $s.Proc.WaitForExit(); $s.Exit = $s.Proc.ExitCode; $s.Proc.Dispose(); $s.Proc = $null }
	if ( $s.Reason ) { $s.Pass = $false; return }   # could not start
	$lines = Read-LogLines $s.Log
	if ( $s.Kind -eq 'py' ) { $s.Lines = @( $s.Lines ) + @( $lines | Select-Object -Last 20 ) }
	$s.Lines = @( $s.Lines ) + @( $lines | Where-Object {
			$_ -match '^(machine state: |cases: |expected-value cases: |known deviation: |host state: |tagged but not observed|  not observed |cpuid: |SUCCESS:|FAILED:|  machine state: |  buckets:|  report: )' -or
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
		default { if ( $s.Exit -ne 0 ) { $s.Reason = "exit status $( $s.Exit )" } }
	}
	$s.Pass = -not $s.Reason
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
