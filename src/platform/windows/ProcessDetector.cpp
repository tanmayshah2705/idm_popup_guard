#include "ProcessDetector.h"

namespace ProcessDetector {

std::string getExecutableName(HWND hwnd) {
    if (!hwnd) return {};

    // Get the process ID that owns this window
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return {};

    // Open process with minimum required access
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return {};

    // Query the full executable image path
    wchar_t pathBuf[MAX_PATH] = {};
    DWORD pathSize = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hProcess, 0, pathBuf, &pathSize);
    CloseHandle(hProcess);

    if (!ok || pathSize == 0) return {};

    // Extract just the filename from the full path
    const wchar_t* filename = pathBuf;
    for (DWORD i = 0; i < pathSize; ++i) {
        if (pathBuf[i] == L'\\' || pathBuf[i] == L'/') {
            filename = &pathBuf[i + 1];
        }
    }

    if (*filename == L'\0') return {};

    // Convert wide filename to UTF-8
    int utf8Size = WideCharToMultiByte(
        CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) return {};

    std::string result(static_cast<size_t>(utf8Size - 1), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, filename, -1, result.data(), utf8Size, nullptr, nullptr);

    return result;
}

}  // namespace ProcessDetector
