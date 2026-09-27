# IDM Popup Guard

A lightweight, native Windows background utility that automatically closes unwanted **Internet Download Manager (IDM)** popup windows (trial reminders, registration prompts, update notifications) while keeping useful windows open (download dialogs, progress windows).

## How It Works

```
Windows fires event → Popup Guard wakes → Is it IDMan.exe? → Is the title protected?
                                                │                      │
                                              NO → ignore          YES → keep
                                                                   NO  → send WM_CLOSE
```

IDM Popup Guard is **event-driven**, not a polling loop. It uses the Windows `SetWinEventHook` API to receive notifications only when windows appear or change titles. When no windows are appearing, CPU usage is effectively **zero**.

### Decision Flow

For every window event the OS delivers:

1. **Filter**: Only process top-level window events (`OBJID_WINDOW`, `CHILDID_SELF`, no `WS_CHILD`).
2. **Identify**: Get the owning process's executable via `GetWindowThreadProcessId` → `OpenProcess` → `QueryFullProcessImageNameW`.
3. **Quick reject**: If the executable is not in `config.json` (e.g., not `IDMan.exe`), ignore immediately.
4. **Title check**: Get the window title. If empty, ignore (fail-safe).
5. **Rule evaluation**: Check the title against configured exact titles and regex patterns.
   - **Match** → keep the window open.
   - **No match** → send `WM_CLOSE` (polite close request, never kills the process).

---

## Architecture

```
IDM_Popup_Guard/
│
├── src/
│   ├── core/                          # Platform-independent
│   │   ├── Config.h / Config.cpp      # JSON config loader
│   │   └── RuleEngine.h / .cpp        # Title matching engine
│   │
│   └── platform/windows/             # Windows-specific
│       ├── ProcessDetector.h / .cpp   # HWND → executable name
│       ├── WindowMonitor.h / .cpp     # SetWinEventHook + message loop
│       └── main.cpp                   # WinMain entry point
│
├── tools/
│   └── WindowInspector.cpp            # Dev tool to see window info
│
├── config/
│   └── config.json                    # Protected window rules
│
├── CMakeLists.txt
└── README.md
```

### Module Responsibilities

| Module | Platform | Purpose |
|--------|----------|---------|
| **Config** | Independent | Parse `config.json` into `AppRule` structs |
| **RuleEngine** | Independent | Compile regex patterns, evaluate titles against rules |
| **ProcessDetector** | Windows | Map `HWND` → process ID → executable filename |
| **WindowMonitor** | Windows | `SetWinEventHook` + `GetMessage` loop |
| **main** | Windows | Single-instance check, startup, error dialog |

The `core/` modules have no Windows dependencies and can be reused if the project is expanded to other platforms.

---

## Windows APIs Used

| API | Purpose |
|-----|---------|
| `SetWinEventHook` | Register callbacks for `EVENT_OBJECT_SHOW` and `EVENT_OBJECT_NAMECHANGE` without polling |
| `GetMessage` / `DispatchMessage` | Standard message loop; **blocks** when idle → zero CPU |
| `GetWindowThreadProcessId` | Get the PID owning a given HWND |
| `OpenProcess` | Open a process handle with `PROCESS_QUERY_LIMITED_INFORMATION` |
| `QueryFullProcessImageNameW` | Get the full executable path from a process handle |
| `GetWindowLongW` | Check window styles (filter out child windows) |
| `GetWindowTextW` | Retrieve the window title |
| `PostMessageW(WM_CLOSE)` | Send a non-blocking close request to a window |
| `CreateMutexW` | Single-instance guard |
| `MessageBoxW` | Fatal error dialog |
| `GetModuleFileNameW` | Locate the executable directory for `config.json` |

### Event Hook Flags

- `WINEVENT_OUTOFCONTEXT` — No DLL injection. The callback runs in our process.
- `WINEVENT_SKIPOWNPROCESS` — Ignore events from our own windows (e.g., the error dialog).

---

## Building

### Prerequisites

