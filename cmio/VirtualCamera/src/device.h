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

#ifndef DEVICE_H
#define DEVICE_H

#include <map>
#include <list>
#include <memory>
#include <string> // For std::string

#include "stream.h"

// Forward declarations for AVFoundation types to keep Obj-C out of header if possible,
// but for implementation simplicity, we might include headers in .mm file.
// If these are directly in the header, it implies this header is for .mm consumption.
#ifdef __OBJC__
@class AVCaptureSession;
@class AVCaptureDevice;
@class AVCaptureVideoDataOutput;
#else
typedef void AVCaptureSession;
typedef void AVCaptureDevice;
typedef void AVCaptureVideoDataOutput;
typedef void dispatch_queue_t;
#endif

namespace AkVCam
{
    class Device;
    typedef std::shared_ptr<Device> DevicePtr;

    class Device: public Object
    {
        AKVCAM_SIGNAL(AddListener, const std::string &deviceId)
        AKVCAM_SIGNAL(RemoveListener, const std::string &deviceId)

        public:
            Device(CMIOHardwarePlugInRef pluginInterface,
                   bool createObject=false);
            ~Device();

            OSStatus createObject();
            OSStatus registerObject(bool regist=true);
            StreamPtr addStream();
            std::list<StreamPtr> addStreams(int n);
            OSStatus registerStreams(bool regist=true);
            std::string deviceId() const;
            void setDeviceId(const std::string &deviceId);
            void stopStreams();

            void serverStateChanged(IpcBridge::ServerState state);
            void frameReady(const VideoFrame &frame);
            void setPicture(const std::string &picture);
            void setBroadcasting(const std::string &broadcaster);
            void setHorizontalMirror(bool horizontalMirror);
            void setVerticalMirror(bool verticalMirror);
            void setScaling(Scaling scaling);
            void setAspectRatio(AspectRatio aspectRatio);
            void setSwapRgb(bool swap);

            // Device Interface
            OSStatus suspend();
            OSStatus resume();
            OSStatus startStream(CMIOStreamID stream);
            OSStatus stopStream(CMIOStreamID stream);
            OSStatus processAVCCommand(CMIODeviceAVCCommand *ioAVCCommand);
            OSStatus processRS422Command(CMIODeviceRS422Command *ioRS422Command);

        private:
            std::string m_deviceId;
            std::map<CMIOObjectID, StreamPtr> m_streams;

            // AVFoundation related members for physical camera capture
            std::string m_sourceCameraUniqueID;
            AVCaptureSession* m_captureSession = nullptr;
            AVCaptureDevice* m_physicalCaptureDevice = nullptr; // The physical AVFoundation device
            AVCaptureVideoDataOutput* m_videoDataOutput = nullptr;
            dispatch_queue_t m_captureSessionQueue = nullptr;
            bool m_isSourcingFromPhysical = false;

            void InitializePhysicalCameraCapture();
            void ReleasePhysicalCameraCapture();
            void updateStreamsProperty();

        // AVCaptureVideoDataOutputSampleBufferDelegate method needs to be callable
        // This typically means the Device class (or a helper) conforms to the protocol.
        // This is hard to represent purely in C++ header if it's an Obj-C protocol method.
        // It will be implemented in the .mm file.
    };
}

#endif // DEVICE_H
