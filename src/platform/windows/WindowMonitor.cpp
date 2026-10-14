#include "WindowMonitor.h"
#include "ProcessDetector.h"
#include "RuleEngine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

// ── Constants ──────────────────────────────────────────────────────────

// These are defined in <winuser.h> but we guard against older SDKs
#ifndef EVENT_SYSTEM_DIALOGSTART
#define EVENT_SYSTEM_DIALOGSTART 0x0010
#endif
#ifndef EVENT_OBJECT_SHOW
#define EVENT_OBJECT_SHOW 0x8002
#endif
#ifndef EVENT_OBJECT_NAMECHANGE
#define EVENT_OBJECT_NAMECHANGE 0x800C
#endif

// Win32 dialog button IDs & notification codes
#ifndef BM_CLICK
#define BM_CLICK 0x00F5
#endif
#ifndef IDOK
#define IDOK 1
#endif
#ifndef IDCANCEL
#define IDCANCEL 2
#endif
#ifndef IDYES
#define IDYES 6
#endif
#ifndef IDNO
#define IDNO 7
#endif


// From <oleacc.h> — defined here to avoid pulling in the full header
#ifndef OBJID_WINDOW
#define OBJID_WINDOW static_cast<LONG>(0x00000000)
#endif
#ifndef CHILDID_SELF
#define CHILDID_SELF static_cast<LONG>(0x00000000)
#endif

// ── Module state ───────────────────────────────────────────────────────

static const RuleEngine* g_engine       = nullptr;
static DWORD             g_monitorTid   = 0;

// ── Helpers ────────────────────────────────────────────────────────────

// Convert a wide string to a UTF-8 std::string.
static std::string wideToUtf8(const wchar_t* wide, int charCount) {
    if (!wide || charCount <= 0) return {};
    int bytes = WideCharToMultiByte(
        CP_UTF8, 0, wide, charCount, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide, charCount, result.data(), bytes, nullptr, nullptr);
    return result;
}

// Check if an IDM window is a download-progress window.
// In IDM, all download-progress windows use class "#32770" and contain a
// progress bar control ("msctls_progress32").
static bool isDownloadProgressWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    wchar_t clsBuf[64] = {};
    if (GetClassNameW(hwnd, clsBuf, 64) <= 0) return false;
    if (wcscmp(clsBuf, L"#32770") != 0) return false;

    // Check direct child for progress bar
    if (FindWindowExW(hwnd, NULL, L"msctls_progress32", NULL) != NULL) {
        return true;
    }

    // Check nested children (e.g. within tabs or sub-dialogs)
    bool hasProgressBar = false;
    EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM lParam) -> BOOL {
            wchar_t childCls[64] = {};
            if (GetClassNameW(child, childCls, 64) > 0) {
                if (wcscmp(childCls, L"msctls_progress32") == 0) {
                    *reinterpret_cast<bool*>(lParam) = true;
                    return FALSE;  // Found, stop enumeration
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&hasProgressBar));

    return hasProgressBar;
}

// Check if a window belongs to a system helper class (tooltips, IME, DDE)
// that should not be treated as user-facing application popups.
static bool isSystemHelperClass(HWND hwnd) {
    wchar_t clsBuf[64] = {};
    if (GetClassNameW(hwnd, clsBuf, 64) <= 0) return false;

    if (wcscmp(clsBuf, L"tooltips_class32") == 0) return true;
    if (wcscmp(clsBuf, L"IME") == 0) return true;
    if (wcscmp(clsBuf, L"MSCTFIME UI") == 0) return true;
    if (wcscmp(clsBuf, L"OleDdeWndClass") == 0) return true;

    return false;
}

// Dismiss/close an unwanted window using standard Win32 mechanisms.
static void closeUnwantedWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;

    // 1. Send WM_CLOSE and WM_SYSCOMMAND SC_CLOSE
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);

    // 2. Click standard dialog dismissal buttons if present:
    // For MB_YESNO dialogs (e.g. IDM registration reminder: "Do you want to register now? [Yes] [No]"):
    // IDNO (7) clicks [No] and immediately dismisses the dialog.
    HWND hNo = GetDlgItem(hwnd, IDNO);
    if (hNo) {
        PostMessageW(hNo, BM_CLICK, 0, 0);
    }

    // For Cancel dialogs: IDCANCEL (2) clicks [Cancel]
    HWND hCancel = GetDlgItem(hwnd, IDCANCEL);
    if (hCancel) {
        PostMessageW(hCancel, BM_CLICK, 0, 0);
    }

    // For OK-only dialogs: IDOK (1) clicks [OK]
    HWND hOk = GetDlgItem(hwnd, IDOK);
    if (hOk) {
        PostMessageW(hOk, BM_CLICK, 0, 0);
    }

    // 3. Fallback: post standard dialog dismissal command messages to the dialog procedure
    PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDNO, BN_CLICKED), reinterpret_cast<LPARAM>(hNo));
    PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(hCancel));
    PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), reinterpret_cast<LPARAM>(hOk));
}