- **CMake** 3.14+
- **C++17 compiler**: MSVC (Visual Studio 2019+), MinGW-w64, or Clang
- **Internet access** during first build (CMake FetchContent downloads [nlohmann/json](https://github.com/nlohmann/json))

### Build with Visual Studio (MSVC)

```powershell
# From the project root
cmake -S . -B build
cmake --build build --config Release
```

Output:
```
build/Release/IDMPopupGuard.exe
build/Release/config.json
build/Release/WindowInspector.exe
```

### Build with MinGW

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Release Build (Optimized)

For MSVC, use `--config Release` (the default `CMAKE_CXX_FLAGS_RELEASE` includes `/O2`).
For MinGW/GCC, use `-DCMAKE_BUILD_TYPE=Release` (includes `-O2 -DNDEBUG`).

---

## Configuration

`config.json` must be placed **next to** `IDMPopupGuard.exe`. The build system copies it automatically.

```json
{
    "applications": [
        {
            "executable": "IDMan.exe",
            "protectedWindows": {
                "exactTitles": [
                    "Download File Info",
                    "Download complete",
                    "Duplicate download link",
                    "Download later"
                ],
                "patterns": [
                    "^[0-9]{1,3}% .+$"
                ]
            }
        }
    ]
}
```

### Adding a New Protected Title

Edit `config.json` and add the title to `exactTitles`:

```json
"exactTitles": [
    "Download File Info",
    "Download complete",
    "Duplicate download link",
    "Download later",
    "Internet Download Manager"
]
```

Then **restart** IDMPopupGuard. No recompilation required.

### Title Matching Rules

- **Exact titles**: Case-sensitive comparison. `"Download File Info"` matches only `Download File Info`, not `download file info`.
- **Patterns**: ECMAScript regex applied with `std::regex_match` (must match the **entire** title). The default pattern `^[0-9]{1,3}% .+$` matches IDM progress windows like `52% ubuntu.iso`.

### Fail-Safe

If `config.json` is missing, malformed, or contains an invalid regex pattern, IDM Popup Guard will **refuse to close any windows** and show the error dialog. It will never default to "close everything."

---

## Auto-Start at Login

### Option 1: Registry (HKCU Run) — Recommended

No admin privileges required. Run in PowerShell:

```powershell
# Enable auto-start
$exePath = "C:\path\to\IDMPopupGuard.exe"
New-ItemProperty -Path "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" `
    -Name "IDMPopupGuard" -Value "`"$exePath`"" -PropertyType String -Force

# Disable auto-start
Remove-ItemProperty -Path "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" `
    -Name "IDMPopupGuard" -Force
```

### Option 2: Startup Folder

1. Press `Win+R`, type `shell:startup`, press Enter.
2. Create a shortcut to `IDMPopupGuard.exe` in this folder.
3. To disable, delete the shortcut.

### Option 3: Task Scheduler

```powershell
# Enable
$action  = New-ScheduledTaskAction -Execute "C:\path\to\IDMPopupGuard.exe"
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
Register-ScheduledTask -TaskName "IDMPopupGuard" -Action $action -Trigger $trigger `
    -Description "IDM Popup Guard" -RunLevel Limited

# Disable
Unregister-ScheduledTask -TaskName "IDMPopupGuard" -Confirm:$false
```

---

## Window Inspector (Dev Tool)

Use the Window Inspector to discover the exact titles IDM uses:

```powershell
.\WindowInspector.exe
```

Then trigger IDM windows (start a download, let a popup appear, etc.). The inspector prints:

```
--------------------------------------------------
  HWND:       0x00040A2E
  PID:        12345
  Executable: C:\Program Files\Internet Download Manager\IDMan.exe
  Title:      "Registration"
  Class:      "#32770"
```

Add any titles you want to **protect** to `config.json`. Any IDM title NOT in the config will be closed by the guard.

Press `Ctrl+C` to stop the inspector.

---

## Behavior Summary

| Scenario | Behavior |
|----------|----------|
| IDM not running | Idle, ~0% CPU, minimal RAM |
| IDM starts after PopupGuard | Monitoring begins automatically (global hook) |
| Protected IDM window appears | Kept open |
| Unprotected IDM window appears | Closed via `WM_CLOSE` |
| Non-IDM window (Chrome, Explorer, etc.) | Never touched |
| Window disappears before inspection | Silently ignored |
| `config.json` missing/broken | Error dialog, no windows closed |
| Another PopupGuard already running | New instance exits silently |
| IDM running elevated, PopupGuard is not | Cannot monitor IDM (UIPI restriction) — no crash |

---

## Resource Usage

| Resource | Usage |
|----------|-------|
| **CPU (idle)** | 0% — `GetMessage` blocks |
| **CPU (event)** | Microseconds per event |
| **RAM** | ~1–2 MB working set |
| **Disk** | `IDMPopupGuard.exe` + `config.json` only |
| **Threads** | 1 (main thread) |
| **Network** | None |
| **Logs/telemetry** | None |

---

## Testing Checklist

### Protected Windows (MUST NOT close)

- [ ] `Download File Info`
- [ ] `Download complete`
- [ ] `Duplicate download link`
- [ ] `Download later`
- [ ] `52% example.zip` (progress pattern)
- [ ] `7% movie.mkv`
- [ ] `100% setup.exe`

### Unwanted IDM Windows (SHOULD close)

- [ ] Trial/registration popups
- [ ] Update notifications
- [ ] Any IDM window not in config

### Safety

- [ ] Chrome, Explorer, Notepad, Terminal → never closed
- [ ] Broken `config.json` → error dialog, nothing closed
- [ ] IDM not running → no errors, negligible CPU
- [ ] Rapidly appearing/disappearing windows → no crash

### Configuration

- [ ] Add new title to config → restart → title is protected
- [ ] Remove title from config → restart → title is now closed

---

## Security

- Runs with **standard user privileges** (no admin required).
- Does **not** inject DLLs, modify IDM files, or patch any executables.
- Uses only read-only observation via `SetWinEventHook(WINEVENT_OUTOFCONTEXT)`.
- The only write action is `PostMessage(WM_CLOSE)` to a specific IDM window.

> **Note**: If IDM is running with **elevated (admin)** privileges and IDM Popup Guard is not, Windows UIPI (User Interface Privilege Isolation) may prevent the guard from receiving events or sending `WM_CLOSE` to IDM windows. In this case, run IDM Popup Guard as administrator too, or run IDM without elevation.

---

## Future Platform Expansion

The `core/` modules (`Config`, `RuleEngine`) have no platform dependencies. To add macOS or Linux support:

```
src/
├── core/              ← reuse as-is
└── platform/
    ├── windows/       ← current
    ├── macos/         ← future: use Accessibility API
    └── linux/         ← future: use AT-SPI or X11 events
```

---

## License

This project is provided as-is for personal use.
