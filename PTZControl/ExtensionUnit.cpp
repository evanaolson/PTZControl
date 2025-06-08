#include "pch.h"
// #include <sstream>
// #include <iomanip>
// #include <iostream>			// Including this before DShow.h avoids a bunch of C4995 warnings for unsafe
// 							// string functions if (and only if) strsafe.h is not included above.

#include <initguid.h>
#include <comdef.h>
#define NO_DSHOW_STRSAFE	// Avoid more C4995 warnings in intrin.h
#include <DShow.h>
#include <Ks.h>
#include <KsMedia.h>

#pragma comment(lib, "strmiids.lib")

#include "ExtensionUnit.h"

#define NONODE 0xFFFFFFFF

//////////////////////////////////////////////////////////////////////////


void MySleep(int mSec)
{
	// TIcks per second
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);

	LARGE_INTEGER liStart;
	QueryPerformanceCounter(&liStart);

	LARGE_INTEGER liEnd;
	liEnd.QuadPart = liStart.QuadPart + (li.QuadPart*mSec)/1000;

	LARGE_INTEGER liNow;
	do 
	{
		QueryPerformanceCounter(&liNow);
	}
	while (liNow.QuadPart<liEnd.QuadPart);
	TRACE(__FUNCTION__ " %d\n", DWORD((liNow.QuadPart-liStart.QuadPart)*1000/li.QuadPart));
}


CWebcamController::CWebcamController()
{
	CloseDevice();
}


CWebcamController::~CWebcamController()
{
	CloseDevice();
}

HRESULT CWebcamController::OpenDevice(BSTR bstrDevicePath, DWORD wVID, DWORD wPID)
{
	// Create the System Device Enumerator
	CComPtr<ICreateDevEnum> pSysDevEnum;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, __uuidof(ICreateDevEnum), (void **)&pSysDevEnum);
	if(FAILED(hr))
		return hr;

	// Obtain a class enumerator for the video input device category
	CComPtr<IEnumMoniker> pEnumCat;
	hr = pSysDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnumCat, 0);
	if(hr == S_OK) 
	{
		hr = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);

		// Enumerate the monikers and check if we can find a matching device
		CComPtr<IMoniker> pMoniker;
		ULONG cFetched;
		while(pEnumCat->Next(1, &pMoniker, &cFetched) == S_OK)
		{
			if(DeviceMatches(pMoniker, bstrDevicePath, wVID, wPID))
			{
				hr = OpenDevice(pMoniker);
				break;			// We're done searching (even if the OpenDevice() call above failed)
			}
			pMoniker = nullptr;
		}
	}

	return hr;
}

void CWebcamController::CloseDevice()
{
	m_spKsControl = nullptr;
	m_spAMCameraControl = nullptr;
	m_spVideoProcAmp = nullptr;
	m_spsPropertySet = nullptr;
	m_spCameraControl = nullptr;

	m_dwXUDeviceInformationNodeId =
	m_dwXUVideoPipeControlNodeId =
	m_dwXUTestDebugNodeId =
	m_dwXUPeripheralControlNodeId = NONODE;

	// Use IAMCameraControl motion control
	m_bUseLogitechMotionControl = false;
	m_iMotorIntervalTimer = DEFAULT_MOTOR_INTERVAL_TIMER;

	// The PTZ Pro 2 has a mechanical pan tilt
	m_bMechanicalPanTilt = false;
	m_lDigitalTiltMin = m_lDigitalTiltMax = m_lDigitalPanMin = m_lDigitalPanMax = -1;
}

HRESULT CWebcamController::IsPeripheralPropertySetSupported()
{
	if (!m_spKsControl) 
		return -1; 

	KSP_NODE extProp{};
	extProp.Property.Set = LOGITECH_XU_PERIPHERAL_CONTROL;
	extProp.Property.Id = 0;
	extProp.Property.Flags = KSPROPERTY_TYPE_SETSUPPORT | KSPROPERTY_TYPE_TOPOLOGY;
	extProp.NodeId = m_dwXUPeripheralControlNodeId;
	extProp.Reserved = 0;
	ULONG ulBytesReturned = 0;
	return m_spKsControl->KsProperty((PKSPROPERTY)&extProp, sizeof(extProp), NULL, 0, &ulBytesReturned);
}

HRESULT CWebcamController::GetProperty(LOGITECH_XU_PROPERTYSET lPropertySet,ULONG ulPropertyId, ULONG ulSize, VOID *pValue)
{
	if (!m_spKsControl)
		return -1;

	ASSERT(pValue!=0 && ulSize!=0);

	KSP_NODE extprop{};
	extprop.Property.Id    = ulPropertyId;
	extprop.Property.Flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;

	switch(lPropertySet)
	{
	case XU_DEVICE_INFORMATION:
		extprop.NodeId         = m_dwXUDeviceInformationNodeId;
		extprop.Property.Set   = LOGITECH_XU_DEVICE_INFORMATION;
		break;
	case XU_VIDEOPIPE_CONTROL:
		extprop.NodeId         = m_dwXUVideoPipeControlNodeId;
		extprop.Property.Set   = LOGITECH_XU_VIDEOPIPE_CONTROL;
		break;
	case XU_TEST_DEBUG:
		extprop.NodeId         = m_dwXUTestDebugNodeId;
		extprop.Property.Set   = LOGITECH_XU_TEST_DEBUG;
		break;
	case XU_PERIPHERAL_CONTROL:
		extprop.NodeId         = m_dwXUPeripheralControlNodeId;
		extprop.Property.Set   = LOGITECH_XU_PERIPHERAL_CONTROL;
		break;
	}

	HRESULT hr = m_spKsControl->KsProperty(
		(PKSPROPERTY)&extprop,
		sizeof(extprop),
		pValue,
		ulSize,
		&ulSize
	);

	return hr;
}


HRESULT CWebcamController::SetProperty(LOGITECH_XU_PROPERTYSET lPropertySet,ULONG ulPropertyId, ULONG ulSize, VOID *pValue)
{
	if (!m_spKsControl)
		return -1;

	ASSERT(pValue != 0 && ulSize != 0);

	KSP_NODE extprop{};
	extprop.Property.Id    = ulPropertyId;
	extprop.Property.Flags = KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY;

	switch(lPropertySet)
	{
	case XU_DEVICE_INFORMATION:
		extprop.NodeId         = m_dwXUDeviceInformationNodeId;
		extprop.Property.Set   = LOGITECH_XU_DEVICE_INFORMATION;
		break;
	case XU_VIDEOPIPE_CONTROL:
		extprop.NodeId         = m_dwXUVideoPipeControlNodeId;
		extprop.Property.Set   = LOGITECH_XU_VIDEOPIPE_CONTROL;
		break;
	case XU_TEST_DEBUG:
		extprop.NodeId         = m_dwXUTestDebugNodeId;
		extprop.Property.Set   = LOGITECH_XU_TEST_DEBUG;
		break;
	case XU_PERIPHERAL_CONTROL:
		extprop.NodeId         = m_dwXUPeripheralControlNodeId;
		extprop.Property.Set   = LOGITECH_XU_PERIPHERAL_CONTROL;
		break;
	}

	ULONG ulBytesReturned;
	HRESULT hr = m_spKsControl->KsProperty(
		(PKSPROPERTY)&extprop,
		sizeof(extprop),
		pValue,
		ulSize,
		&ulBytesReturned
	);


	return hr;
}

