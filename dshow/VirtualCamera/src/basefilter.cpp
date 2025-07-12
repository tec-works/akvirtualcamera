/* akvirtualcamera, virtual camera for Mac and Windows.
 * Copyright (C) 2020  Gonzalo Exequiel Pedone
 *
 * akvirtualcamera is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * akvirtualcamera is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with akvirtualcamera. If not, see <http://www.gnu.org/licenses/>.
 *
 * Web-Site: http://webcamoid.github.io/
 */

#include <algorithm>
#include <dshow.h>
#include <dbt.h>

#include "basefilter.h"
#include "enumpins.h"
#include "filtermiscflags.h"
#include "pin.h"
#include "referenceclock.h"
#include "specifypropertypages.h"
#include "videocontrol.h"
#include "videoprocamp.h"
#include "PlatformUtils/src/preferences.h"
#include "PlatformUtils/src/utils.h"
#include "VCamUtils/src/videoformat.h"
#include "VCamUtils/src/ipcbridge.h"
#include "VCamUtils/src/utils.h"

#define AkVCamPinCall(pins, func, ...) \
    pins->Reset(); \
    Pin *pin = nullptr; \
    \
    while (pins->Next(1, reinterpret_cast<IPin **>(&pin), nullptr) == S_OK) { \
        pin->func(__VA_ARGS__); \
        pin->Release(); \
    }

#define AkVCamDevicePinCall(deviceId, where, func, ...) \
    if (auto pins = where->pinsForDevice(deviceId)) { \
        AkVCamPinCall(pins, func, __VA_ARGS__) \
        pins->Release(); \
    }

namespace AkVCam
{
    class BaseFilterPrivate
    {
        public:
            BaseFilter *self;
            EnumPins *m_pins {nullptr};
            VideoProcAmp *m_videoProcAmp {nullptr};
            ReferenceClock *m_referenceClock {nullptr};
            std::string m_vendor;
            std::string m_filterName;
            IFilterGraph *m_filterGraph {nullptr};
            IpcBridge m_ipcBridge {true};
            IpcBridge::ServerState m_serverState {IpcBridge::ServerStateGone};

            BaseFilterPrivate(BaseFilter *self,
                              const std::string &filterName,
                              const std::string &vendor);
            BaseFilterPrivate(const BaseFilterPrivate &other) = delete;
            ~BaseFilterPrivate();
            IEnumPins *pinsForDevice(const std::string &deviceId);
            void updatePins();
            static void serverStateChanged(void *userData,
                                           IpcBridge::ServerState state);
            static void frameReady(void *userData,
                                   const std::string &deviceId,
                                   const VideoFrame &frame);
            static void pictureChanged(void *userData,
                                       const std::string &picture);
            static void devicesChanged(void *userData,
                                       const std::vector<std::string> &devices);
            static void setBroadcasting(void *userData,
                                        const std::string &deviceId,
                                        const std::string &broadcasting);
            static void setControls(void *userData,
                                    const std::string &deviceId,
                                    const std::map<std::string, int> &controls);
    };
}

BOOL AkVCamEnumWindowsProc(HWND handler, LPARAM userData);

AkVCam::BaseFilter::BaseFilter(const GUID &clsid,
                               const std::string &filterName,
                               const std::string &vendor):
    MediaFilter(clsid, this),
    m_sourceCameraName("") // Initialize m_sourceCameraName
{
    this->setParent(this, &IID_IBaseFilter);
    this->d = new BaseFilterPrivate(this, filterName, vendor);
    InitializeCriticalSection(&m_physicalFrameCritSec);
    ZeroMemory(&m_physicalCameraMediaType, sizeof(AM_MEDIA_TYPE));


    // Retrieve and store source camera name
    std::string currentDeviceId = deviceId();
    if (!currentDeviceId.empty()) {
        int cameraIndex = Preferences::cameraFromId(currentDeviceId);
        if (cameraIndex >= 0) {
            m_sourceCameraName = Preferences::cameraCustomValue(static_cast<size_t>(cameraIndex), "sourceCamera");
            AkLogInfo() << "Source camera for " << currentDeviceId << ": " << m_sourceCameraName << std::endl;
            if (!m_sourceCameraName.empty()) {
                HRESULT hr = S_OK;
                m_pPhysicalSourceInputPin = new PhysicalSourceInputPin(this, &hr, L"PhysicalCamInput");
                if (m_pPhysicalSourceInputPin) m_pPhysicalSourceInputPin->AddRef(); // CUnknown starts with 1, but good practice
                else AkLogError() << "Failed to create PhysicalSourceInputPin";

                InitializeSourceCamera(); // This will attempt to connect to m_pPhysicalSourceInputPin
            }
        }
    }
}

