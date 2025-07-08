#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#include <vector>
#include <string>

std::vector<std::string> listObjCCameras() {
    std::vector<std::string> cameraNames;
    NSArray<AVCaptureDevice *> *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
    if (devices.count == 0) {
        return cameraNames;
    }
    for (AVCaptureDevice *device in devices) {
        cameraNames.push_back(std::string([device.localizedName UTF8String]));
    }
    return cameraNames;
}
