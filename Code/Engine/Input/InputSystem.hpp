#pragma once
#include "Engine/Input/XboxController.hpp"
#include "Engine/Input/KeyButtonState.hpp"
#include "Engine/Core/EngineCommon.hpp"

extern unsigned char const KEYCODE_F1;
extern unsigned char const KEYCODE_F2;
extern unsigned char const KEYCODE_F3;
extern unsigned char const KEYCODE_F4;
extern unsigned char const KEYCODE_F5;
extern unsigned char const KEYCODE_F6;
extern unsigned char const KEYCODE_F7;
extern unsigned char const KEYCODE_F8;
extern unsigned char const KEYCODE_F9;
extern unsigned char const KEYCODE_F10;
extern unsigned char const KEYCODE_F11;
extern unsigned char const KEYCODE_ESC;
extern unsigned char const KEYCODE_UPARROW;
extern unsigned char const KEYCODE_DOWNARROW;
extern unsigned char const KEYCODE_LEFTARROW;
extern unsigned char const KEYCODE_RIGHTARROW;
extern unsigned char const KEYCODE_LEFT_MOUSE;
extern unsigned char const KEYCODE_RIGHT_MOUSE;
extern unsigned char const KEYCODE_TILDE;
extern unsigned char const KEYCODE_LEFTBRACKET;
extern unsigned char const KEYCODE_RIGHTBRACKET;
extern unsigned char const  KEYCODE_LEFTCONTROL;
extern unsigned char const KEYCODE_RIGHTCONTROL;
extern unsigned char const KEYCODE_ENTER;
extern unsigned char const KEYCODE_BACKSPACE;
extern unsigned char const KEYCODE_TAB;
extern unsigned char const KEYCODE_SPACE;
extern unsigned char const KEYCODE_INSERT;
extern unsigned char const KEYCODE_DELETE;
extern unsigned char const KEYCODE_HOME;
extern unsigned char const KEYCODE_END;
extern unsigned char const KEYCODE_SHIFT;

constexpr int NUM_KEYCODES = 256;
constexpr int NUM_XBOX_CONTROLLERS = 4;

struct InputSystemConfig
{
};

enum class CursorMode
{
	POINTER,
	FPS,
};

struct CursorState
{
	IntVec2 m_cursorClientDelta;
	IntVec2 m_cursorClientPosition;
	float      m_mouseWheelDelta      = 0.f; 
	CursorMode m_cursorMode = CursorMode::POINTER;
};

class InputSystem
{
public:
	InputSystem(InputSystemConfig config);
	~InputSystem();
	void Startup();
	void Shutdown();
	void BeginFrame();
	void EndFrame();
	bool WasKeyJustPressed(unsigned char keyCode);
	bool WasKeyJustReleased(unsigned char keyCode);
	bool IsKeyDown(unsigned char keyCode);
	void HandleKeyPressed(unsigned char keyCode);
	void HandleKeyReleased(unsigned char keyCode);
	XboxController const& GetController(int controllerID);

	static bool OnKeyPressed(EventArgs& eventArgs);
	static bool OnKeyReleased(EventArgs& eventArgs);

	//In pointer mode, the cursor should be visible, freely able to move, and not locked to the 
	//window. In FPS mode, the cursor should be hidden, reset to the center of the window each
	// frame, and record the delta each frame
	void SetCursorMode(CursorMode cursorMode);

	//Returns the current frame cursor delta in pixels, relative to the client region. This is
	//how much the cursor moved last frame before it was reset to the center of the screen. Only
	//valid in FPS mode, will be zero otherwise.
	Vec2 GetCursorClientDelta() const;

	//Returns the cursor position, in pixels relative to the client region.
	Vec2 GetCursorClientPosition() const;

	//Returns the cursor position, normalized to the range[0,1], relative to the client region,
	//with the y-axis inverted to map from Windows conventions to game screen camera conventions
	Vec2 GetCursorNormalizedPosition() const;

	float GetMouseWheelDelta() const;
	void AddMouseWheelDelta(float delta);   // 从 Window/消息回调里调用

	bool ShouldIgnoreMouseInput() const;
	bool ShouldIgnoreKeyboardInput() const;
	
public:
	CursorState m_cursorState;
	bool m_isCursorVisible = false;
	Vec2 m_prevCursorClientPosition;
	
protected:
	KeyButtonState m_keystates[NUM_KEYCODES];
	XboxController m_controllers[NUM_XBOX_CONTROLLERS];
	InputSystemConfig m_config;
};
