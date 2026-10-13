# docroot.ps1 (U1411): PowerShell twin of docroot.py - where the tools find the external reference sources.
# Dot-source it:  . ( Join-Path $PSScriptRoot 'docroot.ps1' )
#
# Sources root: $env:NOVMP_SOURCES_ROOT if set, otherwise E:\sources. Expected below it:
#   sde-external-10.13.1-2026-07-28-win\sde.exe   Intel SDE 10.13.1 (labelled reference only)
#   xed-main\datafiles\...                        Intel XED datafiles
#   pin-sources\...                               Pin kits
# A missing path throws with the path, what was expected and the variable to set; no fallback.

$NovmpSourcesEnv = 'NOVMP_SOURCES_ROOT'
$NovmpSourcesDefault = 'E:\sources'
$NovmpSdeKit = 'sde-external-10.13.1-2026-07-28-win'

function Get-NovmpSourcesRoot
{
	$r = [Environment]::GetEnvironmentVariable( $NovmpSourcesEnv )
	if ( $r -and $r.Trim() ) { return $r.Trim().TrimEnd( '\' ) }
	return $NovmpSourcesDefault
}

function Get-NovmpSourcePath( [string]$Rel, [string]$What, [switch]$File )
{
	$root = Get-NovmpSourcesRoot
	$p = Join-Path $root $Rel
	$type = if ( $File ) { 'Leaf' } else { 'Container' }
	if ( -not ( Test-Path -LiteralPath $p -PathType $type ) )
	{
		$r = [Environment]::GetEnvironmentVariable( $NovmpSourcesEnv )
		$src = if ( $r -and $r.Trim() ) { "$NovmpSourcesEnv=$r" } else { "$NovmpSourcesEnv not set, default $NovmpSourcesDefault" }
		throw ( "[docroot] ERROR: missing $p`n" +
			"[docroot]   expected: $What`n" +
			"[docroot]   sources root: $root ($src)`n" +
			"[docroot]   fix: set $NovmpSourcesEnv to the folder that holds xed-main\, $NovmpSdeKit\, pin-sources\ ...`n" +
			"[docroot]        or pass the tool's own path option where it has one" )
	}
	return $p
}

function Get-NovmpSdeExe { return Get-NovmpSourcePath ( Join-Path $NovmpSdeKit 'sde.exe' ) "Intel SDE 10.13.1 launcher ($NovmpSdeKit\sde.exe)" -File }
function Get-NovmpXedDatafiles { return Get-NovmpSourcePath 'xed-main\datafiles' 'Intel XED datafiles folder (xed-main\datafiles)' }
function Get-NovmpPinSources { return Get-NovmpSourcePath 'pin-sources' 'the Pin kits folder (pin-sources\pin-external-*)' }

# SDE 10.13.1's launcher starts pind.exe with an unquoted path: a kit path with a space fails with
# "Create pind process failed with code 5". A path without a space is returned unchanged; one with a
# space becomes its 8.3 short path; if that still has a space (8.3 names disabled on the volume) it throws.
function Get-NovmpSdeLaunchPath( [string]$Path )
{
	$full = ( Resolve-Path -LiteralPath $Path ).Path
	if ( $full -notmatch ' ' ) { return $full }
	$short = ( New-Object -ComObject Scripting.FileSystemObject ).GetFile( $full ).ShortPath
	if ( $short -match ' ' )
	{
		throw ( "[docroot] ERROR: the SDE path has a space and no 8.3 short name: $full`n" +
			"[docroot]   SDE 10.13.1 cannot start pind.exe from such a path (`"Create pind process failed with code 5`").`n" +
			"[docroot]   fix: set $NovmpSourcesEnv (or -Sde) to a folder without spaces" )
	}
	return $short
}
