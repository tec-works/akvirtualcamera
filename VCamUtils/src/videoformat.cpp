#define NOMINMAX // Prevent min/max macros from Windows.h
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

#include <algorithm> // For std::min_element, std::max_element
#include <limits>    // For std::numeric_limits
#include <map>
#include <ostream>
#include <vector>    // For std::vector
#include <string>    // For std::string

#include "videoformat.h"
#include "utils.h" // For AkLogWarning, etc.

#ifdef _WIN32
#include <windows.h> // General Windows header, includes guiddef.h
#include <dshow.h>   // For MEDIASUBTYPE_... GUIDs
// Ensure FourCC_MJPG is usable if not directly a MEDIASUBTYPE
// It's defined in videoformattypes.h, so AkVCam::FourCC_MJPG is the way.
#endif


namespace AkVCam
{
    class VideoFormatPrivate
    {
        public:
            FourCC m_fourcc {0};
            int m_width {0};
            int m_height {0};
            std::vector<Fraction> m_frameRates;

            VideoFormatPrivate() = default;
            VideoFormatPrivate(FourCC fourcc,
                               int width,
                               int height,
                               const std::vector<Fraction> &frameRates);
    };

    using PlaneOffsetFunc = size_t (*)(size_t plane, size_t width, size_t height);
    using ByplFunc = size_t (*)(size_t plane, size_t width);

    class VideoFormatGlobals
    {
        public:
            PixelFormat format;
            size_t bpp;
            size_t planes;
            PlaneOffsetFunc planeOffset;
            ByplFunc bypl;
            std::string str;

            inline static const std::vector<VideoFormatGlobals> &formats();
            static inline const VideoFormatGlobals *byPixelFormat(PixelFormat pixelFormat);
            static inline const VideoFormatGlobals *byStr(const std::string &str);
            static size_t offsetNV(size_t plane, size_t width, size_t height);
            static size_t byplNV(size_t plane, size_t width);

            template<typename T>
            static inline T alignUp(const T &value, const T &align)
            {
                return (value + align - 1) & ~(align - 1);
            }

