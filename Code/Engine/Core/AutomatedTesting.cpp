#include "AutomatedTesting.hpp"
#include "Engine/Core/Clock.hpp"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Renderer/Renderer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <algorithm>

#ifdef ENGINE_DX12_RENDERER
#include "Engine/Renderer/DX12Renderer.hpp"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/Window.hpp"
#include "Engine/Math/IntVec2.hpp"
extern Renderer* g_theRenderer;
extern Scene*    g_theScene;
extern Window*   g_theWindow;
#endif

//--------------------------------------------------------------
// Internal state (file-scoped, same pattern as DebugRenderSystem)
//--------------------------------------------------------------
namespace
{
	bool   s_isActive         = false;

	// Benchmark
	bool   s_benchmarkMode    = false;
	int    s_benchmarkMaxFrames = 600;
	int    s_frameCount       = 0;
	int    s_warmupFrames     = 60;
	bool   s_shouldQuit       = false;
	std::string s_outputPath  = "benchmark_results.json";
	std::vector<float> s_fpsLog;

	// Screenshot
	bool   s_screenshotMode   = false;
	int    s_screenshotFrame  = -1;      // -1 = use warmup + 10 as default
	std::string s_screenshotDir = "Screenshots";
	bool   s_screenshotTaken  = false;
}

//--------------------------------------------------------------
// Command line parsing
//--------------------------------------------------------------
static const char* FindArg(const char* cmdLine, const char* argName)
{
	if (!cmdLine || !argName)
		return nullptr;
	return strstr(cmdLine, argName);
}

static int ParseIntAfterArg(const char* cmdLine, const char* argName, int defaultValue)
{
	const char* pos = FindArg(cmdLine, argName);
	if (!pos)
		return defaultValue;
	pos += strlen(argName);
	while (*pos == ' ') pos++;
	int val = atoi(pos);
	return (val > 0) ? val : defaultValue;
}

static std::string ParseStringAfterArg(const char* cmdLine, const char* argName, const std::string& defaultValue)
{
	const char* pos = FindArg(cmdLine, argName);
	if (!pos)
		return defaultValue;
	pos += strlen(argName);
	while (*pos == ' ') pos++;
	std::string result;
	while (*pos && *pos != ' ')
	{
		result += *pos;
		pos++;
	}
	return result.empty() ? defaultValue : result;
}

//--------------------------------------------------------------
// Public API
//--------------------------------------------------------------
void AutomatedTestingStartup(const char* commandLine)
{
	if (!commandLine || commandLine[0] == '\0')
		return;

	if (FindArg(commandLine, "--benchmark"))
	{
		s_benchmarkMode = true;
		s_isActive = true;
		s_benchmarkMaxFrames = ParseIntAfterArg(commandLine, "--benchmark", 600);
	}

	if (FindArg(commandLine, "--output"))
	{
		s_outputPath = ParseStringAfterArg(commandLine, "--output", "benchmark_results.json");
	}

	if (FindArg(commandLine, "--warmup"))
	{
		s_warmupFrames = ParseIntAfterArg(commandLine, "--warmup", 60);
	}

	if (FindArg(commandLine, "--screenshot"))
	{
		s_screenshotMode = true;
		s_isActive = true;
		s_screenshotDir = ParseStringAfterArg(commandLine, "--screenshot", "Screenshots");
		s_screenshotFrame = ParseIntAfterArg(commandLine, "--screenshot-frame", -1);
	}

	if (s_isActive)
	{
		DebuggerPrintf("[AutoTest] Active: benchmark=%d frames=%d warmup=%d screenshot=%d output=%s\n",
			s_benchmarkMode ? 1 : 0,
			s_benchmarkMaxFrames,
			s_warmupFrames,
			s_screenshotMode ? 1 : 0,
			s_outputPath.c_str());
	}
}