/*
* Checks whether the device represented by the given IMoniker matches the given device path
* or the given VID/PID.
*
* If the device path is NULL no device path matching is performed.
* A VID or PID of 0 is considered to always match. If the VID and PID are both 0 no VID/PID
* matching is performed.
*/
bool CWebcamController::DeviceMatches(CComPtr<IMoniker> pMoniker, BSTR devicePath, DWORD wVID, DWORD wPID)
{

	bool match = false;
	CComPtr<IPropertyBag> pPropBag;
	CComVariant varPath;

									// Open the device properties
	HRESULT hr = pMoniker->BindToStorage(NULL, NULL, __uuidof(IPropertyBag), (void **)&pPropBag);
	if(FAILED(hr))
		goto done;

	// Retrieve the device path
	hr = pPropBag->Read(L"DevicePath", &varPath, 0);
	if(FAILED(hr) || varPath.bstrVal == NULL)
		goto done;

	// Return true if the device path matches
	if(devicePath!=NULL)
	{
		// Parse the device path for vid pid
		m_dwVid = m_dwPid = 0;
		if (FAILED(varPath.ChangeType(VT_BSTR)))
			return false;
			
		if (_wcsicmp(varPath.bstrVal, devicePath) == 0)
		{
			// Lowercase
			_wcslwr_s(varPath.bstrVal, SysStringLen(varPath.bstrVal) + 1);
			ParseDevicePath(varPath.bstrVal, m_dwVid, m_dwPid);

			match = true;
			goto done;
		}
	}

	// Return true if the USB information matches
	if(wVID || wPID)
	{
		// Parse the device path (Convert it to lower-case first for safer parsing)
		_wcslwr_s(varPath.bstrVal, SysStringLen(varPath.bstrVal) + 1);
		DWORD vid = 0, pid = 0;
		if(ParseDevicePath(varPath.bstrVal, vid, pid))
		{
			match =
				(wVID == 0 || vid == wVID) &&
				(wPID == 0 || pid == wPID);
		}
	}

done:
	VariantClear(&varPath);
	return match;
}

HRESULT CWebcamController::OpenDevice(CComPtr<IMoniker> pMoniker)
{
	CComPtr<IKsControl> pKsControl;

	// Get a pointer to the IKsControl interface
	HRESULT hr = pMoniker->BindToObject(NULL, NULL, __uuidof(IKsControl), (void **)&pKsControl);
	if(FAILED(hr))
		return hr;

	// CRITICAL: Initialize DirectShow interfaces properly
	CComPtr<IBaseFilter> pBaseFilter;
	hr = pMoniker->BindToObject(NULL, NULL, __uuidof(IBaseFilter), (void **)&pBaseFilter);
	if (SUCCEEDED(hr)) {
		// Query standard DirectShow interfaces from base filter
		pBaseFilter->QueryInterface(__uuidof(IAMCameraControl), (void **)&m_spAMCameraControl);
		pBaseFilter->QueryInterface(__uuidof(IAMVideoProcAmp), (void **)&m_spVideoProcAmp);
		pBaseFilter->QueryInterface(__uuidof(IKsPropertySet), (void **)&m_spsPropertySet);
		
		TRACE(__FUNCTION__ " DirectShow interfaces initialized: VideoProcAmp=%s, CameraControl=%s\n",
			m_spVideoProcAmp ? "YES" : "NO",
			m_spAMCameraControl ? "YES" : "NO");
	}

	// Find the H.264 XU node (existing functionality)
	hr = InitializeXUNodesArray(pKsControl);
	if(SUCCEEDED(hr))
	{
		// save the pointer, we succeeded
		m_spKsControl = pKsControl;

		// Only set these if we don't already have the interfaces from BaseFilter
		if (!m_spAMCameraControl) {
			m_spAMCameraControl = pKsControl;
		}
		if (!m_spVideoProcAmp) {
			m_spVideoProcAmp = pKsControl;
		}
		if (!m_spsPropertySet) {
			m_spsPropertySet = pKsControl;
		}
		m_spCameraControl = pKsControl;		// not supported

		// Initialize camera settings cache
		RefreshCameraSettings();
	}

	// Test DirectShow interface availability
	if (m_spVideoProcAmp) {
		TRACE(__FUNCTION__ " Testing VideoProcAmp properties:\n");
		long min, max, step, def, flags;
		
		// Test common properties
		if (SUCCEEDED(m_spVideoProcAmp->GetRange(VideoProcAmp_Brightness, &min, &max, &step, &def, &flags))) {
			TRACE("  Brightness: min=%d, max=%d, step=%d, default=%d\n", min, max, step, def);
		}
		if (SUCCEEDED(m_spVideoProcAmp->GetRange(VideoProcAmp_Contrast, &min, &max, &step, &def, &flags))) {
			TRACE("  Contrast: min=%d, max=%d, step=%d, default=%d\n", min, max, step, def);
		}
		if (SUCCEEDED(m_spVideoProcAmp->GetRange(VideoProcAmp_WhiteBalance, &min, &max, &step, &def, &flags))) {
			TRACE("  WhiteBalance: min=%d, max=%d, step=%d, default=%d\n", min, max, step, def);
		}
	}

	if (m_spAMCameraControl) {
		TRACE(__FUNCTION__ " Testing CameraControl properties:\n");
		long min, max, step, def, flags;
		
		if (SUCCEEDED(m_spAMCameraControl->GetRange(CameraControl_Focus, &min, &max, &step, &def, &flags))) {
			TRACE("  Focus: min=%d, max=%d, step=%d, default=%d\n", min, max, step, def);
		}
		if (SUCCEEDED(m_spAMCameraControl->GetRange(CameraControl_Exposure, &min, &max, &step, &def, &flags))) {
			TRACE("  Exposure: min=%d, max=%d, step=%d, default=%d\n", min, max, step, def);
		}
	}

	// COMPREHENSIVE CAMERA PROPERTY ANALYSIS
	TRACE("=== COMPREHENSIVE CAMERA PROPERTY ANALYSIS ===\n");

	if (m_spAMCameraControl) {
		const struct { long prop; const char* name; } cameraProps[] = {
			{CameraControl_Pan, "Pan"},
			{CameraControl_Tilt, "Tilt"},
			{CameraControl_Roll, "Roll"},
			{CameraControl_Zoom, "Zoom"},
			{CameraControl_Exposure, "Exposure"},
			{CameraControl_Iris, "Iris"},
			{CameraControl_Focus, "Focus"}
		};
		
		for (const auto& prop : cameraProps) {
			long min, max, step, def, flags, currentVal, currentFlags;
			HRESULT hr = m_spAMCameraControl->GetRange(prop.prop, &min, &max, &step, &def, &flags);
			if (SUCCEEDED(hr) && flags != 0) {
				m_spAMCameraControl->Get(prop.prop, &currentVal, &currentFlags);
				TRACE("%s: SUPPORTED - Range[%d to %d, step %d, default %d] Caps[%s%s] Current[val=%d, %s]\n",
					  prop.name, min, max, step, def,
					  (flags & CameraControl_Flags_Auto) ? "AUTO " : "",
					  (flags & CameraControl_Flags_Manual) ? "MANUAL" : "",
					  currentVal,
					  (currentFlags & CameraControl_Flags_Auto) ? "AUTO" : "MANUAL");
			} else {
				TRACE("%s: NOT SUPPORTED (hr=0x%x, flags=0x%x)\n", prop.name, hr, flags);
			}
		}
	}

	if (m_spVideoProcAmp) {
		const struct { long prop; const char* name; } videoProcProps[] = {
			{VideoProcAmp_Brightness, "Brightness"},
			{VideoProcAmp_Contrast, "Contrast"},
			{VideoProcAmp_Hue, "Hue"},
			{VideoProcAmp_Saturation, "Saturation"},
			{VideoProcAmp_Sharpness, "Sharpness"},
			{VideoProcAmp_Gamma, "Gamma"},
			{VideoProcAmp_WhiteBalance, "WhiteBalance"},
			{VideoProcAmp_BacklightCompensation, "BacklightComp"},
			{VideoProcAmp_Gain, "Gain"}
		};
		
		for (const auto& prop : videoProcProps) {
			long min, max, step, def, flags, currentVal, currentFlags;
			HRESULT hr = m_spVideoProcAmp->GetRange(prop.prop, &min, &max, &step, &def, &flags);
			if (SUCCEEDED(hr) && flags != 0) {
				m_spVideoProcAmp->Get(prop.prop, &currentVal, &currentFlags);
				TRACE("%s: SUPPORTED - Range[%d to %d, step %d, default %d] Caps[%s%s] Current[val=%d, %s]\n",
					  prop.name, min, max, step, def,
					  (flags & VideoProcAmp_Flags_Auto) ? "AUTO " : "",
					  (flags & VideoProcAmp_Flags_Manual) ? "MANUAL" : "",
					  currentVal,
					  (currentFlags & VideoProcAmp_Flags_Auto) ? "AUTO" : "MANUAL");
			} else {
				TRACE("%s: NOT SUPPORTED (hr=0x%x, flags=0x%x)\n", prop.name, hr, flags);
			}
		}
	}

	// Call the validation logging function
	LogAndValidateCameraRanges();

	if (m_spAMCameraControl!=nullptr)
	{
		long lValue = 0, lMin = 0, lMax = 0, lSteppingSize = 0, lDefaults = 0, lFlags = 0;
		HRESULT hResult = m_spAMCameraControl->Get(KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, &lValue, &lFlags);
		m_bMechanicalPanTilt = SUCCEEDED(hr);

		{
			for (int i=0; i<=19; ++i)
			{
				hResult = m_spAMCameraControl->Get(i, &lValue, &lFlags);
				if (SUCCEEDED(hr))
					TRACE(__FUNCTION__ " m_spAMCameraControl-%d val=%d, flag=%d\n",i, lValue, lFlags);
			}

		}

		// The PTZ Pro 2 has a mechanical pan tilt
		ASSERT(m_bMechanicalPanTilt);

// 		{
// 			HRESULT hResult = GetProperty(  KSPROPERTY_CAMERACONTROL_PANTILT_RELATIVE, &lValue, &lFlags);			
// 		}

		hResult = m_spAMCameraControl->GetRange(CameraControl_Pan, &lMin, &lMax, &lSteppingSize, &lDefaults, &lFlags);
		if (S_OK == hResult)
		{
			m_lDigitalPanMin = lMin;
			m_lDigitalPanMax = lMax;
		}
		lMin = lMax = lSteppingSize = lDefaults = lFlags = 0;
		hResult = m_spAMCameraControl->GetRange(CameraControl_Tilt, &lMin, &lMax, &lSteppingSize, &lDefaults, &lFlags);
		if (S_OK == hResult)
		{
			m_lDigitalTiltMin = lMin;
			m_lDigitalTiltMax = lMax;
		}
	}


	return hr;
}

