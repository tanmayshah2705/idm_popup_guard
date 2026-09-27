#include "Config.h"
#include <nlohmann/json.hpp>
#include <fstream>

Config loadConfig(const std::string& path) {
    Config config;

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            return config;  // valid remains false
        }

        nlohmann::json j = nlohmann::json::parse(file);

        if (!j.contains("applications") || !j["applications"].is_array()) {
            return config;
        }

        for (const auto& app : j["applications"]) {
            AppRule rule;

            if (!app.contains("executable") || !app["executable"].is_string()) {
                return config;  // Strict: any invalid entry invalidates entire config
            }
            rule.executable = app["executable"].get<std::string>();
            if (rule.executable.empty()) {
                return config;
            }

            if (app.contains("protectedWindows") && app["protectedWindows"].is_object()) {
                const auto& pw = app["protectedWindows"];

                if (pw.contains("exactTitles") && pw["exactTitles"].is_array()) {
                    for (const auto& title : pw["exactTitles"]) {
                        if (!title.is_string()) {
                            return config;
                        }
                        rule.protectedWindows.exactTitles.push_back(
                            title.get<std::string>());
                    }
                }

                if (pw.contains("patterns") && pw["patterns"].is_array()) {
                    for (const auto& pattern : pw["patterns"]) {
                        if (!pattern.is_string()) {
                            return config;
                        }
                        rule.protectedWindows.patterns.push_back(
                            pattern.get<std::string>());
                    }
                }
            }

            config.applications.push_back(std::move(rule));
        }

        config.valid = true;

    } catch (...) {
        // Any exception (parse error, bad access, etc.) → invalid config
        config.valid = false;
        config.applications.clear();
    }

    return config;
}
