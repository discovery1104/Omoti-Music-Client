#include "pch.h"
#include "Renderer.h"
#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/event/events/RendererCleanupEvent.h"
#include "client/event/events/RendererInitEvent.h"
#include "client/Omoti.h"
#include "client/resource/InitResources.h"
#include "util/Util.h"
#include <fstream>
#include <dwrite_3.h>

namespace {
	constexpr wchar_t kEmbeddedFontFamily[] = L"Noto Sans JP";
	constexpr wchar_t kEmbeddedFontFileName[] = L"omoti_ui.ttf";
	ComPtr<IDWriteFontCollection> gEmbeddedFontCollection;

	std::wstring normalizeFontPath(std::filesystem::path const& path) {
		std::error_code ec;
		auto normalized = std::filesystem::weakly_canonical(path, ec);
		if (ec) {
			normalized = path.lexically_normal();
		}
		std::wstring value = normalized.wstring();
		std::transform(value.begin(), value.end(), value.begin(), towlower);
		return value;
	}

	std::wstring getLocalizedFamilyName(IDWriteLocalizedStrings* names) {
		if (!names) return {};

		UINT32 index = 0;
		BOOL exists = FALSE;
		if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists) {
			if (FAILED(names->FindLocaleName(L"en", &index, &exists)) || !exists) {
				index = 0;
			}
		}

