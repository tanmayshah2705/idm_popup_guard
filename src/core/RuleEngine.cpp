#include "RuleEngine.h"
#include <cctype>

std::string RuleEngine::toLower(const std::string& s) {
    std::string result = s;
    for (auto& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

RuleEngine::RuleEngine(const Config& config) : m_valid(false) {
    if (!config.valid) {
        return;
    }

    try {
        for (const auto& app : config.applications) {
            CompiledRule rule;
            rule.executableLower = toLower(app.executable);
            rule.exactTitles = app.protectedWindows.exactTitles;

            for (const auto& patternStr : app.protectedWindows.patterns) {
                // Compile regex; throws std::regex_error on invalid pattern
                rule.patterns.emplace_back(patternStr, std::regex::ECMAScript);
            }

            m_rules.push_back(std::move(rule));
        }

        m_valid = true;

    } catch (...) {
        // Regex compilation failure → entire engine is invalid (fail-safe)
        m_rules.clear();
        m_valid = false;
    }
}

bool RuleEngine::isMonitoredExecutable(const std::string& exeName) const {
    if (!m_valid) return false;

    std::string lower = toLower(exeName);
    for (const auto& rule : m_rules) {
        if (rule.executableLower == lower) {
            return true;
        }
    }
    return false;
}

WindowAction RuleEngine::evaluate(const std::string& exeName,
                                  const std::string& title) const {
    // Fail-safe: if engine is invalid, never close anything
    if (!m_valid) {
        return WindowAction::Keep;
    }

    std::string lower = toLower(exeName);

    for (const auto& rule : m_rules) {
        if (rule.executableLower != lower) {
            continue;
        }

        // This executable IS monitored — check if the title is protected

        // Check exact titles (case-sensitive match)
        for (const auto& protectedTitle : rule.exactTitles) {
            if (title == protectedTitle) {
                return WindowAction::Keep;
            }
        }

        // Check regex patterns
        for (const auto& pattern : rule.patterns) {
            try {
                if (std::regex_match(title, pattern)) {
                    return WindowAction::Keep;
                }
            } catch (...) {
                // Regex match error → fail-safe, keep window open
                return WindowAction::Keep;
            }
        }

        // Executable is monitored, title is NOT protected → close
        return WindowAction::Close;
    }

    // Executable is not monitored → keep (should not reach here if
    // isMonitoredExecutable was checked first, but fail-safe anyway)
    return WindowAction::Keep;
}

bool RuleEngine::isValid() const {
    return m_valid;
}
