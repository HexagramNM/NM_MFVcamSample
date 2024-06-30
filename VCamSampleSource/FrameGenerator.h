#pragma once

#include <d3d11_4.h>
#include <dxgi1_2.h>

using namespace winrt;

#define MAX_SOURCE_WIDTH 3840
#define MAX_SOURCE_HEIGHT 2160
#define MUTEX_KEY 5556
#define SHARED_PARAMS_BUF 32

class FrameGenerator
{
	UINT _width;
	UINT _height;
	ULONGLONG _frame;
	MFTIME _prevTime;
	UINT _fps;
	HANDLE _deviceHandle;
	wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
	wil::com_ptr_nothrow<ID2D1RenderTarget> _renderTarget;
	wil::com_ptr_nothrow<ID2D1SolidColorBrush> _whiteBrush;
	wil::com_ptr_nothrow<IDWriteTextFormat> _textFormat;
	wil::com_ptr_nothrow<IDWriteFactory> _dwrite;
	wil::com_ptr_nothrow<IMFTransform> _converter;
	wil::com_ptr_nothrow<IWICBitmap> _bitmap;
	wil::com_ptr_nothrow<IMFDXGIDeviceManager> _dxgiManager;

	HRESULT CreateRenderTargetResources(UINT width, UINT height);

	// 共有テクスチャのために追加した部分
	com_ptr<ID3D11Device1> _textureDevice;
	com_ptr<ID3D11DeviceContext> _textureDeviceContext;

	com_ptr<ID3D11Texture2D> _sharedCaptureWindowTexture;
	HANDLE _sharedCaptureWindowHandle;
	com_ptr<ID3D11Texture2D> _cpuCaptureWindowTexture;
	com_ptr<ID2D1Bitmap> _captureWindowBitmap;
	
	com_ptr<ID3D11Texture2D> _sharedParamsTexture;
	HANDLE _sharedParamsHandle;
	com_ptr<ID3D11Texture2D> _cpuParamsTexture;
	int _captureWindowWidth;
	int _captureWindowHeight;

	void CreateDirectXDeviceForTexture();

	void CreateSharedCaptureWindowTexture();

	void DrawSharedCaptureWindow();

	void CreateSharedParamsTexture();

	void GetParamsFromTexture();
	// 共有テクスチャのために追加した部分 END

public:
	FrameGenerator() :
		_width(0),
		_height(0),
		_frame(0),
		_fps(0),
		_deviceHandle(nullptr),
		_prevTime(MFGetSystemTime()),
		_captureWindowWidth(MAX_SOURCE_WIDTH),
		_captureWindowHeight(MAX_SOURCE_HEIGHT)
	{
		CreateDirectXDeviceForTexture();
		CreateSharedCaptureWindowTexture();
		CreateSharedParamsTexture();
	}

	~FrameGenerator()
	{
		if (_dxgiManager && _deviceHandle)
		{
			auto hr = _dxgiManager->CloseDeviceHandle(_deviceHandle); // don't report error at that point
			if (FAILED(hr))
			{
				WINTRACE(L"FrameGenerator CloseDeviceHandle: 0x%08X", hr);
			}
		}

		if (_sharedCaptureWindowHandle != NULL
			&& _sharedCaptureWindowHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_sharedCaptureWindowHandle);
			_sharedCaptureWindowHandle = NULL;
		}

		if (_sharedParamsHandle != NULL
			&& _sharedParamsHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_sharedParamsHandle);
			_sharedParamsHandle = NULL;
		}
	}

	HRESULT SetD3DManager(IUnknown* manager, UINT width, UINT height);
	const bool HasD3DManager();
	HRESULT EnsureRenderTarget(UINT width, UINT height);
	HRESULT Generate(IMFSample* sample, REFGUID format, IMFSample** outSample);
};