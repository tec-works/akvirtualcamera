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

#import <AVFoundation/AVFoundation.h> // Import AVFoundation
#include "device.h"
#include "PlatformUtils/src/preferences.h" // For Preferences
#include "PlatformUtils/src/utils.h"
#include "VCamUtils/src/logger.h"
#include "VCamUtils/src/videoframe.h" // For VideoFrame

// Helper to convert CMSampleBuffer to AkVCam::VideoFrame
// This is a placeholder and needs careful implementation regarding pixel formats and memory management.
AkVCam::VideoFrame VideoFrameFromCMSampleBuffer(CMSampleBufferRef sampleBuffer) {
    AkVCam::VideoFrame vframe;
    if (!sampleBuffer || !CMSampleBufferIsValid(sampleBuffer)) {
        return vframe;
    }

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) {
        return vframe;
    }

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    int width = CVPixelBufferGetWidth(imageBuffer);
    int height = CVPixelBufferGetHeight(imageBuffer);
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(imageBuffer);
    // CMIO FourCC to AkVCam::PixelFormat conversion needed here
    // Assuming kCVPixelFormatType_32ARGB for now -> PixelFormatRGB32
    // This needs to be robust.
    AkVCam::PixelFormat akPixelFormat = AkVCam::PixelFormatUnknown;
    switch (pixelFormat) {
        case kCVPixelFormatType_32ARGB: akPixelFormat = AkVCam::PixelFormatRGB32; break; // Alpha first
        case kCVPixelFormatType_32BGRA: akPixelFormat = AkVCam::PixelFormatRGB32; break; // Blue first, often native for CV
        case kCVPixelFormatType_24RGB: akPixelFormat = AkVCam::PixelFormatRGB24; break;
        // TODO: Add more mappings, e.g., YUV formats like kCVPixelFormatType_422YpCbCr8 (UYVY/YUY2)
        // For YUV, bytesPerRow might not be width * Bpp. Need to check CVPixelBufferGetPlaneCount.
        default:
            AkLogError() << "Unsupported CVPixelFormatType: " << pixelFormat << " (ensure this is a printable FourCC or handle logging appropriately)";
            CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            return vframe;
    }

    vframe.format() = AkVCam::VideoFormat(akPixelFormat, width, height);

    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    uint8_t* baseAddress = (uint8_t*)CVPixelBufferGetBaseAddress(imageBuffer);

    // Simple copy assuming contiguous data for formats like ARGB/RGB24.
    // For planar formats (YUV), this would be more complex.
    if (CVPixelBufferIsPlanar(imageBuffer)) {
         AkLogError() << "Planar pixel buffer format from physical camera not yet supported for direct copy.";
         // For planar, you'd copy each plane. For now, we'll fail.
         CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
         return AkVCam::VideoFrame();
    }

    size_t dataSize = height * bytesPerRow; // This might be larger than vframe.format().size() due to padding
    if (vframe.data().size() < dataSize) { // Ensure our buffer is large enough
        vframe.data().resize(dataSize); // Or resize to vframe.format().size() if handling stride separately
    }
    memcpy(vframe.data().data(), baseAddress, dataSize); // Or copy row by row if strides differ and format().size() is target

    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
    return vframe;
}


// Define the delegate interface if not already part of the class in Obj-C way
@interface AkVCamDeviceCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
    AkVCam::Device* _deviceImpl; // Pointer to the C++ Device implementation
}
- (instancetype)initWithDevice:(AkVCam::Device*)device;
@end

@implementation AkVCamDeviceCaptureDelegate
- (instancetype)initWithDevice:(AkVCam::Device*)device {
    self = [super init];
    if (self) {
        _deviceImpl = device;
    }
    return self;
}

- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    if (_deviceImpl) {
        // Convert CMSampleBufferRef to AkVCam::VideoFrame
        AkVCam::VideoFrame frame = VideoFrameFromCMSampleBuffer(sampleBuffer);
        if (frame.format().isValid()) {
            // AkLogInfo() << "Delegate: Frame received from physical camera";
            _deviceImpl->frameReady(frame); // Call the C++ method
        } else {
            // AkLogError() << "Delegate: Failed to convert CMSampleBuffer to VideoFrame";
        }
    }
}
@end


