#pragma once

// ProcessDetector.h - Windows-specific process identification
//
// Given an HWND, determines the owning process's executable filename.
// Uses GetWindowThreadProcessId + OpenProcess + QueryFullProcessImageNameW.
// Requires only PROCESS_QUERY_LIMITED_INFORMATION access (no admin needed
// for same-user processes).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

namespace ProcessDetector {

// Get the executable filename (e.g., "IDMan.exe") for the process
// owning the given window handle.
// Returns an empty string on any failure (invalid HWND, access denied, etc.).
std::string getExecutableName(HWND hwnd);

}  // namespace ProcessDetector
