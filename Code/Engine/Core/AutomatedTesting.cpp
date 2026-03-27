#include "AutomatedTesting.hpp"
#include "Engine/Core/Clock.hpp"
#include "Engine/Core/ErrorWarningAssert.hpp"

#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <algorithm>

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
	int    s_warmupFrames     = 60;    // skip initial frames (GI convergence, shader compilation)
	bool   s_shouldQuit       = false;
	std::string s_outputPath  = "benchmark_results.json";
	std::vector<float> s_fpsLog;
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

	if (s_isActive)
	{
		DebuggerPrintf("[AutoTest] Active: benchmark=%d frames=%d warmup=%d output=%s\n",
			s_benchmarkMode ? 1 : 0,
			s_benchmarkMaxFrames,
			s_warmupFrames,
			s_outputPath.c_str());
	}
}

void AutomatedTestingEndFrame()
{
	if (!s_benchmarkMode)
		return;

	s_frameCount++;

	float fps = (float)Clock::GetSystemClock().GetFrameRate();

	// Skip warmup frames (GI convergence, shader compilation, first-frame hitches)
	if (s_frameCount > s_warmupFrames)
	{
		s_fpsLog.push_back(fps);
	}

	// Check if we've collected enough frames
	if (s_frameCount >= s_benchmarkMaxFrames + s_warmupFrames)
	{
		s_shouldQuit = true;
	}
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
