#include "Config.h"
#include "RuleEngine.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "[TEST] Running IDM Popup Guard rule & config tests...\n";

    // 1. Test loading valid config
    Config config = loadConfig("config.json");
    if (!config.valid) {
        config = loadConfig("config/config.json");
    }
    if (!config.valid) {
        config = loadConfig("../config/config.json");
    }
    if (!config.valid) {
        std::cerr << "FAIL: config.json failed to load or is invalid\n";
        return 1;
    }

    assert(config.valid);
    assert(config.applications.size() == 1);
    assert(config.applications[0].executable == "IDMan.exe");

    RuleEngine engine(config);
    assert(engine.isValid());

    // 2. Test monitored executable check (case-insensitive)
    assert(engine.isMonitoredExecutable("IDMan.exe"));
    assert(engine.isMonitoredExecutable("idman.exe"));
    assert(engine.isMonitoredExecutable("IDMAN.EXE"));
    assert(!engine.isMonitoredExecutable("chrome.exe"));
    assert(!engine.isMonitoredExecutable("notepad.exe"));
    assert(!engine.isMonitoredExecutable("explorer.exe"));

    // 3. Test non-monitored executables (MUST NOT close)
    assert(engine.evaluate("chrome.exe", "Download File Info") == WindowAction::Keep);
    assert(engine.evaluate("notepad.exe", "Untitled - Notepad") == WindowAction::Keep);
    assert(engine.evaluate("explorer.exe", "Downloads") == WindowAction::Keep);

    // 4. Test exact protected titles for IDM (MUST NOT close)
    assert(engine.evaluate("IDMan.exe", "Download File Info") == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "Download complete") == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "Duplicate download link") == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "Download later") == WindowAction::Keep);

    // 5. Test case-sensitivity of exact titles (per Section 12)
    assert(engine.evaluate("IDMan.exe", "Download file info") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "download complete") == WindowAction::Close);

    // 6. Test progress pattern regex matches (MUST NOT close)
    assert(engine.evaluate("IDMan.exe", "52% ubuntu.iso") == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "7% movie.mkv") == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "100% setup.exe") == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "83% large file.zip") == WindowAction::Keep);

    // 7. Test unwanted IDM popups and main window (MUST close)
    assert(engine.evaluate("IDMan.exe", "Trial expired") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Registration") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "New version available") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Purchase IDM license") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 6.42") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Random Unregistered Dialog") == WindowAction::Close);

    // 8. Test fail-safe behavior with broken / missing config
    Config brokenConfig = loadConfig("non_existent_file.json");
    assert(!brokenConfig.valid);
    RuleEngine brokenEngine(brokenConfig);
    assert(!brokenEngine.isValid());
    // In fail-safe mode, evaluate MUST NEVER return Close
    assert(brokenEngine.evaluate("IDMan.exe", "Registration") == WindowAction::Keep);
    assert(brokenEngine.evaluate("IDMan.exe", "Trial expired") == WindowAction::Keep);

    std::cout << "[TEST] All rule and config verification tests PASSED successfully!\n";
    return 0;
}
