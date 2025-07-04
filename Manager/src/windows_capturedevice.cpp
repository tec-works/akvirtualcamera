#ifdef _WIN32

#include "windows_capturedevice.h"
#include "VCamUtils/src/logger.h" // For AkLogDebug, AkLogError, etc.
#include <iostream> // For std::cerr (temporary)

// Suppress warnings from dshow.h if any, specific to MSVC
#pragma comment(lib, "strmiids") // Links to strmiids.lib automatically

namespace AkVCam {

// --- SampleGrabberCallback Implementation ---
WindowsCaptureDevice::SampleGrabberCallback::SampleGrabberCallback(WindowsCaptureDevice* pOwner, std::string deviceId)
    : m_refCount(1), m_pOwner(pOwner), m_deviceId(std::move(deviceId)) {
    AkLogDebug() << "SampleGrabberCallback created." << std::endl;
}

STDMETHODIMP_(ULONG) WindowsCaptureDevice::SampleGrabberCallback::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) WindowsCaptureDevice::SampleGrabberCallback::Release() {
    ULONG refCount = InterlockedDecrement(&m_refCount);
    if (refCount == 0) {
        delete this;
    }
    return refCount;
}

STDMETHODIMP WindowsCaptureDevice::SampleGrabberCallback::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (riid == IID_IUnknown) {
        *ppv = static_cast<IUnknown*>(this);
    } else if (riid == IID_ISampleGrabberCB) {
        *ppv = static_cast<ISampleGrabberCB*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP WindowsCaptureDevice::SampleGrabberCallback::SampleCB(double SampleTime, IMediaSample *pSample) {
    if (!m_pOwner || !pSample) {
        return E_POINTER;
    }

    BYTE *pBuffer = nullptr;
    HRESULT hr = pSample->GetPointer(&pBuffer);
    if (FAILED(hr) || pBuffer == nullptr) {
        AkLogError() << "SampleCB: Failed to get sample pointer." << std::endl;
        return E_FAIL;
    }

    long bufferLen = pSample->GetActualDataLength();
    VideoFormat currentFormat = m_pOwner->getCurrentFormat();

    if (currentFormat.fourcc() == 0 || currentFormat.width() == 0 || currentFormat.height() == 0) {
         AkLogError() << "SampleCB: Owner's current format is invalid." << std::endl;
         return E_FAIL;
    }

    // Check if the buffer length matches the expected size from the format
    // This is a basic check; a more robust check would involve VideoFormat::size()
    // if (bufferLen != static_cast<long>(currentFormat.width() * currentFormat.height() * (currentFormat.bitsPerPixel() / 8))) {
    //     AkLogWarning() << "SampleCB: Buffer length " << bufferLen << " does not match expected size for format "
    //                    << VideoFormat::stringFromFourcc(currentFormat.fourcc()) << std::endl;
    //     // Potentially handle this, or just proceed if it's a known benign difference (e.g. padding)
    // }


    if (m_pOwner->m_frameCallback) {
        VideoFrame videoFrame(currentFormat);
        if (videoFrame.data().size() >= static_cast<size_t>(bufferLen)) {
            memcpy(videoFrame.data().data(), pBuffer, bufferLen);
            m_pOwner->m_frameCallback(videoFrame, m_deviceId);
        } else {
            AkLogError() << "SampleCB: VideoFrame buffer too small for sample data." << std::endl;
        }
    }
    return S_OK;
}

STDMETHODIMP WindowsCaptureDevice::SampleGrabberCallback::BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen) {
    // This method is not used if SetBufferSamples(TRUE) is called, as SampleCB will be used instead.
    return E_NOTIMPL;
}


// --- WindowsCaptureDevice Implementation ---
WindowsCaptureDevice::WindowsCaptureDevice()
    : m_isCapturing(false), m_isOpened(false), m_comInitialized(false) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        m_comInitialized = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        // COM already initialized with a different concurrency model. This is usually okay.
        AkLogWarning() << "COM already initialized with a different concurrency model." << std::endl;
        m_comInitialized = true; // Still treat as success for cleanup
    }
    else {
        AkLogError() << "Failed to initialize COM: " << hr << std::endl;
    }
    AkLogDebug() << "WindowsCaptureDevice created." << std::endl;
}

