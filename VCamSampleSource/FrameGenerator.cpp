
#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "FrameGenerator.h"
#include "D3dcompiler.h"

#define HLSL_EXTERNAL_INCLUDE(...) #__VA_ARGS__

// Embeded hlsl shader source code.
const char* hlslCode =
#include "SpriteShader.hlsl"
;


HRESULT FrameGenerator::SetupD3D11Device() {
	UINT resetToken;
	RETURN_IF_FAILED(MFCreateDXGIDeviceManager(&resetToken, _dxgiManager.put()));

	UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	createDeviceFlags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	wil::com_ptr_nothrow<ID3D11Device> device;
	D3D_FEATURE_LEVEL d3dFeatures[7] = {
		D3D_FEATURE_LEVEL_11_1
	};

	RETURN_IF_FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
		nullptr, createDeviceFlags, d3dFeatures, 1, D3D11_SDK_VERSION,
		device.put(), nullptr, _dxDeviceContext.put()));

	_dxDeviceContext->QueryInterface(IID_PPV_ARGS(_dxDeviceMutex.put()));
	_dxDeviceMutex->SetMultithreadProtected(true);

	_dxgiManager->ResetDevice(device.get(), resetToken);

	device->QueryInterface(IID_PPV_ARGS(_dxDevice.put()));

	return S_OK;
}

// キャプチャウィンドウの共有テクスチャをハンドルから取得。
HRESULT FrameGenerator::CreateSharedCaptureWindowTexture()
{
	if (_sharedCaptureWindowTexture != nullptr)
	{
		return S_OK;
	}

	RETURN_IF_FAILED(_dxDevice->OpenSharedResourceByName(
		TEXT("Global\\NM_Capture_Window"), DXGI_SHARED_RESOURCE_READ, 
		IID_PPV_ARGS(_sharedCaptureWindowTexture.put())));

	D3D11_TEXTURE2D_DESC sharedTextureDesc;
	_sharedCaptureWindowTexture->GetDesc(&sharedTextureDesc);
	_captureTextureWidth = sharedTextureDesc.Width;
	_captureTextureHeight = sharedTextureDesc.Height;

	return S_OK;
}

// キャプチャウィンドウのサイズなどパラメタを格納したの共有テクスチャをハンドルから取得。
// CPU上でデータを読みだすためのテクスチャの作成。
HRESULT FrameGenerator::CreateSharedParamsTexture()
{
	if (_sharedParamsTexture != nullptr)
	{
		return S_OK;
	}

	RETURN_IF_FAILED(_dxDevice->OpenSharedResourceByName(
		TEXT("Global\\NM_Capture_Window_Params"), DXGI_SHARED_RESOURCE_READ, 
		IID_PPV_ARGS(_sharedParamsTexture.put())));

	D3D11_TEXTURE2D_DESC sharedTextureDesc;
	_sharedParamsTexture->GetDesc(&sharedTextureDesc);
	UINT sharedParamsTextureWidth = sharedTextureDesc.Width;
	UINT sharedParamsTextureHeight = sharedTextureDesc.Height;

	D3D11_TEXTURE2D_DESC bufferTextureDesc;
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
	RETURN_IF_FAILED(_dxDevice->CreateTexture2D(&bufferTextureDesc, 0, 
		_cpuParamsTexture.put()));

	return S_OK;
}

// _renderTextureへのオフスクリーンレンダリングの準備
HRESULT FrameGenerator::SetupOffscreenRendering() {
	DXGI_FORMAT dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	CD3D11_TEXTURE2D_DESC desc;
	desc.Width = _width;
	desc.Height = _height;
	desc.Format = dxgiFormat;
	desc.ArraySize = 1;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	
	RETURN_IF_FAILED(_dxDevice->CreateTexture2D(&desc, nullptr, _renderTexture.put()));

	CD3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc(D3D11_RTV_DIMENSION_TEXTURE2D, dxgiFormat);
	RETURN_IF_FAILED(_dxDevice->CreateRenderTargetView(_renderTexture.get(),
		&renderTargetViewDesc, _renderTargetView.put()));

	CD3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc(D3D11_SRV_DIMENSION_TEXTURE2D, dxgiFormat);
	RETURN_IF_FAILED(_dxDevice->CreateShaderResourceView(_sharedCaptureWindowTexture.get(),
		&shaderResourceViewDesc, _shaderResourceView.put()));

	_dxDeviceContext->OMSetRenderTargets(1, _renderTargetView.addressof(), nullptr);
	D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)_width, (float)_height, 0.0f, 1.0f };
	_dxDeviceContext->RSSetViewports(1, &vp);

	size_t hlslSize = std::strlen(hlslCode);
	wil::com_ptr_t<ID3DBlob> compiledVS;
	RETURN_IF_FAILED(D3DCompile(hlslCode, hlslSize, nullptr, nullptr, nullptr,
		"VS", "vs_5_0", 0, 0, compiledVS.put(), nullptr));

	wil::com_ptr_t<ID3DBlob> compiledPS;
	RETURN_IF_FAILED(D3DCompile(hlslCode, hlslSize, nullptr, nullptr, nullptr,
		"PS", "ps_5_0", 0, 0, compiledPS.put(), nullptr));

	RETURN_IF_FAILED(_dxDevice->CreateVertexShader(compiledVS->GetBufferPointer(),
		compiledVS->GetBufferSize(), nullptr, _spriteVS.put()));

	RETURN_IF_FAILED(_dxDevice->CreatePixelShader(compiledPS->GetBufferPointer(),
		compiledPS->GetBufferSize(), nullptr, _spritePS.put()));

	D3D11_INPUT_ELEMENT_DESC layout[2] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXUV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	RETURN_IF_FAILED(_dxDevice->CreateInputLayout(layout, 2, compiledVS->GetBufferPointer(),
		compiledVS->GetBufferSize(), _spriteInputLayout.put()));

	_vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	_vbDesc.ByteWidth = sizeof(VertexType) * 4;
	_vbDesc.MiscFlags = 0;
	_vbDesc.StructureByteStride = 0;
	_vbDesc.Usage = D3D11_USAGE_DEFAULT;
	_vbDesc.CPUAccessFlags = 0;

	return S_OK;
}

