# U1040: second, labelled cross-check of the expected-value case files under Intel SDE (user decision
# 2026-10-09). Two passes per suite (at_cases.hpp):
#   1. under SDE: sde.exe -<cpu> <knobs> -- emu-alltest --cases FILE <the suite's options> --expect-only
#      --native-under-sde RESULTS - only the native side of every expected-value line runs, on SDE's
#      emulated CPU, and its result goes to RESULTS (Unicorn's own JIT under SDE's Pin would run at about
#      one case per second);
#   2. a normal run: emu-alltest --cases FILE <the suite's options> --expect-only --sde-results RESULTS -
#      Unicorn runs as in test.cmd and is compared with RESULTS on the bytes each expectation checks.
# Results are SDE-validated, never hardware-validated; the SDM wins any disagreement. SDE is a reference
# tool only: never linked into or shipped with the emulator, and it only ever runs emu-alltest (our own
# generated snippets).
#
#   powershell -File Emulator\tools\sde_crosscheck.ps1 -Sde <kit>\sde.exe [-Out DIR] [-Only id,id]
#       [-Lock <emulator>\buildlock.ps1 [-LockName NAME]] [-Fresh]
#
# One step at a time (user request 2026-10-09, the VM is shared): each pass-1 shard and each pass-2 run
# is a separate process, started only after the previous one ended; with -Lock every step runs through
# the shared build lock. A shard whose results file is complete is not run again (-Fresh: start over);
# an incomplete one resumes after its last finished line.
#
# Notes
# - SDE 10.13.1's launcher builds the pind command line without quotes: a kit path with a space
#   ("Intel docs") fails with "Create pind process failed with code 5". The script uses the 8.3 short
#   path of sde.exe.
# - SDE still ends the process on some conditions (e.g. an AMX #UD it reports as "AMX Exception"): the
#   shard is rerun and emu-alltest resumes after that line (recorded as "SDE ended the process").
# - SDE executes natively whatever the host (i5-13600K) supports (SSE/AVX2/x87/...); only the
#   instructions the host lacks are emulated. For host-native instructions an SDE result is a host
#   result, not an independent one.
# - -chip_check_die 0: an instruction the chosen CPU lacks is reported (sde-chip-check.txt in the
#   suite's folder) instead of ending the process; -emit_illegal_insts 0: an invalid encoding raises #UD
#   in the application instead of ending the process; -align_checker_action warn: SDE's
#   alignment checker otherwise stops the process at a misaligned access of an emulated instruction
#   that needs alignment (with warn it only logs it and the access completes: SDE raises no #GP there,
#   the SDM does); -ptr_raise 1: an emulated access to unmapped memory raises an access violation in
#   the application instead of ending the process; -fp16_fast 0: SDE's exact FP16 emulation (the fast
#   one updates MXCSR less accurately).
param(
	[Parameter( Mandatory = $true )][string]$Sde,
	[string]$Out = '',
	[string]$Config = 'Release',
	[string[]]$Only = @(),
	[int]$ShardLines = 1500,
	[int]$MaxRestarts = 300,
	[string]$Lock = '',
	[string]$LockName = 'sdecheck',
	[switch]$Fresh
)
$ErrorActionPreference = 'Stop'
$Root = ( Resolve-Path ( Join-Path $PSScriptRoot '..\..' ) ).Path
$Exe = Join-Path $Root "build\x64\$Config\tests\emu-alltest.exe"
$D = Join-Path $Root 'Emulator\data'
$Cpuid = Join-Path $D 'cpuid_i5-13600k.txt'
if ( -not $Out ) { $Out = Join-Path $Root "build\x64\$Config\tests\sde" }
New-Item -ItemType Directory -Force $Out | Out-Null
if ( -not ( Test-Path $Exe ) ) { throw "emu-alltest not built: $Exe" }
$SdeShort = ( New-Object -ComObject Scripting.FileSystemObject ).GetFile( ( Resolve-Path $Sde ).Path ).ShortPath
$Knobs = @( '-chip_check_die', '0', '-chip_check_emit_file', '1', '-emit_illegal_insts', '0', '-align_checker_action', 'warn', '-ptr_raise', '1', '-fp16_fast', '0' )

