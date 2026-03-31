#pragma once
// Minimal Wintab API definitions for dynamic loading.
// Based on the official Wintab 1.4 specification.
// Only the subset needed for pressure-sensitive pen input is defined here.

#include <windows.h>

// ---------- Basic types ----------
typedef UINT WTPKT;
typedef HANDLE HCTX;

// ---------- WTInfo categories ----------
#define WTI_INTERFACE    1
#define WTI_DEFSYSCTX    4
#define WTI_DEVICES      100

// WTI_INTERFACE fields
#define IFC_WINTABID     1
#define IFC_SPECVERSION  2

// WTI_DEVICES fields
#define DVC_NPRESSURE    18

// ---------- Context option flags ----------
#define CXO_SYSTEM       0x0001
#define CXO_PEN          0x0002
#define CXO_MESSAGES     0x0004

// ---------- Packet data bits ----------
#define PK_CONTEXT       0x0001
#define PK_STATUS        0x0002
#define PK_TIME          0x0004
#define PK_CHANGED       0x0008
#define PK_SERIAL_NUMBER 0x0010
#define PK_CURSOR        0x0020
#define PK_BUTTONS       0x0040
#define PK_X             0x0080
#define PK_Y             0x0100
#define PK_Z             0x0200
#define PK_NORMAL_PRESSURE 0x0400
#define PK_TANGENT_PRESSURE 0x0800
#define PK_ORIENTATION   0x1000

// ---------- Window messages ----------
#define WT_DEFBASE       0x7FF0
#define WT_PACKET        (WT_DEFBASE + 0)
#define WT_PROXIMITY     (WT_DEFBASE + 5)

// ---------- Axis structure ----------
typedef struct tagAXIS
{
	LONG axMin;
	LONG axMax;
	UINT axUnits;
	LONG axResolution; // FIX32
} AXIS;

// ---------- Logical context ----------
typedef struct tagLOGCONTEXTA
{
	char   lcName[40];
	UINT   lcOptions;
	UINT   lcStatus;
	UINT   lcLocks;
	UINT   lcMsgBase;
	UINT   lcDevice;
	UINT   lcPktRate;
	WTPKT  lcPktData;
	WTPKT  lcPktMode;
	WTPKT  lcMoveMask;
	DWORD  lcBtnDnMask;
	DWORD  lcBtnUpMask;
	LONG   lcInOrgX;
	LONG   lcInOrgY;
	LONG   lcInOrgZ;
	LONG   lcInExtX;
	LONG   lcInExtY;
	LONG   lcInExtZ;
	LONG   lcOutOrgX;
	LONG   lcOutOrgY;
	LONG   lcOutOrgZ;
	LONG   lcOutExtX;
	LONG   lcOutExtY;
	LONG   lcOutExtZ;
	LONG   lcSensX; // FIX32
	LONG   lcSensY; // FIX32
	LONG   lcSensZ; // FIX32
	BOOL   lcSysMode;
	int    lcSysOrgX;
	int    lcSysOrgY;
	int    lcSysExtX;
	int    lcSysExtY;
	LONG   lcSysSensX; // FIX32
	LONG   lcSysSensY; // FIX32
} LOGCONTEXTA;

// ---------- Our custom packet structure ----------
// Must match the PACKETDATA bits we request.
// Fields MUST be in the canonical Wintab order:
//   context, status, time, changed, serial, cursor, buttons, x, y, z, pressure...
// We only include the fields we set in PACKETDATA.
#define WINTAB_PACKETDATA (PK_X | PK_Y | PK_BUTTONS | PK_NORMAL_PRESSURE)

typedef struct tagWINTAB_PACKET
{
	LONG pkX;
	LONG pkY;
	DWORD pkButtons;
	UINT pkNormalPressure;
} WINTAB_PACKET;

// ---------- Function pointer typedefs ----------
typedef UINT  (WINAPI* WTINFOA_FUNC)(UINT wCategory, UINT nIndex, LPVOID lpOutput);
typedef HCTX  (WINAPI* WTOPENA_FUNC)(HWND hWnd, LOGCONTEXTA* lpLogCtx, BOOL fEnable);
typedef BOOL  (WINAPI* WTCLOSE_FUNC)(HCTX hCtx);
typedef BOOL  (WINAPI* WTPACKET_FUNC)(HCTX hCtx, UINT wSerial, LPVOID lpPkt);
typedef BOOL  (WINAPI* WTOVERLAP_FUNC)(HCTX hCtx, BOOL fToTop);
typedef BOOL  (WINAPI* WTENABLE_FUNC)(HCTX hCtx, BOOL fEnable);