// NV12フォーマットのコンバータ (IMFTransform) 作成
HRESULT FrameGenerator::SetupNV12Converter() {
	// create GPU RGB => NV12 converter
	RETURN_IF_FAILED(CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_ALL, IID_PPV_ARGS(_converter.put())));

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
	MFSetAttributeSize(inputType.get(), MF_MT_FRAME_SIZE, _width, _height);
	RETURN_IF_FAILED(_converter->SetInputType(0, inputType.get(), 0));

	wil::com_ptr_nothrow<IMFMediaType> outputType;
	RETURN_IF_FAILED(MFCreateMediaType(&outputType));
	outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, _width, _height);
	RETURN_IF_FAILED(_converter->SetOutputType(0, outputType.get(), 0));

	// make sure the video processor works on GPU
	RETURN_IF_FAILED(_converter->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)_dxgiManager.get()));

	return S_OK;
}

HRESULT FrameGenerator::EnsureRenderTarget(UINT width, UINT height)
{
	if (!_dxgiManager) {
		_width = width;
		_height = height;

		RETURN_IF_FAILED(SetupD3D11Device());

		RETURN_IF_FAILED(CreateSharedCaptureWindowTexture());

		RETURN_IF_FAILED(CreateSharedParamsTexture());

		RETURN_IF_FAILED(SetupOffscreenRendering());

		RETURN_IF_FAILED(SetupNV12Converter());
	}

	return S_OK;
}

void FrameGenerator::DrawSharedCaptureWindow()
{
	if (_sharedCaptureWindowTexture == nullptr)
	{
		return;
	}

	float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	_dxDeviceContext->ClearRenderTargetView(_renderTargetView.get(), color);

	float xPosRate = 1.0f;
	float yPosRate = 1.0f;
	float rectRate = static_cast<float>(_width) / static_cast<float>(_height);
	float floatWindowWidth = static_cast<float>(_captureWindowWidth);
	float floatWindowHeight = static_cast<float>(_captureWindowHeight);

	if (floatWindowWidth > floatWindowHeight * rectRate) {
		yPosRate = floatWindowHeight * rectRate / floatWindowWidth;
	}
	else {
		xPosRate = floatWindowWidth / (floatWindowHeight * rectRate);
	}

	float widthTextureRate = static_cast<float>(_captureWindowWidth) 
		/ static_cast<float>(_captureTextureWidth);
	float heightTextureRate = static_cast<float>(_captureWindowHeight) 
		/ static_cast<float>(_captureTextureHeight);
	
	VertexType v[4] = {
		{{-xPosRate, yPosRate, 0}, {0, 0}},
		{{xPosRate, yPosRate, 0}, {widthTextureRate, 0}},
		{{-xPosRate, -yPosRate, 0}, {0, heightTextureRate}},
		{{xPosRate, -yPosRate, 0}, {widthTextureRate, heightTextureRate}}
	};

	wil::com_ptr_nothrow<ID3D11Buffer> vb;
	D3D11_SUBRESOURCE_DATA initData = {
		v, sizeof(v), 0
	};

	_dxDevice->CreateBuffer(&_vbDesc, &initData, &vb);

	UINT stride = sizeof(VertexType);
	UINT offset = 0;
	_dxDeviceContext->IASetVertexBuffers(0, 1, vb.addressof(), &stride, &offset);
	_dxDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	_dxDeviceContext->VSSetShader(_spriteVS.get(), 0, 0);
	_dxDeviceContext->PSSetShader(_spritePS.get(), 0, 0);
	_dxDeviceContext->IASetInputLayout(_spriteInputLayout.get());

	wil::com_ptr_nothrow<IDXGIKeyedMutex> mutex;
	_sharedCaptureWindowTexture->QueryInterface(IID_PPV_ARGS(mutex.put()));
	mutex->AcquireSync(MUTEX_KEY, INFINITE);
	_dxDeviceContext->PSSetShaderResources(0, 1, _shaderResourceView.addressof());
	_dxDeviceContext->Draw(4, 0);
	_dxDeviceContext->Flush();
	mutex->ReleaseSync(MUTEX_KEY);
}

void FrameGenerator::GetParamsFromTexture()
{
	if (_sharedParamsTexture == nullptr || _cpuParamsTexture == nullptr)
	{
		return;
	}

	wil::com_ptr_nothrow<IDXGIKeyedMutex> mutex;
	_sharedParamsTexture->QueryInterface(IID_PPV_ARGS(mutex.put()));
	mutex->AcquireSync(MUTEX_KEY, INFINITE);
	_dxDeviceContext->CopyResource(_cpuParamsTexture.get(), _sharedParamsTexture.get());
	mutex->ReleaseSync(MUTEX_KEY);

	wil::com_ptr_nothrow<IDXGISurface> dxgiSurface;
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

	_dxDeviceMutex->Enter();
	GetParamsFromTexture();

	DrawSharedCaptureWindow();
	_dxDeviceMutex->Leave();

	wil::com_ptr_nothrow<IMFMediaBuffer> mediaBuffer;

	// remove all existing buffers
	RETURN_IF_FAILED(sample->RemoveAllBuffers());

	// create a buffer from this and add to sample
	RETURN_IF_FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), _renderTexture.get(), 0, 0, &mediaBuffer));
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

	return S_OK;
}
