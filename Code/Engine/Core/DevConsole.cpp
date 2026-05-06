#include "DevConsole.hpp"
#include "Engine/Renderer/BitmapFont.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/StringUtils.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Core/Timer.hpp"
#include "Engine/Core/Clock.hpp"
#include "Engine/Core/FileUtils.hpp"

#include <algorithm>

DevConsole* g_theDevConsole = nullptr;

const Rgba8 DevConsole::ERRORLINE = Rgba8(255, 0, 0, 255);;
const Rgba8 DevConsole::WARNING = Rgba8(255, 255, 0, 255);
const Rgba8 DevConsole::INFO_MAJOR = Rgba8(0, 255, 0, 255);
const Rgba8 DevConsole::INFO_MINOR = Rgba8(120, 120, 120, 255);
const Rgba8 DevConsole::INFO_SHADOW = Rgba8(0, 0, 0, 255);
const Rgba8 DevConsole::INPUT_TEXT = Rgba8(0, 190, 190, 255);
const Rgba8 DevConsole::INPUT_INSERTION_POINT = Rgba8(255, 255, 255, 255);

//-----------------------------------------------------------------------------------------------
// Splits a command string on spaces, treating anything in quotes as a single token.
// Quotation marks themselves are stripped from the output.
//-----------------------------------------------------------------------------------------------
static Strings SplitCommandWithQuotes(std::string const& text)
{
	Strings tokens;
	std::string current;
	bool inQuotes = false;

	for (size_t i = 0; i < text.size(); ++i)
	{
		char c = text[i];
		if (c == '"')
		{
			inQuotes = !inQuotes;
			continue; // strip the quote character
		}
		if (c == ' ' && !inQuotes)
		{
			if (!current.empty())
			{
				tokens.push_back(current);
				current.clear();
			}
			continue;
		}
		current += c;
	}
	if (!current.empty())
		tokens.push_back(current);

	return tokens;
}

DevConsole::DevConsole(DevConsoleConfig const& config)
	:m_config(config)
{
	m_insertionPointBlinkTimer = new Timer(0.5f, &Clock::GetSystemClock());
}

DevConsole::~DevConsole()
{
}

void DevConsole::Startup()
{
	m_fontPath = "Data/Fonts/" + m_config.m_defaultFontName;
	if (m_config.m_defaultRenderer)
	{
		m_config.m_defaultRenderer->CreateOrGetBitmapFont(m_fontPath.c_str());
	}
	g_theEventSystem->SubscribeEventCallBackFunction("Test", Command_Test);
	g_theEventSystem->SubscribeEventCallBackFunction("KeyPressed", OnKeyPressed);
	g_theEventSystem->SubscribeEventCallBackFunction("help", Command_Help);
	g_theEventSystem->SubscribeEventCallBackFunction("clear", Command_Clear);
	g_theEventSystem->SubscribeEventCallBackFunction("CharInput", OnCharInput);
	g_theEventSystem->SubscribeEventCallBackFunction("Warning", Command_Warning);
}

void DevConsole::Shutdown()
{
	g_theEventSystem->UnsubscribeEventCallBackFunction("Test", Command_Test);
	delete m_insertionPointBlinkTimer;
	m_insertionPointBlinkTimer =nullptr;
}

void DevConsole::BeginFrame()
{
	m_frameNumber++;
	if (m_insertionPointBlinkTimer->DecrementPeriodIfElapsed())
	{
		m_insertionPointVisible = !m_insertionPointVisible;
	}
}

void DevConsole::EndFrame()
{
}

void DevConsole::Execute(std::string const& consoleCommandText)
{
	Strings commandLines = SplitStringOnDelimiter(consoleCommandText, '\n');

	for (const std::string& line : commandLines)
	{
		if (line.empty())
			continue;

		// Quote-aware split
		Strings parts = SplitCommandWithQuotes(line);
		if (parts.empty())
			continue;

		// Command name is case-preserving; HCIS handles case-insensitive matching
		std::string commandName = parts[0];

		EventArgs arguments;

		for (size_t i = 1; i < parts.size(); ++i)
		{
			// Split on first '=' only
			size_t eqPos = parts[i].find('=');
			if (eqPos != std::string::npos)
			{
				std::string key = parts[i].substr(0, eqPos);
				std::string value = parts[i].substr(eqPos + 1);
				arguments.SetValue(key, value);
			}
		}

		if (g_theDevConsole)
		{
			{
				std::lock_guard<std::recursive_mutex> lock(m_mutex);
				m_commandHistory.push_back(consoleCommandText);
				m_historyIndex = (int)m_commandHistory.size() - 1;
			}

			g_theEventSystem->FireEvent(commandName, arguments);
			g_theDevConsole->m_inputText.clear();
			g_theDevConsole->m_insertionPointPos = 0;
		}
	}
}

