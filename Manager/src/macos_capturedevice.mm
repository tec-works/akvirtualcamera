#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include "macos_capturedevice.h"
#include "VCamUtils/src/logger.h"
#include "VCamUtils/src/videoformat.h" // For VideoFormat fourcc/guid utils if needed, though less relevant for AVF pixel formats

// Objective-C Delegate for AVCaptureVideoDataOutputSampleBufferDelegate
@interface AkVCamMacOSCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
    AkVCam::MacOSCaptureDevice* _cppOwner; // Pointer back to the C++ owner
    std::string _deviceId;
    dispatch_queue_t _processingQueue; // Optional: for further processing off the sample queue
}
- (instancetype)initWithOwner:(AkVCam::MacOSCaptureDevice*)owner deviceId:(const std::string&)deviceId;
@end

@implementation AkVCamMacOSCaptureDelegate

- (instancetype)initWithOwner:(AkVCam::MacOSCaptureDevice*)owner deviceId:(const std::string&)deviceId {
    self = [super init];
    if (self) {
        _cppOwner = owner;
        _deviceId = deviceId; // Store a copy of deviceId
        // _processingQueue = dispatch_queue_create("akvcam.frameprocessing.queue", DISPATCH_QUEUE_SERIAL);
        AkLogDebug() << "AkVCamMacOSCaptureDelegate created for device: " << _deviceId.c_str() << std::endl;
    }
    return self;
}

- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    if (!_cppOwner) {
        return;
    }

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) {
        AkLogError() << "Delegate: Failed to get image buffer from sample buffer." << std::endl;
        return;
    }

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    void *baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
    size_t width = CVPixelBufferGetWidth(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    OSType pixelFormatType = CVPixelBufferGetPixelFormatType(imageBuffer);

    AkVCam::FourCC akFourCC = 0;
    // Map CVPixelFormatType to AkVCam::FourCC
    // This mapping needs to be comprehensive
    switch (pixelFormatType) {
        case kCVPixelFormatType_32BGRA:
            akFourCC = AkVCam::PixelFormatBGR32; // Or RGB32 if bytes are swapped by VideoFrame
            break;
        case kCVPixelFormatType_24RGB:
            akFourCC = AkVCam::PixelFormatRGB24;
            break;
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange: // NV12
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
            akFourCC = AkVCam::PixelFormatNV12;
            break;
        // Add more mappings as needed, e.g., YUY2 often kCVPixelFormatType_422YpCbCr8
        case 'yuvs': // kCVPixelFormatType_422YpCbCr8 sometimes used for YUY2
             akFourCC = AkVCam::PixelFormatYUY2; // Check byte order
             break;
        default:
            AkLogWarning() << "Delegate: Unhandled CVPixelFormatType: " << pixelFormatType << " (" << FourCCString(pixelFormatType).c_str() << ")" << std::endl;
            CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            return;
    }

    // The actual frame data size might be height * bytesPerRow
    // For planar formats, baseAddress might only be for the first plane.
    // For now, assume packed formats where frameSize is height * bytesPerRow for the relevant plane(s).
    // A more robust solution would handle planar data by getting pointers and sizes for each plane.
    size_t frameDataSize = height * bytesPerRow; // This is an approximation for packed formats.

    // Call the C++ owner's processing method
    // Note: this callback happens on AVFoundation's internal queue.
    // The owner should copy data quickly if further processing is needed on another thread.
    _cppOwner->processFrame(baseAddress, frameDataSize, akFourCC, width, height, bytesPerRow);

    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
}

// Helper to convert FourCC to a printable string, useful for logging unknown OSTypes
static std::string FourCCString(OSType type) {
    char c[5] = {0};
    c[0] = (type >> 24) & 0xFF;
    c[1] = (type >> 16) & 0xFF;
    c[2] = (type >> 8) & 0xFF;
    c[3] = type & 0xFF;
    if (isprint(c[0]) && isprint(c[1]) && isprint(c[2]) && isprint(c[3])) {
        return std::string(c);
    }
    return std::to_string(type);
}

@end


