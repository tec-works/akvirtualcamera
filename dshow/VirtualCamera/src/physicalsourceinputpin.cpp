#include "physicalsourceinputpin.h"
#include "basefilter.h"
#include "VCamUtils/src/logger.h"
#include "PlatformUtils/src/utils.h" // For AkVCam::createMediaType
#include <vector>
#include <wchar.h> // For wcscpy_s, wcsncpy_s

namespace AkVCam
{

PhysicalSourceInputPin::PhysicalSourceInputPin(BaseFilter* pFilter, HRESULT* phr, LPCWSTR pPinName)
    : CUnknown(this, IID_IPin), // Corrected: Pass 'this' and the primary IID
      m_pFilter(pFilter),
      m_pinName(pPinName ? pPinName : L"PhysicalCamInput")
{
    AkLogFunction();
    if (phr) *phr = S_OK;
    InitializeCriticalSection(&m_critSec);
    ZeroMemory(&m_mt, sizeof(m_mt));
    m_mediaTypeSet = false; // Initialize
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
    if (!ppv) return E_POINTER;
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

STDMETHODIMP PhysicalSourceInputPin::Connect(IPin *pReceivePin, const AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();
    return E_UNEXPECTED;
}

STDMETHODIMP PhysicalSourceInputPin::ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();
    if (!pConnector || !pmt) return E_POINTER;
    if (m_ConnectedPin) return VFW_E_ALREADY_CONNECTED;

    HRESULT hr = CheckMediaType(pmt);
    if (FAILED(hr)) {
        AkLogError() << "Media type not acceptable for ReceiveConnection." << std::endl;
        return hr;
    }

    m_ConnectedPin = pConnector;
    m_ConnectedPin->AddRef();

    hr = SetMediaType(pmt);
    if (FAILED(hr)) {
        m_ConnectedPin->Release();
        m_ConnectedPin = nullptr;
        AkLogError() << "Failed to set media type in ReceiveConnection." << std::endl;
        return hr;
    }

    AkLogInfo() << "Successfully connected PhysicalSourceInputPin." << std::endl;
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::Disconnect()
{
    AkLogFunction();
    if (!m_ConnectedPin) return S_FALSE;

    if (m_pAllocator) {
        m_pAllocator->Decommit();
        m_pAllocator->Release();
        m_pAllocator = nullptr;
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
    AkLogInfo() << "Disconnected PhysicalSourceInputPin." << std::endl;
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
    if (!m_ConnectedPin || !m_mediaTypeSet) {
         ZeroMemory(pmt, sizeof(AM_MEDIA_TYPE)); // As per MSDN for VFW_E_NOT_CONNECTED
        return VFW_E_NOT_CONNECTED;
    }

    // The caller provides a structure 'pmt'. We need to fill it.
    // AkVCam::createMediaType returns a NEW AM_MEDIA_TYPE*.
    AM_MEDIA_TYPE *newlyCreated = AkVCam::createMediaType(&m_mt);
    if (!newlyCreated) {
        ZeroMemory(pmt, sizeof(AM_MEDIA_TYPE));
        return E_OUTOFMEMORY;
    }

    // Copy all members from newlyCreated to pmt.
    *pmt = *newlyCreated;
    // Ownership of pbFormat and pUnk is now with pmt.
    // We only need to free the AM_MEDIA_TYPE structure itself that AkVCam::createMediaType allocated.
    CoTaskMemFree(newlyCreated);

    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::QueryPinInfo(PIN_INFO *pInfo)
{
    AkLogFunction();
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = m_pFilter;
    if (m_pFilter) m_pFilter->AddRef();

    pInfo->dir = PINDIR_INPUT;
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
    return CheckMediaType(pmt);
}

STDMETHODIMP PhysicalSourceInputPin::EnumMediaTypes(IEnumMediaTypes **ppEnum)
{
    AkLogFunction();
    if (!ppEnum) return E_POINTER;
    AkLogError() << "PhysicalSourceInputPin::EnumMediaTypes is not fully implemented." << std::endl;
    *ppEnum = nullptr;
    // A real input pin might enumerate types it can accept, often by trying to connect
    // or by having a predefined list. For connection to a physical camera, it's often
    // more about what the physical camera offers.
    return E_NOTIMPL;
}

STDMETHODIMP PhysicalSourceInputPin::QueryInternalConnections(IPin **apPin, ULONG *nPin)
{
    AkLogFunction();
    if (nPin) *nPin = 0;
    return E_NOTIMPL;
}

STDMETHODIMP PhysicalSourceInputPin::EndOfStream()
{
    AkLogFunction();
    // TODO: Forward to owning filter if necessary
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::BeginFlush()
{
    AkLogFunction();
    // TODO: Forward to owning filter if necessary
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::EndFlush()
{
    AkLogFunction();
    // TODO: Forward to owning filter if necessary
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
    AkLogFunction();
    // TODO: Forward to owning filter if necessary
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::GetAllocator(IMemAllocator **ppAllocator)
{
    AkLogFunction();
    if (!ppAllocator) return E_POINTER;
    if (m_pAllocator) {
        *ppAllocator = m_pAllocator;
        m_pAllocator->AddRef();
        return S_OK;
    }
    return VFW_E_NO_ALLOCATOR; // Upstream (output pin) should provide allocator
}

STDMETHODIMP PhysicalSourceInputPin::NotifyAllocator(IMemAllocator *pAllocator, BOOL bReadOnly)
{
    AkLogFunction();
    if (!pAllocator) return E_POINTER;

    if (m_pAllocator) {
        m_pAllocator->Release();
    }
    m_pAllocator = pAllocator;
    m_pAllocator->AddRef();
    // No need to Commit here for an input pin usually; the source filter's output pin handles that.
    AkLogInfo() << "PhysicalSourceInputPin::NotifyAllocator called. ReadOnly: " << (bReadOnly ? "TRUE" : "FALSE") << std::endl;
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::GetAllocatorRequirements(ALLOCATOR_PROPERTIES *pProps)
{
    AkLogFunction();
    if (!pProps) return E_POINTER;
    // Input pins typically don't have requirements; they use the allocator from the output pin.
    return E_NOTIMPL;
}

STDMETHODIMP PhysicalSourceInputPin::Receive(IMediaSample *pSample)
{
    AkLogFunction();
    if (!pSample) return E_POINTER;

    EnterCriticalSection(&m_critSec);

    BYTE *pData = nullptr;
    long len = pSample->GetActualDataLength();
    HRESULT hr = pSample->GetPointer(&pData);

    if (SUCCEEDED(hr) && pData && len > 0) {
        if (m_latestFrameBuffer.size() < static_cast<size_t>(len)) {
            m_latestFrameBuffer.resize(len);
        }
        CopyMemory(m_latestFrameBuffer.data(), pData, len);

        if (m_pFilter) {
            // Pass the current connection's media type (m_mt)
            // as that's the format of the samples we are receiving.
            m_pFilter->NotifyPhysicalFrameReady(m_latestFrameBuffer.data(), len, m_mt);
        }
    } else {
         AkLogError() << "Failed to get data from physical camera sample in Receive. HR: " << hr << std::endl;
    }

    LeaveCriticalSection(&m_critSec);
    return S_OK;
}

STDMETHODIMP PhysicalSourceInputPin::ReceiveCanBlock()
{
    AkLogFunction();
    return S_FALSE; // Does not block
}

STDMETHODIMP PhysicalSourceInputPin::ReceiveMultiple(IMediaSample **pSamples, LONG nSamples, LONG *nSamplesProcessed)
{
    AkLogFunction();
    if (!pSamples || !nSamplesProcessed) return E_POINTER;

    *nSamplesProcessed = 0;
    if (nSamples <= 0) return S_OK;

    HRESULT hr = S_OK;
    for (LONG i = 0; i < nSamples; ++i) {
        if (pSamples[i]) { // Check for NULL sample, though unlikely
            hr = Receive(pSamples[i]);
            if (SUCCEEDED(hr)) {
                (*nSamplesProcessed)++;
            } else {
                AkLogError() << "Error receiving sample " << i << " in ReceiveMultiple. HR: " << hr << std::endl;
                break;
            }
        } else {
            AkLogError() << "NULL sample received in ReceiveMultiple at index " << i << std::endl;
            // Decide if this is an error or just skip
        }
    }
    return hr;
}

HRESULT PhysicalSourceInputPin::CheckMediaType(const AM_MEDIA_TYPE* pmt)
{
    if (!pmt) return E_POINTER;
    if (pmt->majortype != MEDIATYPE_Video) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    // More specific checks can be added here if needed (e.g., subtype, format type)
    // For now, accepting any video type.
    return S_OK;
}

HRESULT PhysicalSourceInputPin::SetMediaType(const AM_MEDIA_TYPE* pmt)
{
    if (!pmt) return E_POINTER;

    // Free existing format block
    if (m_mt.cbFormat != 0) {
        CoTaskMemFree(m_mt.pbFormat);
        m_mt.pbFormat = nullptr; // Important to null out after freeing
    }
    if (m_mt.pUnk != NULL) {
        m_mt.pUnk->Release();
        m_mt.pUnk = NULL; // Important
    }

    m_mt = *pmt; // Shallow copy members like majortype, subtype etc.

    // Deep copy the format block if present
    if (pmt->cbFormat > 0 && pmt->pbFormat != nullptr) {
        m_mt.pbFormat = (BYTE*)CoTaskMemAlloc(pmt->cbFormat);
        if (!m_mt.pbFormat) {
            m_mt.cbFormat = 0; // Allocation failed
            AkLogError() << "Failed to allocate memory for format block in SetMediaType." << std::endl;
            return E_OUTOFMEMORY;
        }
        CopyMemory(m_mt.pbFormat, pmt->pbFormat, pmt->cbFormat);
    } else {
        m_mt.pbFormat = nullptr; // Ensure it's null if source was null or cbFormat was 0
        m_mt.cbFormat = 0;
    }

    // Handle pUnk for format types that use it (rare)
    if (pmt->pUnk != nullptr) {
        m_mt.pUnk = pmt->pUnk;
        m_mt.pUnk->AddRef();
    } else {
        m_mt.pUnk = nullptr;
    }
    m_mediaTypeSet = true;
    return S_OK;
}

} // namespace AkVCam