		UINT32 length = 0;
		if (FAILED(names->GetStringLength(index, &length))) return {};
		std::wstring family(length + 1, L'\0');
		if (FAILED(names->GetString(index, family.data(), length + 1))) return {};
		family.resize(length);
		return family;
	}

	std::optional<std::wstring> resolveFontFamilyFromCollection(IDWriteFontCollection* collection) {
		if (!collection || collection->GetFontFamilyCount() == 0) return std::nullopt;

		ComPtr<IDWriteFontFamily> family;
		if (FAILED(collection->GetFontFamily(0, family.GetAddressOf())) || !family) {
			return std::nullopt;
		}

		ComPtr<IDWriteLocalizedStrings> familyNames;
		if (FAILED(family->GetFamilyNames(familyNames.GetAddressOf())) || !familyNames) {
			return std::nullopt;
		}

		std::wstring resolvedFamily = getLocalizedFamilyName(familyNames.Get());
		if (resolvedFamily.empty()) return std::nullopt;
		return resolvedFamily;
	}

	std::optional<std::wstring> resolveFontFamilyForPath(IDWriteFactory* dWriteFactory, std::filesystem::path const& fontPath) {
		if (!dWriteFactory || fontPath.empty()) return std::nullopt;

		ComPtr<IDWriteFontCollection> collection;
		if (FAILED(dWriteFactory->GetSystemFontCollection(collection.GetAddressOf(), TRUE)) || !collection) {
			return std::nullopt;
		}

		std::wstring targetPath = normalizeFontPath(fontPath);
		UINT32 familyCount = collection->GetFontFamilyCount();
		for (UINT32 familyIndex = 0; familyIndex < familyCount; familyIndex++) {
			ComPtr<IDWriteFontFamily> family;
			if (FAILED(collection->GetFontFamily(familyIndex, family.GetAddressOf())) || !family) continue;

			UINT32 fontCount = family->GetFontCount();
			for (UINT32 fontIndex = 0; fontIndex < fontCount; fontIndex++) {
				ComPtr<IDWriteFont> font;
				if (FAILED(family->GetFont(fontIndex, font.GetAddressOf())) || !font) continue;

				ComPtr<IDWriteFontFace> fontFace;
				if (FAILED(font->CreateFontFace(fontFace.GetAddressOf())) || !fontFace) continue;

				UINT32 fileCount = 0;
				fontFace->GetFiles(&fileCount, nullptr);
				if (fileCount == 0) continue;

				std::vector<IDWriteFontFile*> rawFiles(fileCount, nullptr);
				if (FAILED(fontFace->GetFiles(&fileCount, rawFiles.data()))) continue;

				std::vector<ComPtr<IDWriteFontFile>> files(fileCount);
				for (UINT32 i = 0; i < fileCount; i++) {
					files[i].Attach(rawFiles[i]);
				}

				for (UINT32 fileIndex = 0; fileIndex < fileCount; fileIndex++) {
					auto* file = files[fileIndex].Get();
					if (!file) continue;

					ComPtr<IDWriteFontFileLoader> loader;
					if (FAILED(file->GetLoader(loader.GetAddressOf())) || !loader) continue;

					ComPtr<IDWriteLocalFontFileLoader> localLoader;
					if (FAILED(loader.As(&localLoader)) || !localLoader) continue;

					void const* refKey = nullptr;
					UINT32 refKeySize = 0;
					if (FAILED(file->GetReferenceKey(&refKey, &refKeySize)) || !refKey || refKeySize == 0) continue;

					UINT32 pathLength = 0;
					if (FAILED(localLoader->GetFilePathLengthFromKey(refKey, refKeySize, &pathLength)) || pathLength == 0) continue;

					std::wstring filePath(pathLength + 1, L'\0');
					if (FAILED(localLoader->GetFilePathFromKey(refKey, refKeySize, filePath.data(), pathLength + 1))) continue;
					filePath.resize(pathLength);

					if (normalizeFontPath(filePath) != targetPath) continue;

					ComPtr<IDWriteLocalizedStrings> familyNames;
					if (FAILED(family->GetFamilyNames(familyNames.GetAddressOf())) || !familyNames) continue;

					std::wstring resolvedFamily = getLocalizedFamilyName(familyNames.Get());
					if (!resolvedFamily.empty()) {
						return resolvedFamily;
					}
				}
			}
		}

		return std::nullopt;
	}

	HRESULT createEmbeddedFontCollection(IDWriteFactory* dWriteFactory, std::filesystem::path const& fontPath, IDWriteFontCollection** fontCollection) {
		if (!fontCollection) return E_INVALIDARG;
		*fontCollection = nullptr;
		if (!dWriteFactory || fontPath.empty()) return E_INVALIDARG;

		ComPtr<IDWriteFactory5> factory5;
		if (FAILED(dWriteFactory->QueryInterface(IID_PPV_ARGS(factory5.GetAddressOf()))) || !factory5) {
			return E_NOINTERFACE;
		}

		ComPtr<IDWriteFontFile> fontFile;
		HRESULT hr = dWriteFactory->CreateFontFileReference(fontPath.wstring().c_str(), nullptr, fontFile.GetAddressOf());
		if (FAILED(hr) || !fontFile) return hr;

		ComPtr<IDWriteFontSetBuilder1> fontSetBuilder;
		hr = factory5->CreateFontSetBuilder(fontSetBuilder.GetAddressOf());
		if (FAILED(hr) || !fontSetBuilder) return hr;

		hr = fontSetBuilder->AddFontFile(fontFile.Get());
		if (FAILED(hr)) return hr;

		ComPtr<IDWriteFontSet> fontSet;
		hr = fontSetBuilder->CreateFontSet(fontSet.GetAddressOf());
		if (FAILED(hr) || !fontSet) return hr;

		ComPtr<IDWriteFontCollection1> collection1;
		hr = factory5->CreateFontCollectionFromFontSet(fontSet.Get(), collection1.GetAddressOf());
		if (FAILED(hr) || !collection1) return hr;

		*fontCollection = collection1.Detach();
		return S_OK;
	}
}

Renderer::~Renderer() {
    // ...
	releaseAllResources();
}

bool Renderer::init(IDXGISwapChain* chain) {
	if (!shouldInit) return false;

	bool isDX12 = true;
	if (!dx12Removed && SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&gameDevice12))) && Omoti::get().shouldForceDX11()) {
		static_cast<ID3D12Device5*>(gameDevice12.Get())->RemoveDevice();
		bufferCount = 1;
		Logger::Info("Force DX11 active");
		isDX12 = false;
		dx12Removed = true;
		return false;
	}
    this->gameSwapChain = chain;


	if (!swapChain4)
		ThrowIfFailed(chain->QueryInterface(&swapChain4));

	if (Omoti::get().shouldForceDX11()) {
		isDX12 = false;
	}
	
	if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&gameDevice12))) && !dx12Removed && Omoti::get().shouldForceDX11()) {
		static_cast<ID3D12Device5*>(gameDevice12.Get())->RemoveDevice();
		bufferCount = 1;
		Logger::Info("Force DX11 active");
		isDX12 = false;
		dx12Removed = true;
		return init(chain);
	}

	else gameDevice11 = nullptr;

	if (!gameDevice11.Get() || !gameDevice12.Get()) {
		if (Omoti::get().shouldForceDX11() || FAILED(chain->GetDevice(IID_PPV_ARGS(&gameDevice12)))) {
			ThrowIfFailed(chain->GetDevice(IID_PPV_ARGS(&gameDevice11)));
			Logger::Info("Using DX11");
			d3dDevice = gameDevice11.Get();
			d3dDevice->GetImmediateContext(d3dCtx.GetAddressOf());
			bufferCount = 1;
			isDX11 = true;
			isDX12 = false;
		}
		else {
			Logger::Info("Using DX12");
			bufferCount = 3;
		}
	}

	if (gameDevice12.Get()) {
		if (!this->commandQueue) {
			return false;
		}
	}

	createDeviceIndependentResources();