bool CWebcamController::ParseDevicePath(const wchar_t *devicePath, DWORD &vid, DWORD &pid)
{
	if(swscanf_s(devicePath, L"\\\\?\\usb#vid_%04x&pid_%04x", &vid, &pid) != 2)
		return false;
	return true;
}

void CWebcamController::GetVidPid(DWORD& vid, DWORD& pid)
{
	vid = m_dwVid;
	pid = m_dwPid;
}

void CWebcamController::GotoHome()
{
	// Zoom to Home
	{
		DWORD dwValue = 0;
		SetProperty(XU_VIDEOPIPE_CONTROL, XU_VIDEO_FW_ZOOM_CONTROL, sizeof(dwValue), &dwValue);
	}
	// Zoom to Home
	// Home = No Action
	//			1 ?
	//			2 ?
	// Goto Home = 3
	// 8 Presets
	// Preset 1-8 = 4-11, 
	// Goto Preset 1-8 = 12-19
	// Test 22 
	{
		DWORD dwValue(3);
		SetProperty(XU_PERIPHERAL_CONTROL, XU_PERIPHERALCONTROL_PANTILT_MODE_CONTROL, sizeof(DWORD), &dwValue);
	}
	return;
}

void CWebcamController::SavePreset(int iNum)
{
	if (iNum<0 || iNum>=NUM_PRESETS)
		return;

	// Zoom to Home
	// Home = No Action
	//			1 ?
	//			2 ?
	// Goto Home = 3
	// 8 Presets
	// Preset 1-8 = 4-11, 
	// Goto Preset 1-8 = 12-19
	// Test 22 
	DWORD dwValue(iNum+4);
	SetProperty(XU_PERIPHERAL_CONTROL, XU_PERIPHERALCONTROL_PANTILT_MODE_CONTROL, sizeof(DWORD), &dwValue);	
}

void CWebcamController::GotoPreset(int iNum)
{
	if (iNum<0 || iNum>=NUM_PRESETS)
		return;

	// Zoom to Home
	// Home = No Action
	//			1 ?
	//			2 ?
	// Goto Home = 3
	// 8 Presets
	// Preset 1-8 = 4-11, 
	// Goto Preset 1-8 = 12-19
	// Test 22 
	DWORD dwValue(iNum + 12);
	SetProperty(XU_PERIPHERAL_CONTROL, XU_PERIPHERALCONTROL_PANTILT_MODE_CONTROL, sizeof(DWORD), &dwValue);
}

int CWebcamController::GetCurrentZoom()
{
	if (!m_spAMCameraControl)
		return -1;

	long oldZoom = 0, oldFlags = 0;
	oldFlags = CameraControl_Flags_Manual;
	m_spAMCameraControl->Get(CameraControl_Zoom, &oldZoom, &oldFlags);
	return oldZoom;
}

int CWebcamController::Zoom(int direction)
{
	if (!m_spAMCameraControl)
		return -1;

	long lZoomMin = 0, lZoomMax = 0, lZoomDefault = 0, lZoomStep = 0, lFlags = CameraControl_Flags_Manual;
	m_spAMCameraControl->GetRange(CameraControl_Zoom, &lZoomMin, &lZoomMax, &lZoomStep, &lZoomDefault, &lFlags);

	long lOldZoom = GetCurrentZoom();
	if (lOldZoom<lZoomMin || lOldZoom>lZoomMax)
		lOldZoom = lZoomDefault;
	
	long lNewZoom = lOldZoom;
	
	// Devide in 150 parts
	int iStep = (lZoomMax-lZoomMin)/150;
	if (iStep==0)
		iStep = 1;

	// calculate new zoom
	lNewZoom = lOldZoom + lZoomStep*direction*iStep;	// Just get 100 steps

	m_spAMCameraControl->Set(CameraControl_Zoom, lNewZoom, CameraControl_Flags_Manual);
	return lNewZoom;
}