void AkVCam::BaseFilter::NotifyPhysicalFrameReady(const BYTE* pData, LONG size, const AM_MEDIA_TYPE& mt) {
    AkLogFunction();
    EnterCriticalSection(&m_physicalFrameCritSec);

    m_physicalCameraLatestFrame.assign(pData, pData + size);

    // Free old format block if any
    if (m_physicalCameraMediaType.cbFormat != 0) {
        CoTaskMemFree(m_physicalCameraMediaType.pbFormat);
    }
    if (m_physicalCameraMediaType.pUnk != NULL) {
        m_physicalCameraMediaType.pUnk->Release();
    }

    m_physicalCameraMediaType = mt; // Shallow copy
    if (mt.cbFormat > 0 && mt.pbFormat != nullptr) {
        m_physicalCameraMediaType.pbFormat = (BYTE*)CoTaskMemAlloc(mt.cbFormat);
        if (m_physicalCameraMediaType.pbFormat) {
            CopyMemory(m_physicalCameraMediaType.pbFormat, mt.pbFormat, mt.cbFormat);
        } else {
            m_physicalCameraMediaType.cbFormat = 0; // Failed allocation
        }
    }
    if (mt.pUnk != nullptr) {
        m_physicalCameraMediaType.pUnk = mt.pUnk;
        m_physicalCameraMediaType.pUnk->AddRef();
    }

    LeaveCriticalSection(&m_physicalFrameCritSec);
    // Potentially signal the output pin that a new frame is available if it's not polling
}

HRESULT AkVCam::BaseFilter::GetLatestPhysicalFrame(std::vector<BYTE>& frameBuffer, AM_MEDIA_TYPE& frameMediaType) {
    AkLogFunction();
    EnterCriticalSection(&m_physicalFrameCritSec);

    if (m_physicalCameraLatestFrame.empty()) {
        LeaveCriticalSection(&m_physicalFrameCritSec);
        return VFW_E_WRONG_STATE; // Or S_FALSE if no frame yet
    }

    frameBuffer = m_physicalCameraLatestFrame; // Copy data

    // Copy media type
    if (frameMediaType.cbFormat != 0) CoTaskMemFree(frameMediaType.pbFormat);
    if (frameMediaType.pUnk != NULL) frameMediaType.pUnk->Release();

    frameMediaType = m_physicalCameraMediaType;
    if (m_physicalCameraMediaType.cbFormat > 0 && m_physicalCameraMediaType.pbFormat != nullptr) {
        frameMediaType.pbFormat = (BYTE*)CoTaskMemAlloc(m_physicalCameraMediaType.cbFormat);
        if (frameMediaType.pbFormat) {
            CopyMemory(frameMediaType.pbFormat, m_physicalCameraMediaType.pbFormat, m_physicalCameraMediaType.cbFormat);
        } else {
            frameMediaType.cbFormat = 0;
            LeaveCriticalSection(&m_physicalFrameCritSec);
            return E_OUTOFMEMORY;
        }
    }
    if (m_physicalCameraMediaType.pUnk != nullptr) {
        frameMediaType.pUnk = m_physicalCameraMediaType.pUnk;
        frameMediaType.pUnk->AddRef();
    }

    LeaveCriticalSection(&m_physicalFrameCritSec);
    return S_OK;
}


