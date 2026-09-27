#pragma once

// RuleEngine.h - Platform-independent window title evaluation
//
// Compiles configuration rules (exact titles and regex patterns) at
// construction time and provides fast evaluation. If the configuration
// is invalid or a regex fails to compile, the engine enters a fail-safe
// state where it never requests a window to be closed.

#include "Config.h"
#include <string>
#include <vector>
#include <regex>

enum class WindowAction {
    Keep,   // Window is protected or not monitored — leave it open
    Close   // Window belongs to a monitored app and is not protected — close it
};

class RuleEngine {
public:
    // Compile rules from a loaded Config.
    // If config is invalid or any regex fails to compile, isValid() returns false
    // and evaluate() always returns Keep (fail-safe).
    explicit RuleEngine(const Config& config);

    // Check if a given executable filename is being monitored.
    // Comparison is case-insensitive (Windows filenames are case-insensitive).
    bool isMonitoredExecutable(const std::string& exeName) const;

    // Evaluate a window belonging to the given executable.
    //   - If the executable is not monitored → Keep
    //   - If the title matches any exact title or regex pattern → Keep
    //   - Otherwise → Close
    //   - If the engine is invalid → Keep (fail-safe)
    //
    // exeName:          executable filename, e.g. "IDMan.exe"
    // title:            window title string (UTF-8)
    // isProgressWindow: true if the window is identified as an IDM download-progress window
    WindowAction evaluate(const std::string& exeName,
                          const std::string& title,
                          bool isProgressWindow = false) const;

    // Helper: checks if a title is an IDM version popup or main window title
    // (e.g. "Internet Download Manager 6.42", "Internet Download Manager 6.xx", "Internet Download Manager")
    static bool isIdmVersionTitle(const std::string& title);

    // Whether the configuration was valid and all rules compiled successfully.
    bool isValid() const;


private:
    struct CompiledRule {
        std::string executableLower;           // Lowercase for case-insensitive comparison
        std::vector<std::string> exactTitles;  // Case-sensitive exact matches
        std::vector<std::regex> patterns;      // Compiled regex patterns
    };

    std::vector<CompiledRule> m_rules;
    bool m_valid;

    static std::string toLower(const std::string& s);
};
