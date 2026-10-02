<#
.SYNOPSIS
  Launch KytyPS5 with File logging and summarize top blockers (stubs / not-implemented / opcodes).

.DESCRIPTION
  Wires existing emulator CLI (--printf-direction File, --printf-output-file, --trace)
  so Stubbed / Unresolved import stub / Not implemented / opcode-not-implemented lines
  land in a fixed log folder, then prints a short summary.

.PARAMETER GamePath
  Game directory, ELF, or .zar. Also accepted via env GAME_PATH.

.PARAMETER InstallDir
  Folder containing kyty_emulator.exe. Default: <repo>\_Build\windows\install

.PARAMETER LogDir
  Where logs land. Default: <InstallDir>\logs

.PARAMETER TimeoutSec
  Optional; kill the emulator after N seconds (0 = wait until exit).

.PARAMETER ExtraArgs
  Extra args passed through to kyty_emulator.exe.

.PARAMETER SummarizeOnly
  Skip launch; only summarize an existing kyty log (use -KytyLog).

.EXAMPLE
  $env:GAME_PATH = 'D:\gamesps5\PPSA07429'   # Stray dump title id (set yours)
  .\scripts\Trace-Game.ps1

.EXAMPLE
  .\scripts\Trace-Game.ps1 -GamePath 'D:\gamesps5\PPSA07429' -TimeoutSec 120
#>
[CmdletBinding()]
param(
    [string] $GamePath = $env:GAME_PATH,
    [string] $InstallDir = "",
    [string] $LogDir = "",
    [string] $KytyLog = "",
    [string] $EmulatorExe = "",
    [int] $TimeoutSec = 0,
    [string[]] $ExtraArgs = @(),
    [switch] $PlayGoHack,
    [switch] $SummarizeOnly,
    [switch] $NoTraceFlag
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $InstallDir) {
    $InstallDir = Join-Path $RepoRoot "_Build\windows\install"
}
if (-not $LogDir) {
    $LogDir = Join-Path $InstallDir "logs"
}
if (-not $EmulatorExe) {
    $EmulatorExe = Join-Path $InstallDir "kyty_emulator.exe"
}
if (-not $KytyLog) {
    $KytyLog = Join-Path $LogDir "kyty_trace.txt"
}

$ConsoleLog = Join-Path $LogDir "console.txt"
$SummaryLog = Join-Path $LogDir "blockers_summary.txt"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"

function Write-KytyBlockerSummary {
    param(
        [Parameter(Mandatory)] [string[]] $Sources,
        [Parameter(Mandatory)] [string] $OutFile
    )

    $lines = @()
    foreach ($src in $Sources) {
        if (Test-Path -LiteralPath $src) {
            try {
                $lines += Get-Content -LiteralPath $src -ErrorAction Stop
            } catch {
                Write-Warning "Could not read ${src}: $_"
            }
        }
    }

    $patterns = [ordered]@{
        "Not implemented / EXIT_NOT_IMPLEMENTED" = '(?i)Not implemented\s*\(|EXIT_NOT_IMPLEMENTED|--- Fatal Error ---'
        "Stubbed imports"                        = '(?i)^Stubbed:'
        "Unresolved import stub calls"           = '(?i)Unresolved import stub called'
        "Opcode / shader not implemented"        = '(?i)opcode is not implemented|is not implemented'
        "Other fatal / EXIT"                     = '(?i)^--- (Fatal )?Error ---|^--- Error ---'
    }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("KytyPS5 trace blocker summary")
    [void]$sb.AppendLine("Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') America/Toronto")
    [void]$sb.AppendLine("Sources: $($Sources -join '; ')")
    [void]$sb.AppendLine("Total lines scanned: $($lines.Count)")
    [void]$sb.AppendLine("")

    $any = $false
    foreach ($name in $patterns.Keys) {
        $rx = $patterns[$name]
        $hits = $lines | Where-Object { $_ -match $rx }
        $count = @($hits).Count
        [void]$sb.AppendLine("=== $name ($count) ===")
        if ($count -eq 0) {
            [void]$sb.AppendLine("(none)")
        } else {
            $any = $true
            # Unique-ish top blockers (first 25 unique)
            $unique = $hits | ForEach-Object { $_.Trim() } | Select-Object -Unique -First 25
            foreach ($u in $unique) {
                [void]$sb.AppendLine($u)
            }
            if ($count -gt 25) {
                [void]$sb.AppendLine("... ($($count - 25) more matching lines omitted)")
            }
        }
        [void]$sb.AppendLine("")
    }

    # Top Stubbed symbols (extract after "Stubbed:")
    $stubHits = $lines | Where-Object { $_ -match '(?i)^Stubbed:' }
    if (@($stubHits).Count -gt 0) {
        [void]$sb.AppendLine("=== Top Stubbed lines (first 40 unique) ===")
        $stubHits | ForEach-Object { $_.Trim() } | Select-Object -Unique -First 40 | ForEach-Object {
            [void]$sb.AppendLine($_)
        }
        [void]$sb.AppendLine("")
    }

    $text = $sb.ToString()
    $dir = Split-Path -Parent $OutFile
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    Set-Content -LiteralPath $OutFile -Value $text -Encoding utf8
    Write-Host $text
    return [pscustomobject]@{ AnyBlockers = $any; SummaryPath = $OutFile }
}

