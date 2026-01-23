#include "Engine/Window/Window.hpp"
#include "Engine/Input/InputSystem.hpp"

#include "ThirdParty/ImGui/imgui.h"
#include "ThirdParty/ImGui/imgui_impl_win32.h"
#include "ThirdParty/ImGui/imgui_impl_dx12.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Core/EngineCommon.hpp"

Window* Window::s_mainWindow;

LRESULT CALLBACK WindowsMessageHandlingProcedure(HWND windowHandle, UINT wmMessageCode, WPARAM wParam, LPARAM lParam)
{
	InputSystem* input = Window::s_mainWindow->m_config.m_inputSystem;

	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	if (ImGui_ImplWin32_WndProcHandler(windowHandle, wmMessageCode, wParam, lParam))
	{
		return true;
	}

	switch (wmMessageCode)
	{
	case WM_CLOSE:
	{
		//g_theApp->HandleQuitRequested();
		g_theEventSystem->FireEvent("quit");
		//ERROR_AND_DIE("ClosingTheApp");
		//return 0; // "Consumes" this message (tells Windows "okay, we handled it")
	}

	case WM_CHAR:
	{
		if (input && !input->ShouldIgnoreKeyboardInput())
		{
			EventArgs args;
			args.SetValue("CharCode", Stringf("%d", (unsigned char)wParam));
			FireEvent("CharInput", args);
		}
		break;
	}
	// Raw physical keyboard "key-was-just-depressed" event (case-insensitive, not translated)
	case WM_KEYDOWN:
	{
		if (input && !input->ShouldIgnoreKeyboardInput())
		{
			unsigned char asKey = (unsigned char)wParam;
			EventArgs args;
			args.SetValue("KeyCode", Stringf("%d", asKey));
			FireEvent("KeyPressed", args);
		}
		break;
	}
	// Raw physical keyboard "key-was-just-released" event (case-insensitive, not translated)
	case WM_KEYUP:
	{
		if (input && !input->ShouldIgnoreKeyboardInput())
		{
			unsigned char asKey = (unsigned char)wParam;
			EventArgs args;
			args.SetValue("KeyCode", Stringf("%d", asKey));
			FireEvent("KeyReleased", args);
		}
		break;
	}
	case WM_LBUTTONDOWN:
	{
		if (input && !input->ShouldIgnoreMouseInput())
		{
			input->HandleKeyPressed(KEYCODE_LEFT_MOUSE);
		}
		return 0;
	}

	case WM_LBUTTONUP:
	{
		if (input && !input->ShouldIgnoreMouseInput())
		{
			input->HandleKeyReleased(KEYCODE_LEFT_MOUSE);
		}
		return 0;
	}

	case WM_RBUTTONDOWN:
	{
		if (input && !input->ShouldIgnoreMouseInput())
		{
			input->HandleKeyPressed(KEYCODE_RIGHT_MOUSE);
		}
		return 0;
	}

	case WM_RBUTTONUP:
	{
		if (input && !input->ShouldIgnoreMouseInput())
		{
			input->HandleKeyReleased(KEYCODE_RIGHT_MOUSE);
		}
		return 0;
	}
	case WM_MOUSEWHEEL:
		{
			if (input)
			{
				// ImGui 会处理它需要的滚轮输入
				// 但我们仍然可以记录滚轮 delta 供游戏使用
				if (!input->ShouldIgnoreMouseInput())
				{
					float wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
					input->AddMouseWheelDelta(wheelDelta);
				}
			}
			return 0;
		}
		//Alt+Enter
	case WM_SYSKEYDOWN:
		{
			if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
			{
				if (Window::s_mainWindow)
				{
					Window::s_mainWindow->ToggleFullscreen();
				}
				return 0;
			}
			break;
		}
	}
	// Send back to Windows any unhandled/unconsumed messages we want other apps to see (e.g. play/pause in music apps, etc.)
	return DefWindowProc(windowHandle, wmMessageCode, wParam, lParam);
}

