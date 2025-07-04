#ifndef AKVCAM_MACOS_CAPTUREDEVICE_H
#define AKVCAM_MACOS_CAPTUREDEVICE_H

#ifdef __APPLE__

#include "icapturedevice.h"
#include <string>
#include <vector>
#include <memory> // For std::unique_ptr

// Forward declare Objective-C types to avoid including AVFoundation headers directly in the .h
// if possible, or include minimal necessary headers.
// For simplicity here, we might just include them in the .mm file.
// However, if MacOSCaptureDeviceInternal; is an Obj-C class, it needs to be declared.
#ifdef __OBJC__
@class AVCaptureSession;
@class AVCaptureDeviceInput;
@class AVCaptureVideoDataOutput;
@class AkVCamMacOSCaptureDelegate; // Our Objective-C delegate helper
#else
// Opaque pointers for C++ if not in an Objective-C context (though .h will be included by .mm)
typedef void AVCaptureSession;
typedef void AVCaptureDeviceInput;
typedef void AVCaptureVideoDataOutput;
typedef void AkVCamMacOSCaptureDelegate;
#endif


namespace AkVCam {

class MacOSCaptureDevice : public ICaptureDevice {
public:
    MacOSCaptureDevice();
    ~MacOSCaptureDevice() override;

    std::vector<PhysicalCameraInfo> enumerateDevices() override;
    bool open(const std::string& deviceId, FrameCallback callback, const VideoFormat* preferredFormat = nullptr) override;
    bool start() override;
    void stop() override;
    void close() override;
    bool isCapturing() const override;
    VideoFormat getCurrentFormat() const override;

    // Method called by the Objective-C delegate
    void processFrame(const void* frameData, size_t frameSize, FourCC pixelFormat, int width, int height, size_t bytesPerRow);


private:
    void releaseSession();

    // Using void* and casting in .mm file to keep AVFoundation details out of .h as much as possible
    AVCaptureSession* m_captureSession;
    AVCaptureDeviceInput* m_deviceInput;
    AVCaptureVideoDataOutput* m_videoDataOutput;
    AkVCamMacOSCaptureDelegate* m_delegate; // Objective-C delegate instance

    FrameCallback m_frameCallback;
    std::string m_deviceId; // The uniqueID of the AVCaptureDevice
    VideoFormat m_currentFormat;
    bool m_isCapturing;
    bool m_isOpened;

    // Dispatch queue for sample buffer delegate
    void* m_sampleCallbackQueue; // dispatch_queue_t
};

} // namespace AkVCam

#endif // __APPLE__
#endif // AKVCAM_MACOS_CAPTUREDEVICE_H
