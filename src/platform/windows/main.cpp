// main.cpp - IDM Popup Guard entry point (Windows)
//
// WIN32 subsystem application — no console window is created.
// Flow: single-instance check → load config → compile rules → monitor.
// On any unrecoverable failure, shows a single MessageBox and exits.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

#include "Config.h"
#include "RuleEngine.h"
#include "WindowMonitor.h"

// ── Helpers ────────────────────────────────────────────────────────────

// Return the directory containing the running executable, with trailing
// backslash, as a UTF-8 string. Returns empty on failure.
static std::string getExeDirectory() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};

    // Truncate after the last path separator
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        if (path[i] == L'\\' || path[i] == L'/') {
            path[i + 1] = L'\0';
            break;
        }
    }

    // Convert to UTF-8
    int utf8Size = WideCharToMultiByte(
        CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) return {};

    std::string result(static_cast<size_t>(utf8Size - 1), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, path, -1, result.data(), utf8Size, nullptr, nullptr);
    return result;
}

// ── Entry point ────────────────────────────────────────────────────────

int WINAPI WinMain(
    HINSTANCE /*hInstance*/,
    HINSTANCE /*hPrevInstance*/,
    LPSTR     /*lpCmdLine*/,
    int       /*nCmdShow*/)
{
    // ── Single-instance guard ──
    // If another IDMPopupGuard is already running, exit silently.
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"IDMPopupGuard_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    bool showError = false;

    try {
        // Step 1: Locate config.json next to the executable
        std::string exeDir = getExeDirectory();
        if (exeDir.empty()) { showError = true; goto cleanup; }

        {
            // Step 2: Load and validate configuration
            Config config = loadConfig(exeDir + "config.json");
            if (!config.valid) { showError = true; goto cleanup; }

            // Step 3: Compile rules (regex patterns, title lists)
            RuleEngine engine(config);
            if (!engine.isValid()) { showError = true; goto cleanup; }

            // Step 4: Start event-driven monitoring (blocks on message loop)
            if (!WindowMonitor::start(engine)) {
                showError = true;
            }
        }

    } catch (...) {
        showError = true;
    }

cleanup:
    if (showError) {
        // Show the single required error dialog.
        // MB_SYSTEMMODAL ensures it appears on top even if desktop is busy.
        MessageBoxW(
            NULL,
            L"IDM Popup Guard has stopped.",
            L"IDM Popup Guard",
            MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return showError ? 1 : 0;
}
