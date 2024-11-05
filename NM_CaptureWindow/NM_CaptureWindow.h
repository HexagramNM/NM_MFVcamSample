#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <mfvirtualcamera.h>

using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Graphics::Capture;

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "advapi32.lib")

#define MAX_SOURCE_WIDTH 3840
#define MAX_SOURCE_HEIGHT 2160
#define MUTEX_KEY 5556
#define SHARED_PARAMS_BUF 32

class NM_CaptureWindow
{
public:
	NM_CaptureWindow(): 
		_d3dDevice(nullptr),
		_deviceCtxForCapture(nullptr),
		_bufferTextureForCapture(nullptr),
		_graphicsCaptureItem(nullptr),
		_framePoolForCapture(nullptr),
		_captureSession(nullptr),
		_sharedCaptureWindowHandle(nullptr),
		_sharedParamsHandle(nullptr)
	{
		_capWinSize.Width = 1;
		_capWinSize.Height = 1;
		_capWinSizeInTexture.left = 0;
		_capWinSizeInTexture.right = 1;
		_capWinSizeInTexture.top = 0;
		_capWinSizeInTexture.bottom = 1;
		_capWinSizeInTexture.front = 0;
		_capWinSizeInTexture.back = 1;
	}

	~NM_CaptureWindow()
	{
		StopVirtualCamera();
		StopCapture();
	}

	void CreateVirtualCamera();
	void StopVirtualCamera();

	void CreateDirect3DDeviceForCapture();
	void CreateSharedCaptureWindowTexture();
	void CreateSharedParamsTexture();
	void UpdateSharedParams();

	bool IsCapturing();
	void StopCapture();
	void SetTargetWindowForCapture(HWND targetWindow);
	void OnFrameArrived(Direct3D11CaptureFramePool const& sender,
		winrt::Windows::Foundation::IInspectable const& args);

private:
	com_ptr<IMFVirtualCamera> _vcam;
	com_ptr<ID3D11Device> _d3dDevice;
	IDirect3DDevice _dxDeviceForCapture;
	com_ptr<ID3D11DeviceContext> _deviceCtxForCapture;
	com_ptr<ID3D11Texture2D> _bufferTextureForCapture;
	GraphicsCaptureItem _graphicsCaptureItem;
	Direct3D11CaptureFramePool _framePoolForCapture;
	event_revoker<IDirect3D11CaptureFramePool> _frameArrivedForCapture;
	GraphicsCaptureSession _captureSession;
	SizeInt32 _capWinSize;
	D3D11_BOX _capWinSizeInTexture;
	HANDLE _sharedCaptureWindowHandle;
	com_ptr<ID3D11Texture2D> _bufferParams;
	com_ptr<ID3D11Texture2D> _cpuBufferParams;
	HANDLE _sharedParamsHandle;
};