AkVCam::Device::Device(CMIOHardwarePlugInRef pluginInterface,
                       bool registerObject):
    AkVCam::Object(pluginInterface),
    m_captureSession(nullptr),
    m_physicalCaptureDevice(nullptr),
    m_videoDataOutput(nullptr),
    m_captureSessionQueue(nullptr),
    m_isSourcingFromPhysical(false)
{
    this->m_className = "Device";
    this->m_classID = kCMIODeviceClassID;

    if (registerObject) {
        this->createObject();
        // setDeviceId should be called before registerObject if ID depends on it
        // For now, assuming m_deviceId is set by PluginInterface after creation
        // and before physical camera init.
        this->registerObject();
    }
}

AkVCam::Device::~Device()
{
    ReleasePhysicalCameraCapture();
    this->registerStreams(false);
    this->registerObject(false);
    if (m_captureSessionQueue) {
        dispatch_release(m_captureSessionQueue);
        m_captureSessionQueue = nullptr;
    }
}

void AkVCam::Device::InitializePhysicalCameraCapture() {
    AkLogFunction();
    if (m_sourceCameraUniqueID.empty()) {
        AkLogInfo() << "No source camera unique ID specified.";
        return;
    }

    @autoreleasepool {
        NSString* uniqueID = [NSString stringWithUTF8String:m_sourceCameraUniqueID.c_str()];
        m_physicalCaptureDevice = (AVCaptureDevice*)[AVCaptureDevice deviceWithUniqueID:uniqueID];

        if (!m_physicalCaptureDevice) {
            AkLogError() << "Could not find physical AVCaptureDevice with ID: " << m_sourceCameraUniqueID;
            return;
        }
        [(AVCaptureDevice*)m_physicalCaptureDevice retain];
        AkLogInfo() << "Found physical AVCaptureDevice: " << [[(AVCaptureDevice*)m_physicalCaptureDevice localizedName] UTF8String];

        m_captureSession = [[AVCaptureSession alloc] init];
        if (!m_captureSession) {
            AkLogError() << "Failed to create AVCaptureSession.";
            [(AVCaptureDevice*)m_physicalCaptureDevice release];
            m_physicalCaptureDevice = nullptr;
            return;
        }

        NSError* error = nil;
        AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:(AVCaptureDevice*)m_physicalCaptureDevice error:&error];
        if (error || !input) {
            AkLogError() << "Error creating AVCaptureDeviceInput: " << [[error localizedDescription] UTF8String];
            [(AVCaptureSession*)m_captureSession release];
            m_captureSession = nullptr;
            [(AVCaptureDevice*)m_physicalCaptureDevice release];
            m_physicalCaptureDevice = nullptr;
            return;
        }

        if ([(AVCaptureSession*)m_captureSession canAddInput:input]) {
            [(AVCaptureSession*)m_captureSession addInput:input];
        } else {
            AkLogError() << "Cannot add input to AVCaptureSession.";
            // Release input, session, device
            // [input release]; // Not needed with ARC if it were an obj-c var
            [(AVCaptureSession*)m_captureSession release];
            m_captureSession = nullptr;
            [(AVCaptureDevice*)m_physicalCaptureDevice release];
            m_physicalCaptureDevice = nullptr;
            return;
        }

        m_videoDataOutput = [[AVCaptureVideoDataOutput alloc] init];
        if (!m_videoDataOutput) {
             AkLogError() << "Failed to create AVCaptureVideoDataOutput.";
            // TODO: cleanup session, input, device
            return;
        }

        // Create a dispatch queue for serial execution of sample buffer delegate method
        m_captureSessionQueue = dispatch_queue_create("akvirtualcamera.physicalDeviceCaptureQueue", DISPATCH_QUEUE_SERIAL);

        AkVCamDeviceCaptureDelegate* delegate = [[AkVCamDeviceCaptureDelegate alloc] initWithDevice:this];
        // The delegate needs to be retained by the AVCaptureVideoDataOutput.
        // In manual reference counting, we would not release it here.
        // With ARC, it's managed. Since this is .mm with C++, need to be careful.
        // Let's assume for now it's retained by setSampleBufferDelegate.
        [(AVCaptureVideoDataOutput*)m_videoDataOutput setSampleBufferDelegate:(id<AVCaptureVideoDataOutputSampleBufferDelegate>)delegate queue:m_captureSessionQueue];
        [delegate release]; // Release our ownership, output should retain it.


        // Specify pixel format. Common ones are kCVPixelFormatType_32BGRA or YUV formats.
        // This might need to match what our virtual streams can best handle or convert from.
        // NSDictionary *videoSettings = @{ (NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
        // ((AVCaptureVideoDataOutput*)m_videoDataOutput).videoSettings = videoSettings;
        // For now, let default settings be used or set a common one like 32BGRA
         ((AVCaptureVideoDataOutput*)m_videoDataOutput).videoSettings = [NSDictionary dictionaryWithObject:[NSNumber numberWithInt:kCVPixelFormatType_32BGRA] forKey:(id)kCVPixelBufferPixelFormatTypeKey];


        if ([(AVCaptureSession*)m_captureSession canAddOutput:(AVCaptureVideoDataOutput*)m_videoDataOutput]) {
            [(AVCaptureSession*)m_captureSession addOutput:(AVCaptureVideoDataOutput*)m_videoDataOutput];
        } else {
            AkLogError() << "Cannot add output to AVCaptureSession.";
            // TODO: cleanup output, session, input, device
            return;
        }

        [(AVCaptureSession*)m_captureSession startRunning];
        m_isSourcingFromPhysical = true;
        AkLogInfo() << "AVCaptureSession started for physical camera.";
    } // @autoreleasepool
}