namespace AkVCam {

MacOSCaptureDevice::MacOSCaptureDevice()
    : m_captureSession(nullptr), m_deviceInput(nullptr), m_videoDataOutput(nullptr), m_delegate(nullptr),
      m_isCapturing(false), m_isOpened(false), m_sampleCallbackQueue(nullptr) {
    // Create a serial dispatch queue for the sample buffer delegate.
    m_sampleCallbackQueue = dispatch_queue_create("akvcam.macos.samplequeue", DISPATCH_QUEUE_SERIAL);
    AkLogDebug() << "MacOSCaptureDevice created." << std::endl;
}

MacOSCaptureDevice::~MacOSCaptureDevice() {
    close();
    if (m_sampleCallbackQueue) {
        dispatch_release(static_cast<dispatch_queue_t>(m_sampleCallbackQueue));
        m_sampleCallbackQueue = nullptr;
    }
    AkLogDebug() << "MacOSCaptureDevice destroyed." << std::endl;
}

void MacOSCaptureDevice::releaseSession() {
    if (m_captureSession) {
        if ([(AVCaptureSession*)m_captureSession isRunning]) {
            [(AVCaptureSession*)m_captureSession stopRunning];
        }
        if (m_deviceInput) {
            [(AVCaptureSession*)m_captureSession removeInput:(AVCaptureDeviceInput*)m_deviceInput];
            [(AVCaptureDeviceInput*)m_deviceInput release];
            m_deviceInput = nullptr;
        }
        if (m_videoDataOutput) {
            [(AVCaptureSession*)m_captureSession removeOutput:(AVCaptureVideoDataOutput*)m_videoDataOutput];
            [(AVCaptureVideoDataOutput*)m_videoDataOutput release];
            m_videoDataOutput = nullptr;
        }
        if (m_delegate) {
            [(AkVCamMacOSCaptureDelegate*)m_delegate release];
            m_delegate = nullptr;
        }
        [(AVCaptureSession*)m_captureSession release];
        m_captureSession = nullptr;
    }
}

std::vector<PhysicalCameraInfo> MacOSCaptureDevice::enumerateDevices() {
    std::vector<PhysicalCameraInfo> devicesList;
    @autoreleasepool {
        NSArray<AVCaptureDevice *> *videoDevices;
        if (@available(macOS 10.15, *)) {
            AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession
                discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeExternalUnknown]
                                      mediaType:AVMediaTypeVideo
                                       position:AVCaptureDevicePositionUnspecified];
            videoDevices = discoverySession.devices;
        } else {
            videoDevices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
        }

        for (AVCaptureDevice *device in videoDevices) {
            PhysicalCameraInfo info;
            info.deviceId = std::string([device.uniqueID UTF8String]);
            info.friendlyName = std::string([device.localizedName UTF8String]);
            info.description = std::string([[NSString stringWithFormat:@"%@ (%@)", device.localizedName, device.modelID] UTF8String]);
            devicesList.push_back(info);
            AkLogDebug() << "Found device: " << info.friendlyName.c_str() << " (ID: " << info.deviceId.c_str() << ")" << std::endl;
        }
    }
    return devicesList;
}

