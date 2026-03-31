#pragma once
#include <mutex>

#include "Engine/Core/Rgba8.hpp"
#include "Engine/Core/EventSystem.hpp"
#include <string>
#include <vector>

struct AABB2;
class Renderer;
class BitmapFont;
class Timer;
class Camera;

enum DevConsoleMode
{
	OPEN_FULL,
	OPEN_PARTIAL,
	HIDDEN,
	NUM
};

struct DevConsoleConfig
{
	Renderer* m_defaultRenderer;
	Camera* m_camera = nullptr;
	std::string m_defaultFontName = "SquirrelFixedFont";
	float m_fontAspect = 0.7f;
	int m_linesOnScreen = 40;
	int m_masCommandHistory = 128;
	bool m_startOpen = false;
};

struct DevConsoleLine
{
	Rgba8 m_color;
	std::string m_text;
};

class DevConsole
{
public:
	DevConsole(DevConsoleConfig const& config);
	~DevConsole();

	void Startup();
	void Shutdown();
	void BeginFrame();
	void EndFrame();

	void Execute(std::string const& consoleCommandText);
	void AddLine(Rgba8 const& color, std::string const& text);
	void Render(AABB2 const& bounds, Renderer* rendererOverride = nullptr) const;

	void ExecuteXmlCommandScriptNode(XmlElement const& commandScriptXmlElement);
	void ExecuteXmlCommandScriptFile(std::string const& commandScriptXmlFilePathName);

	DevConsoleMode GetMode() const;
	void SetMode(DevConsoleMode mode);
	void ToggleMode(DevConsoleMode mode);

	static bool Command_Test(EventArgs& args);

	static const Rgba8 ERRORLINE;
	static const Rgba8 WARNING;
	static const Rgba8 INFO_MAJOR;
	static const Rgba8 INFO_MINOR;
	static const Rgba8 INFO_SHADOW;
	static const Rgba8 INPUT_TEXT;
	static const Rgba8 INPUT_INSERTION_POINT;

	static bool OnKeyPressed(EventArgs& args);
	static bool OnCharInput(EventArgs& args);
	static bool Command_Clear(EventArgs& args);
	static bool Command_Help(EventArgs& args);
	static bool Command_Warning(EventArgs& args);

protected:
	void Render_Openfull(AABB2 const& bounds, Renderer& renderer, BitmapFont& font, float fontAspect = 1.f) const;

protected:
	DevConsoleConfig m_config;
	mutable std::recursive_mutex m_mutex;

	DevConsoleMode m_mode = DevConsoleMode::HIDDEN;
	std::vector<DevConsoleLine> m_lines;
	int m_frameNumber = 0;

	std::string m_fontPath;

	std::string m_inputText;
	int m_insertionPointPos = 0;
	bool m_insertionPointVisible = true;
	Timer* m_insertionPointBlinkTimer;
	std::vector<std::string> m_commandHistory;
	int m_historyIndex = -1;
};
