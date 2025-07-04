#ifndef AKVCAM_CAPTURETYPES_H
#define AKVCAM_CAPTURETYPES_H

#include <string>
#include <vector>
#include <functional>
#include "VCamUtils/src/videoframe.h" // Assuming VideoFrame can be reused

namespace AkVCam {

struct PhysicalCameraInfo {
    std::string deviceId; // Platform-specific unique ID
    std::string friendlyName;
    std::string description; // More detailed description if available
    // Potentially add a list of supported VideoFormat an_instance_of_this;
};

// Callback for when a new frame is captured
// The VideoFrame should be owned by the callee (ICaptureDevice implementation)
// and the callback should copy the data if it needs to keep it.
using FrameCallback = std::function<void(const VideoFrame& frame, const std::string& deviceId)>;

} // namespace AkVCam

#endif // AKVCAM_CAPTURETYPES_H
