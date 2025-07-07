# akvirtualcamera, virtual camera for Mac and Windows.
# Copyright (C) 2021  Gonzalo Exequiel Pedone
#
# akvirtualcamera is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# akvirtualcamera is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with akvirtualcamera. If not, see <http://www.gnu.org/licenses/>.
#
# Web-Site: http://webcamoid.github.io/

cmake_minimum_required(VERSION 3.5)

project(Manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (APPLE)
    include(../cmio/cmio.cmake)
    set(INSTALLPATH ${CMIO_PLUGIN_NAME}.plugin/Contents/Resources)
elseif (WIN32)
    include(../dshow/dshow.cmake)
    set(INSTALLPATH ${WIN_TARGET_ARCH})
endif ()

# List of source files for the Manager executable
set(MANAGER_SOURCES
    src/main.cpp
    src/cmdparser.cpp
)

# Conditionally add platform-specific capture device implementations
if(WIN32)
    if(MSVC)
        list(APPEND MANAGER_SOURCES src/windows_capturedevice.cpp)
    endif()
elseif(APPLE)
    list(APPEND MANAGER_SOURCES src/macos_capturedevice.mm)
# Add other platform checks like elseif(UNIX AND NOT APPLE) if you have a Linux specific file
# For example: list(APPEND MANAGER_SOURCES src/linux_capturedevice.cpp)
endif()

add_executable(Manager ${MANAGER_SOURCES})

set_target_properties(Manager PROPERTIES
                      OUTPUT_NAME AkVCamManager
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/${INSTALLPATH})

add_dependencies(Manager VCamIPC PlatformUtils VCamUtils)

target_include_directories(Manager
                           PRIVATE ..)

target_link_libraries(Manager
                      VCamIPC
                      PlatformUtils
                      VCamUtils)

if (APPLE)
    find_library(COREFOUNDATION_FRAMEWORK NAMES CoreFoundation)
    find_library(COREMEDIA_FRAMEWORK NAMES CoreMedia)
    find_library(COREMEDIAIO_FRAMEWORK NAMES CoreMediaIO)
    find_library(COREVIDEO_FRAMEWORK NAMES CoreVideo)
    find_library(FOUNDATION_FRAMEWORK NAMES Foundation)
    find_library(IOKIT_FRAMEWORK NAMES IOKit)
    find_library(IOSURFACE_FRAMEWORK NAMES IOSurface)
    set(EXTRA_LIBS ${COREFOUNDATION_FRAMEWORK}
                   ${COREMEDIA_FRAMEWORK}
                   ${COREMEDIAIO_FRAMEWORK}
                   ${COREVIDEO_FRAMEWORK}
                   ${FOUNDATION_FRAMEWORK}
                   ${IOKIT_FRAMEWORK}
                   ${IOSURFACE_FRAMEWORK})
    target_link_libraries(Manager
                          ${EXTRA_LIBS})
elseif (WIN32)
    target_link_libraries(Manager
                          advapi32
                          gdi32
                          strmiids
                          uuid
                          ole32
                          oleaut32
                          shell32)
endif ()

install(TARGETS Manager DESTINATION ${INSTALLPATH})