void AkVCam::Device::ReleasePhysicalCameraCapture() {
    AkLogFunction();
    @autoreleasepool {
        if (m_captureSession) {
            if ([(AVCaptureSession*)m_captureSession isRunning]) {
                [(AVCaptureSession*)m_captureSession stopRunning];
            }
            // Remove inputs and outputs before releasing session
            NSArray* inputs = [[(AVCaptureSession*)m_captureSession inputs] copy];
            for (AVCaptureInput* input in inputs) {
                [(AVCaptureSession*)m_captureSession removeInput:input];
            }
            [inputs release];

            NSArray* outputs = [[(AVCaptureSession*)m_captureSession outputs] copy];
            for (AVCaptureOutput* output in outputs) {
                 if (output == m_videoDataOutput) { // Clear delegate before removing
                    [(AVCaptureVideoDataOutput*)m_videoDataOutput setSampleBufferDelegate:nil queue:nil];
                }
                [(AVCaptureSession*)m_captureSession removeOutput:output];
            }
            [outputs release];


            [(AVCaptureSession*)m_captureSession release];
            m_captureSession = nullptr;
        }
        if (m_videoDataOutput) {
            // Delegate should be nilled above
            [(AVCaptureVideoDataOutput*)m_videoDataOutput release];
            m_videoDataOutput = nullptr;
        }
        if (m_physicalCaptureDevice) {
            [(AVCaptureDevice*)m_physicalCaptureDevice release];
            m_physicalCaptureDevice = nullptr;
        }
        // m_captureSessionQueue is released in destructor
        m_isSourcingFromPhysical = false;
        AkLogInfo() << "Physical camera capture released.";
    }
}


OSStatus AkVCam::Device::createObject()
{
    AkLogFunction();

    if (!this->m_pluginInterface
        || !*this->m_pluginInterface)
        return kCMIOHardwareUnspecifiedError;

    CMIOObjectID deviceID = 0;

    auto status =
            CMIOObjectCreate(this->m_pluginInterface,
                             kCMIOObjectSystemObject,
                             this->m_classID,
                             &deviceID);

    if (status == kCMIOHardwareNoError) {
        this->m_isCreated = true;
        this->m_objectID = deviceID;
        AkLogInfo() << "Created device: " << this->m_objectID << std::endl;
    }

    return status;
}

OSStatus AkVCam::Device::registerObject(bool regist)
{
    AkLogFunction();
    AkLogDebug() << "Register: " << regist << std::endl;
    OSStatus status = kCMIOHardwareUnspecifiedError;

    if (!this->m_isCreated
        || !this->m_pluginInterface
        || !*this->m_pluginInterface)
        return status;

    if (regist) {
        status = CMIOObjectsPublishedAndDied(this->m_pluginInterface,
                                             kCMIOObjectSystemObject,
                                             1,
                                             &this->m_objectID,
                                             0,
                                             nullptr);
    } else {
        status = CMIOObjectsPublishedAndDied(this->m_pluginInterface,
                                             kCMIOObjectSystemObject,
                                             0,
                                             nullptr,
                                             1,
                                             &this->m_objectID);
    }

    if (status == kCMIOHardwareNoError)
        AkLogDebug() << "Ok";
    else
        AkLogDebug() << "Error registering device" << std::endl;

    return status;
}