WindowsCaptureDevice::~WindowsCaptureDevice() {
    close(); // Ensure everything is released
    if (m_comInitialized) {
        CoUninitialize();
    }
    AkLogDebug() << "WindowsCaptureDevice destroyed." << std::endl;
}

void WindowsCaptureDevice::releaseGraph() {
    if (m_pControl) m_pControl->Stop();

    // TODO: Properly disconnect and remove filters before releasing them
    // This is a simplified release for now. For robust applications,
    // enumerate filters, disconnect pins, remove filters from graph, then release.

    m_pSampleGrabber.Release();
    m_pSampleGrabberFilter.Release();
    m_pSourceFilter.Release();
    m_pControl.Release();
    m_pGraph.Release();
    m_pSampleGrabberCallback.Release(); // Callback is CComPtr, will release
}

std::vector<PhysicalCameraInfo> WindowsCaptureDevice::enumerateDevices() {
    std::vector<PhysicalCameraInfo> devices;
    if (!m_comInitialized) return devices;

    HRESULT hr;
    CComPtr<ICreateDevEnum> pDevEnum;
    CComPtr<IEnumMoniker> pEnum;

    hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pDevEnum));
    if (FAILED(hr)) {
        AkLogError() << "Failed to create SystemDeviceEnum: " << hr << std::endl;
        return devices;
    }

    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
    if (FAILED(hr) || !pEnum) {
        AkLogError() << "Failed to create VideoInputDeviceCategory enumerator: " << hr << std::endl;
        return devices;
    }

    CComPtr<IMoniker> pMoniker;
    while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
        PhysicalCameraInfo info;
        CComPtr<IPropertyBag> pPropBag;
        hr = pMoniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&pPropBag));
        if (FAILED(hr)) {
            pMoniker.Release();
            continue;
        }

        VARIANT var;
        VariantInit(&var);

        // Get FriendlyName
        hr = pPropBag->Read(L"FriendlyName", &var, nullptr);
        if (SUCCEEDED(hr) && var.vt == VT_BSTR) {
            // Convert BSTR to std::string
            char friendlyNameChars[256];
            WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, friendlyNameChars, 256, nullptr, nullptr);
            info.friendlyName = friendlyNameChars;
        }
        VariantClear(&var);

        // Get DevicePath (often used as a unique ID)
        hr = pPropBag->Read(L"DevicePath", &var, nullptr);
        if (SUCCEEDED(hr) && var.vt == VT_BSTR) {
            char devicePathChars[512];
            WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, devicePathChars, 512, nullptr, nullptr);
            info.deviceId = devicePathChars;
        }
        VariantClear(&var);

        // Description can be same as FriendlyName or more detailed if available
        info.description = info.friendlyName;

        if (!info.deviceId.empty()) {
            devices.push_back(info);
            AkLogDebug() << "Found device: " << info.friendlyName << " (ID: " << info.deviceId << ")" << std::endl;
        }
        pMoniker.Release();
    }
    return devices;
}

CComPtr<IBaseFilter> WindowsCaptureDevice::createSourceFilter(const std::string& deviceId) {
    CComPtr<ICreateDevEnum> pDevEnum;
    CComPtr<IEnumMoniker> pEnum;
    CComPtr<IBaseFilter> pSourceFilter;

    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));
    if (FAILED(hr)) return nullptr;

    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
    if (FAILED(hr) || !pEnum) return nullptr;

    CComPtr<IMoniker> pMoniker;
    while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
        CComPtr<IPropertyBag> pPropBag;
        hr = pMoniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&pPropBag));
        if (FAILED(hr)) {
            pMoniker.Release();
            continue;
        }

        VARIANT var;
        VariantInit(&var);
        hr = pPropBag->Read(L"DevicePath", &var, nullptr);
        if (SUCCEEDED(hr) && var.vt == VT_BSTR) {
            char currentDevicePathChars[512];
            WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, currentDevicePathChars, 512, nullptr, nullptr);
            std::string currentDevicePath = currentDevicePathChars;
            VariantClear(&var);

            if (currentDevicePath == deviceId) {
                hr = pMoniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&pSourceFilter));
                if (SUCCEEDED(hr)) {
                    pMoniker.Release();
                    return pSourceFilter;
                }
            }
        }
        VariantClear(&var);
        pMoniker.Release();
    }
    return nullptr;
}


