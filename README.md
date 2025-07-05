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

## Reporting Bugs

Report all issues in the [issues tracker](https://github.com/webcamoid/akvirtualcamera/issues).

## Command-Line Usage (AkVCamManager)

The `AkVCamManager` executable (or its platform-specific equivalent, e.g., `AkVCamManager.exe` on Windows) is used to manage virtual cameras, including the webcam splitting feature.

### Listing Available Cameras

To see a list of available physical cameras and currently configured virtual cameras, you can run the `devices` command:

```bash
./AkVCamManager devices
```

If you only need information about physical cameras, you can use the `list-physical-cameras` command:

```bash
./AkVCamManager list-physical-cameras
```
This command provides a list of all detected physical cameras, including their friendly name, unique device ID, and description. The output is presented in a table format. You can also get this output in a parseable tab-separated format using the `-p` flag:
```bash
./AkVCamManager list-physical-cameras -p
```

Simply running the manager without any commands will also typically list physical cameras by default:
```bash
./AkVCamManager
```
The `deviceId` obtained from these commands is crucial for configuring webcam splitting or other specific device operations.

### Webcam Splitting: One Physical Camera to Multiple Virtual Cameras

The primary way to configure webcam splitting is by using a configuration INI file with the `load` command. This allows you to define multiple virtual cameras that all source their video from a single physical camera.

**Command:**

```bash
./AkVCamManager load /path/to/your/config.ini
```

**INI File Configuration for Splitting:**

In your `config.ini` file, you define your virtual cameras under the `[Cameras]` section. The key to splitting is the `source_camera_id` property for each virtual camera.

1.  **Define the number of virtual cameras:**
    `cameras/size = N` (where N is the number of virtual cameras)

2.  **Configure each virtual camera:**
    For each virtual camera `X` (from 1 to N):
    *   `cameras/X/description = Your Virtual Camera Name X`
    *   `cameras/X/formats = <format_indices>` (e.g., `1` or `1,2` referencing formats defined in `[Formats]`)
    *   `cameras/X/source_camera_id = <your_physical_camera_id>`

    Set `<your_physical_camera_id>` to the actual ID of the physical webcam you want to split. You can find this ID using the `./AkVCamManager devices` command or by running `./AkVCamManager` without arguments. All virtual cameras that should display the feed from this physical camera must use the *same* `source_camera_id`.

**Example `config.ini` for splitting one physical camera to two virtual cameras:**

```ini
[Cameras]
# Define 2 virtual cameras
cameras/size = 2

# Virtual Camera 1 (sourced from physical_cam_123)
cameras/1/description = Split Cam A
cameras/1/formats = 1
cameras/1/id = virtual_cam_A ; Optional: define a persistent ID
cameras/1/source_camera_id = physical_cam_123 # Replace with your actual physical camera ID

# Virtual Camera 2 (also sourced from physical_cam_123)
cameras/2/description = Split Cam B
cameras/2/formats = 1
cameras/2/id = virtual_cam_B ; Optional: define a persistent ID
cameras/2/source_camera_id = physical_cam_123 # Must be the same as above for splitting

[Formats]
formats/size = 1

formats/1/format = YUY2
formats/1/width = 1280
formats/1/height = 720
formats/1/fps = 30

# [General]
# default_frame = /path/to/default_frame.png # Optional: picture if no source
```

**To use this example:**
1.  Replace `physical_cam_123` with the actual ID of your physical webcam.
2.  Save the content as `my_splitter_config.ini` (or any name).
3.  Run: `./AkVCamManager load my_splitter_config.ini`

Now, "Split Cam A" and "Split Cam B" should appear as available webcams in applications, both showing the feed from `physical_cam_123`.

### Other Potentially Relevant Commands

*   `./AkVCamManager add-device DESCRIPTION`: Adds a new virtual camera.
*   `./AkVCamManager remove-device DEVICE_ID`: Removes a virtual camera.
*   `./AkVCamManager set-description DEVICE_ID DESCRIPTION`: Changes the description of a device.
*   `./AkVCamManager --help`: Shows all available commands and global options.

**Note on specific splitting flags:**
While the command-line parser defines flags like `--source-camera` and `--num-virtual-cameras`, the currently implemented and documented method for achieving webcam splitting is through the INI file configuration loaded via the `load` command as described above.
```
