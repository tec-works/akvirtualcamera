# akvirtualcamera, virtual camera for Mac and Windows 

akvirtualcamera is virtual camera implemented as a DirectShow filter in Windows, and as a CoreMediaIO plugin in Mac.

## Features

* Supports emulated camera controls in capture devices (brightness, contrast, saturation, etc.).
* Configurable default picture in case no input signal available.

## Build and Install

Visit the [wiki](https://github.com/webcamoid/akvirtualcamera/wiki) for a comprehensive compile and install instructions.

## Downloads ##

[![Download](https://img.shields.io/badge/Download-Releases-3f2a7e.svg)](https://github.com/webcamoid/akvirtualcamera/releases)
[![Daily Build](https://img.shields.io/badge/Download-Daily%20Build-3f2a7e.svg)](https://github.com/webcamoid/akvirtualcamera/releases/tag/daily-build)
[![Total Downloads](https://img.shields.io/github/downloads/webcamoid/akvirtualcamera/total.svg?label=Total%20Downloads&color=3f2a7e)](https://tooomm.github.io/github-release-stats/?username=webcamoid&repository=akvirtualcamera)

## Donations ##

If you are interested in donating to the project you can look at all available methods in the [donations page](https://webcamoid.github.io/donations).

## Status

[![Linux MinGW](https://github.com/webcamoid/akvirtualcamera/actions/workflows/linux-mingw.yml/badge.svg)](https://github.com/webcamoid/akvirtualcamera/actions/workflows/linux-mingw.yml)
[![Mac](https://github.com/webcamoid/akvirtualcamera/actions/workflows/mac.yml/badge.svg)](https://github.com/webcamoid/akvirtualcamera/actions/workflows/mac.yml)
[![Windows MSYS](https://github.com/webcamoid/akvirtualcamera/actions/workflows/windows-msys.yml/badge.svg)](https://github.com/webcamoid/akvirtualcamera/actions/workflows/windows-msys.yml)
[![Windows MSVC](https://github.com/webcamoid/akvirtualcamera/actions/workflows/windows-vs.yml/badge.svg)](https://github.com/webcamoid/akvirtualcamera/actions/workflows/windows-vs.yml)
[![Build status](https://api.cirrus-ci.com/github/webcamoid/akvirtualcamera.svg)](https://cirrus-ci.com/github/webcamoid/akvirtualcamera)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/1cee2645a3604633a506a203fb8c3161)](https://www.codacy.com/gh/webcamoid/akvirtualcamera/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=webcamoid/akvirtualcamera&amp;utm_campaign=Badge_Grade)
[![Project Stats](https://www.openhub.net/p/akvirtualcamera/widgets/project_thin_badge.gif)](https://www.openhub.net/p/akvirtualcamera)

## Usage

The first step to create a virtual camera is defining a virtual device:
```
AkVCamManager add-device "Virtual Camera"
```
The command will return a message like:
```
Device created as AkVCamVideoDevice0
```
Keep track of `AkVCamVideoDevice0` (the device ID), that will be the identifier of the virtual device and you will need it for the following operations. You can also set a custom device ID with for example:
```
AkVCamManager add-device -i FakeCamera0 "Virtual Camera"
```
This way the device will be created as `FakeCamera0` instead of using the default prefix and numeration (`AkVCamVideoDeviceNUMBER`), there is no particular rule for the custom ID, you can use any character combination at your choice.

The created device is a blank device without any defined capture formats, that meaning it won't work as-is.
Now, you must define a at least one capture format for that device, as:
```
AkVCamManager add-format AkVCamVideoDevice0 FORMAT WIDTH HEIGHT FRAME_RATE
```
For example:
```
AkVCamManager add-format AkVCamVideoDevice0 YUY2 640 480 30
```
First format defined is the default frame format.
You can list all capture formats with:
```
AkVCamManager supported-formats --output
```
The frame rate can be expressed either as an integer or a fraction, for example `15/2`.
Once you finished defining your devices, you must obligatorily apply the changes to the system with.
```
AkVCamManager update
```
**Note:** You must run an update every time you modify any parameter of the virtual camera. Also NEVER modify the virtual camera parameters when a client is running it.

### Managing the devices

List all devices:
```
AkVCamManager devices
```
You can remove a device with:
```
AkVCamManager remove-device AkVCamVideoDevice0
```
Or you can remove all devices:
```
AkVCamManager remove-devices
```
You can change the device description with:
```
AkVCamManager set-description AkVCamVideoDevice0 "My Virtual Camera"
```
Or get the device description with:
```
AkVCamManager description AkVCamVideoDevice0
```
### Webcam splitting

You can also create a virtual camera that splits a physical camera.
First, list the available physical cameras:
```
AkVCamManager list-cameras
```
Then, create the virtual camera using the `--source-camera` option:
```
AkVCamManager add-device --source-camera "My Physical Camera" "My Virtual Camera"
```

### Managing the formats

List all formats:
```
AkVCamManager formats AkVCamVideoDevice0
```
Add a new format at a given index:
```
AkVCamManager add-format --index 5 AkVCamVideoDevice0 YUY2 640 480 30
```
Remove a device format at `INDEX` with:
```
AkVCamManager remove-format AkVCamVideoDevice0 INDEX
```
Remove all device formats:
```
AkVCamManager remove-formats AkVCamVideoDevice0
```
You can get the default recommended output format with:
```
AkVCamManager default-format --output
```
### No signal picture

When a client program try to play the virtual camera but it isn't receiving any frame, it will show a random dot pattern, you can change it to show a custom picture instead with:
```
AkVCamManager set-picture /path/to/place_holder_picture.png
```
And you can get place holder picture path again with:
```
AkVCamManager picture
```
You must give a full path to a picture file, the supported formats for the picture are:
- BMP (24 bpp and 32 bpp).
- JPG
- PNG

### Loading configurations from a file

Alternatively, you can define and configure all virtual devices at once using an INI file like.
The INI file follows `QSettings` file format.
Once you have finished writing your settings file, you can pass it to the manager as:
```
AkVCamManager load settings.ini
```
You don't need to call `update` because `load` will automatically call it for you.
Before continue, let make it clear that the order of the sections (those enclosed between `[ ]`) or it's fields (those lines in the form of `key = value`) does not matters, we are just putting it in natural order to make it easier to understand.

#### Defining the devices

Following is the code for defining the devices:
```ini
[Cameras]
cameras/size = 2

cameras/1/description = My Virtual Camera
cameras/1/formats = 1
cameras/1/id = MyFakeCamera0

cameras/2/description = My Other Virtual Camera
cameras/2/formats = 1, 2
```
First at all you must create a `[Cameras]` section and set the number of webcams that will be defined with `cameras/size`, cameras indexes starts with 1. Each camera has the following properties:

- **description:** The description that will shown to the capture or streaming program.
- **formats:** is a comma separated list of index of formats supported by the device, we will talk about this in a moment.
- **id:** Set a custom device ID. This property is optional.

#### Defining the formats

Next step is defining the formats:
```ini
[Formats]
formats/size = 2

formats/1/format = YUY2
formats/1/width = 640
formats/1/height = 480
formats/1/fps = 30

formats/2/format = RGB24, YUY2
formats/2/width = 640
formats/2/height = 480
formats/2/fps = 20/1, 15/2
```
`format` can be set to any pixel format we been talk before, then set the frame resolution with `width` and `height`, `fps` sets the frame rate and it can be either a positive integer number or a positive fraction in the form `numerator/denominator`.

You may be noted that the second format has comma separated values, this is because you can actually define many formats with similar properties at once with just a minimal number of lines. The driver will combine the values, so for format 2 in this example you get the following formats:
- RGB24 640x480 20 FPS
- RGB24 640x480 7.5 FPS
- YUY2 640x480 20 FPS
- YUY2 640x480 7.5 FPS

If you want a good compatibility with many capture programs you must provide at least the `YUY2 640x480` format in your devices.

#### No signal picture

You must add the following lines to the settings file for setting the place holder picture:
```ini
[General]
default_frame = /path/to/place_holder_picture.png
```

## Reporting Bugs

Report all issues in the [issues tracker](https://github.com/webcamoid/akvirtualcamera/issues).
