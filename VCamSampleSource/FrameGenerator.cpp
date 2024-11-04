#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "FrameGenerator.h"

HRESULT FrameGenerator::EnsureRenderTarget(UINT width, UINT height)
{
	if (!_dxgiManager) {
		UINT resetToken;
		RETURN_IF_FAILED(MFCreateDXGIDeviceManager(&resetToken, _dxgiManager.put()));
		_width = width;
		_height = height;

		UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		createDeviceFlags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		wil::com_ptr_nothrow<ID3D11Device> device;
		D3D_FEATURE_LEVEL d3dFeatures[7] = {
			D3D_FEATURE_LEVEL_11_1
		};

		check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
			nullptr, createDeviceFlags, d3dFeatures, 1, D3D11_SDK_VERSION,
			device.put(), nullptr, _textureDeviceContext.put()));

		_dxgiManager->ResetDevice(device.get(), resetToken);

		device->QueryInterface(IID_PPV_ARGS(_textureDevice.put()));

		// create a texture/surface to write
		CD3D11_TEXTURE2D_DESC desc
		(
			DXGI_FORMAT_B8G8R8A8_UNORM,
			width,
			height,
			1,
			1,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
		);
		RETURN_IF_FAILED(device->CreateTexture2D(&desc, nullptr, &_texture));
		wil::com_ptr_nothrow<IDXGISurface> surface;
		RETURN_IF_FAILED(_texture.copy_to(&surface));

		// create a D2D1 render target from 2D GPU surface
		wil::com_ptr_nothrow<ID2D1Factory> d2d1Factory;
		RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, IID_PPV_ARGS(&d2d1Factory)));

		auto props = D2D1::RenderTargetProperties
		(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
		);
		RETURN_IF_FAILED(d2d1Factory->CreateDxgiSurfaceRenderTarget(surface.get(), props, &_renderTarget));

		RETURN_IF_FAILED(CreateRenderTargetResources());

		// create GPU RGB => NV12 converter
		RETURN_IF_FAILED(CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&_converter)));

		wil::com_ptr_nothrow<IMFAttributes> atts;
		RETURN_IF_FAILED(_converter->GetAttributes(&atts));
		TraceMFAttributes(atts.get(), L"VideoProcessorMFT");

		MFT_OUTPUT_STREAM_INFO info{};
		RETURN_IF_FAILED(_converter->GetOutputStreamInfo(0, &info));
		WINTRACE(L"FrameGenerator::EnsureRenderTarget CLSID_VideoProcessorMFT flags:0x%08X size:%u alignment:%u", info.dwFlags, info.cbSize, info.cbAlignment);

		wil::com_ptr_nothrow<IMFMediaType> inputType;
		RETURN_IF_FAILED(MFCreateMediaType(&inputType));
		inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		MFSetAttributeSize(inputType.get(), MF_MT_FRAME_SIZE, width, height);
		RETURN_IF_FAILED(_converter->SetInputType(0, inputType.get(), 0));

		wil::com_ptr_nothrow<IMFMediaType> outputType;
		RETURN_IF_FAILED(MFCreateMediaType(&outputType));
		outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, width, height);
		RETURN_IF_FAILED(_converter->SetOutputType(0, outputType.get(), 0));

		// make sure the video processor works on GPU
		RETURN_IF_FAILED(_converter->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)_dxgiManager.get()));

		CreateSharedCaptureWindowTexture();

		CreateSharedParamsTexture();
	}

	_prevTime = MFGetSystemTime();
	_frame = 0;
	return S_OK;
}

