#pragma once
#include <Ks.h>
#include <KsProxy.h>		// For IKsControl
#include <vidcap.h>			// For IKsNodeControl

#include "ExtensionUnitDefines.h"


#define DEFAULT_MOTOR_INTERVAL_TIMER 70

/**
* CWebcamExtensionUnit encapsulates the USB extension unit capability of the device.
* (see https://msdn.microsoft.com/en-us/library/windows/hardware/ff568656(v=vs.85).aspx).
*/

class CWebcamController
{
public:
	// Camera settings structure
	struct CameraSettings {
		long brightness = 50;
		long contrast = 50;
		long hue = 0;
		long saturation = 50;
		long sharpness = 25;
		long gamma = 100;
		long whiteBalance = 5200;
		long backlightCompensation = 0;
		long gain = 0;
		long colorEnable = 1;
		long powerlineFrequency = 60;
		long exposure = 0;
		long focus = 100;
		bool autoExposure = true;
		bool autoWhiteBalance = true;
		bool autoFocus = true;
		bool rightLight = false;
	};

	CWebcamController(void);
	~CWebcamController(void);

	HRESULT OpenDevice(BSTR bstrDevicePath, DWORD wVID, DWORD wPID);
	void CloseDevice();
	HRESULT IsPeripheralPropertySetSupported();
	HRESULT GetProperty(LOGITECH_XU_PROPERTYSET lPropertySet, ULONG ulPropertyId, ULONG ulSize, VOID* pValue);
	HRESULT SetProperty(LOGITECH_XU_PROPERTYSET lPropertySet, ULONG ulPropertyId, ULONG ulSize, VOID* pValue);
	void	GetVidPid(DWORD& vid, DWORD& pid);

	int GetCurrentZoom();
	int Zoom(int direction);
	void MoveTilt(int yDirection);
	void MovePan(int xDirection);
	void Tilt(int yDirection);
	void Pan(int xDirection);

	void GotoHome();
	void SavePreset(int iNum);
	void GotoPreset(int iNum);

	static void ListDevices(CStringArray &aDevices);
	static const int NUM_PRESETS = 8;

	bool UseLogitechMotionControl() const	{ return m_bUseLogitechMotionControl; }
	void UseLogitechMotionControl(bool val) { m_bUseLogitechMotionControl = val; }

	int GetMotorIntervalTimer() const		{ return m_iMotorIntervalTimer;	}
	void SetMotorIntervalTimer(int val)		{ m_iMotorIntervalTimer = val;	}

	// Camera settings methods
	HRESULT GetVideoProcAmpProperty(long property, long* value, long* flags);
	HRESULT SetVideoProcAmpProperty(long property, long value, long flags);
	HRESULT GetVideoProcAmpRange(long property, long* min, long* max, long* step, long* default_val, long* flags);
	
	// High-level camera settings methods
	HRESULT GetBrightness(long* value);
	HRESULT SetBrightness(long value);
	HRESULT GetContrast(long* value);
	HRESULT SetContrast(long value);
	HRESULT GetHue(long* value);
	HRESULT SetHue(long value);
	HRESULT GetSaturation(long* value);
	HRESULT SetSaturation(long value);
	HRESULT GetSharpness(long* value);
	HRESULT SetSharpness(long value);
	HRESULT GetGamma(long* value);
	HRESULT SetGamma(long value);
	HRESULT GetWhiteBalance(long* value, bool* isAuto);
	HRESULT SetWhiteBalance(long value, bool isAuto);
	HRESULT GetBacklightCompensation(long* value);
	HRESULT SetBacklightCompensation(long value);
	HRESULT GetGain(long* value);
	HRESULT SetGain(long value);
	HRESULT GetPowerlineFrequency(long* value);
	HRESULT SetPowerlineFrequency(long value);
	
	// Extended camera control methods
	HRESULT GetExposure(long* value, bool* isAuto);
	HRESULT SetExposure(long value, bool isAuto);
	HRESULT GetFocus(long* value, bool* isAuto);
	HRESULT SetFocus(long value, bool isAuto);
	
	// Convenience methods
	CameraSettings GetAllCameraSettings();
	HRESULT SetCameraSettings(const CameraSettings& settings);
	HRESULT ResetCameraSettings();
	HRESULT RefreshCameraSettings();
	
private:
	bool DeviceMatches(CComPtr<IMoniker> pMoniker, BSTR devicePath, DWORD wVID, DWORD wPID);
	HRESULT OpenDevice(CComPtr<IMoniker> pMoniker);
	bool ParseDevicePath(const wchar_t* devicePath, DWORD& vid, DWORD& pid);
	HRESULT InitializeXUNodesArray(CComPtr<IKsControl> pKsControl);
	bool IsExtensionUnitSupported(CComPtr<IKsControl> pKsControl, const GUID& guidExtension, unsigned int nodeId);

private:
	CComPtr<IKsControl>			m_spKsControl;
	CComQIPtr<IAMCameraControl> m_spAMCameraControl;
	CComQIPtr<IAMVideoProcAmp>	m_spVideoProcAmp;
	CComQIPtr<IKsPropertySet>	m_spsPropertySet;
	CComQIPtr<ICameraControl>	m_spCameraControl;

	DWORD					m_dwXUDeviceInformationNodeId;
	DWORD					m_dwXUVideoPipeControlNodeId;
	DWORD					m_dwXUTestDebugNodeId;
	DWORD					m_dwXUPeripheralControlNodeId;
	DWORD                   m_dwVid;
	DWORD					m_dwPid;

	bool m_bMechanicalPanTilt;
	long m_lDigitalTiltMin, m_lDigitalTiltMax,
		 m_lDigitalPanMin, m_lDigitalPanMax;

	bool					m_bUseLogitechMotionControl;
	int						m_iMotorIntervalTimer;

	// Camera settings cache
	CameraSettings			m_cameraSettings;
};