CComPtr<IPin> WindowsCaptureDevice::getPin(IBaseFilter *pFilter, PIN_DIRECTION pinDir, int pinNum) {
    CComPtr<IEnumPins> pEnumPins;
    CComPtr<IPin> pPin;
    HRESULT hr = pFilter->EnumPins(&pEnumPins);
    if (FAILED(hr)) return nullptr;

    ULONG fetched;
    while (pEnumPins->Next(1, &pPin, &fetched) == S_OK) {
        PIN_DIRECTION currentPinDir;
        hr = pPin->QueryDirection(&currentPinDir);
        if (SUCCEEDED(hr) && currentPinDir == pinDir) {
            if (pinNum == 0) return pPin; // Found the first matching pin
            pinNum--;
        }
        pPin.Release();
    }
    return nullptr;
}


bool WindowsCaptureDevice::open(const std::string& deviceId, FrameCallback callback, const VideoFormat* preferredFormat) {
    if (!m_comInitialized) {
        AkLogError() << "COM not initialized, cannot open device." << std::endl;
        return false;
    }
    if (m_isOpened) close(); // Close previous if any

    m_deviceId = deviceId;
    m_frameCallback = callback;
    HRESULT hr;

    // 1. Create Graph Builder
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pGraph));
    if (FAILED(hr)) {
        AkLogError() << "Failed to create Filter Graph Manager: " << hr << std::endl;
        return false;
    }

    // 2. Get MediaControl
    hr = m_pGraph->QueryInterface(IID_PPV_ARGS(&m_pControl));
    if (FAILED(hr)) {
        AkLogError() << "Failed to query IMediaControl: " << hr << std::endl;
        releaseGraph();
        return false;
    }

    // 3. Create Source Filter for the selected device
    m_pSourceFilter = createSourceFilter(deviceId);
    if (!m_pSourceFilter) {
        AkLogError() << "Failed to create source filter for device: " << deviceId << std::endl;
        releaseGraph();
        return false;
    }
    hr = m_pGraph->AddFilter(m_pSourceFilter, L"Video Capture Source");
    if (FAILED(hr)) {
        AkLogError() << "Failed to add source filter to graph: " << hr << std::endl;
        releaseGraph();
        return false;
    }

    // 4. Create Sample Grabber filter
    hr = CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pSampleGrabberFilter));
    if (FAILED(hr)) {
        AkLogError() << "Failed to create Sample Grabber filter: " << hr << std::endl;
        releaseGraph();
        return false;
    }
    hr = m_pSampleGrabberFilter->QueryInterface(IID_PPV_ARGS(&m_pSampleGrabber));
    if (FAILED(hr)) {
        AkLogError() << "Failed to query ISampleGrabber interface: " << hr << std::endl;
        releaseGraph();
        return false;
    }

    // Configure Sample Grabber
    AM_MEDIA_TYPE mt;
    ZeroMemory(&mt, sizeof(AM_MEDIA_TYPE));
    mt.majortype = MEDIATYPE_Video;
    mt.subtype = MEDIASUBTYPE_RGB24; // Prefer RGB24 for easy VideoFrame conversion
    // mt.formattype = FORMAT_VideoInfo; // Important for some webcams
    hr = m_pSampleGrabber->SetMediaType(&mt);
    if (FAILED(hr)) {
        AkLogWarning() << "Failed to set Sample Grabber media type to RGB24, trying YUY2: " << hr << std::endl;
        mt.subtype = MEDIASUBTYPE_YUY2;
        hr = m_pSampleGrabber->SetMediaType(&mt);
        if (FAILED(hr)) {
            AkLogError() << "Failed to set Sample Grabber media type (RGB24 or YUY2): " << hr << std::endl;
            releaseGraph();
            return false;
        }
    }

    m_pSampleGrabber->SetBufferSamples(TRUE); // Process samples in SampleCB
    m_pSampleGrabber->SetOneShot(FALSE);      // Continuous grabbing

    m_pSampleGrabberCallback = new SampleGrabberCallback(this, m_deviceId);
    // m_pSampleGrabberCallback->AddRef(); // CComPtr handles AddRef
    hr = m_pSampleGrabber->SetCallback(m_pSampleGrabberCallback, 0); // 0 for SampleCB, 1 for BufferCB
     if (FAILED(hr)) {
        AkLogError() << "Failed to set Sample Grabber callback: " << hr << std::endl;
        releaseGraph();
        return false;
    }

    hr = m_pGraph->AddFilter(m_pSampleGrabberFilter, L"Sample Grabber");
    if (FAILED(hr)) {
        AkLogError() << "Failed to add Sample Grabber filter to graph: " << hr << std::endl;
        releaseGraph();
        return false;
    }

    // 5. Connect Source to Sample Grabber
    CComPtr<IPin> pSourceOutPin = getPin(m_pSourceFilter, PINDIR_OUTPUT);
    CComPtr<IPin> pGrabberInPin = getPin(m_pSampleGrabberFilter, PINDIR_INPUT);

    if (!pSourceOutPin || !pGrabberInPin) {
        AkLogError() << "Failed to get pins for connecting source to sample grabber." << std::endl;
        releaseGraph();
        return false;
    }

    // Format Negotiation
    CComPtr<IAMStreamConfig> pStreamConfig;
    hr = pSourceOutPin->QueryInterface(IID_PPV_ARGS(&pStreamConfig));
    if (SUCCEEDED(hr)) {
        int iCount = 0, iSize = 0;
        hr = pStreamConfig->GetNumberOfCapabilities(&iCount, &iSize);
        if (SUCCEEDED(hr) && iSize == sizeof(VIDEO_STREAM_CONFIG_CAPS)) {
            bool formatSet = false;
            if (preferredFormat) {
                 // Try to set preferred format
                for (int iFormat = 0; iFormat < iCount; iFormat++) {
                    AM_MEDIA_TYPE *pmtConfig;
                    VIDEO_STREAM_CONFIG_CAPS scc;
                    hr = pStreamConfig->GetStreamCaps(iFormat, &pmtConfig, (BYTE*)&scc);
                    if (SUCCEEDED(hr)) {
                        VIDEOINFOHEADER* vih = reinterpret_cast<VIDEOINFOHEADER*>(pmtConfig->pbFormat);
                        if (vih && pmtConfig->subtype == VideoFormat::fourccToGuid(preferredFormat->fourcc()) &&
                            vih->bmiHeader.biWidth == preferredFormat->width() &&
                            vih->bmiHeader.biHeight == preferredFormat->height()) {
                            // TODO: Check frame rate if preferredFormat includes it
                            hr = pStreamConfig->SetFormat(pmtConfig);
                            if (SUCCEEDED(hr)) {
                                m_currentFormat = *preferredFormat; // Assuming perfect match for now
                                AkLogInfo() << "Set preferred format: " << VideoFormat::stringFromFourcc(m_currentFormat.fourcc())
                                            << " " << m_currentFormat.width() << "x" << m_currentFormat.height() << std::endl;
                                formatSet = true;
                            }
                        }
                        // DeleteMediaType(pmtConfig); // TODO: Add helper for this
                        if (pmtConfig->cbFormat != 0) { CoTaskMemFree((PVOID)pmtConfig->pbFormat); pmtConfig->cbFormat = 0; pmtConfig->pbFormat = NULL; }
                        if (pmtConfig->pUnk != NULL) { pmtConfig->pUnk->Release(); pmtConfig->pUnk = NULL; }
                        CoTaskMemFree((PVOID)pmtConfig);

                        if (formatSet) break;
                    }
                }
            }
            if (!formatSet) { // Set a default or first available format
                AM_MEDIA_TYPE *pmtConfig = nullptr;
                VIDEO_STREAM_CONFIG_CAPS scc;
                // Choose a common format like 640x480 or the first one if specific logic is not added
                hr = pStreamConfig->GetStreamCaps(0, &pmtConfig, (BYTE*)&scc); // Get first format
                if (SUCCEEDED(hr) && pmtConfig) {
                    hr = pStreamConfig->SetFormat(pmtConfig);
                    if (SUCCEEDED(hr)) {
                        VIDEOINFOHEADER* vih = reinterpret_cast<VIDEOINFOHEADER*>(pmtConfig->pbFormat);
                        m_currentFormat = VideoFormat(VideoFormat::guidToFourcc(pmtConfig->subtype),
                                                      vih->bmiHeader.biWidth, vih->bmiHeader.biHeight, {}); // FPS not set here
                        AkLogInfo() << "Set default format: " << VideoFormat::stringFromFourcc(m_currentFormat.fourcc())
                                    << " " << m_currentFormat.width() << "x" << m_currentFormat.height() << std::endl;
                        formatSet = true;
                    }
                    if (pmtConfig->cbFormat != 0) { CoTaskMemFree((PVOID)pmtConfig->pbFormat); pmtConfig->cbFormat = 0; pmtConfig->pbFormat = NULL; }
                    if (pmtConfig->pUnk != NULL) { pmtConfig->pUnk->Release(); pmtConfig->pUnk = NULL; }
                    CoTaskMemFree((PVOID)pmtConfig);
                }
            }
             if (!formatSet) {
                AkLogError() << "Could not set any capture format." << std::endl;
                releaseGraph();
                return false;
            }
        }
    } else {
        AkLogWarning() << "Failed to get IAMStreamConfig from source output pin. Will use default format." << std::endl;
        // Fallback: try to connect and get format later or use a common default
        // For now, we'll rely on SampleGrabber's GetConnectedMediaType after connection
    }


    hr = m_pGraph->Connect(pSourceOutPin, pGrabberInPin);
    if (FAILED(hr)) {
        AkLogError() << "Failed to connect source filter to sample grabber: " << hr << std::endl;
        releaseGraph();
        return false;
    }

    // Get the actual media type from Sample Grabber input pin *after* connection
    // This gives us the format the camera is actually outputting to the grabber
    AM_MEDIA_TYPE connectedMt;
    hr = m_pSampleGrabber->GetConnectedMediaType(&connectedMt);
    if (SUCCEEDED(hr)) {
        VIDEOINFOHEADER* vih = reinterpret_cast<VIDEOINFOHEADER*>(connectedMt.pbFormat);
        m_currentFormat = VideoFormat(
            VideoFormat::guidToFourcc(connectedMt.subtype),
            vih->bmiHeader.biWidth,
            vih->bmiHeader.biHeight,
            {} // Frame rate not directly available here, could parse AvgTimePerFrame
        );
        AkLogInfo() << "Actual connected media type: " << VideoFormat::stringFromFourcc(m_currentFormat.fourcc())
                    << " " << m_currentFormat.width() << "x" << m_currentFormat.height() << std::endl;
        // FreeMediaType
        if (connectedMt.cbFormat != 0) { CoTaskMemFree((PVOID)connectedMt.pbFormat); connectedMt.cbFormat = 0; connectedMt.pbFormat = NULL; }
        if (connectedMt.pUnk != NULL) { connectedMt.pUnk->Release(); connectedMt.pUnk = NULL; }
        // CoTaskMemFree((PVOID)&connectedMt); // This is wrong, AM_MEDIA_TYPE itself is not dynamically allocated here
    } else {
        AkLogWarning() << "Failed to get connected media type from Sample Grabber. m_currentFormat might be inaccurate." << std::endl;
         if (m_currentFormat.fourcc() == 0) { // If not set by IAMStreamConfig either
            AkLogError() << "Current format unknown after connection attempt." << std::endl;
            releaseGraph();
            return false;
         }
    }


    // 6. (Optional) Add Null Renderer and connect Sample Grabber output to it
    CComPtr<IBaseFilter> pNullRenderer;
    hr = CoCreateInstance(CLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pNullRenderer));
    if (SUCCEEDED(hr)) {
        hr = m_pGraph->AddFilter(pNullRenderer, L"Null Renderer");
        if (SUCCEEDED(hr)) {
            CComPtr<IPin> pGrabberOutPin = getPin(m_pSampleGrabberFilter, PINDIR_OUTPUT);
            CComPtr<IPin> pNullRendererInPin = getPin(pNullRenderer, PINDIR_INPUT);
            if (pGrabberOutPin && pNullRendererInPin) {
                hr = m_pGraph->Connect(pGrabberOutPin, pNullRendererInPin);
                if (FAILED(hr)) {
                    AkLogWarning() << "Failed to connect Sample Grabber to Null Renderer: " << hr << std::endl;
                    // This is not fatal, graph might still work for grabbing.
                }
            }
        }
    } else {
         AkLogWarning() << "Failed to create Null Renderer filter." << std::endl;
    }


    m_isOpened = true;
    AkLogInfo() << "Device " << deviceId << " opened successfully." << std::endl;
    return true;
}

