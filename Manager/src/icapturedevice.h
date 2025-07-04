#ifndef AKVCAM_ICAPTUREDEVICE_H
#define AKVCAM_ICAPTUREDEVICE_H

#include "capturetypes.h"
#include "VCamUtils/src/videoformat.h" // For VideoFormat
#include <memory> // For std::unique_ptr

namespace AkVCam {

class ICaptureDevice {
public:
    virtual ~ICaptureDevice() = default;

    // Enumerates available physical cameras on the system.
    virtual std::vector<PhysicalCameraInfo> enumerateDevices() = 0;

    // Opens and prepares a specific camera for capturing.
    // deviceId: The platform-specific unique ID obtained from enumerateDevices.
    // preferredFormat: Optional. If provided, attempts to set this format.
    // callback: The function to call when a new frame is available.
    virtual bool open(const std::string& deviceId, FrameCallback callback, const VideoFormat* preferredFormat = nullptr) = 0;

    // Starts the capture process. open() must have been called successfully.
    virtual bool start() = 0;

    // Stops the capture process.
    virtual void stop() = 0;

    // Closes the camera and releases resources.
    virtual void close() = 0;

    // Returns true if the camera is currently capturing.
    virtual bool isCapturing() const = 0;

    // Returns the VideoFormat the camera is currently capturing in.
    // Valid only after open() and successful format negotiation.
    virtual VideoFormat getCurrentFormat() const = 0;
};

// Factory function to create the appropriate platform-specific implementation
std::unique_ptr<ICaptureDevice> createPlatformCaptureDevice();

} // namespace AkVCam

#endif // AKVCAM_ICAPTUREDEVICE_H
