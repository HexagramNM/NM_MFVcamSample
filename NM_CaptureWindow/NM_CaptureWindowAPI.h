#pragma once

#include "NM_CaptureWindow.h"

extern "C" __declspec(dllexport) NM_CaptureWindow* createCaptureWindowObject(HWND hwnd);

extern "C" __declspec(dllexport) void deleteCaptureWindowObject(NM_CaptureWindow * captureWindowObj);