void DevConsole::AddLine(Rgba8 const& color, std::string const& text)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

	DevConsoleLine line;
	line.m_color = color;
	line.m_text = text;
	m_lines.push_back(line);
}

void DevConsole::Render(AABB2 const& bounds, Renderer* rendererOverride) const
{
	if (m_mode == HIDDEN)
	{
		return;
	}

	Renderer* renderer = rendererOverride ? rendererOverride : m_config.m_defaultRenderer;

	if (m_mode == OPEN_FULL)
	{
		BitmapFont* renderFont = renderer->CreateOrGetBitmapFont(m_fontPath.c_str());
		Render_Openfull(bounds, *renderer, *renderFont, m_config.m_fontAspect);
	}
}

//-----------------------------------------------------------------------------------------------
// XML command scripts
//-----------------------------------------------------------------------------------------------
void DevConsole::ExecuteXmlCommandScriptNode(XmlElement const& commandScriptXmlElement)
{
	XmlElement const* childElem = commandScriptXmlElement.FirstChildElement();
	while (childElem)
	{
		std::string commandLine = childElem->Name();

		// Each attribute becomes a key=value argument
		XmlAttribute const* attr = childElem->FirstAttribute();
		while (attr)
		{
			std::string attrValue = attr->Value();
			// If the value has spaces, wrap it in quotes so our parser handles it correctly
			if (attrValue.find(' ') != std::string::npos)
				commandLine += Stringf(" %s=\"%s\"", attr->Name(), attrValue.c_str());
			else
				commandLine += Stringf(" %s=%s", attr->Name(), attrValue.c_str());

			attr = attr->Next();
		}

		Execute(commandLine);
		childElem = childElem->NextSiblingElement();
	}
}

void DevConsole::ExecuteXmlCommandScriptFile(std::string const& commandScriptXmlFilePathName)
{
	XmlDocument doc;
	XmlResult result = doc.LoadFile(commandScriptXmlFilePathName.c_str());
	if (result != tinyxml2::XML_SUCCESS)
	{
		AddLine(ERRORLINE, Stringf("Failed to load command script: %s", commandScriptXmlFilePathName.c_str()));
		return;
	}

	XmlElement* rootElem = doc.RootElement();
	if (rootElem)
	{
		ExecuteXmlCommandScriptNode(*rootElem);
	}
}

//-----------------------------------------------------------------------------------------------
DevConsoleMode DevConsole::GetMode() const
{
	return m_mode;
}

void DevConsole::SetMode(DevConsoleMode mode)
{
	m_mode = mode;
}

void DevConsole::ToggleMode(DevConsoleMode mode)
{
	if (m_mode == mode)
	{
		m_mode = DevConsoleMode::HIDDEN;
		m_insertionPointBlinkTimer->Stop();
	}
	else
	{
		m_mode = mode;
		m_insertionPointBlinkTimer->Start();
	}
}

