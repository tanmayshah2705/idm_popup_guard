// WindowInspector.cpp — Development / debugging tool
//
// Monitors the desktop for newly visible top-level windows and prints
// their HWND, PID, executable path, title, and window class to the
// console. Does NOT close any windows.
//
// Usage:
//   1. Build and run WindowInspector.exe
//   2. Trigger IDM windows (downloads, dialogs, popups)
//   3. Observe the reported titles and classes
//   4. Press Ctrl+C to stop
//
// This tool helps verify the exact window titles IDM uses so they
// can be added to config.json.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>

#ifndef EVENT_OBJECT_SHOW
#define EVENT_OBJECT_SHOW 0x8002
#endif

// Convert a wide string to UTF-8 into a caller-provided buffer.
static void wideToUtf8(const wchar_t* wide, char* buf, int bufSize) {
    if (!wide || !wide[0] || bufSize <= 0) {
        if (bufSize > 0) buf[0] = '\0';
        return;
    }
    int written = WideCharToMultiByte(
        CP_UTF8, 0, wide, -1, buf, bufSize, NULL, NULL);
    if (written <= 0) buf[0] = '\0';
}

// ── WinEvent callback ──────────────────────────────────────────────────

static void CALLBACK WinEventProc(
    HWINEVENTHOOK /*hook*/,
    DWORD         /*event*/,
    HWND          hwnd,
    LONG          idObject,
    LONG          idChild,
    DWORD         /*dwEventThread*/,
    DWORD         /*dwmsEventTime*/)
{
    // Only window-level, self events
    if (idObject != 0 || idChild != 0) return;
    if (!hwnd || !IsWindow(hwnd)) return;

    // Skip child windows
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (style & WS_CHILD) return;

    // ── Gather information ──

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    wchar_t wExePath[MAX_PATH] = {};
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        DWORD size = MAX_PATH;
        QueryFullProcessImageNameW(hProc, 0, wExePath, &size);
        CloseHandle(hProc);
    }

    wchar_t wTitle[512] = {};
    GetWindowTextW(hwnd, wTitle, 512);

    wchar_t wClass[256] = {};
    GetClassNameW(hwnd, wClass, 256);

    // ── Convert to UTF-8 for console output ──

    char exePath[MAX_PATH];
    char title[512];
    char className[256];

    wideToUtf8(wExePath, exePath, MAX_PATH);
    wideToUtf8(wTitle,   title,   512);
    wideToUtf8(wClass,   className, 256);

    // ── Print ──

    printf("--------------------------------------------------\n");
    printf("  HWND:       0x%p\n", static_cast<void*>(hwnd));
    printf("  PID:        %lu\n", static_cast<unsigned long>(pid));
    printf("  Executable: %s\n", exePath);
    printf("  Title:      \"%s\"\n", title);
    printf("  Class:      \"%s\"\n", className);
    printf("\n");
    fflush(stdout);
}

// ── Main ───────────────────────────────────────────────────────────────

int main() {
    // Enable UTF-8 output in the Windows console
    SetConsoleOutputCP(CP_UTF8);

    printf("============================================\n");
    printf("  IDM Popup Guard - Window Inspector\n");
    printf("============================================\n");
    printf("  Monitoring new top-level windows...\n");
    printf("  Open IDM and trigger its dialogs.\n");
    printf("  Press Ctrl+C to stop.\n");
    printf("============================================\n\n");
    fflush(stdout);

    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
        NULL,
        WinEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT);

    if (!hook) {
        fprintf(stderr,
            "ERROR: SetWinEventHook failed (error %lu).\n",
            GetLastError());
        return 1;
    }

    // Standard message loop — blocks with zero CPU when idle.
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWinEvent(hook);
    return 0;
}