void CWebcamController::Tilt(int yDirection)
{
	if (m_spAMCameraControl)
		m_spAMCameraControl->Set(KSPROPERTY_CAMERACONTROL_TILT_RELATIVE, yDirection!=0 ? (yDirection < 0 ? -1 : 1) : 0, 0);
}

void CWebcamController::MoveTilt(int yDirection)
{
	if (m_bUseLogitechMotionControl && m_dwXUPeripheralControlNodeId!=NONODE)
	{
		DWORD dwValue = MAKELONG(MAKEWORD(0, 0), MAKEWORD(0, yDirection < 0 ? 1 : -1));
		SetProperty(XU_PERIPHERAL_CONTROL, XU_PERIPHERALCONTROL_PANTILT_RELATIVE_CONTROL, sizeof(DWORD), &dwValue);
	}
	else
	{
		if (!m_spAMCameraControl)
			return;

		if (m_bMechanicalPanTilt)
		{
			if (yDirection != 0)
			{
				Tilt(yDirection);
				MySleep(m_iMotorIntervalTimer);
				Tilt(0);
			}
		}
		else
		{
			long lValue(0), lFlags(0);
			if (yDirection != 0)
			{
				HRESULT hResult = m_spAMCameraControl->Get(CameraControl_Tilt, &lValue, &lFlags);
				if (S_OK == hResult)
				{
					lValue += yDirection;
					if (yDirection > 0 && lValue > m_lDigitalTiltMax)
						lValue = m_lDigitalTiltMax;
					if (yDirection < 0 && lValue < m_lDigitalTiltMin)
						lValue = m_lDigitalTiltMin;
					hResult = m_spAMCameraControl->Set(CameraControl_Tilt, lValue, lFlags);
				}
			}
		}
	}
	return;
}


void CWebcamController::Pan(int xDirection)
{
	if (m_spAMCameraControl)
		m_spAMCameraControl->Set(KSPROPERTY_CAMERACONTROL_PAN_RELATIVE, xDirection!=0 ? (xDirection < 0 ? -1 : 1) : 0, 0);
}

void CWebcamController::MovePan(int xDirection)
{
	if (m_bUseLogitechMotionControl)
	{
		DWORD dwValue = MAKELONG(MAKEWORD(0, xDirection), MAKEWORD(0, 0));
		SetProperty(XU_PERIPHERAL_CONTROL, XU_PERIPHERALCONTROL_PANTILT_RELATIVE_CONTROL, sizeof(DWORD), &dwValue);
	}
	else
	{
		if (!m_spAMCameraControl)
			return;

		if (m_bMechanicalPanTilt)
		{
			if (xDirection != 0)
			{
				Pan(xDirection);
				MySleep(m_iMotorIntervalTimer);
				Pan(0);
			}
		}
		else
		{
			long lValue(0), lFlags(0);
			if (xDirection != 0)
			{
				HRESULT hResult = m_spAMCameraControl->Get(CameraControl_Pan, &lValue, &lFlags);
				if (S_OK == hResult)
				{
					lValue += xDirection;
					if (xDirection > 0 && lValue > m_lDigitalPanMax)
						lValue = m_lDigitalPanMax;
					if (xDirection < 0 && lValue < m_lDigitalPanMin)
						lValue = m_lDigitalPanMin;
					hResult = m_spAMCameraControl->Set(CameraControl_Pan, lValue, lFlags);
				}
			}
		}
	}
	return;
}


/*
* Tries to locate the node that carries H.264 XU extension and saves its ID.
*/
HRESULT CWebcamController::InitializeXUNodesArray(CComPtr<IKsControl> pKsControl)
{
	// Get the IKsTopologyInfo interface
	CComQIPtr<IKsTopologyInfo> pKsTopologyInfo = pKsControl;
	if (!pKsTopologyInfo)
		return E_NOINTERFACE;

	// Retrieve the number of nodes in the filter
	DWORD dwNumNodes = 0;
	HRESULT hr = pKsTopologyInfo->get_NumNodes(&dwNumNodes);
	if(FAILED(hr))
		return hr;

	// Go through all extension unit nodes and try to find the required XU node
	hr = E_FAIL;
	std::set<CString> setGuids;
	for(unsigned int nodeId = 0; nodeId < dwNumNodes; nodeId++)
	{
		GUID guidNodeType;
		hr = pKsTopologyInfo->get_NodeType(nodeId, &guidNodeType);
		if(FAILED(hr))
			continue;

		// All Node types we have
// 		{ 941C7AC0 - C559 - 11D0 - 8A2B - 00A0C9255AC1 } KSNODETYPE_DEV_SPECIFIC
// 		{ DFF229E1 - F70F - 11D0 - B917 - 00A0C9223196 } KSNODETYPE_VIDEO_STREAMING
// 		{ DFF229E5 - F70F - 11D0 - B917 - 00A0C9223196 } KSNODETYPE_VIDEO_PROCESSING
// 		{ DFF229E6 - F70F - 11D0 - B917 - 00A0C9223196 } KSNODETYPE_VIDEO_CAMERA_TERMINAL
		{
			wchar_t szText[100];
			StringFromGUID2(guidNodeType, szText, _countof(szText));
			setGuids.emplace(szText);
		}


		if(!IsEqualGUID(guidNodeType, KSNODETYPE_DEV_SPECIFIC))
			continue;

		if(IsExtensionUnitSupported(pKsControl,LOGITECH_XU_DEVICE_INFORMATION,nodeId))
		{
			m_dwXUDeviceInformationNodeId = nodeId;
		}
		else if(IsExtensionUnitSupported(pKsControl,LOGITECH_XU_VIDEOPIPE_CONTROL,nodeId))
		{
			m_dwXUVideoPipeControlNodeId = nodeId;
		}
		else if(IsExtensionUnitSupported(pKsControl,LOGITECH_XU_TEST_DEBUG,nodeId))
		{
			m_dwXUTestDebugNodeId = nodeId;
		}
		else if(IsExtensionUnitSupported(pKsControl,LOGITECH_XU_PERIPHERAL_CONTROL,nodeId))
		{
			m_dwXUPeripheralControlNodeId = nodeId;
		}
// 		else if (IsExtensionUnitSupported(pKsControl, PROPSETID_VIDCAP_CAMERACONTROL, nodeId))
// 		{
// 			// DWORD dwNodeId = nodeId;
// 		}
	}
	for (const auto &str : setGuids)
		TRACE(__FUNCTION__ " - %ls\n", str.GetString());
	return hr;
}

bool CWebcamController::IsExtensionUnitSupported(CComPtr<IKsControl> pKsControl,const GUID& guidExtension,unsigned int nodeId)
{
	KSP_NODE extProp{};
	extProp.Property.Set = guidExtension;
	extProp.Property.Id = 0;
	extProp.Property.Flags = KSPROPERTY_TYPE_SETSUPPORT | KSPROPERTY_TYPE_TOPOLOGY;
	extProp.NodeId = nodeId;
	extProp.Reserved = 0;
	ULONG ulBytesReturned = 0;
	HRESULT hr = pKsControl->KsProperty((PKSPROPERTY)&extProp, sizeof(extProp), NULL, 0, &ulBytesReturned);
	return SUCCEEDED(hr);
}


