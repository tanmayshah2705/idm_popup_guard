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
#ifndef EVENT_OBJECT_SHOW
#define EVENT_OBJECT_SHOW 0x8002
#endif
#ifndef EVENT_OBJECT_NAMECHANGE
#define EVENT_OBJECT_NAMECHANGE 0x800C
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

    // ── Validate the window handle ──
    if (!hwnd || !IsWindow(hwnd))
        return;

    // ── Skip child windows (only top-level windows) ──
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (style & WS_CHILD)
        return;

    // ── Fail-safe: engine must be present and valid ──
    if (!g_engine || !g_engine->isValid())
        return;

    // ── Identify the owning process's executable ──
    std::string exeName = ProcessDetector::getExecutableName(hwnd);
    if (exeName.empty())
        return;

    // ── Quick reject: only process monitored executables ──
    if (!g_engine->isMonitoredExecutable(exeName))
        return;

    // ── Get the window title (wide → UTF-8) ──
    wchar_t titleBuf[512];
    int titleLen = GetWindowTextW(hwnd, titleBuf, 512);
    if (titleLen <= 0)
        return;  // Empty title → fail-safe, do not close

    std::string title = wideToUtf8(titleBuf, titleLen);
    if (title.empty())
        return;

    // ── Evaluate against configured rules ──
    WindowAction action = g_engine->evaluate(exeName, title);

    if (action == WindowAction::Close) {
        // Send a polite close request (non-blocking).
        // If the HWND is already invalid, PostMessage simply returns FALSE.
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

// ── Public API ─────────────────────────────────────────────────────────

namespace WindowMonitor {

bool start(const RuleEngine& engine) {
    g_engine     = &engine;
    g_monitorTid = GetCurrentThreadId();

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
        g_engine = nullptr;
        return false;
    }

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
    g_engine = nullptr;

    return true;  // Normal exit via WM_QUIT
}

void stop() {
    if (g_monitorTid != 0) {
        PostThreadMessage(g_monitorTid, WM_QUIT, 0, 0);
    }
}

}  // namespace WindowMonitor