// Inspect a window and close it if it is an unwanted window belonging to a
// monitored application.
static void processWindow(HWND hwnd) {
    // ── Validate the window handle ──
    if (!hwnd || !IsWindow(hwnd))
        return;

    // ── Only process visible windows ──
    if (!IsWindowVisible(hwnd))
        return;

    // ── Skip child windows (only top-level/popup windows) ──
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (style & WS_CHILD)
        return;

    // ── Fail-safe: engine must be present and valid ──
    if (!g_engine || !g_engine->isValid())
        return;

    // ── Skip system helper classes (IME, DDE, tooltips) ──
    if (isSystemHelperClass(hwnd))
        return;

    // ── Identify the owning process's executable ──
    std::string exeName = ProcessDetector::getExecutableName(hwnd);
    if (exeName.empty())
        return;

    // ── Quick reject: only process monitored executables ──
    if (!g_engine->isMonitoredExecutable(exeName))
        return;

    // ── Get the window title (wide → UTF-8) ──
    // Note: Do NOT early-return on empty title! Custom IDM popups (such as the
    // trial-expiration popup) can have an empty window title.
    wchar_t titleBuf[512] = {};
    int titleLen = GetWindowTextW(hwnd, titleBuf, 512);
    std::string title;
    if (titleLen > 0) {
        title = wideToUtf8(titleBuf, titleLen);
    }

    // ── Detect IDM download-progress window ──
    bool isProgress = isDownloadProgressWindow(hwnd);

    // ── Evaluate against configured rules ──
    WindowAction action = g_engine->evaluate(exeName, title, isProgress);

    if (action == WindowAction::Close) {
        closeUnwantedWindow(hwnd);
    }
}

// ── WinEvent callback ──────────────────────────────────────────────────

static void CALLBACK WinEventProc(
    HWINEVENTHOOK /*hWinEventHook*/,
    DWORD         /*event*/,
    HWND          hwnd,
    LONG          idObject,
    LONG          idChild,
    DWORD         /*dwEventThread*/,
    DWORD         /*dwmsEventTime*/)
{
    // ── Filter to window-level events only ──
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        return;

    processWindow(hwnd);
}


// ── Public API ─────────────────────────────────────────────────────────

namespace WindowMonitor {

bool start(const RuleEngine& engine) {
    g_engine     = &engine;
    g_monitorTid = GetCurrentThreadId();

    // Hook EVENT_SYSTEM_DIALOGSTART — fires when a modal dialog or message box is displayed.
    HWINEVENTHOOK hookDialog = SetWinEventHook(
        EVENT_SYSTEM_DIALOGSTART, EVENT_SYSTEM_DIALOGSTART,
        NULL,
        WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Hook EVENT_OBJECT_SHOW — fires when a window becomes visible.
    // WINEVENT_OUTOFCONTEXT: no DLL injection.
    // WINEVENT_SKIPOWNPROCESS: ignore our own windows (e.g. error dialog).
    HWINEVENTHOOK hookShow = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
        NULL,                   // No DLL module
        WinEventProc,           // Callback
        0,                      // All processes
        0,                      // All threads
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (!hookShow) {
        if (hookDialog) UnhookWinEvent(hookDialog);
        g_engine = nullptr;
        return false;
    }

    // Hook EVENT_OBJECT_NAMECHANGE — fires when a window's title changes.
    // Catches windows whose title is set after they become visible.
    HWINEVENTHOOK hookNameChange = SetWinEventHook(
        EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
        NULL,
        WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (!hookNameChange) {
        UnhookWinEvent(hookShow);
        if (hookDialog) UnhookWinEvent(hookDialog);
        g_engine = nullptr;
        return false;
    }

    // ── Initial pass: close any unwanted windows already open on startup ──
    EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
        processWindow(hwnd);
        return TRUE;
    }, 0);

    // ── Message loop ──
    // GetMessage blocks when no messages are pending → zero CPU when idle.
    // WinEvent callbacks are dispatched through this loop.

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ── Cleanup ──
    UnhookWinEvent(hookNameChange);
    UnhookWinEvent(hookShow);
    if (hookDialog) UnhookWinEvent(hookDialog);
    g_engine = nullptr;

    return true;  // Normal exit via WM_QUIT
}


void stop() {
    if (g_monitorTid != 0) {
        PostThreadMessage(g_monitorTid, WM_QUIT, 0, 0);
    }
}

}  // namespace WindowMonitor
