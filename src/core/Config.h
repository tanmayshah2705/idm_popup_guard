#pragma once

// Config.h - Platform-independent configuration loading
//
// Defines the data structures for application monitoring rules
// and the function to load them from a JSON configuration file.
// This module has no platform-specific dependencies.

#include <string>
#include <vector>

// Rules for which windows to protect for a given application.
struct ProtectedWindows {
    std::vector<std::string> exactTitles;  // Exact title strings (case-sensitive)
    std::vector<std::string> patterns;     // Regex pattern strings
};

// Monitoring rule for a single application.
struct AppRule {
    std::string executable;                // e.g., "IDMan.exe"
    ProtectedWindows protectedWindows;
};

// Complete parsed configuration.
struct Config {
    std::vector<AppRule> applications;
    bool valid = false;                    // false if loading/parsing failed
};

// Load configuration from a JSON file.
// Returns a Config with valid=false on any error (missing file, malformed JSON,
// invalid structure). Never throws.
Config loadConfig(const std::string& path);