void AutomatedTestingEndFrame()
{
	if (!s_isActive)
		return;

	s_frameCount++;

	// Benchmark: record FPS
	if (s_benchmarkMode)
	{
		float fps = (float)Clock::GetSystemClock().GetFrameRate();
		if (s_frameCount > s_warmupFrames)
			s_fpsLog.push_back(fps);

		if (s_frameCount >= s_benchmarkMaxFrames + s_warmupFrames)
			s_shouldQuit = true;
	}

	// Screenshot: capture at the right frame
#ifdef ENGINE_DX12_RENDERER
	if (s_screenshotMode && !s_screenshotTaken)
	{
		int captureFrame = (s_screenshotFrame > 0) ? s_screenshotFrame : (s_warmupFrames + 10);
		if (s_frameCount == captureFrame)
		{
			// Create output directory
			CreateDirectoryA(s_screenshotDir.c_str(), NULL);

			std::string filePath = s_screenshotDir + "/screenshot.png";
			DX12Renderer* sub = g_theRenderer->GetSubRenderer();
			sub->CaptureScreenshot(filePath);

			// Also dump scene JSON alongside for LuminaGI-CudaRef validation
			if (g_theScene && g_theWindow)
			{
				std::string jsonPath = s_screenshotDir + "/screenshot.json";
				IntVec2 dim = g_theWindow->GetClientDimensions();
				g_theScene->DumpToJSON(jsonPath, sub->GetCurrentCamera(), dim.x, dim.y);
			}

			s_screenshotTaken = true;

			// If no benchmark mode, quit after screenshot
			if (!s_benchmarkMode)
				s_shouldQuit = true;
		}
	}
#endif
}

bool AutomatedTestingShouldQuit()
{
	return s_shouldQuit;
}

bool AutomatedTestingIsActive()
{
	return s_isActive;
}

void AutomatedTestingShutdown()
{
	// Reset screenshot state
	s_screenshotMode = false;
	s_screenshotTaken = false;

	if (!s_benchmarkMode || s_fpsLog.empty())
		return;

	// Calculate statistics
	float sum = 0.f;
	float minFPS = 999999.f;
	float maxFPS = 0.f;
	for (float fps : s_fpsLog)
	{
		sum += fps;
		if (fps < minFPS) minFPS = fps;
		if (fps > maxFPS) maxFPS = fps;
	}
	float avgFPS = sum / (float)s_fpsLog.size();

	// Percentiles
	std::vector<float> sorted = s_fpsLog;
	std::sort(sorted.begin(), sorted.end());
	float p1  = sorted[(size_t)(sorted.size() * 0.01f)];
	float p5  = sorted[(size_t)(sorted.size() * 0.05f)];
	float p50 = sorted[(size_t)(sorted.size() * 0.50f)];

	// Write JSON
	std::ofstream out(s_outputPath);
	if (!out.is_open())
	{
		DebuggerPrintf("[AutoTest] ERROR: cannot write %s\n", s_outputPath.c_str());
		return;
	}

	out << "{\n";
	out << "  \"frame_count\": " << s_fpsLog.size() << ",\n";
	out << "  \"avg_fps\": " << avgFPS << ",\n";
	out << "  \"min_fps\": " << minFPS << ",\n";
	out << "  \"max_fps\": " << maxFPS << ",\n";
	out << "  \"p1_fps\": " << p1 << ",\n";
	out << "  \"p5_fps\": " << p5 << ",\n";
	out << "  \"median_fps\": " << p50 << ",\n";
	out << "  \"per_frame_fps\": [";
	for (size_t i = 0; i < s_fpsLog.size(); i++)
	{
		if (i > 0) out << ", ";
		out << s_fpsLog[i];
	}
	out << "]\n";
	out << "}\n";
	out.close();

	DebuggerPrintf("[AutoTest] Benchmark results: avg=%.1f min=%.1f p1=%.1f median=%.1f (%zu frames)\n",
		avgFPS, minFPS, p1, p50, s_fpsLog.size());
	DebuggerPrintf("[AutoTest] Written to %s\n", s_outputPath.c_str());

	// Reset state
	s_fpsLog.clear();
	s_frameCount = 0;
	s_shouldQuit = false;
	s_benchmarkMode = false;
	s_isActive = false;
}