bool WindowsCaptureDevice::start() {
    if (!m_isOpened || m_isCapturing || !m_pControl) {
        AkLogError() << "Cannot start capture. Device not opened or already capturing." << std::endl;
        return false;
    }
    HRESULT hr = m_pControl->Run();
    if (FAILED(hr)) {
        AkLogError() << "Failed to run the filter graph: " << hr << std::endl;
        // Try to get more error info
        long ec;
        if (SUCCEEDED(m_pControl->GetState(100, (OAFilterState*)&ec))) { // GMF_TO_WAIT
             AkLogError() << "Graph state is: " << ec << std::endl;
        }
        return false;
    }
    m_isCapturing = true;
    AkLogInfo() << "Capture started for device " << m_deviceId << std::endl;
    return true;
}

void WindowsCaptureDevice::stop() {
    if (!m_isOpened || !m_isCapturing || !m_pControl) {
        return;
    }
    HRESULT hr = m_pControl->Stop();
    if (FAILED(hr)) {
        AkLogError() << "Failed to stop the filter graph: " << hr << std::endl;
    }
    m_isCapturing = false;
    AkLogInfo() << "Capture stopped for device " << m_deviceId << std::endl;
}

void WindowsCaptureDevice::close() {
    stop();
    releaseGraph();
    m_isOpened = false;
    m_frameCallback = nullptr;
    m_deviceId.clear();
    m_currentFormat = VideoFormat();
    AkLogInfo() << "Device closed." << std::endl;
}

bool WindowsCaptureDevice::isCapturing() const {
    return m_isCapturing;
}

VideoFormat WindowsCaptureDevice::getCurrentFormat() const {
    return m_currentFormat;
}


// Factory function implementation (goes into icapturedevice.cpp or a new platform_factory.cpp)
// For now, I'll put a stub here and then move it if this file gets too large.
// This should actually be in icapturedevice.cpp to avoid circular dependencies if WindowsCaptureDevice includes icapturedevice.h
/*
std::unique_ptr<ICaptureDevice> createPlatformCaptureDevice() {
#ifdef _WIN32
    return std::make_unique<WindowsCaptureDevice>();
#else
    // Placeholder for other platforms
    return nullptr;
#endif
}
*/

} // namespace AkVCam

#endif // _WIN32
