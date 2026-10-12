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

    // 6. Test IDM download-progress windows whose title is simply the filename (MUST NOT close)
    assert(engine.evaluate("IDMan.exe", "ubuntu.iso", /*isProgressWindow=*/true) == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "setup.exe", /*isProgressWindow=*/true) == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "movie.mkv", /*isProgressWindow=*/true) == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "large_file.tar.gz", /*isProgressWindow=*/true) == WindowAction::Keep);

    // Also support progress pattern if present (e.g. "52% ubuntu.iso")
    assert(engine.evaluate("IDMan.exe", "52% ubuntu.iso") == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "7% movie.mkv") == WindowAction::Keep);
    assert(engine.evaluate("IDMan.exe", "100% setup.exe") == WindowAction::Keep);

    // 7. Test IDM version popups (MUST BE CLOSED - not matching any protected rule)
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 6.42") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 6.41") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 6.40") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 6.xx") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 6.39") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 7.00") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager") == WindowAction::Close);

    // Even if isProgressWindow was erroneously flagged on a version window, it MUST STILL CLOSE:
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 6.42", /*isProgressWindow=*/true) == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Internet Download Manager 6.41", /*isProgressWindow=*/true) == WindowAction::Close);

    // 8. Test unwanted IDM popups including empty-title trial expiration popup (MUST close)
    assert(engine.evaluate("IDMan.exe", "Trial expired") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Registration") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "New version available") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Purchase IDM license") == WindowAction::Close);
    assert(engine.evaluate("IDMan.exe", "Random Unregistered Dialog") == WindowAction::Close);

    // Empty-title IDM popup (e.g. trial-expiration popup shown without window title) MUST close:
    assert(engine.evaluate("IDMan.exe", "", /*isProgressWindow=*/false) == WindowAction::Close);

    // Empty-title window for non-monitored process MUST NOT close:
    assert(engine.evaluate("chrome.exe", "", /*isProgressWindow=*/false) == WindowAction::Keep);
    assert(engine.evaluate("notepad.exe", "", /*isProgressWindow=*/false) == WindowAction::Keep);

    // Empty-title progress window with progress bar MUST NOT close:
    assert(engine.evaluate("IDMan.exe", "", /*isProgressWindow=*/true) == WindowAction::Keep);


    // 9. Test fail-safe behavior with broken / missing config
    Config brokenConfig = loadConfig("non_existent_file.json");
    assert(!brokenConfig.valid);
    RuleEngine brokenEngine(brokenConfig);
    assert(!brokenEngine.isValid());
    // In fail-safe mode, evaluate MUST NEVER return Close
    assert(brokenEngine.evaluate("IDMan.exe", "Registration") == WindowAction::Keep);
    assert(brokenEngine.evaluate("IDMan.exe", "Trial expired") == WindowAction::Keep);
    assert(brokenEngine.evaluate("IDMan.exe", "Internet Download Manager 6.42") == WindowAction::Keep);


    std::cout << "[TEST] All rule and config verification tests PASSED successfully!\n";
    return 0;
}
