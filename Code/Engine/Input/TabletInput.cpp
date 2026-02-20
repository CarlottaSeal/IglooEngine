#include "Engine/Input/TabletInput.hpp"
#include "ThirdParty/Wintab/wintab.h"
#include <cmath>

// Convenience casts for function pointers
#define FN_WTINFO   reinterpret_cast<WTINFOA_FUNC>(m_fnWTInfoA)
#define FN_WTOPEN   reinterpret_cast<WTOPENA_FUNC>(m_fnWTOpenA)
#define FN_WTCLOSE  reinterpret_cast<WTCLOSE_FUNC>(m_fnWTClose)
#define FN_WTPACKET reinterpret_cast<WTPACKET_FUNC>(m_fnWTPacket)
#define FN_WTOVERLAP reinterpret_cast<WTOVERLAP_FUNC>(m_fnWTOverlap)
#define FN_WTENABLE reinterpret_cast<WTENABLE_FUNC>(m_fnWTEnable)

// ---------- WM_POINTER definitions (Windows 8+) ----------
#ifndef WM_POINTERUPDATE
#define WM_POINTERUPDATE  0x0245
#define WM_POINTERDOWN    0x0246
#define WM_POINTERUP      0x0247
#define WM_POINTERENTER   0x0249
#define WM_POINTERLEAVE   0x024A
#endif

#ifndef GET_POINTERID_WPARAM
#define GET_POINTERID_WPARAM(wParam) (LOWORD(wParam))
#endif

// Layout-compatible copies of POINTER_INFO / POINTER_PEN_INFO.
struct Fallback_POINTER_INFO
{
	DWORD    pointerType;
	UINT32   pointerId;
	UINT32   frameId;
	UINT32   pointerFlags;
	HANDLE   sourceDevice;
	HWND     hwndTarget;
	POINT    ptPixelLocation;
	POINT    ptHimetricLocation;
	POINT    ptPixelLocationRaw;
	POINT    ptHimetricLocationRaw;
	DWORD    dwTime;
	UINT32   historyCount;
	INT32    InputData;
	DWORD    dwKeyStates;
	UINT64   PerformanceCount;
	INT32    ButtonChangeType;
};

struct Fallback_POINTER_PEN_INFO
{
	Fallback_POINTER_INFO pointerInfo;
	UINT32   penFlags;
	UINT32   penMask;
	UINT32   pressure;   // 0 – 1024
	UINT32   rotation;
	INT32    tiltX;
	INT32    tiltY;
};

constexpr DWORD FALLBACK_PT_PEN = 3; // PT_PEN

typedef BOOL (WINAPI* PFN_GetPointerType)(UINT32 pointerId, DWORD* pointerType);
typedef BOOL (WINAPI* PFN_GetPointerPenInfo)(UINT32 pointerId, void* penInfo);

// -----------------------------------------------------------------

TabletInput::~TabletInput()
{
	Shutdown();
}

bool TabletInput::LoadWintabDLL()
{
	m_wintabDLL = LoadLibraryA("Wintab32.dll");
	if (!m_wintabDLL)
		return false;

	HMODULE dll = static_cast<HMODULE>(m_wintabDLL);
	m_fnWTInfoA   = reinterpret_cast<void*>(GetProcAddress(dll, "WTInfoA"));
	m_fnWTOpenA   = reinterpret_cast<void*>(GetProcAddress(dll, "WTOpenA"));
	m_fnWTClose   = reinterpret_cast<void*>(GetProcAddress(dll, "WTClose"));
	m_fnWTPacket  = reinterpret_cast<void*>(GetProcAddress(dll, "WTPacket"));
	m_fnWTOverlap = reinterpret_cast<void*>(GetProcAddress(dll, "WTOverlap"));
	m_fnWTEnable  = reinterpret_cast<void*>(GetProcAddress(dll, "WTEnable"));

	if (!m_fnWTInfoA || !m_fnWTOpenA || !m_fnWTClose || !m_fnWTPacket)
	{
		UnloadWintabDLL();
		return false;
	}

	return true;
}

void TabletInput::UnloadWintabDLL()
{
	if (m_wintabDLL)
	{
		FreeLibrary(static_cast<HMODULE>(m_wintabDLL));
		m_wintabDLL = nullptr;
	}
	m_fnWTInfoA   = nullptr;
	m_fnWTOpenA   = nullptr;
	m_fnWTClose   = nullptr;
	m_fnWTPacket  = nullptr;
	m_fnWTOverlap = nullptr;
	m_fnWTEnable  = nullptr;
}

