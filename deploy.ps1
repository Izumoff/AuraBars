# Deploys a locally built AuraBars.dll into AIMP's Plugins folder for
# manual testing. Called by deploy.bat - not meant to be run standalone
# with a double-click, since PowerShell's default execution policy would
# block it; deploy.bat passes -ExecutionPolicy Bypass explicitly.
#
# Requires an elevated (Run as Administrator) session: C:\Program
# Files\AIMP\Plugins is not writable otherwise. Checked explicitly below,
# and the actual copy is also guarded, so a lack of elevation is reported
# as a clear failure rather than a silent no-op or a false "success".

param(
    [ValidateSet('x86', 'x64')]
    [string]$Arch = 'x64'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root "build\$Arch\AuraBars.dll"
$pluginDir = 'C:\Program Files\AIMP\Plugins\AuraBars'
$dst = Join-Path $pluginDir 'AuraBars.dll'
$aimpExe = 'C:\Program Files\AIMP\AIMP.exe'

function Fail($message) {
    Write-Host ''
    Write-Host "*** DEPLOY FAILED ***" -ForegroundColor Red
    Write-Host $message -ForegroundColor Red
    exit 1
}

Write-Host "=== AuraBars deploy ($Arch) ==="

if (-not (Test-Path $src)) {
    Fail "$src not found - build it first: build.bat $Arch"
}

# Fail fast, before touching AIMP or the Plugins folder at all, if this
# session isn't elevated - the copy below would fail anyway, but there's
# no reason to close the user's running player first only to then be
# unable to finish the deploy.
$isElevated = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole( `
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isElevated) {
    Fail "Not running elevated. $pluginDir requires administrator rights.`nRight-click deploy.bat and choose 'Run as administrator', or run from an elevated prompt."
}

# --- Close AIMP if running: graceful WM_CLOSE first, wait, then force as a fallback. ---
$proc = Get-Process -Name 'AIMP' -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "AIMP is running (PID $($proc.Id)) - closing gracefully..."
    $closeRequested = $proc.CloseMainWindow()
    $exited = $false
    if ($closeRequested) {
        $exited = $proc.WaitForExit(10000)
    }
    if (-not $exited) {
        $proc.Refresh()
        if (-not $proc.HasExited) {
            Write-Host 'AIMP did not close gracefully within 10s - forcing termination...'
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            Start-Sleep -Milliseconds 500
        }
    }
    Write-Host 'AIMP closed.'
} else {
    Write-Host 'AIMP is not running.'
}

# --- Report source, copy, report destination - a stale DLL must be obvious, not inferred. ---
$srcInfo = Get-Item $src
Write-Host ''
Write-Host 'Source:'
Write-Host "  Path:      $src"
Write-Host "  Modified:  $($srcInfo.LastWriteTime)"
Write-Host "  Size:      $($srcInfo.Length) bytes"

if (-not (Test-Path $pluginDir)) {
    New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
}

try {
    Copy-Item -Path $src -Destination $dst -Force -ErrorAction Stop
} catch [System.UnauthorizedAccessException] {
    Fail "Access denied copying to $dst.`nThis session passed the elevation check above but the copy was still denied - the file may still be held open by AIMP (it may not have fully closed), or by another process."
} catch {
    Fail "Copy failed: $($_.Exception.Message)"
}

$dstInfo = Get-Item $dst
Write-Host ''
Write-Host 'Destination:'
Write-Host "  Path:      $dst"
Write-Host "  Modified:  $($dstInfo.LastWriteTime)"
Write-Host "  Size:      $($dstInfo.Length) bytes"

if ($dstInfo.Length -ne $srcInfo.Length) {
    Fail "Destination size ($($dstInfo.Length) bytes) does not match source ($($srcInfo.Length) bytes) after copy."
}

# --- Relaunch AIMP ---
Write-Host ''
if (Test-Path $aimpExe) {
    Write-Host 'Relaunching AIMP...'
    Start-Process -FilePath $aimpExe
} else {
    Write-Host "WARNING: $aimpExe not found - could not relaunch AIMP automatically." -ForegroundColor Yellow
}

Write-Host ''
Write-Host '=== Deploy complete ==='
