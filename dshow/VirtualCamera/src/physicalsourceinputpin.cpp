#include "physicalsourceinputpin.h"
#include "basefilter.h" // To access BaseFilter methods/members
#include "VCamUtils/src/logger.h"
#include "PlatformUtils/src/utils.h" // For string conversions, etc.
#include <vector>
#include <wchar.h> // For wcscpy_s, wcsncpy_s

// Define any necessary GUIDs if not available (e.g. MEDIASUBTYPE_RGB24 if not in dshow.h)
// For example:
// DEFINE_GUID(MEDIASUBTYPE_RGB24, 0xe436eb7d, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70);


namespace AkVCam
{

PhysicalSourceInputPin::PhysicalSourceInputPin(BaseFilter* pFilter, HRESULT* phr, LPCWSTR pPinName)
    : CUnknown(this, IID_IPin), // Corrected CUnknown initialization
      m_pFilter(pFilter),
      m_pinName(pPinName ? pPinName : L"PhysicalCamInput")
{
    AkLogFunction();
    if (phr) *phr = S_OK;
    InitializeCriticalSection(&m_critSec);
    ZeroMemory(&m_mt, sizeof(m_mt));
}

PhysicalSourceInputPin::~PhysicalSourceInputPin()
{
    AkLogFunction();
    if (m_ConnectedPin) m_ConnectedPin->Release();
    if (m_pAllocator) m_pAllocator->Release();
    DeleteCriticalSection(&m_critSec);
    if (m_mt.cbFormat != 0) {
        CoTaskMemFree((PVOID)m_mt.pbFormat);
        m_mt.cbFormat = 0;
    }
    if (m_mt.pUnk != NULL) {
        m_mt.pUnk->Release();
        m_mt.pUnk = NULL;
    }
}

STDMETHODIMP PhysicalSourceInputPin::QueryInterface(REFIID riid, void **ppv)
{
    AkLogFunction();
    if (riid == IID_IUnknown) {
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IPin) {
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IMemInputPin) {
        *ppv = static_cast<IMemInputPin*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

// --- IPin methods ---

STDMETHODIMP PhysicalSourceInputPin::Connect(IPin *pReceivePin, const AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();
    // This is an input pin, it doesn't call Connect on another pin.
    // It receives a connection via ReceiveConnection.
    return E_UNEXPECTED;
}

STDMETHODIMP PhysicalSourceInputPin::ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();
    if (!pConnector || !pmt) return E_POINTER;
    if (m_ConnectedPin) return VFW_E_ALREADY_CONNECTED;

    // Check if media type is acceptable
    HRESULT hr = CheckMediaType(pmt);
    if (FAILED(hr)) {
        AkLogError() << "Media type not acceptable." << std::endl;
        return hr;
    }

    // Store connection info
    m_ConnectedPin = pConnector;
    m_ConnectedPin->AddRef();

    hr = SetMediaType(pmt); // Copy and store the media type
    if (FAILED(hr)) {
        m_ConnectedPin->Release();
        m_ConnectedPin = nullptr;
        return hr;
    }
    m_mediaTypeSet = true;

    AkLogInfo() << "Successfully connected input pin." << std::endl;
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::Disconnect()
{
    AkLogFunction();
    if (!m_ConnectedPin) return S_FALSE; // Not connected

    if (m_pAllocator) {
        m_pAllocator->Decommit();
    }

    if (m_mt.cbFormat != 0) {
        CoTaskMemFree(m_mt.pbFormat);
        m_mt.pbFormat = nullptr;
        m_mt.cbFormat = 0;
    }
    if (m_mt.pUnk != NULL) {
        m_mt.pUnk->Release();
        m_mt.pUnk = NULL;
    }
    ZeroMemory(&m_mt, sizeof(m_mt));
    m_mediaTypeSet = false;


    m_ConnectedPin->Release();
    m_ConnectedPin = nullptr;
    AkLogInfo() << "Disconnected input pin." << std::endl;
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::ConnectedTo(IPin **pPin)
{
    AkLogFunction();
    if (!pPin) return E_POINTER;
    *pPin = m_ConnectedPin;
    if (m_ConnectedPin) {
        m_ConnectedPin->AddRef();
        return S_OK;
    }
    return VFW_E_NOT_CONNECTED;
}

STDMETHODIMP PhysicalSourceInputPin::ConnectionMediaType(AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();
    if (!pmt) return E_POINTER;
    if (!m_ConnectedPin || !m_mediaTypeSet) return VFW_E_NOT_CONNECTED;

    // Free existing format block in pmt if any
    if (pmt->cbFormat != 0) { CoTaskMemFree(pmt->pbFormat); pmt->pbFormat = nullptr; pmt->cbFormat = 0; }
    if (pmt->pUnk != NULL) { pmt->pUnk->Release(); pmt->pUnk = NULL; }

    // Use the project's AkVCam::createMediaType
    AkVCam::createMediaType(pmt, &m_mt);
    // This function is expected to handle deep copy of pbFormat and AddRef of pUnk.
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::QueryPinInfo(PIN_INFO *pInfo)
{
    AkLogFunction();
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = m_pFilter; // Owning filter
    if (m_pFilter) m_pFilter->AddRef();

    pInfo->dir = PINDIR_INPUT;
    // Safe string copy
    if (m_pinName.length() < MAX_PIN_NAME) {
        wcscpy_s(pInfo->achName, MAX_PIN_NAME, m_pinName.c_str());
    } else {
        wcsncpy_s(pInfo->achName, MAX_PIN_NAME, m_pinName.c_str(), _TRUNCATE);
    }
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::QueryDirection(PIN_DIRECTION *pPinDir)
{
    AkLogFunction();
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_INPUT;
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::QueryId(LPWSTR *Id)
{
    AkLogFunction();
    if (!Id) return E_POINTER;
    size_t len = m_pinName.length() + 1;
    *Id = (LPWSTR)CoTaskMemAlloc(len * sizeof(WCHAR));
    if (!*Id) return E_OUTOFMEMORY;
    wcscpy_s(*Id, len, m_pinName.c_str());
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::QueryAccept(const AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();
    if (!pmt) return E_POINTER;
    // For simplicity, accept common uncompressed video types.
    // A real implementation would be more thorough.
    return CheckMediaType(pmt);
}

STDMETHODIMP PhysicalSourceInputPin::EnumMediaTypes(IEnumMediaTypes **ppEnum)
{
    AkLogFunction();
    if (!ppEnum) return E_POINTER;
    // TODO: Implement a media type enumerator.
    // For now, return E_NOTIMPL or a very basic enumerator if only one type is supported.
    // This pin will typically accept whatever the physical camera offers, so it might
    // reflect the types QueryAccept would allow.
    AkLogError() << "EnumMediaTypes is not fully implemented." << std::endl;
    *ppEnum = nullptr;
    return E_NOTIMPL;
}

STDMETHODIMP PhysicalSourceInputPin::QueryInternalConnections(IPin **apPin, ULONG *nPin)
{
    AkLogFunction();
    // This pin does not have internal connections.
    if (nPin) *nPin = 0;
    return E_NOTIMPL;
}

STDMETHODIMP PhysicalSourceInputPin::EndOfStream()
{
    AkLogFunction();
    // TODO: Handle EndOfStream from upstream (physical camera)
    // This might involve signaling the owning filter.
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::BeginFlush()
{
    AkLogFunction();
    // TODO: Handle BeginFlush. Discard any queued samples.
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::EndFlush()
{
    AkLogFunction();
    // TODO: Handle EndFlush. Resume normal operation.
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
    AkLogFunction();
    // TODO: Propagate NewSegment if necessary.
    return S_OK;
}

// --- IMemInputPin methods ---

STDMETHODIMP PhysicalSourceInputPin::GetAllocator(IMemAllocator **ppAllocator)
{
    AkLogFunction();
    if (!ppAllocator) return E_POINTER;
    if (m_pAllocator) {
        *ppAllocator = m_pAllocator;
        m_pAllocator->AddRef();
        return S_OK;
    }
    return VFW_E_NO_ALLOCATOR;
}

STDMETHODIMP PhysicalSourceInputPin::NotifyAllocator(IMemAllocator *pAllocator, BOOL bReadOnly)
{
    AkLogFunction();
    if (!pAllocator) return E_POINTER;

    if (m_pAllocator) m_pAllocator->Release();
    m_pAllocator = pAllocator;
    m_pAllocator->AddRef();

    // Typically, an input pin would call Commit on the allocator here or in Active state.
    // However, for simplicity in this stage, we'll assume the upstream pin or graph manages this.
    // If issues arise, we might need to call m_pAllocator->Commit() when the filter goes active.
    AkLogInfo() << "Allocator notified. ReadOnly: " << (bReadOnly ? "TRUE" : "FALSE") << std::endl;
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::GetAllocatorRequirements(ALLOCATOR_PROPERTIES *pProps)
{
    AkLogFunction();
    if (!pProps) return E_POINTER;
    // This pin has no specific allocator requirements; it accepts what the upstream filter provides.
    // ZeroMemory(pProps, sizeof(ALLOCATOR_PROPERTIES));
    // However, some filters might expect some defaults.
    // For now, let's not specify any. The connected output pin should propose properties.
    return E_NOTIMPL; // Or S_OK if we provide some defaults, but usually input pins don't dictate this.
}

STDMETHODIMP PhysicalSourceInputPin::Receive(IMediaSample *pSample)
{
    AkLogFunction();
    if (!pSample) return E_POINTER;

    // TODO: This is where the frame from the physical camera arrives.
    // 1. Lock the critical section.
    // 2. Get data pointer and size from pSample.
    // 3. Copy/Store this data into m_pFilter's buffer for the physical camera frame.
    //    This buffer needs to be accessible by the AkVCam::Pin (output pin) when it constructs its samples.
    // 4. Unlock critical section.
    // 5. The AkVCam::Pin's sendFrame/FillBuffer method will then pick this up.

    EnterCriticalSection(&m_critSec);

    BYTE *pData = nullptr;
    long len = pSample->GetActualDataLength();
    HRESULT hr = pSample->GetPointer(&pData);

    if (SUCCEEDED(hr) && pData && len > 0) {
        // AkLogInfo() << "Received sample from physical camera, size: " << len << std::endl;
        // Ensure our buffer is large enough
        if (m_latestFrameBuffer.size() < static_cast<size_t>(len)) {
            m_latestFrameBuffer.resize(len);
        }
        CopyMemory(m_latestFrameBuffer.data(), pData, len);

        // TODO: Update m_latestFrameFormat if necessary, or get it from m_mt
        // For now, assume m_mt is the correct format.
        // The BaseFilter needs a way to expose this m_latestFrameBuffer and its format/size
        // to the output AkVCam::Pin.

        // Example: Signal BaseFilter that a new frame is ready
        if (m_pFilter) { // Ensure filter pointer is valid
            m_pFilter->NotifyPhysicalFrameReady(m_latestFrameBuffer.data(), len, m_mt);
        }
    } else {
         AkLogError() << "Failed to get data from physical camera sample. HR: " << hr << std::endl;
    }

    LeaveCriticalSection(&m_critSec);

    // pSample is automatically released by the caller (Sample Grabber or upstream filter's output pin)
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::ReceiveCanBlock()
{
    AkLogFunction();
    // Return S_FALSE to indicate that Receive does not block.
    // If Receive might block, return S_OK.
    return S_FALSE;
}

STDMETHODIMP PhysicalSourceInputPin::ReceiveMultiple(IMediaSample **pSamples, LONG nSamples, LONG *nSamplesProcessed)
{
    AkLogFunction();
    if (!pSamples || !nSamplesProcessed) return E_POINTER;

    *nSamplesProcessed = 0;
    if (nSamples <= 0) return S_OK; // No samples to process

    // This basic implementation will process one sample at a time by calling Receive.
    // A more optimized version might handle multiple samples if possible, but
    // for many source-like scenarios, one-by-one is sufficient.
    HRESULT hr = S_OK;
    for (LONG i = 0; i < nSamples; ++i) {
        hr = Receive(pSamples[i]);
        if (SUCCEEDED(hr)) {
            (*nSamplesProcessed)++;
        } else {
            // If one sample fails, we might stop or continue.
            // For now, stop and report the error for that sample.
            AkLogError() << "Error receiving sample " << i << " in ReceiveMultiple. HR: " << hr << std::endl;
            break;
        }
    }
    return hr; // Return status of the last Receive call or first error
}


// Helper methods
HRESULT PhysicalSourceInputPin::CheckMediaType(const AM_MEDIA_TYPE* pmt)
{
    if (!pmt) return E_POINTER;

    // For an input pin connecting to a physical camera, we should be quite flexible.
    // We primarily care that it's video.
    if (pmt->majortype != MEDIATYPE_Video) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    // Could add checks for common uncompressed subtypes if necessary, e.g.:
    // if (pmt->subtype != MEDIASUBTYPE_RGB24 && pmt->subtype != MEDIASUBTYPE_YUY2 && ...) {
    //     return VFW_E_TYPE_NOT_ACCEPTED;
    // }

    if (pmt->formattype != FORMAT_VideoInfo && pmt->formattype != FORMAT_VideoInfo2) {
         // Allow FORMAT_None if subtype is MJPG or other compressed types that don't need VideoInfo
        if (pmt->subtype != MEDIASUBTYPE_MJPG && pmt->formattype != FORMAT_None) {
             return VFW_E_TYPE_NOT_ACCEPTED;
        }
    }

    // AkLogInfo() << "Accepted media type: " << AkVCam::stringFromMediaType(pmt) << std::endl;
    return S_OK;
}

HRESULT PhysicalSourceInputPin::SetMediaType(const AM_MEDIA_TYPE* pmt)
{
    if (!pmt) return E_POINTER;

    if (m_mt.cbFormat != 0) {
        CoTaskMemFree(m_mt.pbFormat);
        m_mt.pbFormat = nullptr;
    }
    if (m_mt.pUnk != NULL) {
        m_mt.pUnk->Release();
        m_mt.pUnk = NULL;
    }

    m_mt = *pmt; // Shallow copy first

    if (pmt->cbFormat > 0 && pmt->pbFormat != nullptr) {
        m_mt.pbFormat = (BYTE*)CoTaskMemAlloc(pmt->cbFormat);
        if (!m_mt.pbFormat) {
            m_mt.cbFormat = 0;
            return E_OUTOFMEMORY;
        }
        CopyMemory(m_mt.pbFormat, pmt->pbFormat, pmt->cbFormat);
    }
    if (pmt->pUnk != nullptr) {
        m_mt.pUnk = pmt->pUnk;
        m_mt.pUnk->AddRef();
    }
    m_mediaTypeSet = true;
    return S_OK;
}

} // namespace AkVCam