Window::Window(WindowConfig const& config)
	:m_config(config)
{
	s_mainWindow = this;

	m_windowedPlacementData = new WINDOWPLACEMENT{ sizeof(WINDOWPLACEMENT) };
}

Window::~Window()
{
	if (m_windowedPlacementData)
	{
		delete static_cast<WINDOWPLACEMENT*>(m_windowedPlacementData);
		m_windowedPlacementData = nullptr;
	}
}

void Window::Startup()
{
	//CreateOSWindow(applicationInstanceHandle, m_config.m_aspectRatio);
	CreateOSWindow();
}

void Window::BeginFrame()
{
	RunMessagePump();
}

void Window::EndFrame()
{
}

void Window::Shutdown()
{
}

void Window::ToggleFullscreen()
{
}

WindowConfig const& Window::GetConfig() const
{
	return m_config;
}

void* Window::GetDisplayContext() const
{
	return m_displayContext;
}

void* Window::GetHwnd() const
{
	return m_windowHandle;
}

IntVec2 Window::GetClientDimensions() const
{
	if (m_windowHandle == nullptr) 
	{
		return IntVec2(0, 0); 
	}

	RECT clientRect;
	::GetClientRect(static_cast<HWND>(m_windowHandle), &clientRect);

	int width = clientRect.right - clientRect.left;
	int height = clientRect.bottom - clientRect.top;

	return IntVec2(width, height);
}

Vec2 Window::GetNormalizedMouseUV() const
{
	HWND windowHandle = static_cast<HWND>(m_windowHandle);
	POINT cursorCoords;
	RECT clientRect;
	::GetCursorPos(&cursorCoords);
	::ScreenToClient(windowHandle, &cursorCoords);
	::GetClientRect(windowHandle, &clientRect);
	float cursorX = static_cast<float>(cursorCoords.x)/static_cast<float>(clientRect.right);
	float cursorY = static_cast<float>(cursorCoords.y)/static_cast<float>(clientRect.bottom);
	return Vec2(cursorX, 1.f - cursorY);
}

float Window::GetCurrentAspectRatio() const
{
	return m_currentAspectRatio;
}

bool Window::WindowHasFocus() const
{
	return GetForegroundWindow() == GetHwnd();
}

void Window::RunMessagePump()
{
	MSG queuedMessage;
	for (;; )
	{
		BOOL const wasMessagePresent = PeekMessage(&queuedMessage, NULL, 0, 0, PM_REMOVE);
		if (!wasMessagePresent)
		{
			break;
		}

		TranslateMessage(&queuedMessage);
		DispatchMessage(&queuedMessage); // This tells Windows to call our "WindowsMessageHandlingProcedure" (a.k.a. "WinProc") function
	}
}

