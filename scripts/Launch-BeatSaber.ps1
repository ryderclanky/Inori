
$ErrorActionPreference = "Stop"
$INSTALL = "C:\Users\ryder\Desktop\KytyPS5-New\_Build\windows\install"
$LOG = Join-Path $INSTALL "logs"
$GAME = "C:\Users\ryder\Documents\ps5games\Beat Saber\PPSA15035-app"
$exe = Join-Path $INSTALL "kyty_emulator.exe"
$traceOut = Join-Path $LOG "kyty_trace.txt"
$stdoutPath = Join-Path $LOG "stdout.txt"
$stderrPath = Join-Path $LOG "stderr.txt"

New-Item -ItemType Directory -Force -Path $LOG | Out-Null
Remove-Item $stdoutPath, $stderrPath, $traceOut -Force -ErrorAction SilentlyContinue
Get-Process kyty_emulator -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

if (-not (Test-Path -LiteralPath $GAME)) { Write-Output "GAME MISSING: $GAME"; exit 2 }
if (-not (Test-Path -LiteralPath $exe)) { Write-Output "EXE MISSING: $exe"; exit 3 }

# Single Arguments string with Windows quoting; file redirects avoid pipe deadlock.
$argLine = '--game "' + $GAME + '" --trace --vr --printf-direction File --printf-output-file "' + $traceOut + '"'
$p = Start-Process -FilePath $exe -ArgumentList $argLine -WorkingDirectory $INSTALL `
  -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
$p.Id | Set-Content -Path (Join-Path $LOG "_beatsaber_emu_pid.txt")
"LAUNCHED PID=$($p.Id)"
"ARGS=$argLine"

$timeoutSec = 110
$sw = [Diagnostics.Stopwatch]::StartNew()
while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt $timeoutSec) {
  Start-Sleep -Milliseconds 1000
}
if (-not $p.HasExited) {
  "TIMEOUT after ${timeoutSec}s - killing PID=$($p.Id)"
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  try { $p.WaitForExit(15000) | Out-Null } catch {}
} else {
  "EXITED code=$($p.ExitCode) after $([int]$sw.Elapsed.TotalSeconds)s"
}

"DONE"
Get-Item $stdoutPath, $stderrPath -ErrorAction SilentlyContinue | Format-Table Name, Length, LastWriteTime -AutoSize
if (Test-Path $traceOut) { Get-Item $traceOut | Format-Table Name, Length, LastWriteTime -AutoSize } else { "NO TRACE FILE" }
"==== KEY LINES ===="
$paths = @($stdoutPath)
if (Test-Path $traceOut) { $paths += $traceOut }
Select-String -Path $paths -Pattern "Image::Resolve|clamped/adapted|Fatal Error|image\.cpp:414|--game must|Guest fault|Not implemented|Title ID|Build ---" -ErrorAction SilentlyContinue |
  Select-Object -Last 50 |
  ForEach-Object { "$($_.Filename):$($_.LineNumber):$($_.Line.Trim())" }
"==== STDOUT TAIL ===="
if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Tail 50 }
