#ifndef PHYSICALSOURCEINPUTPIN_H
#define PHYSICALSOURCEINPUTPIN_H

#include <dshow.h>
#include <string>
#include <vector>
// #include "VCamUtils/src/videoframe.h" // AkVCam::VideoFormat is forward declared here
#include "VCamUtils/src/videoformat.h" // Include full definition for AkVCam::VideoFormat
#include "cunknown.h" // For CUnknown base

// Forward declaration
namespace AkVCam {
    class BaseFilter;
    // class VideoFormat; // Full definition included above
}

namespace AkVCam
{
    class PhysicalSourceInputPin : public IPin, public IMemInputPin, public CUnknown
    {
    public:
        PhysicalSourceInputPin(BaseFilter* pFilter, HRESULT* phr, LPCWSTR pPinName);
        virtual ~PhysicalSourceInputPin();

        DECLARE_IUNKNOWN_NQ // This handles AddRef, Release, QueryInterface (basic version)

        // IUnknown methods
        // QueryInterface is often overridden for specific interfaces, AddRef/Release usually taken from CUnknown.
        STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
        // AddRef & Release are now solely from DECLARE_IUNKNOWN_NQ / CUnknown

        // IPin methods
        STDMETHODIMP Connect(IPin *pReceivePin, const AM_MEDIA_TYPE *pmt) override;
        STDMETHODIMP ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) override;
        STDMETHODIMP Disconnect() override;
        STDMETHODIMP ConnectedTo(IPin **pPin) override;
        STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE *pmt) override;
        STDMETHODIMP QueryPinInfo(PIN_INFO *pInfo) override;
        STDMETHODIMP QueryDirection(PIN_DIRECTION *pPinDir) override;
        STDMETHODIMP QueryId(LPWSTR *Id) override;
        STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE *pmt) override;
        STDMETHODIMP EnumMediaTypes(IEnumMediaTypes **ppEnum) override;
        STDMETHODIMP QueryInternalConnections(IPin **apPin, ULONG *nPin) override;
        STDMETHODIMP EndOfStream() override;
        STDMETHODIMP BeginFlush() override;
        STDMETHODIMP EndFlush() override;
        STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override;

        // IMemInputPin methods
        STDMETHODIMP GetAllocator(IMemAllocator **ppAllocator) override;
        STDMETHODIMP NotifyAllocator(IMemAllocator *pAllocator, BOOL bReadOnly) override;
        STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES *pProps) override;
        STDMETHODIMP Receive(IMediaSample *pSample) override;
        STDMETHODIMP ReceiveMultiple(IMediaSample **pSamples, LONG nSamples, LONG *nSamplesProcessed) override;
        STDMETHODIMP ReceiveCanBlock() override;

        // Helper methods
        HRESULT CheckMediaType(const AM_MEDIA_TYPE* pmt);
        HRESULT SetMediaType(const AM_MEDIA_TYPE* pmt);
        AM_MEDIA_TYPE& CurrentMediaType() { return m_mt; }

    private:
        BaseFilter* m_pFilter; // Owning filter
        std::wstring m_pinName;
        IPin* m_ConnectedPin = nullptr; // Pin we are connected to (output pin of physical camera)
        AM_MEDIA_TYPE m_mt;
        IMemAllocator* m_pAllocator = nullptr;

        // Buffer for the latest frame from physical camera
        std::vector<BYTE> m_latestFrameBuffer;
        AkVCam::VideoFormat m_latestFrameFormat; // Consider if needed, or just raw buffer + AM_MEDIA_TYPE
        CRITICAL_SECTION m_critSec; // For protecting access to the frame buffer
        bool m_mediaTypeSet = false;
    };
}

#endif // PHYSICALSOURCEINPUTPIN_H
