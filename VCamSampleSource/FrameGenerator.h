#pragma once

#include <d3d11_4.h>
#include <dxgi1_2.h>

using namespace winrt;

#define MUTEX_KEY 5556

class FrameGenerator
{
	UINT _width;
	UINT _height;
	ULONGLONG _frame;
	MFTIME _prevTime;
	UINT _fps;
	wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
	wil::com_ptr_nothrow<ID2D1RenderTarget> _renderTarget;
	wil::com_ptr_nothrow<ID2D1SolidColorBrush> _whiteBrush;
	wil::com_ptr_nothrow<IDWriteTextFormat> _textFormat;
	wil::com_ptr_nothrow<IDWriteFactory> _dwrite;
	wil::com_ptr_nothrow<IMFTransform> _converter;
	wil::com_ptr_nothrow<IWICBitmap> _bitmap;
	wil::com_ptr_nothrow<IMFDXGIDeviceManager> _dxgiManager;
	wil::com_ptr_nothrow<ID3D11RenderTargetView> _renderTargetView;
	wil::com_ptr_nothrow<ID3D11ShaderResourceView> _shaderResourceView;
	wil::com_ptr_nothrow<ID3D11VertexShader> _spriteVS;
	wil::com_ptr_nothrow<ID3D11PixelShader> _spritePS;
	wil::com_ptr_nothrow<ID3D11InputLayout> _spriteInputLayout;

	HRESULT CreateRenderTargetResources();

	// 共有テクスチャのために追加した部分
	com_ptr<ID3D11Device1> _textureDevice;
	com_ptr<ID3D11DeviceContext> _textureDeviceContext;
	com_ptr<ID3D11Multithread> _deviceMutex;

	com_ptr<ID3D11Texture2D> _sharedCaptureWindowTexture;
	HANDLE _sharedCaptureWindowHandle;
	int _captureTextureWidth;
	int _captureTextureHeight;
	
	com_ptr<ID3D11Texture2D> _sharedParamsTexture;
	HANDLE _sharedParamsHandle;
	com_ptr<ID3D11Texture2D> _cpuParamsTexture;
	int _captureWindowWidth;
	int _captureWindowHeight;

	HRESULT SetupD3D11Device();

	HRESULT CreateSharedCaptureWindowTexture();

	HRESULT CreateSharedParamsTexture();

	HRESULT SetupOffscreenRendering();

	HRESULT SetupNV12Converter();

	void DrawSharedCaptureWindow();

	void GetParamsFromTexture();
	// 共有テクスチャのために追加した部分 END

public:
	FrameGenerator() :
		_width(0),
		_height(0),
		_frame(0),
		_fps(0),
		_prevTime(MFGetSystemTime()),
		_captureTextureWidth(0),
		_captureTextureHeight(0),
		_captureWindowWidth(0),
		_captureWindowHeight(0)
	{
	}

	~FrameGenerator()
	{
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

	HRESULT EnsureRenderTarget(UINT width, UINT height);
	HRESULT Generate(IMFSample* sample, REFGUID format, IMFSample** outSample);
};