#if Omoti_DEBUG
	if (gameDevice12) {
		ComPtr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(gameDevice12->QueryInterface(IID_PPV_ARGS(&infoQueue))))
		{
			D3D12_MESSAGE_SEVERITY severities[] =
			{
				D3D12_MESSAGE_SEVERITY_INFO,
			};

			D3D12_MESSAGE_ID denyIds[] =
			{
				D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,
			};

			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumSeverities = _countof(severities);
			filter.DenyList.pSeverityList = severities;
			filter.DenyList.NumIDs = _countof(denyIds);
			filter.DenyList.pIDList = denyIds;

			infoQueue->PushStorageFilter(&filter);
		}
    }
#endif
	if (gameDevice12) {
		D3D11On12CreateDevice(gameDevice12.Get(),
#ifdef Omoti_DEBUG
			D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG
#else
			D3D11_CREATE_DEVICE_BGRA_SUPPORT
#endif
			, nullptr, 0,
			reinterpret_cast<IUnknown**>(&commandQueue),
			1, 0, &d3dDevice, d3dCtx.GetAddressOf(),
			nullptr
		);
		d3dDevice->QueryInterface(d3d11On12Device.GetAddressOf());
		// TODO: is this necessary, we already reference the d3ddevice in 11on12createdevice
		d3dDevice->GetImmediateContext(&d3dCtx);
	}

	ThrowIfFailed(d3dDevice->QueryInterface(dxgiDevice.GetAddressOf()));
	ThrowIfFailed(d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf()));
	ThrowIfFailed(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, d2dCtx.GetAddressOf()));
	d2dCtx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

	renderTargets.clear();
	d3d11Targets.clear();
	for (int i = 0; i < bufferCount; i++) {
		ComPtr<IDXGISurface> surf;
		if (gameDevice12.Get()) {
			ID3D12Resource* backBuffer;
			ThrowIfFailed(swapChain4->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
			D3D11_RESOURCE_FLAGS flags{ D3D11_BIND_RENDER_TARGET };
			ID3D11Resource* res;
			ThrowIfFailed(d3d11On12Device->CreateWrappedResource(backBuffer, &flags, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT, __uuidof(ID3D11Resource), (void**)&res));
			d3d11Targets.push_back(res);
			d3d12Targets.push_back(backBuffer);
		}
		else {
			ID3D11Resource* backBuffer;
			ThrowIfFailed(swapChain4->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
			d3d11Targets.push_back(backBuffer);
		}

		auto ss = d2dCtx->GetPixelSize();

		ThrowIfFailed(d3d11Targets[i]->QueryInterface(surf.GetAddressOf()));
		//auto info = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView(); // crashes
		float dpiX = 0;// info.LogicalDpi();
		float dpiY = 0;// info.LogicalDpi();
#pragma warning(push)
#pragma warning(disable : 4996)
		d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
#pragma warning(pop)

		D2D1_BITMAP_PROPERTIES1 prop = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
			dpiX, dpiY);
		ID2D1Bitmap1* targ;
		ThrowIfFailed(d2dCtx->CreateBitmapFromDxgiSurface(surf.Get(), &prop, &targ));
		renderTargets.push_back(targ);
	}

	// brushes
	createDeviceDependentResources();

	// blur buffers
	blurBuffers = { 0 };

	auto idx = swapChain4->GetCurrentBackBufferIndex();
	ID2D1Bitmap1* myBitmap = this->renderTargets[idx];
	ID2D1Bitmap1* bmp;
	D2D1_SIZE_U bitmapSize = myBitmap->GetPixelSize();
	D2D1_PIXEL_FORMAT pixelFormat = myBitmap->GetPixelFormat();

	ThrowIfFailed(d2dCtx->CreateBitmap(bitmapSize, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, pixelFormat), &bmp));
	this->blurBuffers[0] = bmp;
	this->hasCopiedBitmap = true;

	hasInit = true;
	firstInit = true;

	RendererInitEvent ev{};
	Eventing::get().dispatch(ev);

    return true;
}