// Placeholder for actual physical camera initialization
void AkVCam::BaseFilter::InitializeSourceCamera() {
    AkLogFunction();
    if (m_sourceCameraName.empty() || !m_pPhysicalSourceInputPin) {
        AkLogInfo() << "No source camera specified." << std::endl;
        return;
    }
    AkLogInfo() << "Initializing physical source camera: " << m_sourceCameraName << std::endl;
    // TODO:
    // 1. Find the physical camera filter by its FriendlyName (m_sourceCameraName).
    //    - Use ICreateDevEnum and IEnumMoniker, similar to camera listing.
    // 2. CoCreateInstance the physical camera filter.
    // 3. Add it to an internal filter graph (m_pGraph in PushSource/BasePin).
    // 4. Connect its output pin to an internal Tee filter or directly to a transform
    //    that then feeds the virtual camera's output pin logic.
    //    This part is complex and involves managing a separate DirectShow graph
    //    within this filter.

    // Find and instantiate the physical source filter
    HRESULT hr;
    ICreateDevEnum *pDevEnum = nullptr;
    IEnumMoniker *pEnum = nullptr;
    IMoniker *pMoniker = nullptr;

    hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));
    if (FAILED(hr)) {
        AkLogError() << "Failed to create SystemDeviceEnum: " << hr << std::endl;
        return;
    }

    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
    if (FAILED(hr) || pEnum == nullptr) {
        AkLogError() << "Failed to create class enumerator for video input devices or no devices found: " << hr << std::endl;
        if(pDevEnum) pDevEnum->Release();
        return;
    }

    bool found = false;
    while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
        IPropertyBag *pPropBag = nullptr;
        hr = pMoniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&pPropBag));
        if (SUCCEEDED(hr)) {
            VARIANT varName;
            VariantInit(&varName);
            hr = pPropBag->Read(L"FriendlyName", &varName, nullptr);
            if (SUCCEEDED(hr)) {
                // Use stringFromWSTR from global namespace
                std::string friendlyName = stringFromWSTR(varName.bstrVal);
                VariantClear(&varName);

                if (friendlyName == m_sourceCameraName) {
                    AkLogInfo() << "Found physical camera: " << m_sourceCameraName << std::endl;
                    hr = pMoniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void**)&m_pPhysicalSourceFilter);
                    if (SUCCEEDED(hr)) {
                        AkLogInfo() << "Successfully bound to physical camera filter." << std::endl;
                        found = true;
                    } else {
                        AkLogError() << "Failed to bind to physical camera filter: " << hr << std::endl;
                    }
                    // We found our camera, break from loop after releasing propbag and moniker
                    pPropBag->Release();
                    pMoniker->Release();
                    break;
                }
            }
             if (SUCCEEDED(hr)) VariantClear(&varName); // Ensure varName is cleared if Read succeeded but name didn't match
            pPropBag->Release();
        }
        pMoniker->Release(); // Release moniker for current iteration
    }

    if (pEnum) pEnum->Release();
    if (pDevEnum) pDevEnum->Release();

    if (!found) {
        AkLogError() << "Physical camera '" << m_sourceCameraName << "' not found." << std::endl;
        m_pPhysicalSourceFilter = nullptr; // Ensure it's null if not found
        return;
    }

    // TODO: If m_pPhysicalSourceFilter is not null, create internal graph, add filter, connect, run.
    // For now, we just have the filter. The actual frame piping is next.
    // This will likely involve creating m_pPhysicalSourceGraphBuilder, adding m_pPhysicalSourceFilter,
    // and then connecting it within an internal graph. The output of that graph
    // will then need to be fed into the virtual pin's data stream.

    if (!m_pPhysicalSourceFilter) {
        AkLogError() << "Physical source filter not available to initialize." << std::endl;
        return;
    }

    // HRESULT hr; // Removed redeclaration, hr is already in scope from the outer block of this function.
    // Create the Filter Graph Manager for the physical camera
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&m_pPhysicalSourceGraphBuilder);
    if (FAILED(hr) || !m_pPhysicalSourceGraphBuilder) {
        AkLogError() << "Failed to create physical source graph builder: " << hr << std::endl;
        if (m_pPhysicalSourceFilter) { // Still release the source filter if graph fails
            m_pPhysicalSourceFilter->Release();
            m_pPhysicalSourceFilter = nullptr;
        }
        return;
    }
    AkLogInfo() << "Physical source graph builder created." << std::endl;

    hr = m_pPhysicalSourceGraphBuilder->AddFilter(m_pPhysicalSourceFilter, L"Physical Source Camera");
    if (FAILED(hr)) {
        AkLogError() << "Failed to add physical source filter to graph: " << hr << std::endl;
        // Release graph and filter
        if (m_pPhysicalSourceFilter) { m_pPhysicalSourceFilter->Release(); m_pPhysicalSourceFilter = nullptr; }
        if (m_pPhysicalSourceGraphBuilder) { m_pPhysicalSourceGraphBuilder->Release(); m_pPhysicalSourceGraphBuilder = nullptr; }
        return;
    }
    AkLogInfo() << "Physical source filter added to graph." << std::endl;

    // Find output pin of physical camera
    IPin *pPhysOutPin = nullptr;
    IEnumPins *pEnumPins = nullptr;
    hr = m_pPhysicalSourceFilter->EnumPins(&pEnumPins);
    if (SUCCEEDED(hr)) {
        IPin *pPin = nullptr;
        while (pEnumPins->Next(1, &pPin, nullptr) == S_OK) {
            PIN_DIRECTION pinDir;
            pPin->QueryDirection(&pinDir);
            if (pinDir == PINDIR_OUTPUT) {
                pPhysOutPin = pPin;
                // pPhysOutPin already AddRef'd by Next
                break;
            }
            pPin->Release();
        }
        pEnumPins->Release();
    }

    if (!pPhysOutPin) {
        AkLogError() << "Could not find output pin on physical source filter." << std::endl;
        // Release graph and filter
        if (m_pPhysicalSourceFilter) { m_pPhysicalSourceFilter->Release(); m_pPhysicalSourceFilter = nullptr; }
        if (m_pPhysicalSourceGraphBuilder) { m_pPhysicalSourceGraphBuilder->Release(); m_pPhysicalSourceGraphBuilder = nullptr; }
        return;
    }
    AkLogInfo() << "Found output pin on physical source filter." << std::endl;

    // Connect physical camera output to our custom input pin
    hr = m_pPhysicalSourceGraphBuilder->Connect(pPhysOutPin, m_pPhysicalSourceInputPin);
    pPhysOutPin->Release(); // Release our ref to the physical output pin

    if (FAILED(hr)) {
        AkLogError() << "Failed to connect physical source output to our input pin: " << hr << std::endl;
        // Release graph and filter
        if (m_pPhysicalSourceFilter) { m_pPhysicalSourceFilter->Release(); m_pPhysicalSourceFilter = nullptr; }
        if (m_pPhysicalSourceGraphBuilder) { m_pPhysicalSourceGraphBuilder->Release(); m_pPhysicalSourceGraphBuilder = nullptr; }
        return;
    }
    AkLogInfo() << "Successfully connected physical camera to our input pin." << std::endl;

    // Run the physical camera graph
    IMediaControl *pMC = nullptr;
    hr = m_pPhysicalSourceGraphBuilder->QueryInterface(IID_IMediaControl, (void**)&pMC);
    if (SUCCEEDED(hr)) {
        hr = pMC->Run();
        if (FAILED(hr)) {
            AkLogError() << "Failed to run the physical source graph: " << hr << std::endl;
        } else {
            AkLogInfo() << "Physical source graph is running." << std::endl;
        }
        pMC->Release();
    } else {
        AkLogError() << "Failed to get IMediaControl for physical source graph: " << hr << std::endl;
    }
}

