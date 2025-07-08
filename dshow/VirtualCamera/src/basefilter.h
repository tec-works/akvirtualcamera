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

#ifndef BASEFILTER_H
#define BASEFILTER_H

#include <string>
#include <vector>

#include "mediafilter.h"
#include "physicalsourceinputpin.h" // Include for PhysicalSourceInputPin
#include <vector> // For std::vector
#include <windows.h> // For CRITICAL_SECTION

namespace AkVCam
{
    class BaseFilterPrivate;
    class VideoFormat;
    // class PhysicalSourceInputPin; // Forward declare if not including header, but better to include

    class BaseFilter:
            public IBaseFilter,
            public MediaFilter
    {
        public:
            BaseFilter(const GUID &clsid,
                       const std::string &filterName={},
                       const std::string &vendor={});
            virtual ~BaseFilter();

            void addPin(const std::vector<VideoFormat> &formats={},
                        const std::string &pinName={},
                        bool changed=true);
            void removePin(IPin *pin, bool changed=true);
            static BaseFilter *create(const GUID &clsid);
            IFilterGraph *filterGraph() const;
            IReferenceClock *referenceClock() const;
            std::string deviceId();
            std::string broadcaster();
            std::string sourceCameraName() const; // Getter for source camera name
            PhysicalSourceInputPin* GetPhysicalSourceInputPin() { return m_pPhysicalSourceInputPin; } // Getter for the input pin
            void NotifyPhysicalFrameReady(const BYTE* pData, LONG size, const AM_MEDIA_TYPE& mt);
            HRESULT GetLatestPhysicalFrame(std::vector<BYTE>& frameBuffer, AM_MEDIA_TYPE& frameMediaType);


            DECLARE_IMEDIAFILTER_NQ

            // IUnknown
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                                     void **ppvObject);

            // IBaseFilter
            HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins **ppEnum);
            HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR Id, IPin **ppPin);
            HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO *pInfo);
            HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph *pGraph,
                                                      LPCWSTR pName);
            HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR *pVendorInfo);

        private:
            BaseFilterPrivate *d;
            std::string m_sourceCameraName; // Added to store source camera
            // Members for physical camera graph
            IGraphBuilder *m_pPhysicalSourceGraphBuilder = nullptr;
            IBaseFilter *m_pPhysicalSourceFilter = nullptr;
            PhysicalSourceInputPin* m_pPhysicalSourceInputPin = nullptr;
            // Potentially add MediaControl, SampleGrabber interfaces etc.

            // Buffer for latest frame from physical camera
            std::vector<BYTE> m_physicalCameraLatestFrame;
            CRITICAL_SECTION m_physicalFrameCritSec;
            AM_MEDIA_TYPE m_physicalCameraMediaType;


        protected:
            void stateChanged(FILTER_STATE state);
            void InitializeSourceCamera(); // Added to initialize physical camera
            void ReleaseSourceCamera(); // Added to release physical camera resources
    };
}

#endif // BASEFILTER_H
