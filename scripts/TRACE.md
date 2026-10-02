# KytyPS5 trace harness (Stray / blocker logs)

Practical automation to raise emulator logging and summarize why a title (e.g. Stray) stalls.

This is **not** an AI inside the emulator. It wires existing logging and greps for blockers.

## Existing logging (discovery)

| Mechanism | Details |
|-----------|---------|
| Default printf log file | `_kyty.txt` (relative to process CWD) via `Config::printf_output_file` |
| Default direction | **Silent** — stubs / LOGF are dropped unless raised |
| CLI | `--printf-direction Silent\|Console\|File` |
| CLI | `--printf-output-file <path>` |
| CLI (new) | `--trace` — forces File direction |
| Env (new) | `KYTY_TRACE=1`, `KYTY_PRINTF_DIRECTION`, `KYTY_PRINTF_OUTPUT_FILE` (applied before CLI; CLI wins) |
| Stub lines | `Stubbed: ...` and `Unresolved import stub called ...` (`runtimeLinker.cpp`) |
| Fatal not-impl | `Not implemented (...)` + `--- Fatal Error ---` (`EXIT_NOT_IMPLEMENTED` → exit **321**) |
| Shader opcodes | `* opcode is not implemented` (reason stored on instruction; often surfaces when shaders fail) |

Built binaries live under `_Build\windows\install\` (`kyty_emulator.exe`, `launcher.exe`).

## Run Stray with the harness

1. Set your dump path (parameterized — **Ryder must set this** if unknown):

```powershell
# Hint from .vscode/launch.json (confirm on disk):
$env:GAME_PATH = 'D:\gamesps5\PPSA07429'
```

2. From the repo root (after a Release install exists):

```powershell
cd C:\Users\ryder\Desktop\KytyPS5-New
.\scripts\Trace-Game.ps1
# or with explicit path / timeout:
.\scripts\Trace-Game.ps1 -GamePath 'D:\gamesps5\PPSA07429' -TimeoutSec 180
# PlayGo stub fallback if needed:
.\scripts\Trace-Game.ps1 -GamePath $env:GAME_PATH -PlayGoHack
```

Or:

```bat
set GAME_PATH=D:\gamesps5\PPSA07429
scripts\Trace-Game.bat
```

3. Close the emu window (or wait for crash / timeout). The script prints a summary and exits.

### Manual equivalent (no script)

```powershell
cd C:\Users\ryder\Desktop\KytyPS5-New\_Build\windows\install
mkdir logs -Force
.\kyty_emulator.exe `
  --game $env:GAME_PATH `
  --trace `
  --printf-direction File `
  --printf-output-file "$PWD\logs\kyty_trace.txt"
```

## Where logs land

Under `_Build\windows\install\logs\` by default:

| File | Contents |
|------|----------|
| `kyty_trace.txt` | Emulator File printf log (stubs, reloc, fatals) |
| `console.txt` | Merged stdout/stderr + exit code |
| `stdout.txt` / `stderr.txt` | Raw redirects |
| `blockers_summary.txt` | Top `Not implemented` / `Stubbed` / unresolved stubs / opcode lines |

Re-summarize without relaunching:

```powershell
.\scripts\Trace-Game.ps1 -SummarizeOnly
```

## Rebuild (if you pulled the `--trace` / env change)

From a VS / clang-cl developer environment used for this tree:

```powershell
cmake --build C:\Users\ryder\Desktop\KytyPS5-New\_Build\windows --target install --parallel
```

Binaries stay in `_Build\windows\install\`.