void AkVCam::BaseFilter::ReleaseSourceCamera() {
    AkLogFunction();
    if (m_pPhysicalSourceGraphBuilder) {
        IMediaControl *pMC = nullptr;
        m_pPhysicalSourceGraphBuilder->QueryInterface(IID_IMediaControl, (void**)&pMC);
        if (pMC) {
            pMC->Stop(); // Stop the graph before dismantling
            AkLogInfo() << "Stopped physical source graph." << std::endl;
            pMC->Release();
        }
    }
    // TODO: Disconnect pins, remove filters from graph before releasing.
    // For now, direct release. Proper cleanup would involve:
    // IEnumPins on m_pPhysicalSourceFilter, for each pin, call Disconnect.
    // m_pPhysicalSourceGraphBuilder->RemoveFilter(m_pPhysicalSourceFilter);
    // Similar for any other filters added to m_pPhysicalSourceGraphBuilder.

    if (m_pPhysicalSourceFilter) {
        m_pPhysicalSourceFilter->Release();
        m_pPhysicalSourceFilter = nullptr;
        AkLogInfo() << "Released physical source filter." << std::endl;
    }
    if (m_pPhysicalSourceGraphBuilder) {
        // TODO: Remove filters from graph before releasing graph builder
        m_pPhysicalSourceGraphBuilder->Release();
        m_pPhysicalSourceGraphBuilder = nullptr;
        AkLogInfo() << "Released physical source graph builder." << std::endl;
    }
}