void CWebcamController::ListDevices(CStringArray &aDevices)
{
	aDevices.RemoveAll();

	// Get a device list
	CComPtr<ICreateDevEnum> pSysDevEnum;
	HRESULT hr;
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pSysDevEnum);
	if (SUCCEEDED(hr))
	{
		//create a device class enumerator
		CComPtr<IEnumMoniker> pIEnumMoniker;
		HRESULT hResult = pSysDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pIEnumMoniker, 0);
		if (SUCCEEDED(hr))
		{
			ULONG	pFetched = NULL;
			CComPtr<IMoniker> pImoniker;
			while (S_OK == pIEnumMoniker->Next(1, &pImoniker, &pFetched))
			{
				CComPtr<IPropertyBag> pPropBag;
				hResult = pImoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);
				if (SUCCEEDED(hResult) && pPropBag!=nullptr)
				{
					CComVariant varCameraName, varDevicePath;
					pPropBag->Read(L"FriendlyName", &varCameraName, 0);
					pPropBag->Read(L"DevicePath", &varDevicePath, 0);

					if (SUCCEEDED(varCameraName.ChangeType(VT_BSTR)) &&
						SUCCEEDED(varDevicePath.ChangeType(VT_BSTR)))
					{
						CString strCameraName(varCameraName.bstrVal), strDevicePath(varDevicePath.bstrVal);
						aDevices.Add(strCameraName + _T('\t') + strDevicePath);
					}
				}

				// Set to null
				pImoniker = nullptr;
			}
		}
	}
}

void CWebcamController::LogAndValidateCameraRanges()
{
    TRACE("\n=== CAMERA PROPERTY RANGES VALIDATION ===\n");
    
    if (m_spVideoProcAmp) {
        TRACE("VideoProcAmp Properties:\n");
        
        struct PropertyInfo {
            long prop;
            const char* name;
            long expectedMin;
            long expectedMax;
            long expectedDefault;
        };
        
        PropertyInfo vpProps[] = {
            {VideoProcAmp_Brightness, "Brightness", 0, 255, 128},
            {VideoProcAmp_Contrast, "Contrast", 0, 255, 128},
            {VideoProcAmp_Saturation, "Saturation", 0, 255, 128},
            {VideoProcAmp_Sharpness, "Sharpness", 0, 255, 128},
            {VideoProcAmp_WhiteBalance, "WhiteBalance", 2800, 6500, 4600},
            {VideoProcAmp_BacklightCompensation, "BacklightComp", 0, 1, 0},
            {VideoProcAmp_Gain, "Gain", 0, 32, 0}
        };
        
        for (int i = 0; i < _countof(vpProps); i++) {
            PropertyInfo& prop = vpProps[i];
            long min, max, step, def, flags;
            HRESULT hr = m_spVideoProcAmp->GetRange(prop.prop, &min, &max, &step, &def, &flags);
            
            if (SUCCEEDED(hr) && flags != 0) {
                TRACE("  ✓ %s: %ld to %ld, step=%ld, default=%ld, flags=0x%lx", 
                       prop.name, min, max, step, def, flags);
                
                // Validate against expected ranges
                bool rangeMatches = (min == prop.expectedMin && max == prop.expectedMax);
                bool defaultMatches = (def == prop.expectedDefault);
                
                if (rangeMatches && defaultMatches) {
                    TRACE(" [MATCHES OBS]");
                } else {
                    TRACE(" [DIFFERS: expected %ld-%ld, default %ld]", 
                           prop.expectedMin, prop.expectedMax, prop.expectedDefault);
                }
                
                // Check auto/manual support
                TRACE(" Modes:[%s%s]", 
                       (flags & VideoProcAmp_Flags_Auto) ? "AUTO " : "",
                       (flags & VideoProcAmp_Flags_Manual) ? "MANUAL" : "");
                
                TRACE("\n");
            } else {
                TRACE("  ✗ %s: NOT SUPPORTED (hr=0x%lx, flags=0x%lx)\n", prop.name, hr, flags);
            }
        }
    }
    
    if (m_spAMCameraControl) {
        TRACE("\nCameraControl Properties:\n");
        
        struct PropertyInfo {
            long prop;
            const char* name;
            long expectedMin;
            long expectedMax;
            long expectedDefault;
        };
        
        PropertyInfo ccProps[] = {
            {CameraControl_Focus, "Focus", 0, 255, 8},
            {CameraControl_Exposure, "Exposure", -11, -2, -6},
            {CameraControl_Zoom, "Zoom", 100, 500, 100},
            {CameraControl_Pan, "Pan", -180, 180, 0},
            {CameraControl_Tilt, "Tilt", -90, 90, 0}
        };
        
        for (int i = 0; i < _countof(ccProps); i++) {
            PropertyInfo& prop = ccProps[i];
            long min, max, step, def, flags;
            HRESULT hr = m_spAMCameraControl->GetRange(prop.prop, &min, &max, &step, &def, &flags);
            
            if (SUCCEEDED(hr) && flags != 0) {
                TRACE("  ✓ %s: %ld to %ld, step=%ld, default=%ld, flags=0x%lx", 
                       prop.name, min, max, step, def, flags);
                
                // Validate against expected ranges  
                bool rangeMatches = (min == prop.expectedMin && max == prop.expectedMax);
                bool defaultMatches = (def == prop.expectedDefault);
                
                if (rangeMatches && defaultMatches) {
                    TRACE(" [MATCHES OBS]");
                } else {
                    TRACE(" [DIFFERS: expected %ld-%ld, default %ld]", 
                           prop.expectedMin, prop.expectedMax, prop.expectedDefault);
                }
                
                // Check auto/manual support
                TRACE(" Modes:[%s%s]", 
                       (flags & CameraControl_Flags_Auto) ? "AUTO " : "",
                       (flags & CameraControl_Flags_Manual) ? "MANUAL" : "");
                
                TRACE("\n");
            } else {
                TRACE("  ✗ %s: NOT SUPPORTED (hr=0x%lx, flags=0x%lx)\n", prop.name, hr, flags);
            }
        }
    }
    
    TRACE("==========================================\n\n");
}

//////////////////////////////////////////////////////////////////////////
// Dynamic Range Detection Implementation

