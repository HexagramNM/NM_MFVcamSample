#include "NM_CaptureWindowAPI.h"

NM_CaptureWindow* createCaptureWindowObject(HWND hwnd)
{
	NM_CaptureWindow* result = new NM_CaptureWindow();

	result->CreateDirect3DDeviceForCapture();
	result->CreateSharedCaptureWindowTexture();
	result->CreateSharedParamsTexture();
	result->SetTargetWindowForCapture(hwnd);
	result->CreateVirtualCamera();

	return result;
}

void deleteCaptureWindowObject(NM_CaptureWindow* captureWindowObj)
{
	delete captureWindowObj;
}