if ($SummarizeOnly) {
    if (-not (Test-Path -LiteralPath $KytyLog) -and -not (Test-Path -LiteralPath $ConsoleLog)) {
        throw "SummarizeOnly: neither KytyLog nor console log found. KytyLog=$KytyLog"
    }
    $result = Write-KytyBlockerSummary -Sources @($KytyLog, $ConsoleLog) -OutFile $SummaryLog
    if ($result.AnyBlockers) { exit 2 } else { exit 0 }
}

if ([string]::IsNullOrWhiteSpace($GamePath)) {
    throw @"
GAME_PATH is not set. Pass -GamePath or set `$env:GAME_PATH to your Stray (or other) dump.

Example (title id from .vscode/launch.json hint):
  `$env:GAME_PATH = 'D:\gamesps5\PPSA07429'
  .\scripts\Trace-Game.ps1

Or:
  .\scripts\Trace-Game.ps1 -GamePath 'C:\path\to\Stray'
"@
}

if (-not (Test-Path -LiteralPath $GamePath)) {
    throw "GamePath does not exist: $GamePath"
}
if (-not (Test-Path -LiteralPath $EmulatorExe)) {
    throw "Emulator not found: $EmulatorExe (build/install Release first)"
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

# Rotate previous logs with a stamp copy if present
foreach ($f in @($KytyLog, $ConsoleLog, $SummaryLog)) {
    if (Test-Path -LiteralPath $f) {
        $bak = "$f.$Stamp.bak"
        Move-Item -LiteralPath $f -Destination $bak -Force
    }
}

$emuArgs = @(
    "--game", $GamePath,
    "--printf-direction", "File",
    "--printf-output-file", $KytyLog
)
if (-not $NoTraceFlag) {
    # Harmless if older binary lacks --trace; detect via help first
    $help = & $EmulatorExe --help 2>&1 | Out-String
    if ($help -match '--trace') {
        $emuArgs += "--trace"
    }
}
if ($PlayGoHack) {
    $emuArgs += "--playgo-hack"
}
if ($ExtraArgs) {
    $emuArgs += $ExtraArgs
}

Write-Host "InstallDir : $InstallDir"
Write-Host "Emulator   : $EmulatorExe"
Write-Host "GamePath   : $GamePath"
Write-Host "Kyty log   : $KytyLog"
Write-Host "Console log: $ConsoleLog"
Write-Host "Summary    : $SummaryLog"
Write-Host "Args       : $($emuArgs -join ' ')"
Write-Host ""

$env:KYTY_TRACE = "1"
$env:KYTY_PRINTF_DIRECTION = "File"
$env:KYTY_PRINTF_OUTPUT_FILE = $KytyLog

$stdoutFile = Join-Path $LogDir "stdout.txt"
$stderrFile = Join-Path $LogDir "stderr.txt"

# Start-Process -ArgumentList with a string[] quietly splits on spaces inside values
# (breaks GAME_PATH like "...\Beat Saber\..."). Quote each arg, then pass one string.
function Format-ProcessArgumentList([string[]] $ArgsIn) {
    ($ArgsIn | ForEach-Object {
        $a = [string]$_
        if ($null -eq $a) { $a = "" }
        if ($a -match '[\s"]') {
            '"' + ($a -replace '"', '"') + '"'
        } else {
            $a
        }
    }) -join ' '
}
$argListString = Format-ProcessArgumentList $emuArgs

$proc = Start-Process -FilePath $EmulatorExe `
    -ArgumentList $argListString `
    -WorkingDirectory $InstallDir `
    -RedirectStandardOutput $stdoutFile `
    -RedirectStandardError $stderrFile `
    -PassThru `
    -NoNewWindow

$exitCode = $null
try {
    if ($TimeoutSec -gt 0) {
        $ok = $proc.WaitForExit($TimeoutSec * 1000)
        if (-not $ok) {
            Write-Warning "Timeout ${TimeoutSec}s reached; stopping pid $($proc.Id)"
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            Start-Sleep -Milliseconds 500
            $exitCode = 124
        } else {
            $exitCode = $proc.ExitCode
        }
    } else {
        $proc.WaitForExit()
        $exitCode = $proc.ExitCode
    }
} finally {
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}

# Merge stdout/stderr into console.txt for one place to grep
$merged = New-Object System.Text.StringBuilder
[void]$merged.AppendLine("=== kyty_emulator exit code: $exitCode ===")
foreach ($pair in @(@("STDOUT", $stdoutFile), @("STDERR", $stderrFile))) {
    [void]$merged.AppendLine("=== $($pair[0]) ===")
    if (Test-Path -LiteralPath $pair[1]) {
        [void]$merged.AppendLine((Get-Content -LiteralPath $pair[1] -Raw -ErrorAction SilentlyContinue))
    }
}
Set-Content -LiteralPath $ConsoleLog -Value $merged.ToString() -Encoding utf8

Write-Host ""
Write-Host "Emulator exited with code $exitCode (321 often means EXIT_HALT / not-implemented)."
$result = Write-KytyBlockerSummary -Sources @($KytyLog, $ConsoleLog, $stdoutFile, $stderrFile) -OutFile $SummaryLog

Write-Host ""
Write-Host "Logs:"
Write-Host "  kyty     : $KytyLog"
Write-Host "  console  : $ConsoleLog"
Write-Host "  summary  : $SummaryLog"
Write-Host "  stdout   : $stdoutFile"
Write-Host "  stderr   : $stderrFile"

if ($null -eq $exitCode) { $exitCode = 1 }
if ($result.AnyBlockers -and $exitCode -eq 0) { exit 2 }
exit $exitCode