bool MacOSCaptureDevice::open(const std::string& deviceId, FrameCallback callback, const VideoFormat* preferredFormat) {
    if (m_isOpened) close();

    m_deviceId = deviceId;
    m_frameCallback = callback;

    @autoreleasepool {
        AVCaptureDevice *captureDevice = [AVCaptureDevice deviceWithUniqueID:[NSString stringWithUTF8String:deviceId.c_str()]];
        if (!captureDevice) {
            AkLogError() << "Failed to find AVCaptureDevice with uniqueID: " << deviceId.c_str() << std::endl;
            return false;
        }

        NSError *error = nil;
        AVCaptureDeviceInput *deviceInput = [AVCaptureDeviceInput deviceInputWithDevice:captureDevice error:&error];
        if (error || !deviceInput) {
            AkLogError() << "Failed to create AVCaptureDeviceInput: " << (error ? [[error localizedDescription] UTF8String] : "Unknown error") << std::endl;
            return false;
        }
        m_deviceInput = [deviceInput retain];

        m_captureSession = [[AVCaptureSession alloc] init];
        if (![(AVCaptureSession*)m_captureSession canAddInput:(AVCaptureDeviceInput*)m_deviceInput]) {
            AkLogError() << "Cannot add input to capture session." << std::endl;
            releaseSession();
            return false;
        }
        [(AVCaptureSession*)m_captureSession addInput:(AVCaptureDeviceInput*)m_deviceInput];

        m_videoDataOutput = [[AVCaptureVideoDataOutput alloc] init];

        // Configure video settings - try to set a common uncompressed format
        // kCVPixelFormatType_32BGRA is common and easy to work with
        // kCVPixelFormatType_422YpCbCr8 for YUY2
        FourCC targetFourCC = PixelFormatBGR32; // Default to BGRA as it's common on macOS
        OSType targetPixelFormat = kCVPixelFormatType_32BGRA;

        if (preferredFormat) {
            // Attempt to map preferredFormat->fourcc() to an OSType
            // This mapping needs to be robust
            if (preferredFormat->fourcc() == PixelFormatRGB24) targetPixelFormat = kCVPixelFormatType_24RGB;
            else if (preferredFormat->fourcc() == PixelFormatYUY2) targetPixelFormat = kCVPixelFormatType_422YpCbCr8; // 'yuvs'
            else if (preferredFormat->fourcc() == PixelFormatNV12) targetPixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
            // else keep default BGRA
            targetFourCC = preferredFormat->fourcc();
        }

        NSDictionary *videoSettings = @{
            (NSString *)kCVPixelBufferPixelFormatTypeKey : @(targetPixelFormat)
            // Optionally add kCVPixelBufferWidthKey, kCVPixelBufferHeightKey if specific resolution needed
            // However, AVFoundation usually picks a resolution. We get it from the sample buffer.
        };
        ((AVCaptureVideoDataOutput*)m_videoDataOutput).videoSettings = videoSettings;
        ((AVCaptureVideoDataOutput*)m_videoDataOutput).alwaysDiscardsLateVideoFrames = YES;


        m_delegate = [[AkVCamMacOSCaptureDelegate alloc] initWithOwner:this deviceId:m_deviceId];
        [(AVCaptureVideoDataOutput*)m_videoDataOutput setSampleBufferDelegate:(AkVCamMacOSCaptureDelegate*)m_delegate queue:(dispatch_queue_t)m_sampleCallbackQueue];

        if (![(AVCaptureSession*)m_captureSession canAddOutput:(AVCaptureVideoDataOutput*)m_videoDataOutput]) {
            AkLogError() << "Cannot add output to capture session." << std::endl;
            releaseSession();
            return false;
        }
        [(AVCaptureSession*)m_captureSession addOutput:(AVCaptureVideoDataOutput*)m_videoDataOutput];

        // Store an initial format. This will be updated by the first frame.
        // Width/Height from preferredFormat if available, otherwise 0.
        // This is a bit tricky as AVF might select a different resolution.
        // The true format is known once frames arrive.
        m_currentFormat = VideoFormat(targetFourCC,
                                      preferredFormat ? preferredFormat->width() : 0,
                                      preferredFormat ? preferredFormat->height() : 0, {});


        // It's good practice to set sessionPreset if controlling resolution,
        // but for just getting frames, often not strictly needed if output settings are specific.
        // Example: [m_captureSession setSessionPreset:AVCaptureSessionPreset640x480];
        // This should be done *before* adding inputs/outputs if it's to influence format selection heavily.
        // For now, we rely on the videoDataOutput settings and what the camera provides.

    } // @autoreleasepool

    m_isOpened = true;
    AkLogInfo() << "MacOS device " << m_deviceId.c_str() << " opened successfully." << std::endl;
    return true;
}

bool MacOSCaptureDevice::start() {
    if (!m_isOpened || m_isCapturing || !m_captureSession) {
         AkLogError() << "Cannot start capture. Device not opened or already capturing." << std::endl;
        return false;
    }
    @autoreleasepool {
        [(AVCaptureSession*)m_captureSession startRunning];
    }
    m_isCapturing = true;
    AkLogInfo() << "MacOS capture started for device " << m_deviceId.c_str() << std::endl;
    return true;
}

void MacOSCaptureDevice::stop() {
    if (!m_isOpened || !m_isCapturing || !m_captureSession) {
        return;
    }
    @autoreleasepool {
        [(AVCaptureSession*)m_captureSession stopRunning];
    }
    m_isCapturing = false;
    AkLogInfo() << "MacOS capture stopped for device " << m_deviceId.c_str() << std::endl;
}

void MacOSCaptureDevice::close() {
    stop();
    @autoreleasepool {
        releaseSession();
    }
    m_isOpened = false;
    m_frameCallback = nullptr;
    m_deviceId.clear();
    m_currentFormat = VideoFormat();
    AkLogInfo() << "MacOS device closed." << std::endl;
}

bool MacOSCaptureDevice::isCapturing() const {
    if (!m_captureSession || !m_isOpened) return false;
    bool isRunning = false;
    @autoreleasepool {
      isRunning = [(AVCaptureSession*)m_captureSession isRunning];
    }
    return isRunning;
}

VideoFormat MacOSCaptureDevice::getCurrentFormat() const {
    // m_currentFormat should be updated by processFrame based on actual frame data
    return m_currentFormat;
}