            template<typename T>
            static inline T align32(const T &value)
            {
                return alignUp<T>(value, 32);
            }
    };


VideoFormat::VideoFormat()
{
    this->d = new VideoFormatPrivate;
}

VideoFormat::VideoFormat(FourCC fourcc,
                                 int width,
                                 int height,
                                 const std::vector<Fraction> &frameRates)
{
    this->d = new VideoFormatPrivate(fourcc, width, height, frameRates);
}

VideoFormat::VideoFormat(const VideoFormat &other)
{
    this->d = new VideoFormatPrivate(other.d->m_fourcc,
                                     other.d->m_width,
                                     other.d->m_height,
                                     other.d->m_frameRates);
}

VideoFormat::~VideoFormat()
{
    delete this->d;
}

VideoFormat &VideoFormat::operator =(const VideoFormat &other)
{
    if (this != &other) {
        this->d->m_fourcc = other.d->m_fourcc;
        this->d->m_width = other.d->m_width;
        this->d->m_height = other.d->m_height;
        this->d->m_frameRates = other.d->m_frameRates;
    }

    return *this;
}

bool VideoFormat::operator ==(const AkVCam::VideoFormat &other) const
{
    return this->d->m_fourcc == other.d->m_fourcc
           && this->d->m_width == other.d->m_width
           && this->d->m_height == other.d->m_height
           && this->d->m_frameRates == other.d->m_frameRates;
}

bool VideoFormat::operator !=(const AkVCam::VideoFormat &other) const
{
    return this->d->m_fourcc != other.d->m_fourcc
           || this->d->m_width != other.d->m_width
           || this->d->m_height != other.d->m_height
           || this->d->m_frameRates != other.d->m_frameRates;
}

VideoFormat::operator bool() const
{
    return this->isValid();
}

FourCC VideoFormat::fourcc() const
{
    return this->d->m_fourcc;
}

FourCC &VideoFormat::fourcc()
{
    return this->d->m_fourcc;
}

int VideoFormat::width() const
{
    return this->d->m_width;
}

int &VideoFormat::width()
{
    return this->d->m_width;
}

int VideoFormat::height() const
{
    return this->d->m_height;
}

int &VideoFormat::height()
{
    return this->d->m_height;
}

std::vector<Fraction> VideoFormat::frameRates() const
{
    return this->d->m_frameRates;
}

std::vector<Fraction> &VideoFormat::frameRates()
{
    return this->d->m_frameRates;
}

std::vector<FractionRange> VideoFormat::frameRateRanges() const
{
    std::vector<FractionRange> ranges;

    if (!this->d->m_frameRates.empty()) {
        // Ensure NOMINMAX is defined before this if windows.h is included
        auto min_it = std::min_element(this->d->m_frameRates.begin(), this->d->m_frameRates.end());
        auto max_it = std::max_element(this->d->m_frameRates.begin(), this->d->m_frameRates.end());
        if (min_it != this->d->m_frameRates.end() && max_it != this->d->m_frameRates.end()){
             ranges.emplace_back(FractionRange {*min_it, *max_it});
        }
    }

    return ranges;
}

Fraction VideoFormat::minimumFrameRate() const
{
    if (this->d->m_frameRates.empty())
        return {0, 0};

    return *std::min_element(this->d->m_frameRates.begin(),
                             this->d->m_frameRates.end());
}

size_t VideoFormat::bpp() const
{
    auto vf = VideoFormatGlobals::byPixelFormat(static_cast<PixelFormat>(this->d->m_fourcc));
    return vf? vf->bpp: 0;
}

size_t VideoFormat::bypl(size_t plane) const
{
    auto vf = VideoFormatGlobals::byPixelFormat(static_cast<PixelFormat>(this->d->m_fourcc));
    if (!vf) return 0;
    if (vf->bypl) return vf->bypl(plane, static_cast<size_t>(this->d->m_width));
    return VideoFormatGlobals::align32(static_cast<size_t>(this->d->m_width) * vf->bpp) / 8;
}

size_t VideoFormat::size() const
{
    auto vf = VideoFormatGlobals::byPixelFormat(static_cast<PixelFormat>(this->d->m_fourcc));
    if (!vf) return 0;
    if (vf->planeOffset)
        return vf->planeOffset(vf->planes, static_cast<size_t>(this->d->m_width), static_cast<size_t>(this->d->m_height));
    return static_cast<size_t>(this->d->m_height) * VideoFormatGlobals::align32(static_cast<size_t>(this->d->m_width) * vf->bpp) / 8;
}

size_t VideoFormat::planes() const
{
    auto vf = VideoFormatGlobals::byPixelFormat(static_cast<PixelFormat>(this->d->m_fourcc));
    return vf? vf->planes: 0;
}

size_t VideoFormat::offset(size_t plane) const
{
    auto vf = VideoFormatGlobals::byPixelFormat(static_cast<PixelFormat>(this->d->m_fourcc));
    if (!vf) return 0;
    if (vf->planeOffset)
        return vf->planeOffset(plane, static_cast<size_t>(this->d->m_width), static_cast<size_t>(this->d->m_height));
    return 0;
}

size_t VideoFormat::planeSize(size_t plane) const
{
    return static_cast<size_t>(this->d->m_height) * this->bypl(plane);
}

bool VideoFormat::isValid() const
{
    if (this->size() < 1) return false;
    if (this->d->m_frameRates.empty()) return false;
    for (auto &fps: this->d->m_frameRates)
        if (fps.num() < 1 || fps.den() < 1) return false;
    return true;
}

void VideoFormat::clear()
{
    this->d->m_fourcc = 0;
    this->d->m_width = 0;
    this->d->m_height = 0;
    this->d->m_frameRates.clear();
}

VideoFormat VideoFormat::nearest(const std::vector<VideoFormat> &formats) const
{
    VideoFormat nearestFormat;
    uint64_t q = std::numeric_limits<uint64_t>::max(); // Correct initialization of q
    auto svf = VideoFormatGlobals::byPixelFormat(static_cast<PixelFormat>(this->d->m_fourcc));

    for (auto &format: formats) {
        auto vf = VideoFormatGlobals::byPixelFormat(static_cast<PixelFormat>(format.d->m_fourcc));
        uint64_t diffFourcc = format.d->m_fourcc == this->d->m_fourcc? 0: 1;
        auto diffWidth = format.d->m_width - this->d->m_width;
        auto diffHeight = format.d->m_height - this->d->m_height;

        size_t svf_bpp = svf ? svf->bpp : 0;
        size_t vf_bpp = vf ? vf->bpp : 0;
        size_t svf_planes = svf ? svf->planes : 0;
        size_t vf_planes = vf ? vf->planes : 0;

        auto diffBpp = vf_bpp - svf_bpp;
        auto diffPlanes = vf_planes - svf_planes;

        uint64_t k = diffFourcc
                   + static_cast<uint64_t>(diffWidth * diffWidth)
                   + static_cast<uint64_t>(diffHeight * diffHeight)
                   + diffBpp * diffBpp
                   + diffPlanes * diffPlanes;

        if (k < q) {
            nearestFormat = format;
            q = k;
        }
    }
    return nearestFormat;
}

void VideoFormat::roundNearest(int width, int height,
                                       int *owidth, int *oheight,
                                       int align)
{
    *owidth = (width + (align >> 1)) & ~(align - 1);
    *oheight = (2 * height * *owidth + width) / (2 * width);
}

FourCC VideoFormat::fourccFromString(const std::string &fourccStr)
{
    auto vf = VideoFormatGlobals::byStr(fourccStr);
    return vf? vf->format: 0;
}

std::string VideoFormat::stringFromFourcc(FourCC fourcc)
{
    auto vf = VideoFormatGlobals::byPixelFormat(static_cast<PixelFormat>(fourcc));
    if (vf) return vf->str;
    if (fourcc == AkVCam::FourCC_MJPG) return "MJPG"; // Handle if not in global list
    return std::string(); // Return empty string for unknown FourCCs
}

#ifdef _WIN32
// These functions are static members of VideoFormat class
FourCC VideoFormat::guidToFourcc(const GUID &guid) {
    if (guid == MEDIASUBTYPE_RGB32) return AkVCam::PixelFormatRGB32;
    if (guid == MEDIASUBTYPE_RGB24) return AkVCam::PixelFormatRGB24;
    if (guid == MEDIASUBTYPE_RGB565) return AkVCam::PixelFormatRGB16;
    if (guid == MEDIASUBTYPE_RGB555) return AkVCam::PixelFormatRGB15;
    if (guid == MEDIASUBTYPE_YUY2) return AkVCam::PixelFormatYUY2;
    if (guid == MEDIASUBTYPE_UYVY) return AkVCam::PixelFormatUYVY;
    if (guid == MEDIASUBTYPE_NV12) return AkVCam::PixelFormatNV12;
    if (guid == MEDIASUBTYPE_MJPG) return AkVCam::FourCC_MJPG;

    if (guid.Data2 == 0x0000 && guid.Data3 == 0x0010 &&
        guid.Data4[0] == 0x80 && guid.Data4[1] == 0x00 &&
        guid.Data4[2] == 0x00 && guid.Data4[3] == 0xaa &&
        guid.Data4[4] == 0x00 && guid.Data4[5] == 0x38 &&
        guid.Data4[6] == 0x9b && guid.Data4[7] == 0x71) {
        return guid.Data1;
    }

    // AkLogWarning() << "VideoFormat::guidToFourcc: Unknown GUID." << std::endl;
    return 0;
}

GUID VideoFormat::fourccToGuid(AkVCam::FourCC fourcc) {
    // Use static_cast for clarity if PixelFormat and FourCC are different types underlyingly
    // but used interchangeably for these known values.
    switch (static_cast<PixelFormat>(fourcc)) {
        case AkVCam::PixelFormatRGB32: return MEDIASUBTYPE_RGB32;
        case AkVCam::PixelFormatRGB24: return MEDIASUBTYPE_RGB24;
        case AkVCam::PixelFormatRGB16: return MEDIASUBTYPE_RGB565;
        case AkVCam::PixelFormatRGB15: return MEDIASUBTYPE_RGB555;
        case AkVCam::PixelFormatYUY2:  return MEDIASUBTYPE_YUY2;
        case AkVCam::PixelFormatUYVY:  return MEDIASUBTYPE_UYVY;
        case AkVCam::PixelFormatNV12:  return MEDIASUBTYPE_NV12;
        // AkVCam::FourCC_MJPG is not part of PixelFormat enum, handle separately
        default:
            if (fourcc == AkVCam::FourCC_MJPG) return MEDIASUBTYPE_MJPG;

            // Default construction for other FOURCCs
            GUID newGuid = {fourcc, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
            return newGuid;
    }
}
#endif

VideoFormatPrivate::VideoFormatPrivate(FourCC fourcc,
                                       int width,
                                       int height,
                                       const std::vector<Fraction> &frameRates):
    m_fourcc(fourcc),
    m_width(width),
    m_height(height),
    m_frameRates(frameRates)
{
}

const std::vector<VideoFormatGlobals> &VideoFormatGlobals::formats()
{
    static const std::vector<VideoFormatGlobals> s_formats {
        {PixelFormatRGB32, 32, 1,  nullptr, nullptr, "RGB32"},
        {PixelFormatRGB24, 24, 1,  nullptr, nullptr, "RGB24"},
        {PixelFormatRGB16, 16, 1,  nullptr, nullptr, "RGB16"},
        {PixelFormatRGB15, 16, 1,  nullptr, nullptr, "RGB15"},
        {PixelFormatBGR32, 32, 1,  nullptr, nullptr, "BGR32"},
        {PixelFormatBGR24, 24, 1,  nullptr, nullptr, "BGR24"},
        {PixelFormatBGR16, 16, 1,  nullptr, nullptr, "BGR16"},
        {PixelFormatBGR15, 16, 1,  nullptr, nullptr, "BGR15"},
        {PixelFormatUYVY , 16, 1,  nullptr, nullptr,  "UYVY"},
        {PixelFormatYUY2 , 16, 1,  nullptr, nullptr,  "YUY2"},
        {PixelFormatNV12 , 12, 2, offsetNV,  byplNV,  "NV12"},
        {PixelFormatNV21 , 12, 2, offsetNV,  byplNV,  "NV21"}
        // Note: FourCC_MJPG is not in this list, so stringFromFourcc(FourCC_MJPG) would be empty.
    };
    return s_formats;
}

const VideoFormatGlobals *VideoFormatGlobals::byPixelFormat(PixelFormat pixelFormat)
{
    for (auto &format_item: formats()) // Changed 'format' to 'format_item' to avoid conflict
        if (format_item.format == pixelFormat)
            return &format_item;
    if (pixelFormat == static_cast<PixelFormat>(AkVCam::FourCC_MJPG)) {
        static const VideoFormatGlobals mjpgFormat = {static_cast<PixelFormat>(AkVCam::FourCC_MJPG), 0, 1, nullptr, nullptr, "MJPG"};
        return &mjpgFormat;
    }
    return nullptr;
}

const VideoFormatGlobals *VideoFormatGlobals::byStr(const std::string &str)
{
    for (auto &format_item: formats()) // Changed 'format' to 'format_item' to avoid conflict
        if (format_item.str == str)
            return &format_item;
    if (str == "MJPG") {
        static const VideoFormatGlobals mjpgFormat = {static_cast<PixelFormat>(AkVCam::FourCC_MJPG), 0, 1, nullptr, nullptr, "MJPG"};
        return &mjpgFormat;
    }
    return nullptr;
}

size_t VideoFormatGlobals::offsetNV(size_t plane, size_t width, size_t height)
{
    size_t offset_arr[] = { // Renamed 'offset' to 'offset_arr'
        0,
        align32(size_t(width)) * height,
        5 * align32(size_t(width)) * height / 4
    };
    if (plane >= sizeof(offset_arr)/sizeof(offset_arr[0])) return 0;
    return offset_arr[plane];
}

size_t VideoFormatGlobals::byplNV(size_t plane, size_t width)
{
    UNUSED(plane);
    return align32(size_t(width));
}

} // namespace AkVCam

std::ostream &operator <<(std::ostream &os, const AkVCam::VideoFormat &format)
{
    std::string formatStr = AkVCam::VideoFormat::stringFromFourcc(format.fourcc());

    os << "VideoFormat("
       << (formatStr.empty() ? std::to_string(format.fourcc()) : formatStr)
       << ' '
       << format.width()
       << 'x'
       << format.height();
    if (!format.frameRates().empty()) {
         os << ' ' << format.minimumFrameRate();
    }
    os << ')';

    return os;
}

std::ostream &operator <<(std::ostream &os, const AkVCam::VideoFormats &formats)
{
    bool writeComma = false;
    os << "VideoFormats(";

    for (auto &format_item: formats) { // Changed 'format' to 'format_item'
        if (writeComma)
            os << ", ";

        os << format_item;
        writeComma = true;
    }

    os << ')';

    return os;
}