//void Window::CreateOSWindow(void* applicationInstanceHandle, float clientAspect)
void Window::CreateOSWindow()
{
	HMODULE applicationInstanceHandle = GetModuleHandle(NULL);
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// Define a window style/class
	WNDCLASSEX windowClassDescription;
	memset(&windowClassDescription, 0, sizeof(windowClassDescription));
	windowClassDescription.cbSize = sizeof(windowClassDescription);
	windowClassDescription.style = CS_OWNDC| CS_HREDRAW | CS_VREDRAW; // Redraw on move, request own Display Context; redraw
	windowClassDescription.lpfnWndProc = static_cast<WNDPROC>(WindowsMessageHandlingProcedure); // Register our Windows message-handling function
	windowClassDescription.hInstance = applicationInstanceHandle;
	windowClassDescription.hIcon = NULL;
	windowClassDescription.hCursor = NULL;
	windowClassDescription.lpszClassName = TEXT("Simple Window Class");
	RegisterClassEx(&windowClassDescription);
	
	// Get desktop rect, dimensions, aspect
	RECT desktopRect;
	HWND desktopWindowHandle = GetDesktopWindow();
	GetClientRect(desktopWindowHandle, &desktopRect);
	float desktopWidth = (float)(desktopRect.right - desktopRect.left);
	float desktopHeight = (float)(desktopRect.bottom - desktopRect.top);
	float desktopAspect = desktopWidth / desktopHeight;
	
	// DWORD const windowStyleFlags = WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_OVERLAPPED;
	// DWORD const windowStyleExFlags = WS_EX_APPWINDOW;
	DWORD windowStyleFlags;
	DWORD windowStyleExFlags;
	RECT windowRect;

	m_isCurrentlyFullscreen = m_config.m_isFullscreen; //store the initial fullScreen or not

	if (m_config.m_isFullscreen)
	{
		windowStyleFlags = WS_POPUP | WS_VISIBLE;
		windowStyleExFlags = WS_EX_APPWINDOW;
        
		windowRect.left = 0;
		windowRect.top = 0;
		windowRect.right = (int)desktopWidth;
		windowRect.bottom = (int)desktopHeight;
        
		m_currentAspectRatio = desktopWidth / desktopHeight;

		// // 保存窗口模式的默认位置（居中）
		// WINDOWPLACEMENT* wp = static_cast<WINDOWPLACEMENT*>(m_windowedPlacementData);
		// wp->rcNormalPosition.left = (int)((desktopWidth - m_config.m_windowedWidth) * 0.5f);
		// wp->rcNormalPosition.top = (int)((desktopHeight - m_config.m_windowedHeight) * 0.5f);
		// wp->rcNormalPosition.right = wp->rcNormalPosition.left + m_config.m_windowedWidth;
		// wp->rcNormalPosition.bottom = wp->rcNormalPosition.top + m_config.m_windowedHeight;
	}
	else
	{
		// raise flags
		windowStyleFlags = WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_OVERLAPPED; //cannot resize or minimize/maximize window
		//windowStyleFlags = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
		windowStyleExFlags = WS_EX_APPWINDOW;

		// Calculate maximum client size (as some % of desktop size)
		constexpr float maxClientFractionOfDesktop = 0.90f;
		float clientWidth = desktopWidth * maxClientFractionOfDesktop;
		float clientHeight = desktopHeight * maxClientFractionOfDesktop;
		float clientAspect = m_config.m_aspectRatio;

		if (clientAspect > desktopAspect) // Client window has a wider aspect than desktop; shrink client height to match its width
		{
			clientHeight = clientWidth / clientAspect;
		}
		else // Client window has a taller aspect than desktop; shrink client width to match its height
		{
			clientWidth = clientHeight * clientAspect;
		}

		// Calculate client rect bounds by centering the client area
		float clientMarginX = 0.5f * (desktopWidth - clientWidth);
		float clientMarginY = 0.5f * (desktopHeight - clientHeight);
		RECT clientRect;
		clientRect.left = (int)clientMarginX;
		clientRect.right = clientRect.left + (int)clientWidth;
		clientRect.top = (int)clientMarginY;
		clientRect.bottom = clientRect.top + (int)clientHeight;

		// Calculate the outer dimensions of the physical window, including frame et. al.
		windowRect = clientRect;
		AdjustWindowRectEx(&windowRect, windowStyleFlags, FALSE, windowStyleExFlags);
        
		m_currentAspectRatio = m_config.m_aspectRatio;
	}

	WCHAR windowTitle[1024];
	MultiByteToWideChar(GetACP(), 0, m_config.m_windowTitle.c_str(), -1, windowTitle, sizeof(windowTitle) / sizeof(windowTitle[0]));
	m_windowHandle = CreateWindowEx(
		windowStyleExFlags,
		windowClassDescription.lpszClassName,
		windowTitle,
		windowStyleFlags,
		windowRect.left,
		windowRect.top,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		NULL,
		NULL,
		applicationInstanceHandle,
		NULL);

	ShowWindow(HWND(m_windowHandle), SW_SHOW);
	SetForegroundWindow(HWND(m_windowHandle));
	SetFocus(HWND(m_windowHandle));

	m_displayContext = GetDC(HWND(m_windowHandle));

	HCURSOR cursor = LoadCursor(NULL, IDC_ARROW);
	SetCursor(cursor);
}