AkVCam::BaseFilter::~BaseFilter()
{
    ReleaseSourceCamera(); // Release physical camera resources
    if (m_pPhysicalSourceInputPin) {
        m_pPhysicalSourceInputPin->Release();
        m_pPhysicalSourceInputPin = nullptr;
    }
    DeleteCriticalSection(&m_physicalFrameCritSec);
    // Free media type if allocated
    if (m_physicalCameraMediaType.cbFormat != 0) CoTaskMemFree(m_physicalCameraMediaType.pbFormat);
    if (m_physicalCameraMediaType.pUnk != NULL) m_physicalCameraMediaType.pUnk->Release();

    delete this->d;
}

void AkVCam::BaseFilter::addPin(const std::vector<AkVCam::VideoFormat> &formats,
                                const std::string &pinName,
                                bool changed)
{
    AkLogFunction();
    this->d->m_pins->addPin(new Pin(this, formats, pinName), changed);
}

void AkVCam::BaseFilter::removePin(IPin *pin, bool changed)
{
    AkLogFunction();
    this->d->m_pins->removePin(pin, changed);
}

AkVCam::BaseFilter *AkVCam::BaseFilter::create(const GUID &clsid)
{
    AkLogFunction();
    auto camera = Preferences::cameraFromCLSID(clsid);
    AkLogInfo() << "CLSID: " << stringFromIid(clsid) << std::endl;
    AkLogInfo() << "ID: " << camera << std::endl;

    if (camera < 0)
        return nullptr;

    auto description = Preferences::cameraDescription(size_t(camera));
    AkLogInfo() << "Description: " << description << std::endl;
    auto baseFilter = new BaseFilter(clsid,
                                     description,
                                     DSHOW_PLUGIN_VENDOR);
    auto formats = Preferences::cameraFormats(size_t(camera));
    baseFilter->addPin(formats, "Video", false);

    return baseFilter;
}

IFilterGraph *AkVCam::BaseFilter::filterGraph() const
{
    return this->d->m_filterGraph;
}

IReferenceClock *AkVCam::BaseFilter::referenceClock() const
{
    return this->d->m_referenceClock;
}

std::string AkVCam::BaseFilter::deviceId()
{
    CLSID clsid;
    this->GetClassID(&clsid);
    auto cameraIndex = Preferences::cameraFromCLSID(clsid);

    if (cameraIndex < 0)
        return {};

    return Preferences::cameraId(size_t(cameraIndex));
}

std::string AkVCam::BaseFilter::broadcaster()
{
    auto deviceId = this->deviceId();

    if (deviceId.empty())
        return {};

    return this->d->m_ipcBridge.broadcaster(deviceId);
}