HRESULT Renderer::reinit() {
	releaseAllResources(false);
    hasInit = false;
    return S_OK;
}

void Renderer::setShouldReinit() {
	shouldReinit = true;
}

void Renderer::setShouldInit() {
	shouldInit = true;
}

std::shared_lock<std::shared_mutex> Renderer::lock() {
	return std::shared_lock<std::shared_mutex>(mutex);
}

void Renderer::render() {
	if (gameDevice12) {
		ThrowIfFailed(gameDevice12->GetDeviceRemovedReason());
	}
	else {
		ThrowIfFailed(gameDevice11->GetDeviceRemovedReason());
	}

	if (shouldReinit) {
		shouldReinit = false;
		reinit();
		return;
	}

	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);
	deltaTime = std::clamp(static_cast<float>(diff.count()) / 17.f, 0.02f, 20.f); // based on 60-ish FPS

	lastTime = now;

	if (!hasInit) return;

	auto idx = swapChain4->GetCurrentBackBufferIndex();
	if (gameDevice12) {
		d3d11On12Device->AcquireWrappedResources(&d3d11Targets[idx], 1);
	}


	d2dCtx->SetTarget(renderTargets[idx]);
	d2dCtx->BeginDraw();

	RenderOverlayEvent ev{ d2dCtx.Get() };
	Eventing::get().dispatch(ev);

	ThrowIfFailed(d2dCtx->EndDraw());

	if (gameDevice12) {
		d3d11On12Device->ReleaseWrappedResources(&d3d11Targets[idx], 1);
	}

	d3dCtx->Flush();
	this->hasCopiedBitmap = false;
}

void Renderer::releaseAllResources(bool indep) {
	RendererCleanupEvent ev{};
	Eventing::get().dispatch(ev);

	if (d2dCtx) d2dCtx->SetTarget(nullptr);

	bool isDX12 = gameDevice12;
	if (isDX12) {
		ID3D11RenderTargetView* nullViews[] = { nullptr, nullptr, nullptr };
		if (d3dCtx)
			d3dCtx->OMSetRenderTargets(3, nullViews, nullptr);
	}
	else {
		ID3D11RenderTargetView* nullViews[] = { nullptr };
		if (d3dCtx)
			d3dCtx->OMSetRenderTargets(1, nullViews, nullptr);
	}

	cachedLayouts.clear();
	gameDevice11 = nullptr;

	for (auto& i : renderTargets) {
		SafeRelease(&i);
	}
	renderTargets.clear();

	if (isDX12) {
		for (auto& i : d3d12Targets) {
			SafeRelease(&i);
		}
		d3d12Targets.clear();
	}

	if (isDX12) gameDevice12 = nullptr;
	SafeRelease(&swapChain4);
	SafeRelease(&d3dDevice);
	d2dDevice = nullptr;
	d2dCtx = nullptr;

	for (auto& i : d3d11Targets) {
		SafeRelease(&i);
	}
	d3d11Targets.clear();

	releaseDeviceResources();

	if (d3dCtx) {
		d3dCtx->Flush();
	}

	dxgiDevice = nullptr;
	SafeRelease(&swapChain4);

	d3d11On12Device = nullptr;
	d3dCtx = nullptr;

	for (auto& mb : this->blurBuffers) {
		SafeRelease(&mb);
	}

	this->blurBuffers.clear();

	if (indep) releaseDeviceIndependentResources();
}