# id, case file, Unicorn options (as in test_runner.ps1), SDE CPU (the newest one SDE 10.13.1 names
# that has the file's features; misc\cpuid\<cpu>\cpuid.def), extra SDE knobs
$Suites = @(
	@( 'expect_selftest', 'expect_selftest', @(), 'future', @() ),
	@( 'cases_opmask', 'cases_opmask', @( '--avx512' ), 'spr', @() ),
	@( 'cases_opmask_avx10_1', 'cases_opmask', @( '--avx10', '1' ), 'gnr', @() ),
	@( 'cases_amx', 'cases_amx', @( '--amx' ), 'gnr', @() ),   # AMX-FP16 / AMX-COMPLEX: Granite Rapids
	@( 'cases_amx2', 'cases_amx2', @( '--amx', '--avx512', '--apx' ), 'dmr', @() ),
	@( 'cases_evex_m1', 'cases_evex_m1', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m1_avx10_1', 'cases_evex_m1', @( '--avx10', '1' ), 'gnr', @() ),
	@( 'cases_nan_evex', 'cases_nan_evex', @( '--avx512' ), 'spr', @() ),
	@( 'cases_keylocker', 'cases_keylocker', @(), 'future', @( '-keylocker', '1' ) ),
	@( 'cases_sha_sm', 'cases_sha_sm', @(), 'arl', @() ),
	@( 'cases_vnni_ifma', 'cases_vnni_ifma', @(), 'future', @() ),
	@( 'cases_dp_nan_sdm', 'cases_dp_nan_sdm', @(), 'rpl', @() ),
	@( 'cases_dp_nan_sdm_sse41', 'cases_dp_nan_sdm', @(), 'rpl', @( '-sse41', '1' ) ),   # DPPS/DPPD by SDE's own SSE4.1 emulation (the host has SSE4.1)
	@( 'cases_evex_m2_gather', 'cases_evex_m2_gather', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m2_engine', 'cases_evex_m2_engine', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m2_perm', 'cases_evex_m2_perm', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m2_cvt', 'cases_evex_m2_cvt', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m3_bw', 'cases_evex_m3_bw', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m3_dq', 'cases_evex_m3_dq', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m3_cd', 'cases_evex_m3_cd', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m3_dq_post', 'cases_evex_m3_dq_post', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m4a', 'cases_evex_m4a', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_m4a_avx10_1', 'cases_evex_m4a', @( '--avx10', '1' ), 'gnr', @() ),
	@( 'cases_evex_m4b', 'cases_evex_m4b', @( '--avx512' ), 'spr', @() ),
	@( 'cases_evex_fp16', 'cases_evex_fp16', @( '--avx512' ), 'spr', @() ),
	@( 'cases_avx10_a', 'cases_avx10_a', @( '--avx10', '2' ), 'dmr', @() ),
	@( 'cases_avx10_b', 'cases_avx10_b', @( '--avx10', '2' ), 'dmr', @() ),
	@( 'cases_evex_sae_unmasked', 'cases_evex_sae_unmasked', @( '--avx10', '2' ), 'dmr', @() ),
	@( 'cases_backport_t2_sdm', 'cases_backport_t2_sdm', @(), 'rpl', @() ),
	@( 'cases_backport_t1', 'cases_backport_t1', @( '--cpuid', $Cpuid ), 'rpl', @() ),
	@( 'cases_fixes2', 'cases_fixes2', @( '--cpuid', $Cpuid ), 'rpl', @() ),
	@( 'cases_fix3', 'cases_fix3', @( '--cpuid', $Cpuid ), 'rpl', @() ),
	@( 'cases_fix4', 'cases_fix4', @( '--cpuid', $Cpuid ), 'rpl', @() ),
	@( 'cases_cet2', 'cases_cet2', @( '--apx', '--cr0', '0x10011' ), 'dmr', @() ),
	@( 'cases_sweep_sdm', 'cases_sweep_sdm', @(), 'future', @() ),
	@( 'cases_tsx_cet_max', 'cases_tsx_cet_max', @(), 'future', @() ),
	@( 'cases_apx_core', 'cases_apx_core', @( '--apx' ), 'dmr', @() ),
	@( 'cases_apx_evex', 'cases_apx_evex', @( '--apx', '--avx512' ), 'dmr', @() ),
	@( 'cases_apx_map4', 'cases_apx_map4', @( '--apx' ), 'dmr', @() ),
	@( 'cases_apx_map4_sweep', 'cases_apx_map4_sweep', @( '--apx' ), 'dmr', @() ),
	@( 'cases_apx_map4_ext', 'cases_apx_map4_ext', @( '--apx', '--avx512', '--amx' ), 'dmr', @() ),
	@( 'cases_apx_sys', 'cases_apx_sys', @( '--apx', '--amx' ), 'dmr', @() ),
	@( 'cases_rdrand', 'cases_rdrand', @(), 'rpl', @() ),
	@( 'cases_rdrand_seed', 'cases_rdrand_seed', @( '--rdrand-seed', '0x1234' ), 'rpl', @() ),
	@( 'cases_rdrand_host', 'cases_rdrand_host', @( '--HostSeed' ), 'rpl', @() ),
	@( 'cases_ac', 'cases_ac', @( '--avx512', '--apx', '--cr0', '0x40011' ), 'dmr', @() ),
	@( 'cases_sysins_dq', 'cases_sysins_dq', @( '--avx512' ), 'spr', @() ),
	@( 'cases_sysins_dq_avx10_1', 'cases_sysins_dq', @( '--avx10', '1' ), 'gnr', @() ),
	@( 'cases_sysins', 'cases_sysins', @(), 'future', @() ),
	@( 'cases_sysins_apx', 'cases_sysins_apx', @( '--apx' ), 'dmr', @() )
)
$Only = @( $Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ } )   # -File passes "a,b" as one string
if ( $Only.Count ) { $Suites = @( $Suites | Where-Object { $Only -contains $_[ 0 ] } ) }

# one step: a command line run in $Dir (through the shared lock with -Lock), stdout+stderr to $Log
function Invoke-Step( [string]$Dir, [string]$Cmd, [string]$Log )
{
	$full = "$Cmd *> '$Log'"
	if ( $Lock ) { $a = "-NoProfile -ExecutionPolicy Bypass -File `"$Lock`" -Name $LockName -Cmd `"$full`"" }
	else { $a = "-NoProfile -ExecutionPolicy Bypass -Command `"$full`"" }
	$p = Start-Process -FilePath 'powershell.exe' -ArgumentList $a -WorkingDirectory $Dir -NoNewWindow -Wait -PassThru
	return $p.ExitCode
}
function Sq( [string]$a ) { return "'" + ( $a -replace "'", "''" ) + "'" }
function Done( [string]$Log ) { return ( Test-Path $Log ) -and ( Select-String -Path $Log -Pattern '^sde native run:' -Quiet ) }

# pass 2 of one suite: Unicorn (a normal run, as in test.cmd) against its pass-1 results
function Pass2( $s )
{
	$id = $s[ 0 ]
	$res = Join-Path $Out "$id.sde.txt"
	$parts = @( Get-ChildItem $Out -Filter "$id.*of*.sde.txt" | Sort-Object { [int]( $_.Name -replace '^.*\.(\d+)of\d+\.sde\.txt$', '$1' ) } )
	if ( $parts.Count ) { Get-Content $parts.FullName | Out-File -Encoding ascii $res }
	$log = Join-Path $Out "$id.log"
	$argv = @( '--cases', ( Join-Path $D "$( $s[ 1 ] ).txt" ) ) + $s[ 2 ] + @( '--expect-only', '--sde-results', $res )
	$step = Join-Path $Out "$id.pass2.txt"
	$code = Invoke-Step $Out ( "& $( Sq $Exe ) " + ( ( $argv | ForEach-Object { Sq $_ } ) -join ' ' ) ) $step
	"[sde] $id (SDE CPU -$( $s[ 3 ] )): emu-alltest $( $argv -join ' ' )" | Out-File -Encoding utf8 $log
	Get-Content $step | Out-File -Append -Encoding utf8 $log
	"[sde] exit $code" | Out-File -Append -Encoding utf8 $log
	Remove-Item -Force $step -ErrorAction SilentlyContinue
	$sum = Select-String -Path $log -Pattern '^sde cases:' | Select-Object -Last 1
	Write-Host "[sde] $id (-$( $s[ 3 ] )): $( if ( $sum ) { $sum.Line } else { 'no summary' } )"
}

# pass 1: the suites under SDE, one shard after the other, then pass 2 of the suite; a long file runs as shards of about
# $ShardLines case lines (emu-alltest --shard K/N; the results keep the file's case-line numbers and
# are joined for pass 2)
$start = Get-Date
foreach ( $s in $Suites )
{
	$id = $s[ 0 ]
	$lines = @( Get-Content ( Join-Path $D "$( $s[ 1 ] ).txt" ) | Where-Object { $_ -and -not $_.StartsWith( '#' ) } ).Count
	$n = [Math]::Max( 1, [int][Math]::Ceiling( $lines / $ShardLines ) )
	for ( $k = 1; $k -le $n; $k++ )
	{
		$tag = if ( $n -gt 1 ) { "$id.$( $k )of$( $n )" } else { $id }
		$dir = Join-Path $Out $tag
		New-Item -ItemType Directory -Force $dir | Out-Null
		$res = Join-Path $Out "$tag.sde.txt"
		$log = Join-Path $Out "$tag.pass1.log"
		if ( $Fresh ) { Remove-Item -Force $res, $log -ErrorAction SilentlyContinue }
		$shard = if ( $n -gt 1 ) { @( '--shard', "$k/$n" ) } else { @() }
		$argv = @( "-$( $s[ 3 ] )" ) + $Knobs + $s[ 4 ] + @( '--', $Exe, '--cases', ( Join-Path $D "$( $s[ 1 ] ).txt" ) ) + $s[ 2 ] + $shard + @( '--expect-only', '--native-under-sde', $res )
		$cmd = "& $( Sq $SdeShort ) " + ( ( $argv | ForEach-Object { Sq $_ } ) -join ' ' )
		for ( $try = 0; -not ( Done $log ) -and $try -le $MaxRestarts; $try++ )
		{
			$t0 = Get-Date
			$step = Join-Path $dir "step$try.txt"
			$code = Invoke-Step $dir $cmd $step
			"[sde] $tag run $try : $cmd" | Out-File -Append -Encoding utf8 $log
			Get-Content $step | Out-File -Append -Encoding utf8 $log
			"[sde] exit $code after $( [int]( ( Get-Date ) - $t0 ).TotalSeconds ) s" | Out-File -Append -Encoding utf8 $log
			Remove-Item -Force $step -ErrorAction SilentlyContinue
			# no summary: SDE ended the process at one case line; the next run resumes after it
			$sum = Select-String -Path $log -Pattern '^sde native run:' | Select-Object -Last 1
			Write-Host "[sde] pass 1 $tag (-$( $s[ 3 ] )) run $try exit $( $code ): $( if ( $sum ) { $sum.Line } else { 'ended early, resuming' } )"
		}
	}
	Pass2 $s
}
Write-Host "[sde] all done in $( [int]( ( Get-Date ) - $start ).TotalSeconds ) s; logs in $Out"