std::string AkVCam::BaseFilter::sourceCameraName() const
{
    return m_sourceCameraName;
}

HRESULT AkVCam::BaseFilter::QueryInterface(const IID &riid, void **ppvObject)
{
    AkLogFunction();
    AkLogInfo() << "IID: " << AkVCam::stringFromClsid(riid) << std::endl;

    if (!ppvObject)
        return E_POINTER;

    *ppvObject = nullptr;

    if (IsEqualIID(riid, IID_IUnknown)
        || IsEqualIID(riid, IID_IBaseFilter)
        || IsEqualIID(riid, IID_IMediaFilter)) {
        AkLogInterface(IBaseFilter, this);
        this->AddRef();
        *ppvObject = this;

        return S_OK;
    } else if (IsEqualIID(riid, IID_IAMFilterMiscFlags)) {
        auto filterMiscFlags = new FilterMiscFlags;
        AkLogInterface(IAMFilterMiscFlags, filterMiscFlags);
        filterMiscFlags->AddRef();
        *ppvObject = filterMiscFlags;

        return S_OK;
    } else if (IsEqualIID(riid, IID_IAMVideoControl)) {
        IEnumPins *pins = nullptr;
        this->d->m_pins->Clone(&pins);
        auto videoControl = new VideoControl(pins);
        pins->Release();
        AkLogInterface(IAMVideoControl, videoControl);
        videoControl->AddRef();
        *ppvObject = videoControl;

        return S_OK;
    } else if (IsEqualIID(riid, IID_IAMVideoProcAmp)) {
        auto videoProcAmp = this->d->m_videoProcAmp;
        AkLogInterface(IAMVideoProcAmp, videoProcAmp);
        videoProcAmp->AddRef();
        *ppvObject = videoProcAmp;

        return S_OK;
    } else if (IsEqualIID(riid, IID_IReferenceClock)) {
        auto referenceClock = this->d->m_referenceClock;
        AkLogInterface(IReferenceClock, referenceClock);
        referenceClock->AddRef();
        *ppvObject = referenceClock;

        return S_OK;
    } else if (IsEqualIID(riid, IID_ISpecifyPropertyPages)) {
        this->d->m_pins->Reset();
        IPin *pin = nullptr;
        this->d->m_pins->Next(1, &pin, nullptr);
        auto specifyPropertyPages = new SpecifyPropertyPages(pin);
        pin->Release();
        AkLogInterface(ISpecifyPropertyPages, specifyPropertyPages);
        specifyPropertyPages->AddRef();
        *ppvObject = specifyPropertyPages;

        return S_OK;
    } else {
        this->d->m_pins->Reset();
        IPin *pin = nullptr;
        this->d->m_pins->Next(1, &pin, nullptr);
        auto result = pin->QueryInterface(riid, ppvObject);
        pin->Release();

        if (SUCCEEDED(result))
            return result;
    }

    return MediaFilter::QueryInterface(riid, ppvObject);
}

HRESULT AkVCam::BaseFilter::EnumPins(IEnumPins **ppEnum)
{
    AkLogFunction();

    if (!this->d->m_pins)
        return E_FAIL;

    auto result = this->d->m_pins->Clone(ppEnum);

    if (SUCCEEDED(result))
        (*ppEnum)->Reset();

    return result;
}

HRESULT AkVCam::BaseFilter::FindPin(LPCWSTR Id, IPin **ppPin)
{
    AkLogFunction();

    if (!ppPin)
        return E_POINTER;

    *ppPin = nullptr;

    if (!Id)
        return VFW_E_NOT_FOUND;

    IPin *pin = nullptr;
    HRESULT result = VFW_E_NOT_FOUND;
    this->d->m_pins->Reset();

    while (this->d->m_pins->Next(1, &pin, nullptr) == S_OK) {
        WCHAR *pinId = nullptr;
        auto ok = pin->QueryId(&pinId);

        if (ok == S_OK && wcscmp(pinId, Id) == 0) {
            *ppPin = pin;
            (*ppPin)->AddRef();
            result = S_OK;
        }

        CoTaskMemFree(pinId);
        pin->Release();
        pin = nullptr;

        if (result == S_OK)
            break;
    }

    return result;
}