void Renderer::createTextFormats() {
	float fontSize = 10.f;
	IDWriteFontCollection* primaryCollection = gEmbeddedFontCollection.Get();
	IDWriteFontCollection* secondaryCollection = nullptr;
	DWRITE_FONT_WEIGHT primaryRegularWeight = primaryCollection ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_NORMAL;
	DWRITE_FONT_WEIGHT primarySemiLightWeight = primaryCollection ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_LIGHT;
	DWRITE_FONT_WEIGHT primaryLightWeight = primaryCollection ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_LIGHT;
	DWRITE_FONT_WEIGHT secondaryRegularWeight = secondaryCollection ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_NORMAL;
	DWRITE_FONT_WEIGHT secondarySemiLightWeight = secondaryCollection ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_LIGHT;
	DWRITE_FONT_WEIGHT secondaryLightWeight = secondaryCollection ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_LIGHT;
	if (primaryCollection && _wcsicmp(fontFamily2.c_str(), fontFamily.c_str()) == 0) {
		secondaryCollection = primaryCollection;
		secondaryRegularWeight = DWRITE_FONT_WEIGHT_NORMAL;
		secondarySemiLightWeight = DWRITE_FONT_WEIGHT_NORMAL;
		secondaryLightWeight = DWRITE_FONT_WEIGHT_NORMAL;
	}

	ThrowIfFailed(dWriteFactory->CreateTextFormat(fontFamily.c_str(),
		primaryCollection,
		primaryRegularWeight,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		fontSize,
		L"en-us",
		this->primaryFont.GetAddressOf()));

	ThrowIfFailed(dWriteFactory->CreateTextFormat(fontFamily.c_str(),
		primaryCollection,
		primarySemiLightWeight,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		fontSize,
		L"en-us",
		this->primarySemilight.GetAddressOf()));

	ThrowIfFailed(dWriteFactory->CreateTextFormat(fontFamily.c_str(),
		primaryCollection,
		primaryLightWeight,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		fontSize,
		L"en-us",
		this->primaryLight.GetAddressOf()));

	ThrowIfFailed(dWriteFactory->CreateTextFormat(fontFamily2.c_str(),
		secondaryCollection,
		secondaryRegularWeight,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		fontSize,
		L"en-us",
		this->secondaryFont.GetAddressOf()));

	ThrowIfFailed(dWriteFactory->CreateTextFormat(fontFamily2.c_str(),
		secondaryCollection,
		secondarySemiLightWeight,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		fontSize,
		L"en-us",
		this->secondarySemilight.GetAddressOf()));

	ThrowIfFailed(dWriteFactory->CreateTextFormat(fontFamily2.c_str(),
		secondaryCollection,
		secondaryLightWeight,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		fontSize,
		L"en-us",
		this->secondaryLight.GetAddressOf()));

}

void Renderer::releaseTextFormats() {
	primaryFont = nullptr;
	primaryLight = nullptr;
	primarySemilight = nullptr;
	secondaryFont = nullptr;
	secondaryLight = nullptr;
	secondarySemilight = nullptr;
}

void Renderer::createDeviceIndependentResources() {
	tryInstallEmbeddedFont();
	ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dWriteFactory.GetAddressOf())));
	ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory3), (void**)d2dFactory.GetAddressOf()));
	if (!embeddedFontPath.empty()) {
		HRESULT collectionHr = createEmbeddedFontCollection(dWriteFactory.Get(), embeddedFontPath, gEmbeddedFontCollection.ReleaseAndGetAddressOf());
		if (FAILED(collectionHr)) {
			Logger::Warn("Failed to create embedded font collection from {} ({:#x})", embeddedFontPath.string(), static_cast<unsigned int>(collectionHr));
		}
	}
	if (auto resolvedFamily = resolveFontFamilyFromCollection(gEmbeddedFontCollection.Get())) {
		fontFamily = *resolvedFamily;
		if (fontFamily2 == kEmbeddedFontFamily || fontFamily2 == L"Segoe UI" || fontFamily2.empty()) {
			fontFamily2 = *resolvedFamily;
		}
		Logger::Info("Resolved UI font family '{}' from embedded collection", util::WStrToStr(*resolvedFamily));
	}
	else if (auto resolvedFamily = resolveFontFamilyForPath(dWriteFactory.Get(), embeddedFontPath)) {
		fontFamily = *resolvedFamily;
		if (fontFamily2 == kEmbeddedFontFamily || fontFamily2 == L"Segoe UI" || fontFamily2.empty()) {
			fontFamily2 = *resolvedFamily;
		}
		Logger::Info("Resolved UI font family '{}' from {}", util::WStrToStr(*resolvedFamily), embeddedFontPath.string());
	}
	createTextFormats();
	CoInitialize(nullptr);
	ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory2), reinterpret_cast<void**>(this->wicFactory.GetAddressOf())));
}

