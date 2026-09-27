#pragma once

// WindowMonitor.h - Windows-specific event-driven window monitoring
//
// Uses SetWinEventHook (EVENT_OBJECT_SHOW and EVENT_OBJECT_NAMECHANGE)
// to receive callbacks when windows appear or change titles, without
// polling. Runs a standard Windows message loop that blocks with
// zero CPU when no events are pending.

class RuleEngine;

namespace WindowMonitor {

// Start the event-driven window monitor.
// Sets up WinEvent hooks and runs a blocking message loop.
// Returns true on normal exit (WM_QUIT), false on hook setup failure.
bool start(const RuleEngine& engine);

// Signal the monitor to stop by posting WM_QUIT to the message loop.
// Safe to call from any thread.
void stop();

}  // namespace WindowMonitor
