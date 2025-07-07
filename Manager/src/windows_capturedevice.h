#ifndef AKVCAM_WINDOWS_CAPTUREDEVICE_H
#define AKVCAM_WINDOWS_CAPTUREDEVICE_H

#ifdef _WIN32

#include "icapturedevice.h"
#include <vector>
#include <string>
#include <atlbase.h> // For CComPtr
#include <dshow.h>
#include <qedit.h>   // For ISampleGrabber, ISampleGrabberCB, CLSID_SampleGrabber, CLSID_NullRenderer

namespace AkVCam {

class WindowsCaptureDevice : public ICaptureDevice {
public:
    WindowsCaptureDevice();
    ~WindowsCaptureDevice() override;

    std::vector<PhysicalCameraInfo> enumerateDevices() override;
    bool open(const std::string& deviceId, FrameCallback callback, const VideoFormat* preferredFormat = nullptr) override;
    bool start() override;
    void stop() override;
    void close() override;
    bool isCapturing() const override;
    VideoFormat getCurrentFormat() const override;

private:
    // Sample Grabber Callback Class
    class SampleGrabberCallback : public ISampleGrabberCB {
    public:
        SampleGrabberCallback(WindowsCaptureDevice* pOwner, std::string deviceId);
        STDMETHODIMP_(ULONG) AddRef() override;
        STDMETHODIMP_(ULONG) Release() override;
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
        STDMETHODIMP SampleCB(double SampleTime, IMediaSample *pSample) override;
        STDMETHODIMP BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen) override;

    private:
        volatile ULONG m_refCount;
        WindowsCaptureDevice* m_pOwner;
        std::string m_deviceId; // To pass to the owner's callback
    };

    void releaseGraph();
    CComPtr<IBaseFilter> createSourceFilter(const std::string& deviceId);
    CComPtr<IPin> getPin(IBaseFilter *pFilter, PIN_DIRECTION pinDir, int pinNum = 0);

    CComPtr<IGraphBuilder> m_pGraph;
    CComPtr<IMediaControl> m_pControl;
    CComPtr<IBaseFilter> m_pSourceFilter;
    CComPtr<IBaseFilter> m_pSampleGrabberFilter;
    CComPtr<ISampleGrabber> m_pSampleGrabber;
    CComPtr<SampleGrabberCallback> m_pSampleGrabberCallback;

    FrameCallback m_frameCallback;
    std::string m_deviceId;
    VideoFormat m_currentFormat;
    bool m_isCapturing;
    bool m_isOpened;

    // For CoInitialize
    bool m_comInitialized;
};

} // namespace AkVCam

#endif // _WIN32
#endif // AKVCAM_WINDOWS_CAPTUREDEVICE_H
