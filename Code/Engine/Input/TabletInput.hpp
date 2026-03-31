#pragma once

struct TabletState
{
	float m_pressure      = 0.f;  // normalized 0.0 ~ 1.0
	float m_rawPressure   = 0.f;  // raw value before curve
	int   m_tabletX       = 0;    // tablet coordinate X
	int   m_tabletY       = 0;    // tablet coordinate Y
	bool  m_isConnected   = false;
	bool  m_isPenInRange  = false; // pen hovering over tablet
	int   m_maxPressure   = 1;    // max raw pressure value from device
};

class TabletInput
{
public:
	TabletInput() = default;
	~TabletInput();

	// Call after window is created. Returns true if tablet was found.
	bool Startup(void* hwnd);
	void Shutdown();

	// Called from Window message handler
	bool HandleMessage(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam);

	// Accessors
	float GetPressure() const          { return m_state.m_pressure; }
	float GetRawPressure() const       { return m_state.m_rawPressure; }
	bool  IsConnected() const          { return m_state.m_isConnected; }
	bool  IsPenInRange() const         { return m_state.m_isPenInRange; }
	int   GetMaxPressure() const       { return m_state.m_maxPressure; }
	TabletState const& GetState() const { return m_state; }

	// Pressure curve: adjustedPressure = pow(raw, curvePower)
	// < 1.0 = more sensitive (soft touch), > 1.0 = harder
	void  SetPressureCurve(float power) { m_pressureCurvePower = power; }
	float GetPressureCurve() const      { return m_pressureCurvePower; }

	// Pressure normalization: raw hardware value is divided by this before curve.
	// Default 1.0 = hardware reports 0-1.  Lower if your pen can't reach 100%.
	void  SetPressureMax(float max)     { if (max > 0.01f) m_pressureNormMax = max; }
	float GetPressureMax() const        { return m_pressureNormMax; }
	float GetObservedMax() const        { return m_observedMax; }
	void  ResetObservedMax()            { m_observedMax = 0.0f; }

private:
	bool LoadWintabDLL();
	void UnloadWintabDLL();

	TabletState m_state;
	float       m_pressureCurvePower = 1.0f; // linear by default
	float       m_pressureNormMax    = 1.0f; // raw values are divided by this
	float       m_observedMax        = 0.0f; // highest raw value seen so far

	// Wintab internals (opaque to avoid leaking wintab.h)
	void*       m_wintabDLL   = nullptr; // HMODULE
	void*       m_hCtx        = nullptr; // HCTX

	// Function pointers (stored as void*, cast internally)
	void*       m_fnWTInfoA   = nullptr;
	void*       m_fnWTOpenA   = nullptr;
	void*       m_fnWTClose   = nullptr;
	void*       m_fnWTPacket  = nullptr;
	void*       m_fnWTOverlap = nullptr;
	void*       m_fnWTEnable  = nullptr;

	// WM_POINTER fallback (Windows Ink, for tablets without Wintab)
	bool        m_useWMPointer = false;
	void*       m_fnGetPointerType    = nullptr;
	void*       m_fnGetPointerPenInfo = nullptr;
};