HRESULT AkVCam::BaseFilter::QueryFilterInfo(FILTER_INFO *pInfo)
{
    AkLogFunction();

    if (!pInfo)
        return E_POINTER;

    memset(pInfo->achName, 0, MAX_FILTER_NAME * sizeof(WCHAR));

    if (!this->d->m_filterName.empty()) {
        auto filterName = stringToWSTR(this->d->m_filterName);
        memcpy(pInfo->achName,
               filterName,
               (std::min<size_t>)(wcsnlen(filterName, MAX_FILTER_NAME)
                                  * sizeof(WCHAR),
                                  MAX_FILTER_NAME));
        CoTaskMemFree(filterName);
    }

    pInfo->pGraph = this->d->m_filterGraph;

    if (pInfo->pGraph)
        pInfo->pGraph->AddRef();

    return S_OK;
}

HRESULT AkVCam::BaseFilter::JoinFilterGraph(IFilterGraph *pGraph, LPCWSTR pName)
{
    AkLogFunction();

    this->d->m_filterGraph = pGraph;
    this->d->m_filterName = pName? stringFromWSTR(pName): "";

    AkLogInfo() << "Filter graph: " << this->d->m_filterGraph << std::endl;
    AkLogInfo() << "Name: " << this->d->m_filterName << std::endl;

    return S_OK;
}

HRESULT AkVCam::BaseFilter::QueryVendorInfo(LPWSTR *pVendorInfo)
{
    AkLogFunction();

    if (this->d->m_vendor.size() < 1)
        return E_NOTIMPL;

    if (!pVendorInfo)
        return E_POINTER;

    *pVendorInfo = stringToWSTR(this->d->m_vendor);

    return S_OK;
}

void AkVCam::BaseFilter::stateChanged(FILTER_STATE state)
{
    CLSID clsid;
    this->GetClassID(&clsid);
    auto cameraIndex = Preferences::cameraFromCLSID(clsid);

    if (cameraIndex < 0)
        return;

    auto deviceId = Preferences::cameraId(size_t(cameraIndex));

    if (state == State_Running)
        this->d->m_ipcBridge.addListener(deviceId);
    else
        this->d->m_ipcBridge.removeListener(deviceId);
}

AkVCam::BaseFilterPrivate::BaseFilterPrivate(AkVCam::BaseFilter *self,
                                             const std::string &filterName,
                                             const std::string &vendor):
    self(self),
    m_pins(new AkVCam::EnumPins),
    m_videoProcAmp(new VideoProcAmp),
    m_referenceClock(new ReferenceClock),
    m_vendor(vendor),
    m_filterName(filterName)
{
    this->m_pins->AddRef();
    this->m_videoProcAmp->AddRef();
    this->m_referenceClock->AddRef();

    this->m_ipcBridge.connectServerStateChanged(this,
                                                &BaseFilterPrivate::serverStateChanged);
    this->m_ipcBridge.connectDevicesChanged(this,
                                            &BaseFilterPrivate::devicesChanged);
    this->m_ipcBridge.connectFrameReady(this,
                                        &BaseFilterPrivate::frameReady);
    this->m_ipcBridge.connectPictureChanged(this,
                                            &BaseFilterPrivate::pictureChanged);
    this->m_ipcBridge.connectBroadcastingChanged(this,
                                                 &BaseFilterPrivate::setBroadcasting);
    this->m_ipcBridge.connectControlsChanged(this,
                                             &BaseFilterPrivate::setControls);
}

AkVCam::BaseFilterPrivate::~BaseFilterPrivate()
{
    this->m_pins->setBaseFilter(nullptr);
    this->m_pins->Release();
    this->m_videoProcAmp->Release();
    this->m_referenceClock->Release();
}