HRESULT CWebcamController::GetPropertyRanges(std::map<long, PropertyRange>& videoProcAmpRanges, std::map<long, PropertyRange>& cameraControlRanges)
{
    // Clear existing ranges
    videoProcAmpRanges.clear();
    cameraControlRanges.clear();
    
    // VideoProcAmp properties to test
    long videoProcAmpProperties[] = {
        VideoProcAmp_Brightness,
        VideoProcAmp_Contrast,
        VideoProcAmp_Hue,
        VideoProcAmp_Saturation,
        VideoProcAmp_Sharpness,
        VideoProcAmp_Gamma,
        VideoProcAmp_WhiteBalance,
        VideoProcAmp_BacklightCompensation,
        VideoProcAmp_Gain,
        VideoProcAmp_ColorEnable
    };
    
    // Test VideoProcAmp properties
    if (m_spVideoProcAmp) {
        for (long property : videoProcAmpProperties) {
            long min, max, step, defaultVal, flags;
            HRESULT hr = m_spVideoProcAmp->GetRange(property, &min, &max, &step, &defaultVal, &flags);
            if (SUCCEEDED(hr) && flags != 0) { // flags == 0 means not supported
                PropertyRange range;
                range.min = min;
                range.max = max;
                range.step = step;
                range.defaultValue = defaultVal;
                range.flags = flags;
                range.supported = true;
                videoProcAmpRanges[property] = range;
                
                TRACE("VideoProcAmp property %d: min=%d, max=%d, step=%d, default=%d, flags=%d\n", 
                      property, min, max, step, defaultVal, flags);
            }
        }
    }
    
    // CameraControl properties to test
    long cameraControlProperties[] = {
        CameraControl_Pan,
        CameraControl_Tilt,
        CameraControl_Roll,
        CameraControl_Zoom,
        CameraControl_Exposure,
        CameraControl_Iris,
        CameraControl_Focus
    };
    
    // Test CameraControl properties
    if (m_spAMCameraControl) {
        for (long property : cameraControlProperties) {
            long min, max, step, defaultVal, flags;
            HRESULT hr = m_spAMCameraControl->GetRange(property, &min, &max, &step, &defaultVal, &flags);
            if (SUCCEEDED(hr) && flags != 0) { // flags == 0 means not supported
                PropertyRange range;
                range.min = min;
                range.max = max;
                range.step = step;
                range.defaultValue = defaultVal;
                range.flags = flags;
                range.supported = true;
                cameraControlRanges[property] = range;
                
                TRACE("CameraControl property %d: min=%d, max=%d, step=%d, default=%d, flags=%d\n", 
                      property, min, max, step, defaultVal, flags);
            }
        }
    }
    
    return S_OK;
}

CWebcamController::PropertyRange CWebcamController::GetVideoProcAmpRange(long property)
{
    CWebcamController::PropertyRange range;
    
    if (m_spVideoProcAmp) {
        long min, max, step, defaultVal, flags;
        HRESULT hr = m_spVideoProcAmp->GetRange(property, &min, &max, &step, &defaultVal, &flags);
        if (SUCCEEDED(hr) && flags != 0) {
            range.min = min;
            range.max = max;
            range.step = step;
            range.defaultValue = defaultVal;
            range.flags = flags;
            range.supported = true;
        }
    }
    
    return range;
}

CWebcamController::PropertyRange CWebcamController::GetCameraControlRange(long property)
{
    CWebcamController::PropertyRange range;
    
    if (m_spAMCameraControl) {
        long min, max, step, defaultVal, flags;
        HRESULT hr = m_spAMCameraControl->GetRange(property, &min, &max, &step, &defaultVal, &flags);
        if (SUCCEEDED(hr) && flags != 0) {
            range.min = min;
            range.max = max;
            range.step = step;
            range.defaultValue = defaultVal;
            range.flags = flags;
            range.supported = true;
        }
    }
    
    return range;
}

//////////////////////////////////////////////////////////////////////////
// Enhanced DirectShow Interface Support

bool CWebcamController::SupportsVideoProcAmpProperty(long property)
{
	if (!m_spVideoProcAmp)
		return false;

	long min, max, step, def, flags;
	HRESULT hr = m_spVideoProcAmp->GetRange(property, &min, &max, &step, &def, &flags);
	return SUCCEEDED(hr);
}

bool CWebcamController::SupportsCameraControlProperty(long property)
{
	if (!m_spAMCameraControl)
		return false;

	long min, max, step, def, flags;
	HRESULT hr = m_spAMCameraControl->GetRange(property, &min, &max, &step, &def, &flags);
	return SUCCEEDED(hr);
}

HRESULT CWebcamController::TestAndGetVideoProcAmpRange(long property, long* min, long* max, long* step, long* default_val, long* flags)
{
	if (!m_spVideoProcAmp || !min || !max || !step || !default_val || !flags)
		return E_INVALIDARG;

	return m_spVideoProcAmp->GetRange(property, min, max, step, default_val, flags);
}

HRESULT CWebcamController::TestAndGetCameraControlRange(long property, long* min, long* max, long* step, long* default_val, long* flags)
{
	if (!m_spAMCameraControl || !min || !max || !step || !default_val || !flags)
		return E_INVALIDARG;

	return m_spAMCameraControl->GetRange(property, min, max, step, default_val, flags);
}

HRESULT CWebcamController::SetPropertyHybrid(long property, long value, bool isVideoProcAmp)
{
	HRESULT hr = E_FAIL;

	// First try Logitech Extension Unit (existing functionality)
	if (isVideoProcAmp) {
		// Try setting via extension unit first for Logitech cameras
		if (m_dwXUVideoPipeControlNodeId != NONODE) {
			// Map DirectShow property to extension unit property
			ULONG xuProperty = 0;
			bool hasXUMapping = false;

			switch (property) {
				case VideoProcAmp_BacklightCompensation:
					// Map to RightLight mode control
					xuProperty = XU_VIDEO_RIGHTLIGHT_MODE_CONTROL;
					hasXUMapping = true;
					break;
				case VideoProcAmp_ColorEnable:
					// Map to color boost control
					xuProperty = XU_VIDEO_COLOR_BOOST_CONTROL;
					hasXUMapping = true;
					break;
				// Note: Other properties like brightness, contrast, etc. don't have direct XU mappings
				// They will fall through to DirectShow interface
			}

			if (hasXUMapping) {
				DWORD dwValue = static_cast<DWORD>(value);
				hr = SetProperty(XU_VIDEOPIPE_CONTROL, xuProperty, sizeof(DWORD), &dwValue);
				if (SUCCEEDED(hr)) {
					return hr;
				}
			}
		}

		// Fallback to standard DirectShow VideoProcAmp
		if (m_spVideoProcAmp) {
			hr = m_spVideoProcAmp->Set(property, value, VideoProcAmp_Flags_Manual);
			if (SUCCEEDED(hr)) {
				return hr;
			}
		}
	} else {
		// Camera Control properties
		if (m_spAMCameraControl) {
			hr = m_spAMCameraControl->Set(property, value, CameraControl_Flags_Manual);
			if (SUCCEEDED(hr)) {
				return hr;
			}
		}
	}

	return hr;
}