void DevConsole::Render_Openfull(AABB2 const& bounds, Renderer& renderer, BitmapFont& font, float fontAspect) const
{
#ifdef ENGINE_DX12_RENDERER
	renderer.SetRenderMode(RenderMode::FORWARD);
#endif

	//begin camera
	renderer.BeginCamera(*m_config.m_camera);
	renderer.SetModelConstants(m_config.m_camera->GetCameraToRenderTransform(),Rgba8::WHITE);

	std::vector<Vertex_PCU> consoleQuadVerts;
#if defined(ENGINE_DX11_RENDERER) || defined(ENGINE_VULKAN_RENDERER)
	renderer.BindTexture(nullptr);
#endif
	#ifdef ENGINE_DX12_RENDERER
	renderer.SetMaterialConstants(nullptr, nullptr, nullptr);
#endif
	renderer.BindShader(nullptr);

	renderer.SetBlendMode(BlendMode::ALPHA);
	renderer.SetRasterizerMode(RasterizerMode::SOLID_CULL_NONE);
	renderer.SetDepthMode(DepthMode::DISABLED);
	renderer.SetSamplerMode(SamplerMode::POINT_CLAMP);

	AddVertsForAABB2D(consoleQuadVerts, bounds, Rgba8(180, 180, 180, 180));
	renderer.DrawVertexArray(consoleQuadVerts);

	Vec2 textDimensions = bounds.GetDimensions();
	textDimensions.y = textDimensions.y / m_config.m_linesOnScreen;
	Vec2 prevBoxMin = bounds.m_mins;
	Vec2 prevBoxMax = Vec2(bounds.m_maxs.x, bounds.m_mins.y + textDimensions.y);

	std::vector<Vertex_PCU> textVerts;
	std::vector<Vertex_PCU> shadowVerts;

	if (m_insertionPointVisible)
	{
		float originalInsertionPos = textDimensions.y * fontAspect * float(m_insertionPointPos);
		originalInsertionPos -= textDimensions.y * fontAspect * 0.4f;
		Vec2 insertPosMin = Vec2(prevBoxMin.x + originalInsertionPos, prevBoxMin.y);
		font.AddVertsForTextInBox2D(textVerts, "|",
			AABB2(Vec2(insertPosMin), insertPosMin + Vec2(textDimensions.y*fontAspect, textDimensions.y)),
			textDimensions.y, INPUT_INSERTION_POINT, fontAspect, Vec2(0.f, 0.f));
	}
	font.AddVertsForTextInBox2D(textVerts, m_inputText, AABB2(prevBoxMin, prevBoxMin + textDimensions), textDimensions.y, DevConsole::INPUT_TEXT, fontAspect, Vec2(0.f, 0.f));

	prevBoxMin += Vec2(0.f, textDimensions.y);
	prevBoxMax += Vec2(0.f, textDimensions.y);

	Vec2 offset = Vec2(textDimensions.y*0.08f*fontAspect, textDimensions.y * 0.08f);

	for (int lineIndex = 0; lineIndex < m_lines.size(); lineIndex++)
	{
		DevConsoleLine currentLine = m_lines[m_lines.size() - 1 - lineIndex];
		const std::string& lineText = currentLine.m_text;
		const Rgba8& lineColor = currentLine.m_color;

		AABB2 unitBox = AABB2(prevBoxMin, prevBoxMin + textDimensions);
		font.AddVertsForTextInBox2D(textVerts, lineText, unitBox, textDimensions.y, lineColor, fontAspect, Vec2(0.f,0.f));
		font.AddVertsForTextInBox2D(shadowVerts, lineText,AABB2(unitBox.m_mins + offset, unitBox.m_maxs + offset), textDimensions.y, DevConsole::INFO_SHADOW, fontAspect, Vec2(0.f, 0.f));

		prevBoxMin.y += textDimensions.y;
	}

#if defined(ENGINE_DX11_RENDERER) || defined(ENGINE_VULKAN_RENDERER)
	renderer.BindTexture(&font.GetTexture());
#endif
#ifdef ENGINE_DX12_RENDERER
	renderer.SetMaterialConstants(&font.GetTexture());
#endif
	renderer.BindShader(nullptr);

	renderer.SetBlendMode(BlendMode::ALPHA);
	renderer.SetDepthMode(DepthMode::DISABLED);
	renderer.SetSamplerMode(SamplerMode::POINT_CLAMP);

	renderer.SetModelConstants(m_config.m_camera->GetCameraToRenderTransform(),Rgba8::WHITE);

	renderer.DrawVertexArray(shadowVerts);
	renderer.DrawVertexArray(textVerts);
	textVerts.clear();
	shadowVerts.clear();

	renderer.EndCamera(*m_config.m_camera);
}

bool DevConsole::Command_Test(EventArgs& args)
{
	UNUSED(args);
	printf("Test command received");
	args.DebugPrintContents();
	return false;
}