void Renderer::createDeviceDependentResources() {
	D2D1_COLOR_F col = { 1.f, 0.f, 0.f, 1.f };
	ThrowIfFailed(d2dCtx->CreateSolidColorBrush(col, solidBrush.GetAddressOf()));
	ThrowIfFailed(d2dCtx->CreateEffect(CLSID_D2D1Shadow, this->shadowEffect.GetAddressOf()));
	ThrowIfFailed(d2dCtx->CreateEffect(CLSID_D2D12DAffineTransform, this->affineTransformEffect.GetAddressOf()));
	ThrowIfFailed(d2dCtx->CreateEffect(CLSID_D2D1GaussianBlur, this->blurEffect.GetAddressOf()));
}

void Renderer::releaseDeviceIndependentResources() {
	//CoUninitialize();
	gEmbeddedFontCollection = nullptr;
	dWriteFactory = nullptr;
	wicFactory = nullptr;
	d2dFactory = nullptr;
	releaseTextFormats();
	uninstallEmbeddedFont();
}

void Renderer::tryInstallEmbeddedFont() {
	if (embeddedFontCount > 0) return;

	try {
		std::error_code ec;
		auto fontDir = util::GetOmotiPath() / "Assets" / "Fonts";
		std::filesystem::create_directories(fontDir, ec);

		embeddedFontPath = fontDir / kEmbeddedFontFileName;
		auto res = GET_RESOURCE(fonts_omoti_ui_ttf);
		if (res.size() == 0) {
			Logger::Warn("Embedded font resource is empty; using default font");
			return;
		}

		bool shouldWrite = true;
		if (std::filesystem::exists(embeddedFontPath, ec)) {
			auto sz = std::filesystem::file_size(embeddedFontPath, ec);
			shouldWrite = ec || sz != res.size();
		}

		if (shouldWrite) {
			std::ofstream ofs(embeddedFontPath, std::ios::binary | std::ios::trunc);
			if (!ofs.is_open()) {
				Logger::Warn("Could not write embedded font file; using default font");
				embeddedFontPath.clear();
				return;
			}
			ofs.write(res.data(), static_cast<std::streamsize>(res.size()));
		}

		embeddedFontCount = AddFontResourceExW(embeddedFontPath.wstring().c_str(), FR_PRIVATE, nullptr);
		if (embeddedFontCount > 0) {
			fontFamily = kEmbeddedFontFamily;
			if (fontFamily2 == L"Segoe UI" || fontFamily2.empty()) {
				fontFamily2 = kEmbeddedFontFamily;
			}
			Logger::Info("Loaded UI font from embedded DLL resource");
		}
		else {
			Logger::Warn("Could not register embedded font; using default font");
			embeddedFontPath.clear();
		}
	}
	catch (...) {
		Logger::Warn("Failed to initialize embedded font; using default font");
		embeddedFontPath.clear();
	}
}

void Renderer::uninstallEmbeddedFont() {
	if (embeddedFontCount <= 0 || embeddedFontPath.empty()) return;
	for (int i = 0; i < embeddedFontCount; i++) {
		RemoveFontResourceExW(embeddedFontPath.wstring().c_str(), FR_PRIVATE, nullptr);
	}
	embeddedFontCount = 0;
}

void Renderer::releaseDeviceResources() {
	solidBrush = nullptr;
	shadowEffect = nullptr;
	affineTransformEffect = nullptr;
	affineTransformEffect = nullptr;
	shadowEffect = nullptr;
	blurEffect = nullptr;
}

IDWriteTextLayout* Renderer::getLayout(IDWriteTextFormat* fmt, std::wstring const& str, bool cache) {
	auto hash = util::fnv1a_64w(str);
	auto it = this->cachedLayouts.find(hash);
	if (it != cachedLayouts.end()) {
		if (it->second.first == fmt) {
			return it->second.second.Get();
		}
	}

	auto [width, height] = getScreenSize();
	ComPtr<IDWriteTextLayout> layout;
	dWriteFactory->CreateTextLayout(str.c_str(), static_cast<uint32_t>(str.size()), fmt, width, height, layout.GetAddressOf());
	this->cachedLayouts[hash] = { fmt, layout };
	return layout.Get(); // Im pretty sure it implicitly adds a ref when I add it to cachedLayouts
}