AkVCam::StreamPtr AkVCam::Device::addStream()
{
    AkLogFunction();
    auto stream = StreamPtr(new Stream(false, this));

    if (stream->createObject() == kCMIOHardwareNoError) {
        this->m_streams[stream->objectID()] = stream;
        this->updateStreamsProperty();

        return stream;
    }

    return StreamPtr();
}

std::list<AkVCam::StreamPtr> AkVCam::Device::addStreams(int n)
{
    AkLogFunction();
    std::list<StreamPtr> streams;

    for (int i = 0; i < n; i++) {
        auto stream = StreamPtr(new Stream(false, this));

        if (stream->createObject() != kCMIOHardwareNoError)
            return std::list<StreamPtr>();

        streams.push_back(stream);
    }

    for (auto &stream: streams) {
        this->m_streams[stream->objectID()] = stream;
        this->updateStreamsProperty();
    }

    return streams;
}

OSStatus AkVCam::Device::registerStreams(bool regist)
{
    AkLogFunction();
    AkLogDebug() << "Register: " << regist << std::endl;
    OSStatus status = kCMIOHardwareUnspecifiedError;

    if (!this->m_isCreated
        || !this->m_pluginInterface
        || !*this->m_pluginInterface
        || this->m_streams.empty())
        return status;

    std::vector<CMIOObjectID> streams;

    for (auto &stream: this->m_streams)
        streams.push_back(stream.first);

    if (regist) {
        status = CMIOObjectsPublishedAndDied(this->m_pluginInterface,
                                             kCMIOObjectSystemObject,
                                             UInt32(streams.size()),
                                             streams.data(),
                                             0,
                                             nullptr);
    } else {
        status = CMIOObjectsPublishedAndDied(this->m_pluginInterface,
                                             kCMIOObjectSystemObject,
                                             0,
                                             nullptr,
                                             UInt32(streams.size()),
                                             streams.data());
    }

    if (status == kCMIOHardwareNoError)
        AkLogDebug() << "Ok";
    else
        AkLogDebug() << "Error registering streams" << std::endl;

    return status;
}

std::string AkVCam::Device::deviceId() const
{
    return this->m_deviceId;
}

void AkVCam::Device::setDeviceId(const std::string &deviceId)
{
    this->m_deviceId = deviceId;
    AkLogInfo() << "Device::setDeviceId: " << deviceId << std::endl;

    // After deviceId is set, check for sourceCamera and initialize
    if (!m_deviceId.empty()) {
        int cameraIndex = Preferences::cameraFromId(m_deviceId);
        if (cameraIndex >= 0) {
            m_sourceCameraUniqueID = Preferences::cameraCustomValue(static_cast<size_t>(cameraIndex), "sourceCamera");
            AkLogInfo() << "Source camera for " << m_deviceId << " (from prefs): " << m_sourceCameraUniqueID << std::endl;
            if (!m_sourceCameraUniqueID.empty()) {
                InitializePhysicalCameraCapture();
            }
        } else {
            AkLogError() << "Could not find camera index for deviceId: " << m_deviceId;
        }
    }
}

void AkVCam::Device::stopStreams()
{
    for (auto &stream: this->m_streams)
        stream.second->stop();
}

void AkVCam::Device::serverStateChanged(IpcBridge::ServerState state)
{
    for (auto &stream: this->m_streams)
        stream.second->serverStateChanged(state);
}

void AkVCam::Device::frameReady(const AkVCam::VideoFrame &frame)
{
    for (auto &stream: this->m_streams)
        stream.second->frameReady(frame);
}

void AkVCam::Device::setPicture(const std::string &picture)
{
    for (auto &stream: this->m_streams)
        stream.second->setPicture(picture);
}