IEnumPins *AkVCam::BaseFilterPrivate::pinsForDevice(const std::string &deviceId)
{
    AkLogFunction();
    CLSID clsid;
    self->GetClassID(&clsid);
    auto cameraIndex = Preferences::cameraFromCLSID(clsid);

    if (cameraIndex < 0)
        return nullptr;

    auto id = Preferences::cameraId(size_t(cameraIndex));

    if (id.empty() || id != deviceId)
        return nullptr;

    IEnumPins *pins = nullptr;
    self->EnumPins(&pins);

    return pins;
}

void AkVCam::BaseFilterPrivate::updatePins()
{
    CLSID clsid;
    this->self->GetClassID(&clsid);
    auto cameraIndex = Preferences::cameraFromCLSID(clsid);

    if (cameraIndex < 0)
        return;

    auto deviceId = Preferences::cameraId(size_t(cameraIndex));

    auto broadcaster = this->m_ipcBridge.broadcaster(deviceId);
    AkVCamDevicePinCall(deviceId,
                        this,
                        setBroadcasting,
                        broadcaster)
    auto controlsList = this->m_ipcBridge.controls(deviceId);
    std::map<std::string, int> controls;

    for (auto &control: controlsList)
        controls[control.id] = control.value;

    AkVCamDevicePinCall(deviceId, this, setControls, controls)
}

void AkVCam::BaseFilterPrivate::serverStateChanged(void *userData,
                                                   IpcBridge::ServerState state)
{
    AkLogFunction();
    auto self = reinterpret_cast<BaseFilterPrivate *>(userData);

    if (self->m_serverState == state)
        return;

    FILTER_STATE filterState = State_Stopped;
    self->self->GetState(0, &filterState);

    if (filterState != State_Stopped)
        return;

    IEnumPins *pins = nullptr;
    self->self->EnumPins(&pins);

    if (pins) {
        AkVCamPinCall(pins, serverStateChanged, state)
        pins->Release();
    }

    if (state == IpcBridge::ServerStateAvailable)
        self->updatePins();

    self->m_serverState = state;
}

void AkVCam::BaseFilterPrivate::frameReady(void *userData,
                                           const std::string &deviceId,
                                           const VideoFrame &frame)
{
    AkLogFunction();
    auto self = reinterpret_cast<BaseFilterPrivate *>(userData);
    AkVCamDevicePinCall(deviceId, self, frameReady, frame)
}

void AkVCam::BaseFilterPrivate::pictureChanged(void *userData,
                                               const std::string &picture)
{
    AkLogFunction();
    auto self = reinterpret_cast<BaseFilterPrivate *>(userData);
    IEnumPins *pins = nullptr;
    self->self->EnumPins(&pins);

    if (pins) {
        AkVCamPinCall(pins, setPicture, picture)
        pins->Release();
    }
}

void AkVCam::BaseFilterPrivate::devicesChanged(void *userData,
                                               const std::vector<std::string> &devices)
{
    UNUSED(userData);
    UNUSED(devices);
    AkLogFunction();
    std::vector<HWND> handlers;
    EnumWindows(WNDENUMPROC(AkVCamEnumWindowsProc), LPARAM(&handlers));

    for (auto &handler: handlers)
        SendMessage(handler, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0);
}

void AkVCam::BaseFilterPrivate::setBroadcasting(void *userData,
                                                const std::string &deviceId,
                                                const std::string &broadcaster)
{
    AkLogFunction();
    auto self = reinterpret_cast<BaseFilterPrivate *>(userData);
    AkVCamDevicePinCall(deviceId, self, setBroadcasting, broadcaster)
}

void AkVCam::BaseFilterPrivate::setControls(void *userData,
                                            const std::string &deviceId,
                                            const std::map<std::string, int> &controls)
{
    AkLogFunction();
    auto self = reinterpret_cast<BaseFilterPrivate *>(userData);
    AkVCamDevicePinCall(deviceId, self, setControls, controls)
}

BOOL AkVCamEnumWindowsProc(HWND handler, LPARAM userData)
{
    auto handlers = reinterpret_cast<std::vector<HWND> *>(userData);
    handlers->push_back(handler);

    return TRUE;
}
