#include "InputSystem.hpp"
#include "XboxController.hpp"
#include "Engine/Math/MathUtils.hpp"
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Window/Window.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "ThirdParty/ImGui/imgui.h"

extern InputSystem* g_theInput;
extern Window* g_theWindow;

unsigned char const KEYCODE_F1 = VK_F1;
unsigned char const KEYCODE_F2 = VK_F2;
unsigned char const KEYCODE_F3 = VK_F3;
unsigned char const KEYCODE_F4 = VK_F4;
unsigned char const KEYCODE_F5 = VK_F5;
unsigned char const KEYCODE_F6 = VK_F6;
unsigned char const KEYCODE_F7 = VK_F7;
unsigned char const KEYCODE_F8 = VK_F8;
unsigned char const KEYCODE_F9 = VK_F9;
unsigned char const KEYCODE_F10 = VK_F10;
unsigned char const KEYCODE_F11 = VK_F11;
unsigned char const KEYCODE_ESC = VK_ESCAPE;
unsigned char const KEYCODE_UPARROW = VK_UP;
unsigned char const KEYCODE_DOWNARROW = VK_DOWN;
unsigned char const KEYCODE_LEFTARROW = VK_LEFT;
unsigned char const KEYCODE_RIGHTARROW = VK_RIGHT;
unsigned char const KEYCODE_LEFT_MOUSE = VK_LBUTTON;
unsigned char const KEYCODE_RIGHT_MOUSE = VK_RBUTTON;
unsigned char const KEYCODE_TILDE = 0xC0;
unsigned char const KEYCODE_LEFTBRACKET = 0xDB;
unsigned char const KEYCODE_RIGHTBRACKET = 0xDD;
unsigned char const KEYCODE_LEFTCONTROL = VK_CONTROL;
unsigned char const KEYCODE_RIGHTCONTROL = VK_CONTROL;
unsigned char const KEYCODE_ENTER = VK_RETURN;
unsigned char const KEYCODE_BACKSPACE = VK_BACK;
unsigned char const KEYCODE_TAB = VK_TAB;
unsigned char const KEYCODE_SPACE = VK_SPACE;
unsigned char const KEYCODE_INSERT = VK_INSERT;
unsigned char const KEYCODE_DELETE = VK_DELETE;
unsigned char const KEYCODE_HOME = VK_HOME;
unsigned char const KEYCODE_END = VK_END;
unsigned char const KEYCODE_SHIFT = VK_SHIFT;

InputSystem::InputSystem(InputSystemConfig config)
	:m_config(config)
{

}

InputSystem::~InputSystem()
{
}

void InputSystem::Startup()
{
	for (int controllerID = 0; controllerID < NUM_XBOX_CONTROLLERS; ++controllerID)
	{
		XboxController& controller = m_controllers[controllerID];
		if (controller.IsConnected())
		{
			// Update every button's state
			for (int buttonIndex = 0; buttonIndex < (int)XboxButtonID::NUM; ++buttonIndex)
			{
				KeyButtonState& buttonState = controller.m_buttons[buttonIndex];
				buttonState.m_isPressed = false;
				buttonState.m_wasPressedLastFrame = false;
			}
		}
	}
	g_theEventSystem->SubscribeEventCallBackFunction("KeyPressed", OnKeyPressed);
	g_theEventSystem->SubscribeEventCallBackFunction("KeyReleased", OnKeyReleased);
}

void InputSystem::Shutdown()
{
}

void InputSystem::BeginFrame()
{
	m_cursorState.m_mouseWheelDelta = 0.f;
	
	CURSORINFO cursorInfo = {}; // 检查当前光标状态
	cursorInfo.cbSize = sizeof(CURSORINFO);
	GetCursorInfo(&cursorInfo);

	bool isCursorCurrentlyVisible = (cursorInfo.flags & CURSOR_SHOWING) != 0; // Windows 认为光标可见

	if (m_isCursorVisible != isCursorCurrentlyVisible)
	{
		if (m_isCursorVisible)
		{
			while (ShowCursor(TRUE) < 0) {}
		}
		else
		{
			while (ShowCursor(FALSE) >= 0) {}
		}
	}

	m_prevCursorClientPosition = GetCursorClientPosition();
	POINT currentMousePos;
	GetCursorPos(&currentMousePos);           
	ScreenToClient((HWND)g_theWindow->GetHwnd(), &currentMousePos); 

	IntVec2 currentPos = IntVec2(currentMousePos.x, currentMousePos.y);

	if (m_cursorState.m_cursorMode == CursorMode::FPS)
	{
		// **计算鼠标增量（delta）**
		m_cursorState.m_cursorClientDelta = currentPos - IntVec2((int)m_prevCursorClientPosition.x, (int)m_prevCursorClientPosition.y);

		// **获取窗口中心**
		IntVec2 dimensions = g_theWindow->GetClientDimensions();
		IntVec2 clientCenter = IntVec2(RoundDownToInt(dimensions.x / 2.f), RoundDownToInt(dimensions.y / 2.f));

		// **将鼠标重置到窗口中心**
		POINT centerPoint = { clientCenter.x, clientCenter.y };
		ClientToScreen((HWND)g_theWindow->GetHwnd(), &centerPoint);
		SetCursorPos(centerPoint.x, centerPoint.y);

		// **更新当前鼠标位置**
		m_cursorState.m_cursorClientPosition = clientCenter;
	}

	if (m_cursorState.m_cursorMode == CursorMode::POINTER)
	{
		m_cursorState.m_cursorClientDelta = IntVec2(0, 0);
		m_cursorState.m_cursorClientPosition = IntVec2(currentMousePos.x, currentMousePos.y);
	}

    for (int controllerID = 0; controllerID < NUM_XBOX_CONTROLLERS; ++controllerID)
    {
        m_controllers[controllerID].Update();
    }
}