bool TabletInput::Startup(void* hwnd)
{
	// ---- Always load WM_POINTER functions (needed even if Wintab opens) ----
	HMODULE user32 = GetModuleHandleA("user32.dll");
	if (user32)
	{
		m_fnGetPointerType    = reinterpret_cast<void*>(GetProcAddress(user32, "GetPointerType"));
		m_fnGetPointerPenInfo = reinterpret_cast<void*>(GetProcAddress(user32, "GetPointerPenInfo"));
	}

	// ---- Try Wintab ----
	if (LoadWintabDLL())
	{
		char wintabName[256] = {};
		if (FN_WTINFO(WTI_INTERFACE, IFC_WINTABID, wintabName))
		{
			AXIS pressureAxis = {};
			if (FN_WTINFO(WTI_DEVICES, DVC_NPRESSURE, &pressureAxis))
			{
				m_state.m_maxPressure = pressureAxis.axMax;
			}
			if (m_state.m_maxPressure <= 0)
			{
				m_state.m_maxPressure = 1;
			}

			LOGCONTEXTA logContext = {};
			if (FN_WTINFO(WTI_DEFSYSCTX, 0, &logContext))
			{
				logContext.lcPktData  = WINTAB_PACKETDATA;
				logContext.lcPktMode  = 0;
				logContext.lcOptions |= CXO_MESSAGES;
				logContext.lcMoveMask = WINTAB_PACKETDATA;

				logContext.lcOutOrgX = GetSystemMetrics(SM_XVIRTUALSCREEN);
				logContext.lcOutOrgY = GetSystemMetrics(SM_YVIRTUALSCREEN);
				logContext.lcOutExtX = GetSystemMetrics(SM_CXVIRTUALSCREEN);
				logContext.lcOutExtY = -GetSystemMetrics(SM_CYVIRTUALSCREEN);

				m_hCtx = FN_WTOPEN(static_cast<HWND>(hwnd), &logContext, TRUE);
				if (m_hCtx)
				{
					if (m_fnWTOverlap)
					{
						FN_WTOVERLAP(static_cast<HCTX>(m_hCtx), TRUE);
					}
					m_state.m_isConnected = true;
					// Don't return yet — we also want WM_POINTER as backup
				}
			}
		}

		if (!m_state.m_isConnected)
		{
			UnloadWintabDLL();
		}
	}

	// If WM_POINTER is available, prefer it over Wintab.
	// An open Wintab context suppresses WM_POINTER messages on many drivers,
	// so we must close it first.
	if (m_fnGetPointerType && m_fnGetPointerPenInfo)
	{
		// Close Wintab context so it stops intercepting pen input
		if (m_hCtx && m_fnWTClose)
		{
			FN_WTCLOSE(static_cast<HCTX>(m_hCtx));
			m_hCtx = nullptr;
		}
		UnloadWintabDLL();

		m_useWMPointer = true;
		m_state.m_isConnected = true;
	}

	return m_state.m_isConnected;
}

void TabletInput::Shutdown()
{
	if (m_hCtx && m_fnWTClose)
	{
		FN_WTCLOSE(static_cast<HCTX>(m_hCtx));
		m_hCtx = nullptr;
	}
	UnloadWintabDLL();
	m_state = TabletState();
	m_useWMPointer = false;
	m_fnGetPointerType = nullptr;
	m_fnGetPointerPenInfo = nullptr;
}

bool TabletInput::HandleMessage(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam)
{
	(void)hwnd;

	// ---- Wintab path ----
	// Skip Wintab pressure if WM_POINTER is available (Wintab often gives
	// garbage values when the driver is in Windows Ink mode).
	if (msg == WT_PACKET)
	{
		if (!m_useWMPointer && m_fnWTPacket && m_hCtx)
		{
			WINTAB_PACKET pkt = {};
			if (FN_WTPACKET(static_cast<HCTX>(m_hCtx), static_cast<UINT>(wParam), &pkt))
			{
				m_state.m_tabletX = pkt.pkX;
				m_state.m_tabletY = pkt.pkY;

				float rawNormalized = static_cast<float>(pkt.pkNormalPressure) / static_cast<float>(m_state.m_maxPressure);
				if (rawNormalized > 1.0f) rawNormalized = 1.0f;
				m_state.m_rawPressure = rawNormalized;
				m_state.m_pressure = powf(rawNormalized, m_pressureCurvePower);
				m_state.m_isPenInRange = true;
			}
		}
		return true; // always consume WT_PACKET
	}

	if (msg == WT_PROXIMITY)
	{
		if (!m_useWMPointer)
		{
			m_state.m_isPenInRange = (LOWORD(lParam) != 0);
			if (!m_state.m_isPenInRange)
			{
				m_state.m_pressure = 0.f;
				m_state.m_rawPressure = 0.f;
			}
		}
		return true; // always consume WT_PROXIMITY
	}

	// ---- WM_POINTER path (Windows Ink) ----
	// IMPORTANT: return false so Windows still generates WM_MOUSE from these.
	if (m_useWMPointer)
	{
		if (msg == WM_POINTERUPDATE || msg == WM_POINTERDOWN)
		{
			UINT32 pointerId = GET_POINTERID_WPARAM(wParam);

			DWORD pointerType = 0;
			auto fnType = reinterpret_cast<PFN_GetPointerType>(m_fnGetPointerType);
			if (fnType(pointerId, &pointerType) && pointerType == FALLBACK_PT_PEN)
			{
				Fallback_POINTER_PEN_INFO penInfo = {};
				auto fnPen = reinterpret_cast<PFN_GetPointerPenInfo>(m_fnGetPointerPenInfo);
				if (fnPen(pointerId, &penInfo))
				{
					m_state.m_isPenInRange = true;
					float raw = static_cast<float>(penInfo.pressure) / 1024.0f;
					if (raw > m_observedMax) m_observedMax = raw;
					m_state.m_rawPressure = raw;

					// Normalize by user-adjustable max, then apply curve
					float normalized = raw / m_pressureNormMax;
					if (normalized > 1.0f) normalized = 1.0f;
					m_state.m_pressure = powf(normalized, m_pressureCurvePower);
				}
			}
			return false; // let DefWindowProc generate WM_MOUSE
		}

		if (msg == WM_POINTERUP)
		{
			UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
			DWORD pointerType = 0;
			auto fnType = reinterpret_cast<PFN_GetPointerType>(m_fnGetPointerType);
			if (fnType(pointerId, &pointerType) && pointerType == FALLBACK_PT_PEN)
			{
				m_state.m_pressure = 0.f;
				m_state.m_rawPressure = 0.f;
			}
			return false; // let DefWindowProc generate WM_MOUSE
		}

		if (msg == WM_POINTERLEAVE)
		{
			m_state.m_isPenInRange = false;
			m_state.m_pressure = 0.f;
			m_state.m_rawPressure = 0.f;
			return false;
		}
	}

	return false;
}
