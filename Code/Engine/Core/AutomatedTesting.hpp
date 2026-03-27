#pragma once

//--------------------------------------------------------------
// AutomatedTesting — Engine-level automated test harness.
//
// Provides --benchmark, --screenshot, and --output support for
// any application built on this engine. Follows the same global
// function pattern as DebugRenderSystem.
//
// Integration (game's App.cpp, ~4 lines):
//   Startup:   AutomatedTestingStartup(commandLineString);
//   RunFrame:  AutomatedTestingEndFrame();
//              if (AutomatedTestingShouldQuit()) HandleQuitRequested();
//   Shutdown:  AutomatedTestingShutdown();
//--------------------------------------------------------------

// Call once at startup with the raw command line string.
// Parses: --benchmark <frames> --output <path>
void AutomatedTestingStartup(const char* commandLine);

// Call once per frame after Render/EndFrame. Records FPS, increments frame counter.
void AutomatedTestingEndFrame();

// Returns true when the test run is complete and the app should quit.
bool AutomatedTestingShouldQuit();

// Returns true if any automated testing mode is active.
bool AutomatedTestingIsActive();

// Call during app shutdown. Writes result files (benchmark JSON, etc.).
void AutomatedTestingShutdown();