void InputSystem::EndFrame()
{
	//Update every controller's state, set "if is pressed in the last frame
	for (int controllerID = 0; controllerID < NUM_XBOX_CONTROLLERS; ++controllerID)
	{
		XboxController& controller = m_controllers[controllerID];
		controller.m_id = controllerID;
		if (controller.IsConnected())
		{
			// Update every button's state
			for (int buttonIndex = 0; buttonIndex < (int)XboxButtonID::NUM; ++buttonIndex)
			{
				KeyButtonState& buttonState = controller.m_buttons[buttonIndex];
				buttonState.EndFrame();
			}
		}
	}

	// Update key states
	for (int keyIndex = 0; keyIndex < NUM_KEYCODES; ++keyIndex)
	{
		m_keystates[keyIndex].EndFrame();
	}
}

bool InputSystem::IsKeyDown(unsigned char keyCode)
{
    return m_keystates[keyCode].IsPressed(); // check in this frame if it's pressed
}

bool InputSystem::WasKeyJustPressed(unsigned char keyCode)
{
    return m_keystates[keyCode].WasJustPressed(); // check in this frame if it's just pressed
}

bool InputSystem::WasKeyJustReleased(unsigned char keyCode)
{
    return m_keystates[keyCode].WasJustReleased(); // check in this frame if it's just released
}

void InputSystem::HandleKeyPressed(unsigned char keyCode)
{
    m_keystates[keyCode].UpdateStatus(true); // update the status as down
}

void InputSystem::HandleKeyReleased(unsigned char keyCode)
{
    m_keystates[keyCode].UpdateStatus(false); // update the status as up
}

XboxController const& InputSystem::GetController(int controllerID)
{
    return m_controllers[controllerID]; // return to the specific controller
}

bool InputSystem::OnKeyPressed(EventArgs& eventArgs)
{
	if (g_theInput)
	{
		unsigned char keyCode = (unsigned char)eventArgs.GetValue("KeyCode", -1);
		g_theInput->HandleKeyPressed(keyCode);
		return true;
	}
	return false;
}

bool InputSystem::OnKeyReleased(EventArgs& eventArgs)
{
	if (g_theInput)
	{
		unsigned char keyCode = (unsigned char)eventArgs.GetValue("KeyCode", -1);
		g_theInput->HandleKeyReleased(keyCode);
		return true;
	}
	return false;
}

void InputSystem::SetCursorMode(CursorMode cursorMode)
{
	m_cursorState.m_cursorMode = cursorMode;
	
	if (cursorMode == CursorMode::FPS)
	{
		m_isCursorVisible = false;
		/*while (ShowCursor(FALSE) >= 0) {}

		IntVec2 dimensions = g_theWindow->GetClientDimensions();
		m_cursorState.m_cursorClientPosition = IntVec2(RoundDownToInt(dimensions.x / 2.f), RoundDownToInt(dimensions.y / 2.f));
		m_cursorState.m_cursorClientDelta = IntVec2(0, 0);*/
	}
	else
	{
		m_isCursorVisible = true;
		//while (ShowCursor(TRUE) < 0) {} 

		//ClipCursor(nullptr); //可以拿到window的RECT然后限制在这里
	}
}

Vec2 InputSystem::GetCursorClientDelta() const
{
	return Vec2((float)m_cursorState.m_cursorClientDelta.x, (float)m_cursorState.m_cursorClientDelta.y);
}

Vec2 InputSystem::GetCursorClientPosition() const
{
	return Vec2((float)m_cursorState.m_cursorClientPosition.x, (float)m_cursorState.m_cursorClientPosition.y);
}

Vec2 InputSystem::GetCursorNormalizedPosition() const
{
	return g_theWindow->GetNormalizedMouseUV();
}

float InputSystem::GetMouseWheelDelta() const
{
	return m_cursorState.m_mouseWheelDelta;
}

void InputSystem::AddMouseWheelDelta(float delta)
{
	m_cursorState.m_mouseWheelDelta += delta;
}

bool InputSystem::ShouldIgnoreMouseInput() const
{
	if (ImGui::GetCurrentContext() == nullptr)
	{
		return false;
	}
	return ImGui::GetIO().WantCaptureMouse;
}

bool InputSystem::ShouldIgnoreKeyboardInput() const
{
	if (ImGui::GetCurrentContext() == nullptr)
	{
		return false;
	}
	return ImGui::GetIO().WantCaptureKeyboard;
}