HRESULT CWebcamController::GetPropertyHybrid(long property, long* value, bool isVideoProcAmp)
{
	if (!value)
		return E_INVALIDARG;

	HRESULT hr = E_FAIL;
	long flags = 0;

	if (isVideoProcAmp) {
		// Try extension unit first, then fallback to DirectShow
		if (m_dwXUVideoPipeControlNodeId != NONODE) {
			// Try extension unit retrieval
			DWORD dwValue = 0;
			ULONG xuProperty = 0;
			bool hasXUMapping = false;

			switch (property) {
				case VideoProcAmp_BacklightCompensation:
					// Map to RightLight mode control
					xuProperty = XU_VIDEO_RIGHTLIGHT_MODE_CONTROL;
					hasXUMapping = true;
					break;
				case VideoProcAmp_ColorEnable:
					// Map to color boost control
					xuProperty = XU_VIDEO_COLOR_BOOST_CONTROL;
					hasXUMapping = true;
					break;
				// Note: Other properties like brightness, contrast, etc. don't have direct XU mappings
				// They will fall through to DirectShow interface
			}

			if (hasXUMapping) {
				hr = GetProperty(XU_VIDEOPIPE_CONTROL, xuProperty, sizeof(DWORD), &dwValue);
				if (SUCCEEDED(hr)) {
					*value = static_cast<long>(dwValue);
					return hr;
				}
			}
		}

		// Fallback to standard DirectShow VideoProcAmp
		if (m_spVideoProcAmp) {
			hr = m_spVideoProcAmp->Get(property, value, &flags);
		}
	} else {
		// Camera Control properties
		if (m_spAMCameraControl) {
			hr = m_spAMCameraControl->Get(property, value, &flags);
		}
	}

	return hr;
}

//////////////////////////////////////////////////////////////////////////
// Camera Settings Implementation

HRESULT CWebcamController::GetVideoProcAmpProperty(long property, long* value, long* flags)
{
	if (!m_spVideoProcAmp || !value || !flags)
		return E_INVALIDARG;
		
	return m_spVideoProcAmp->Get(property, value, flags);
}

HRESULT CWebcamController::SetVideoProcAmpProperty(long property, long value, long flags)
{
	if (!m_spVideoProcAmp)
		return E_INVALIDARG;
		
	return m_spVideoProcAmp->Set(property, value, flags);
}

HRESULT CWebcamController::GetVideoProcAmpRange(long property, long* min, long* max, long* step, long* default_val, long* flags)
{
	if (!m_spVideoProcAmp || !min || !max || !step || !default_val || !flags)
		return E_INVALIDARG;
		
	return m_spVideoProcAmp->GetRange(property, min, max, step, default_val, flags);
}

HRESULT CWebcamController::GetBrightness(long* value)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_Brightness, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.brightness = *value;
	}
	return hr;
}

HRESULT CWebcamController::SetBrightness(long value)
{
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_Brightness, value, VideoProcAmp_Flags_Manual);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.brightness = value;
	}
	return hr;
}

HRESULT CWebcamController::GetContrast(long* value)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_Contrast, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.contrast = *value;
	}
	return hr;
}

HRESULT CWebcamController::SetContrast(long value)
{
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_Contrast, value, VideoProcAmp_Flags_Manual);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.contrast = value;
	}
	return hr;
}

HRESULT CWebcamController::GetHue(long* value)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_Hue, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.hue = *value;
	}
	return hr;
}

HRESULT CWebcamController::SetHue(long value)
{
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_Hue, value, VideoProcAmp_Flags_Manual);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.hue = value;
	}
	return hr;
}

HRESULT CWebcamController::GetSaturation(long* value)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_Saturation, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.saturation = *value;
	}
	return hr;
}

HRESULT CWebcamController::SetSaturation(long value)
{
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_Saturation, value, VideoProcAmp_Flags_Manual);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.saturation = value;
	}
	return hr;
}

HRESULT CWebcamController::GetSharpness(long* value)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_Sharpness, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.sharpness = *value;
	}
	return hr;
}

HRESULT CWebcamController::SetSharpness(long value)
{
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_Sharpness, value, VideoProcAmp_Flags_Manual);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.sharpness = value;
	}
	return hr;
}

HRESULT CWebcamController::GetGamma(long* value)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_Gamma, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.gamma = *value;
	}
	return hr;
}

HRESULT CWebcamController::SetGamma(long value)
{
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_Gamma, value, VideoProcAmp_Flags_Manual);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.gamma = value;
	}
	return hr;
}

HRESULT CWebcamController::GetWhiteBalance(long* value, bool* isAuto)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_WhiteBalance, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.whiteBalance = *value;
		m_cameraSettings.autoWhiteBalance = (flags & VideoProcAmp_Flags_Auto) != 0;
		if (isAuto) *isAuto = m_cameraSettings.autoWhiteBalance;
	}
	return hr;
}

HRESULT CWebcamController::SetWhiteBalance(long value, bool isAuto)
{
	// Test if white balance is supported
	long min, max, step, defaultVal, capFlags;
	HRESULT hrTest = GetVideoProcAmpRange(VideoProcAmp_WhiteBalance, &min, &max, &step, &defaultVal, &capFlags);
	if (FAILED(hrTest) || capFlags == 0) {
		TRACE("SetWhiteBalance: White balance control not supported (hr=0x%x, capFlags=0x%x)\n", hrTest, capFlags);
		return E_NOTIMPL;
	}
	
	TRACE("SetWhiteBalance: Camera supports range %d to %d, step=%d, default=%d, capFlags=0x%x\n", 
		  min, max, step, defaultVal, capFlags);
	
	// Check if the requested mode is supported
	if (isAuto && !(capFlags & VideoProcAmp_Flags_Auto)) {
		TRACE("SetWhiteBalance: Auto white balance not supported by camera\n");
		return E_INVALIDARG;
	}
	if (!isAuto && !(capFlags & VideoProcAmp_Flags_Manual)) {
		TRACE("SetWhiteBalance: Manual white balance not supported by camera\n");
		return E_INVALIDARG;
	}
	
	// For auto mode, use the default value
	if (isAuto) {
		value = defaultVal;
	} else {
		// Clamp manual value to supported range
		if (value < min) value = min;
		if (value > max) value = max;
		
		// Align to step size
		long adjustedValue = min + ((value - min) / step) * step;
		value = adjustedValue;
	}
	
	long flags = isAuto ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual;
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_WhiteBalance, value, flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.whiteBalance = value;
		m_cameraSettings.autoWhiteBalance = isAuto;
		TRACE("SetWhiteBalance SUCCESS: value=%d, flags=0x%x, auto=%s\n", value, flags, isAuto ? "YES" : "NO");
	} else {
		TRACE("SetWhiteBalance FAILED: hr=0x%x, value=%d, flags=0x%x\n", hr, value, flags);
	}
	return hr;
}

HRESULT CWebcamController::GetBacklightCompensation(long* value)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_BacklightCompensation, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.backlightCompensation = *value;
		m_cameraSettings.rightLight = (*value > 0);
	}
	return hr;
}

HRESULT CWebcamController::SetBacklightCompensation(long value)
{
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_BacklightCompensation, value, VideoProcAmp_Flags_Manual);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.backlightCompensation = value;
		m_cameraSettings.rightLight = (value > 0);
	}
	return hr;
}

HRESULT CWebcamController::GetGain(long* value)
{
	long flags;
	HRESULT hr = GetVideoProcAmpProperty(VideoProcAmp_Gain, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.gain = *value;
	}
	return hr;
}

HRESULT CWebcamController::SetGain(long value)
{
	HRESULT hr = SetVideoProcAmpProperty(VideoProcAmp_Gain, value, VideoProcAmp_Flags_Manual);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.gain = value;
	}
	return hr;
}

HRESULT CWebcamController::GetPowerlineFrequency(long* value)
{
	// PowerlineFrequency might not be available in all DirectShow versions
	// Use a fallback approach or skip if not supported
	*value = m_cameraSettings.powerlineFrequency;
	return S_OK;
}