void AkVCam::Device::setBroadcasting(const std::string &broadcaster)
{
    for (auto &stream: this->m_streams)
        stream.second->setBroadcasting(broadcaster);
}

void AkVCam::Device::setHorizontalMirror(bool horizontalMirror)
{
    for (auto &stream: this->m_streams)
        stream.second->setHorizontalMirror(horizontalMirror);
}

void AkVCam::Device::setVerticalMirror(bool verticalMirror)
{
    for (auto &stream: this->m_streams)
        stream.second->setVerticalMirror(verticalMirror);
}

void AkVCam::Device::setScaling(Scaling scaling)
{
    for (auto &stream: this->m_streams)
        stream.second->setScaling(scaling);
}

void AkVCam::Device::setAspectRatio(AspectRatio aspectRatio)
{
    for (auto &stream: this->m_streams)
        stream.second->setAspectRatio(aspectRatio);
}

void AkVCam::Device::setSwapRgb(bool swap)
{
    for (auto &stream: this->m_streams)
        stream.second->setSwapRgb(swap);
}

OSStatus AkVCam::Device::suspend()
{
    AkLogFunction();
    AkLogDebug() << "STUB" << std::endl;

    return kCMIOHardwareUnspecifiedError;
}

OSStatus AkVCam::Device::resume()
{
    AkLogFunction();
    AkLogDebug() << "STUB" << std::endl;

    return kCMIOHardwareUnspecifiedError;
}

OSStatus AkVCam::Device::startStream(CMIOStreamID stream)
{
    AkLogFunction();

    UInt32 isRunning = 0;
    this->m_properties.getProperty(kCMIODevicePropertyDeviceIsRunning,
                                   &isRunning);

    if (isRunning)
        return kCMIOHardwareUnspecifiedError;

    if (!this->m_streams.count(stream))
        return kCMIOHardwareNotRunningError;

    if (!this->m_streams[stream]->start())
        return kCMIOHardwareNotRunningError;

    bool deviceRunning = true;

    for (auto &stream: this->m_streams)
        deviceRunning &= stream.second->running();

    if (deviceRunning) {
        this->m_properties.setProperty(kCMIODevicePropertyDeviceIsRunning,
                                       UInt32(1));
        auto address = this->address(kCMIODevicePropertyDeviceIsRunning);
        this->propertyChanged(1, &address);
    }

    AKVCAM_EMIT(this, AddListener, this->m_deviceId)

    return kCMIOHardwareNoError;
}

OSStatus AkVCam::Device::stopStream(CMIOStreamID stream)
{
    AkLogFunction();

    UInt32 isRunning = 0;
    this->m_properties.getProperty(kCMIODevicePropertyDeviceIsRunning,
                                   &isRunning);

    if (!isRunning)
        return kCMIOHardwareNotRunningError;

    if (!this->m_streams.count(stream))
        return kCMIOHardwareNotRunningError;

    this->m_streams[stream]->stop();
    bool deviceRunning = false;

    for (auto &stream: this->m_streams)
        deviceRunning |= stream.second->running();

    if (!deviceRunning) {
        this->m_properties.setProperty(kCMIODevicePropertyDeviceIsRunning,
                                       UInt32(0));
        auto address = this->address(kCMIODevicePropertyDeviceIsRunning);
        this->propertyChanged(1, &address);
    }

    AKVCAM_EMIT(this, RemoveListener, this->m_deviceId)

    return kCMIOHardwareNoError;
}

OSStatus AkVCam::Device::processAVCCommand(CMIODeviceAVCCommand *ioAVCCommand)
{
    UNUSED(ioAVCCommand);
    AkLogFunction();
    AkLogDebug() << "STUB" << std::endl;

    return kCMIOHardwareUnspecifiedError;
}

OSStatus AkVCam::Device::processRS422Command(CMIODeviceRS422Command *ioRS422Command)
{
    UNUSED(ioRS422Command);
    AkLogFunction();
    AkLogDebug() << "STUB" << std::endl;

    return kCMIOHardwareUnspecifiedError;
}

void AkVCam::Device::updateStreamsProperty()
{
    AkLogFunction();
    std::vector<ObjectPtr> streams;

    for (auto &stream: this->m_streams)
        streams.push_back(stream.second);

    this->m_properties.setProperty(kCMIODevicePropertyStreams, streams);
}