bool DevConsole::OnKeyPressed(EventArgs& args)
{
	unsigned char keyCode = (unsigned char)args.GetValue("KeyCode", -1);
	if (keyCode == KEYCODE_TILDE)
	{
		g_theDevConsole->ToggleMode(OPEN_FULL);
	}
	if (g_theDevConsole->m_mode == HIDDEN)
	{
		return false;
	}
	if (keyCode == KEYCODE_DELETE)
	{
		std::string suffix = g_theDevConsole->m_inputText.substr(g_theDevConsole->m_insertionPointPos);
		if (suffix.size() > 0)
		{
			std::string temp = g_theDevConsole->m_inputText;
			g_theDevConsole->m_inputText = temp.substr(0, g_theDevConsole->m_insertionPointPos) + temp.substr(g_theDevConsole->m_insertionPointPos + 1);
		}
	}
	if (keyCode == KEYCODE_HOME)
	{
		g_theDevConsole->m_insertionPointPos = 0;
	}
	if (keyCode == KEYCODE_END)
	{
		g_theDevConsole->m_insertionPointPos = (int)g_theDevConsole->m_inputText.size();
	}
	if (keyCode == KEYCODE_RIGHTARROW)
	{
		if (g_theDevConsole->m_insertionPointPos < (int)g_theDevConsole->m_inputText.size())
		{
			g_theDevConsole->m_insertionPointPos++;
		}
	}
	if (keyCode == KEYCODE_LEFTARROW)
	{
		if (g_theDevConsole->m_insertionPointPos > 0)
		{
			g_theDevConsole->m_insertionPointPos--;
		}
	}
	if (keyCode == KEYCODE_UPARROW)
	{
		if (g_theDevConsole->m_commandHistory.size() > 0)
		{
			g_theDevConsole->m_inputText = g_theDevConsole->m_commandHistory[g_theDevConsole->m_historyIndex];
		}

		if (g_theDevConsole->m_historyIndex > 0)
		{
			g_theDevConsole->m_historyIndex--;
		}
		if (g_theDevConsole->m_historyIndex == 0)
		{
			g_theDevConsole->m_historyIndex = (int)g_theDevConsole->m_commandHistory.size() - 1;
		}
	}
	if (keyCode == KEYCODE_DOWNARROW)
	{
		if (g_theDevConsole->m_commandHistory.size() > 0)
		{
			g_theDevConsole->m_inputText = g_theDevConsole->m_commandHistory[g_theDevConsole->m_historyIndex];
		}
		if (g_theDevConsole->m_historyIndex < g_theDevConsole->m_commandHistory.size() - 1)
		{
			g_theDevConsole->m_historyIndex++;
		}
		if (g_theDevConsole->m_historyIndex >= g_theDevConsole->m_commandHistory.size() - 1)
		{
			g_theDevConsole->m_historyIndex = 0;
		}
	}
	if (keyCode == KEYCODE_BACKSPACE)
	{
		std::string prefix = g_theDevConsole->m_inputText.substr(0, g_theDevConsole->m_insertionPointPos);
		if (prefix.size() > 0)
		{
			std::string temp = g_theDevConsole->m_inputText;
			--g_theDevConsole->m_insertionPointPos;
			g_theDevConsole->m_inputText = temp.substr(0, g_theDevConsole->m_insertionPointPos) + temp.substr(g_theDevConsole->m_insertionPointPos + 1);
		}
		return true;
	}
	if (keyCode == KEYCODE_ENTER)
	{
		if (g_theDevConsole->m_inputText.empty())
		{
			g_theDevConsole->ToggleMode(HIDDEN);
		}
		else
		{
			g_theDevConsole->Execute(g_theDevConsole->m_inputText);
		}
	}
	if (keyCode == KEYCODE_ESC)
	{
		if (g_theDevConsole->m_inputText.empty())
		{
			g_theDevConsole->ToggleMode(OPEN_FULL);
		}
		else
		{
			g_theDevConsole->m_inputText.clear();
			g_theDevConsole->m_insertionPointPos = 0;
		}
	}
	g_theDevConsole->m_insertionPointVisible = true;
	return true;
}

bool DevConsole::OnCharInput(EventArgs& args)
{
	if (g_theDevConsole)
	{
		unsigned char charCode = (unsigned char)args.GetValue("CharCode", -1);
		if (g_theDevConsole->m_mode == OPEN_FULL)
		{
			if (charCode > 126 || charCode < 32 || charCode == '`')
			{
				return true;
			}
			std::string tempString = g_theDevConsole->m_inputText;
			if (g_theDevConsole->m_insertionPointVisible)
			{
				g_theDevConsole->m_inputText = tempString.substr(0, g_theDevConsole->m_insertionPointPos) + (char)charCode + tempString.substr((g_theDevConsole->m_insertionPointPos++));
			}
			else
			{
				g_theDevConsole->m_inputText = tempString.substr(0, g_theDevConsole->m_insertionPointPos) + (char)charCode + tempString.substr(g_theDevConsole->m_insertionPointPos++);
			}
			return true;
		}
		return false;
	}
	return false;
}

bool DevConsole::Command_Clear(EventArgs& args)
{
	UNUSED(args);
	if (g_theDevConsole)
	{
		g_theDevConsole->m_lines.clear();
	}
	return false;
}

bool DevConsole::Command_Help(EventArgs& args)
{
	UNUSED(args);
	if (g_theDevConsole)
	{
		g_theDevConsole->AddLine(INFO_MAJOR, "Register Command");
		Strings eventsList = g_theEventSystem->GetAllRegisteredCommands();
		for (std::string event : eventsList)
		{
			g_theDevConsole->AddLine(INFO_MINOR, event);
		}
	}
	return false;
}

bool DevConsole::Command_Warning(EventArgs& args)
{
	UNUSED(args);
	if (g_theDevConsole)
	{
		g_theDevConsole->AddLine(g_theDevConsole->ERRORLINE, "Unknown Command: " + args.GetValue("warningEvent", "this is a warning event."));
	}
	return false;
}