HRESULT CWebcamController::SetPowerlineFrequency(long value)
{
	// PowerlineFrequency might not be available in all DirectShow versions
	// Store the value locally for now
	m_cameraSettings.powerlineFrequency = value;
	return S_OK;
}

HRESULT CWebcamController::GetExposure(long* value, bool* isAuto)
{
	if (!m_spAMCameraControl || !value)
		return E_INVALIDARG;
		
	long flags;
	HRESULT hr = m_spAMCameraControl->Get(CameraControl_Exposure, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.exposure = *value;
		m_cameraSettings.autoExposure = (flags & CameraControl_Flags_Auto) != 0;
		if (isAuto) *isAuto = m_cameraSettings.autoExposure;
	}
	return hr;
}

HRESULT CWebcamController::SetExposure(long value, bool isAuto)
{
	if (!m_spAMCameraControl)
		return E_INVALIDARG;
	
	// CRITICAL: Test exposure support and get valid range
	long min, max, step, defaultVal, capFlags;
	HRESULT hrTest = m_spAMCameraControl->GetRange(CameraControl_Exposure, &min, &max, &step, &defaultVal, &capFlags);
	if (FAILED(hrTest) || capFlags == 0) {
		TRACE("SetExposure: Exposure control not supported (hr=0x%x, capFlags=0x%x)\n", hrTest, capFlags);
		return E_NOTIMPL;
	}
	
	TRACE("SetExposure: Camera supports range %d to %d, step=%d, default=%d, capFlags=0x%x\n", 
		  min, max, step, defaultVal, capFlags);
	
	// Check if the requested mode is supported
	if (isAuto && !(capFlags & CameraControl_Flags_Auto)) {
		TRACE("SetExposure: Auto exposure not supported by camera\n");
		return E_INVALIDARG;
	}
	if (!isAuto && !(capFlags & CameraControl_Flags_Manual)) {
		TRACE("SetExposure: Manual exposure not supported by camera\n");
		return E_INVALIDARG;
	}
	
	// For auto mode, use the default value
	if (isAuto) {
		value = defaultVal;
	} else {
		// Clamp manual value to supported range
		if (value < min) value = min;
		if (value > max) value = max;
		
		// Align to step size
		long adjustedValue = min + ((value - min) / step) * step;
		value = adjustedValue;
	}
	
	long flags = isAuto ? CameraControl_Flags_Auto : CameraControl_Flags_Manual;
	HRESULT hr = m_spAMCameraControl->Set(CameraControl_Exposure, value, flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.exposure = value;
		m_cameraSettings.autoExposure = isAuto;
		TRACE("SetExposure SUCCESS: value=%d, flags=0x%x, auto=%s\n", value, flags, isAuto ? "YES" : "NO");
	} else {
		TRACE("SetExposure FAILED: hr=0x%x, value=%d, flags=0x%x\n", hr, value, flags);
	}
	return hr;
}

HRESULT CWebcamController::GetFocus(long* value, bool* isAuto)
{
	if (!m_spAMCameraControl || !value)
		return E_INVALIDARG;
		
	long flags;
	HRESULT hr = m_spAMCameraControl->Get(CameraControl_Focus, value, &flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.focus = *value;
		m_cameraSettings.autoFocus = (flags & CameraControl_Flags_Auto) != 0;
		if (isAuto) *isAuto = m_cameraSettings.autoFocus;
	}
	return hr;
}

HRESULT CWebcamController::SetFocus(long value, bool isAuto)
{
	if (!m_spAMCameraControl)
		return E_INVALIDARG;
		
	long flags = isAuto ? CameraControl_Flags_Auto : CameraControl_Flags_Manual;
	HRESULT hr = m_spAMCameraControl->Set(CameraControl_Focus, value, flags);
	if (SUCCEEDED(hr)) {
		m_cameraSettings.focus = value;
		m_cameraSettings.autoFocus = isAuto;
	}
	return hr;
}

CWebcamController::CameraSettings CWebcamController::GetAllCameraSettings()
{
	RefreshCameraSettings();
	return m_cameraSettings;
}

HRESULT CWebcamController::SetCameraSettings(const CameraSettings& settings)
{
	HRESULT hr = S_OK;
	
	// Set VideoProcAmp properties
	if (FAILED(SetBrightness(settings.brightness))) hr = E_FAIL;
	if (FAILED(SetContrast(settings.contrast))) hr = E_FAIL;
	if (FAILED(SetHue(settings.hue))) hr = E_FAIL;
	if (FAILED(SetSaturation(settings.saturation))) hr = E_FAIL;
	if (FAILED(SetSharpness(settings.sharpness))) hr = E_FAIL;
	if (FAILED(SetGamma(settings.gamma))) hr = E_FAIL;
	if (FAILED(SetWhiteBalance(settings.whiteBalance, settings.autoWhiteBalance))) hr = E_FAIL;
	if (FAILED(SetBacklightCompensation(settings.backlightCompensation))) hr = E_FAIL;
	if (FAILED(SetGain(settings.gain))) hr = E_FAIL;
	if (FAILED(SetPowerlineFrequency(settings.powerlineFrequency))) hr = E_FAIL;
	
	// Set CameraControl properties
	if (FAILED(SetExposure(settings.exposure, settings.autoExposure))) hr = E_FAIL;
	if (FAILED(SetFocus(settings.focus, settings.autoFocus))) hr = E_FAIL;
	
	return hr;
}

HRESULT CWebcamController::ResetCameraSettings()
{
	// Reset to default values
	CameraSettings defaults;
	return SetCameraSettings(defaults);
}

HRESULT CWebcamController::RefreshCameraSettings()
{
	HRESULT hr = S_OK;
	
	// Refresh VideoProcAmp properties
	long value;
	bool isAuto;
	
	if (SUCCEEDED(GetBrightness(&value))) m_cameraSettings.brightness = value;
	if (SUCCEEDED(GetContrast(&value))) m_cameraSettings.contrast = value;
	if (SUCCEEDED(GetHue(&value))) m_cameraSettings.hue = value;
	if (SUCCEEDED(GetSaturation(&value))) m_cameraSettings.saturation = value;
	if (SUCCEEDED(GetSharpness(&value))) m_cameraSettings.sharpness = value;
	if (SUCCEEDED(GetGamma(&value))) m_cameraSettings.gamma = value;
	if (SUCCEEDED(GetWhiteBalance(&value, &isAuto))) {
		m_cameraSettings.whiteBalance = value;
		m_cameraSettings.autoWhiteBalance = isAuto;
	}
	if (SUCCEEDED(GetBacklightCompensation(&value))) {
		m_cameraSettings.backlightCompensation = value;
		m_cameraSettings.rightLight = (value > 0);
	}
	if (SUCCEEDED(GetGain(&value))) m_cameraSettings.gain = value;
	if (SUCCEEDED(GetPowerlineFrequency(&value))) m_cameraSettings.powerlineFrequency = value;
	
	// Refresh CameraControl properties
	if (SUCCEEDED(GetExposure(&value, &isAuto))) {
		m_cameraSettings.exposure = value;
		m_cameraSettings.autoExposure = isAuto;
	}
	if (SUCCEEDED(GetFocus(&value, &isAuto))) {
		m_cameraSettings.focus = value;
		m_cameraSettings.autoFocus = isAuto;
	}
	
	return hr;
}