HRESULT FrameGenerator::CreateRenderTargetResources()
{
	assert(_renderTarget);
	RETURN_IF_FAILED(_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &_whiteBrush));

	RETURN_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&_dwrite));
	RETURN_IF_FAILED(_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 40, L"", &_textFormat));
	RETURN_IF_FAILED(_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
	RETURN_IF_FAILED(_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
	return S_OK;
}

// CPUからアクセス可能なテクスチャを作成し、
// ハンドルから共有されたテクスチャを取得。
void FrameGenerator::CreateSharedCaptureWindowTexture()
{
	if (_sharedCaptureWindowTexture != nullptr)
	{
		return;
	}

	HRESULT hr = _textureDevice->OpenSharedResourceByName(TEXT("Global\\NM_Capture_Window"),
		DXGI_SHARED_RESOURCE_READ, IID_PPV_ARGS(_sharedCaptureWindowTexture.put()));

	D3D11_TEXTURE2D_DESC textureDesc;
	_sharedCaptureWindowTexture->GetDesc(&textureDesc);
	_captureTextureWidth = textureDesc.Width;
	_captureTextureHeight = textureDesc.Height;

	D3D11_TEXTURE2D_DESC bufferTextureDesc;

	bufferTextureDesc.Width = _captureTextureWidth;
	bufferTextureDesc.Height = _captureTextureHeight;
	bufferTextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	bufferTextureDesc.ArraySize = 1;
	bufferTextureDesc.BindFlags = 0;
	bufferTextureDesc.MipLevels = 1;
	bufferTextureDesc.SampleDesc.Count = 1;
	bufferTextureDesc.SampleDesc.Quality = 0;
	bufferTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	bufferTextureDesc.MiscFlags = 0;
	bufferTextureDesc.Usage = D3D11_USAGE_STAGING;
	check_hresult(_textureDevice->CreateTexture2D(&bufferTextureDesc, 0, _cpuCaptureWindowTexture.put()));
}

void FrameGenerator::CreateSharedParamsTexture()
{
	if (_sharedParamsTexture != nullptr)
	{
		return;
	}

	D3D11_TEXTURE2D_DESC bufferTextureDesc;

	_textureDevice->OpenSharedResourceByName(TEXT("Global\\NM_Capture_Window_Params"),
		DXGI_SHARED_RESOURCE_READ, IID_PPV_ARGS(_sharedParamsTexture.put()));

	D3D11_TEXTURE2D_DESC textureDesc;
	_sharedParamsTexture->GetDesc(&textureDesc);
	UINT sharedParamsTextureWidth = textureDesc.Width;
	UINT sharedParamsTextureHeight = textureDesc.Height;

	bufferTextureDesc.Width = sharedParamsTextureWidth;
	bufferTextureDesc.Height = sharedParamsTextureHeight;
	bufferTextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	bufferTextureDesc.ArraySize = 1;
	bufferTextureDesc.BindFlags = 0;
	bufferTextureDesc.MipLevels = 1;
	bufferTextureDesc.SampleDesc.Count = 1;
	bufferTextureDesc.SampleDesc.Quality = 0;
	bufferTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	bufferTextureDesc.MiscFlags = 0;
	bufferTextureDesc.Usage = D3D11_USAGE_STAGING;
	check_hresult(_textureDevice->CreateTexture2D(&bufferTextureDesc, 0, _cpuParamsTexture.put()));
}

void FrameGenerator::DrawSharedCaptureWindow()
{
	if (_sharedCaptureWindowTexture == nullptr || _cpuCaptureWindowTexture == nullptr)
	{
		return;
	}

	// 共有されたテクスチャの内容をCPUからアクセス可能なテクスチャにコピー
	com_ptr<IDXGIKeyedMutex> mutex;
	_sharedCaptureWindowTexture.as(mutex);
	mutex->AcquireSync(MUTEX_KEY, INFINITE);
	_textureDeviceContext->CopyResource(_cpuCaptureWindowTexture.get(), _sharedCaptureWindowTexture.get());
	mutex->ReleaseSync(MUTEX_KEY);

	com_ptr<IDXGISurface> dxgiSurface;
	_cpuCaptureWindowTexture->QueryInterface(IID_PPV_ARGS(dxgiSurface.put()));

	DXGI_MAPPED_RECT mapFromTexture;
	dxgiSurface->Map(&mapFromTexture, DXGI_MAP_READ);
	D2D1_BITMAP_PROPERTIES bitmapFormat;
	bitmapFormat.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	bitmapFormat.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
	bitmapFormat.dpiX = 0.0f;
	bitmapFormat.dpiY = 0.0f;

	// CPUからアクセス可能なテクスチャをもとにビットマップを作成
	D2D1_SIZE_U size = D2D1::SizeU(_captureTextureWidth, _captureTextureHeight);
	_renderTarget->CreateBitmap(size, (void*)mapFromTexture.pBits,
		mapFromTexture.Pitch, bitmapFormat, _captureWindowBitmap.put());
	dxgiSurface->Unmap();

	D2D1_RECT_F sourceRect, destRect;
	if (_captureWindowWidth > 0 && _captureWindowHeight > 0) {
		sourceRect.left = 0.0;
		sourceRect.right = static_cast<float>(_captureWindowWidth - 1);
		sourceRect.top = 0.0;
		sourceRect.bottom = static_cast<float>(_captureWindowHeight - 1);

		if (_captureWindowWidth > _captureWindowHeight * _width / _height) {
			float heightInDest = static_cast<float>(_width * _captureWindowHeight) / static_cast<float>(_captureWindowWidth);
			destRect.left = 0.0;
			destRect.right = static_cast<float>(_width - 1);
			destRect.top = (static_cast<float>(_height) - heightInDest) * 0.5f;
			destRect.bottom = static_cast<float>(_height - 1) - (static_cast<float>(_height) - heightInDest) * 0.5f;
		}
		else {
			float widthInDest = static_cast<float>(_height * _captureWindowWidth) / static_cast<float>(_captureWindowHeight);
			destRect.left = (static_cast<float>(_width) - widthInDest) * 0.5f;
			destRect.right = static_cast<float>(_width - 1) - (static_cast<float>(_width) - widthInDest) * 0.5f;
			destRect.top = 0.0;
			destRect.bottom = static_cast<float>(_height - 1);
		}

		// ビットマップを仮想カメラの映像に描画
		_renderTarget->DrawBitmap(_captureWindowBitmap.get(), destRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, sourceRect);
	}
}

void FrameGenerator::GetParamsFromTexture()
{
	if (_sharedParamsTexture == nullptr || _cpuParamsTexture == nullptr)
	{
		return;
	}
	com_ptr<IDXGIKeyedMutex> mutex;
	_sharedParamsTexture.as(mutex);
	mutex->AcquireSync(MUTEX_KEY, INFINITE);
	_textureDeviceContext->CopyResource(_cpuParamsTexture.get(), _sharedParamsTexture.get());
	mutex->ReleaseSync(MUTEX_KEY);

	com_ptr<IDXGISurface> dxgiSurface;
	_cpuParamsTexture->QueryInterface(IID_PPV_ARGS(dxgiSurface.put()));

	DXGI_MAPPED_RECT mapFromTexture;
	dxgiSurface->Map(&mapFromTexture, DXGI_MAP_READ);
	int memIdx = 0;
	CopyMemory((PVOID)&_captureWindowWidth, (PVOID)(mapFromTexture.pBits + memIdx), sizeof(int));
	memIdx += sizeof(int32_t);
	CopyMemory((PVOID)&_captureWindowHeight, (PVOID)(mapFromTexture.pBits + memIdx), sizeof(int));
	memIdx += sizeof(int32_t);
	dxgiSurface->Unmap();
}

HRESULT FrameGenerator::Generate(IMFSample* sample, REFGUID format, IMFSample** outSample)
{
	RETURN_HR_IF_NULL(E_POINTER, sample);
	RETURN_HR_IF_NULL(E_POINTER, outSample);
	*outSample = nullptr;

	GetParamsFromTexture();

	// render something on image common to CPU & GPU
	if (_renderTarget)
	{
		_renderTarget->BeginDraw();
		_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 1));

		DrawSharedCaptureWindow();

		// draw resolution at center
		// note: we could optimize here and compute layout only once if text doesn't change (depending on the font, etc.)
#ifdef _DEBUG
		if (_textFormat && _dwrite && _whiteBrush)
		{
			wchar_t text[127];
			wchar_t fmt[15];
			if (format == MFVideoFormat_NV12)
			{
				lstrcpy(fmt, L"NV12 (GPU)");
			}
			else
			{
				lstrcpy(fmt, L"RGB32 (GPU)");
			}

#define FRAMES_FOR_FPS 60 // number of frames to wait to compute fps from last measure
#define NS_PER_MS 10000
#define MS_PER_S 1000

			if (!_fps || !(_frame % FRAMES_FOR_FPS))
			{
				auto time = MFGetSystemTime();
				_fps = (UINT)(MS_PER_S * NS_PER_MS * FRAMES_FOR_FPS / (time - _prevTime));
				_prevTime = time;
			}

			auto len = wsprintf(text, L"Format: %s\nFrame#: %I64i\nFps: %u\nResolution: %u x %u", fmt, _frame, _fps, _width, _height);

			wil::com_ptr_nothrow<IDWriteTextLayout> layout;
			RETURN_IF_FAILED(_dwrite->CreateTextLayout(text, len, _textFormat.get(), (FLOAT)_width, (FLOAT)_height, &layout));

			_renderTarget->DrawTextLayout(D2D1::Point2F(0, 0), layout.get(), _whiteBrush.get());
		}
#endif
		_renderTarget->EndDraw();
	}

	// build a sample using either D3D/DXGI (GPU) or WIC (CPU)
	wil::com_ptr_nothrow<IMFMediaBuffer> mediaBuffer;

	// remove all existing buffers
	RETURN_IF_FAILED(sample->RemoveAllBuffers());

	// create a buffer from this and add to sample
	RETURN_IF_FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), _texture.get(), 0, 0, &mediaBuffer));
	RETURN_IF_FAILED(sample->AddBuffer(mediaBuffer.get()));

	// if we're on GPU & format is not RGB, convert using GPU
	if (format == MFVideoFormat_NV12)
	{
		assert(_converter);
		RETURN_IF_FAILED(_converter->ProcessInput(0, sample, 0));

		// let converter build the sample for us, note it works because we gave it the D3DManager
		MFT_OUTPUT_DATA_BUFFER buffer = {};
		DWORD status = 0;
		RETURN_IF_FAILED(_converter->ProcessOutput(0, 1, &buffer, &status));
		*outSample = buffer.pSample;
	}
	else
	{
		sample->AddRef();
		*outSample = sample;
	}

	_frame++;
	return S_OK;
}