void MacOSCaptureDevice::processFrame(const void* frameData, size_t frameSize, FourCC pixelFormat, int width, int height, size_t bytesPerRow) {
    // This method is called by AkVCamMacOSCaptureDelegate on AVFoundation's queue

    // Update current format if it changed (e.g. first frame or dynamic change)
    // Note: bytesPerRow might be different from width * bpp due to padding.
    // VideoFrame expects tightly packed data for some formats.
    if (m_currentFormat.fourcc() != pixelFormat || m_currentFormat.width() != width || m_currentFormat.height() != height) {
        AkLogInfo() << "MacOSCaptureDevice: Format changed/detected for " << m_deviceId.c_str()
                    << ": " << VideoFormat::stringFromFourcc(pixelFormat).c_str()
                    << " " << width << "x" << height << std::endl;
        m_currentFormat = VideoFormat(pixelFormat, width, height, {{30,1}}); // Assuming 30fps for now
    }

    if (m_frameCallback) {
        VideoFrame akFrame(m_currentFormat); // Uses the potentially updated m_currentFormat

        // Handle potential stride differences.
        // If akFrame.data().size() is based on width * height * bpp, and bytesPerRow is larger,
        // we need to copy row by row or ensure VideoFrame can handle stride.
        // For now, assume VideoFrame expects packed data or its size matches frameSize.

        size_t expectedPackedSize = akFrame.size(); // Size based on VideoFormat definition

        if (akFrame.data().empty()) {
             AkLogError() << "ProcessFrame: AkFrame data is empty (invalid format?)" << m_deviceId.c_str() << std::endl;
             return;
        }

        if (bytesPerRow == static_cast<size_t>(m_currentFormat.width() * (m_currentFormat.bpp()/8)) && frameSize >= expectedPackedSize ) {
            // If stride matches width * bpp, and total frameSize is sufficient, direct copy is fine.
             if (expectedPackedSize > frameSize) { // Should not happen if check above is good
                AkLogWarning() << "ProcessFrame: expected packed size " << expectedPackedSize << " > frame buffer size " << frameSize << m_deviceId.c_str() << std::endl;
                memcpy(akFrame.data().data(), frameData, frameSize); // Copy what we can
            } else {
                memcpy(akFrame.data().data(), frameData, expectedPackedSize);
            }
        } else if (frameSize >= expectedPackedSize && m_currentFormat.planes() == 1) {
            // Stride might be different, copy row by row for single-plane formats
            AkLogDebug() << "ProcessFrame: Copying row by row due to stride mismatch or larger buffer. BytesPerRow: " << bytesPerRow
                         << ", Expected row size: " << (m_currentFormat.width() * (m_currentFormat.bpp()/8)) << std::endl;
            uint8_t* dst = akFrame.data().data();
            const uint8_t* src = static_cast<const uint8_t*>(frameData);
            size_t copyWidthBytes = m_currentFormat.width() * (m_currentFormat.bpp() / 8);
            if (copyWidthBytes > bytesPerRow) { // Should not happen if width * bpp is correct
                 AkLogWarning() << "ProcessFrame: copyWidthBytes " << copyWidthBytes << " > bytesPerRow " << bytesPerRow << ". Clamping." << std::endl;
                 copyWidthBytes = bytesPerRow;
            }

            for (int y = 0; y < m_currentFormat.height(); ++y) {
                memcpy(dst, src, copyWidthBytes);
                dst += copyWidthBytes; // Assuming VideoFrame is tightly packed
                src += bytesPerRow;
            }
        } else {
            // Simpler copy if frameSize is what VideoFrame expects, or if it's planar (more complex)
            // This part needs to be more robust for planar formats or when frameSize != expectedPackedSize
            if (akFrame.data().size() >= frameSize) {
                 AkLogDebug() << "ProcessFrame: frameSize " << frameSize << ", akFrame.data().size() " << akFrame.data().size() << ". Direct copy of frameSize." << m_deviceId.c_str() << std::endl;
                memcpy(akFrame.data().data(), frameData, frameSize);
            } else {
                 AkLogWarning() << "ProcessFrame: akFrame data size " << akFrame.data().size() << " < frame buffer size " << frameSize
                               << ". Truncating copy for " << m_deviceId.c_str() << std::endl;
                memcpy(akFrame.data().data(), frameData, akFrame.data().size());
            }
        }
        m_frameCallback(akFrame, m_deviceId);
    }
}


} // namespace AkVCam

#endif // __APPLE__
