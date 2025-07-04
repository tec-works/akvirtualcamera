#include "icapturedevice.h"

// Forward declarations for platform-specific implementations
// These will be defined in their respective .cpp/.mm files
#ifdef _WIN32
namespace AkVCam { class WindowsCaptureDevice; }
#elif __APPLE__
namespace AkVCam { class MacOSCaptureDevice; }
#else
// Potentially a LinuxCaptureDevice or a NullCaptureDevice
namespace AkVCam { class NullCaptureDevice; }
#endif

namespace AkVCam {

std::unique_ptr<ICaptureDevice> createPlatformCaptureDevice() {
#ifdef _WIN32
    // Needs actual class: return std::make_unique<WindowsCaptureDevice>();
    return nullptr; // Placeholder
#elif __APPLE__
    // Needs actual class: return std::make_unique<MacOSCaptureDevice>();
    return nullptr; // Placeholder
#else
    // Needs actual class: return std::make_unique<NullCaptureDevice>();
    return nullptr; // Placeholder for other platforms or a default null implementation
#endif
}

} // namespace AkVCam
