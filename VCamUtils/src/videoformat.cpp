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

#include <algorithm>
#include <limits>
#include <map>
#include <ostream>

#include "videoformat.h"
#include "utils.h"

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
}

AkVCam::VideoFormat::VideoFormat()
{
    this->d = new VideoFormatPrivate;
}

AkVCam::VideoFormat::VideoFormat(FourCC fourcc,
                                 int width,
                                 int height,
                                 const std::vector<Fraction> &frameRates)
{
    this->d = new VideoFormatPrivate(fourcc, width, height, frameRates);
}

AkVCam::VideoFormat::VideoFormat(const VideoFormat &other)
{
    this->d = new VideoFormatPrivate(other.d->m_fourcc,
                                     other.d->m_width,
                                     other.d->m_height,
                                     other.d->m_frameRates);
}

AkVCam::VideoFormat::~VideoFormat()
{
    delete this->d;
}

AkVCam::VideoFormat &AkVCam::VideoFormat::operator =(const VideoFormat &other)
{
    if (this != &other) {
        this->d->m_fourcc = other.d->m_fourcc;
        this->d->m_width = other.d->m_width;
        this->d->m_height = other.d->m_height;
        this->d->m_frameRates = other.d->m_frameRates;
    }

    return *this;
}

bool AkVCam::VideoFormat::operator ==(const AkVCam::VideoFormat &other) const
{
    return this->d->m_fourcc == other.d->m_fourcc
           && this->d->m_width == other.d->m_width
           && this->d->m_height == other.d->m_height
           && this->d->m_frameRates == other.d->m_frameRates;
}

bool AkVCam::VideoFormat::operator !=(const AkVCam::VideoFormat &other) const
{
    return this->d->m_fourcc != other.d->m_fourcc
           || this->d->m_width != other.d->m_width
           || this->d->m_height != other.d->m_height
           || this->d->m_frameRates != other.d->m_frameRates;
}

AkVCam::VideoFormat::operator bool() const
{
    return this->isValid();
}

AkVCam::FourCC AkVCam::VideoFormat::fourcc() const
{
    return this->d->m_fourcc;
}

AkVCam::FourCC &AkVCam::VideoFormat::fourcc()
{
    return this->d->m_fourcc;
}

int AkVCam::VideoFormat::width() const
{
    return this->d->m_width;
}

int &AkVCam::VideoFormat::width()
{
    return this->d->m_width;
}

int AkVCam::VideoFormat::height() const
{
    return this->d->m_height;
}

int &AkVCam::VideoFormat::height()
{
    return this->d->m_height;
}

std::vector<AkVCam::Fraction> AkVCam::VideoFormat::frameRates() const
{
    return this->d->m_frameRates;
}

std::vector<AkVCam::Fraction> &AkVCam::VideoFormat::frameRates()
{
    return this->d->m_frameRates;
}

std::vector<AkVCam::FractionRange> AkVCam::VideoFormat::frameRateRanges() const
{
    std::vector<FractionRange> ranges;

    if (!this->d->m_frameRates.empty()) {
        auto min = *std::min_element(this->d->m_frameRates.begin(),
                                     this->d->m_frameRates.end());
        auto max = *std::max_element(this->d->m_frameRates.begin(),
                                     this->d->m_frameRates.end());
        ranges.emplace_back(FractionRange {min, max});
    }

    return ranges;
}

AkVCam::Fraction AkVCam::VideoFormat::minimumFrameRate() const
{
    if (this->d->m_frameRates.empty())
        return {0, 0};

    return *std::min_element(this->d->m_frameRates.begin(),
                             this->d->m_frameRates.end());
}

size_t AkVCam::VideoFormat::bpp() const
{
    auto vf = VideoFormatGlobals::byPixelFormat(PixelFormat(this->d->m_fourcc));

    return vf? vf->bpp: 0;
}

size_t AkVCam::VideoFormat::bypl(size_t plane) const
{
    auto vf = VideoFormatGlobals::byPixelFormat(PixelFormat(this->d->m_fourcc));

    if (!vf)
        return 0;

    if (vf->bypl)
        return vf->bypl(plane, size_t(this->d->m_width));

    return VideoFormatGlobals::align32(size_t(this->d->m_width) * vf->bpp) / 8;
}

size_t AkVCam::VideoFormat::size() const
{
    auto vf = VideoFormatGlobals::byPixelFormat(PixelFormat(this->d->m_fourcc));

    if (!vf)
        return 0;

    if (vf->planeOffset)
        return vf->planeOffset(vf->planes,
                               size_t(this->d->m_width),
                               size_t(this->d->m_height));

    return size_t(this->d->m_height)
           * VideoFormatGlobals::align32(size_t(this->d->m_width)
                                         * vf->bpp) / 8;
}

size_t AkVCam::VideoFormat::planes() const
{
    auto vf = VideoFormatGlobals::byPixelFormat(PixelFormat(this->d->m_fourcc));

    return vf? vf->planes: 0;
}

size_t AkVCam::VideoFormat::offset(size_t plane) const
{
    auto vf = VideoFormatGlobals::byPixelFormat(PixelFormat(this->d->m_fourcc));

    if (!vf)
        return 0;

    if (vf->planeOffset)
        return vf->planeOffset(plane,
                               size_t(this->d->m_width),
                               size_t(this->d->m_height));

    return 0;
}

size_t AkVCam::VideoFormat::planeSize(size_t plane) const
{
    return size_t(this->d->m_height) * this->bypl(plane);
}

bool AkVCam::VideoFormat::isValid() const
{
    if (this->size() < 1)
        return false;

    if (this->d->m_frameRates.empty())
        return false;

    for (auto &fps: this->d->m_frameRates)
        if (fps.num() < 1 || fps.den() < 1)
            return false;

    return true;
}

void AkVCam::VideoFormat::clear()
{
    this->d->m_fourcc = 0;
    this->d->m_width = 0;
    this->d->m_height = 0;
    this->d->m_frameRates.clear();
}

AkVCam::VideoFormat AkVCam::VideoFormat::nearest(const std::vector<VideoFormat> &formats) const
{
    VideoFormat nearestFormat;
    auto q = std::numeric_limits<uint64_t>::max();
    auto svf = VideoFormatGlobals::byPixelFormat(PixelFormat(this->d->m_fourcc));

    for (auto &format: formats) {
        auto vf = VideoFormatGlobals::byPixelFormat(PixelFormat(format.d->m_fourcc));
        uint64_t diffFourcc = format.d->m_fourcc == this->d->m_fourcc? 0: 1;
        auto diffWidth = format.d->m_width - this->d->m_width;
        auto diffHeight = format.d->m_height - this->d->m_height;
        auto diffBpp = vf->bpp - svf->bpp;
        auto diffPlanes = vf->planes - svf->planes;

        uint64_t k = diffFourcc
                   + uint64_t(diffWidth * diffWidth)
                   + uint64_t(diffHeight * diffHeight)
                   + diffBpp * diffBpp
                   + diffPlanes * diffPlanes;

        if (k < q) {
            nearestFormat = format;
            q = k;
        }
    }

    return nearestFormat;
}

void AkVCam::VideoFormat::roundNearest(int width, int height,
                                       int *owidth, int *oheight,
                                       int align)
{
    /* Explanation:
     *
     * When 'align' is a power of 2, the left most bit will be 1 (the pivot),
     * while all other bits be 0, if destination width is multiple of 'align'
     * all bits after pivot position will be 0, then we create a mask
     * substracting 1 to the align, so all bits after pivot position in the
     * mask will 1.
     * Then we negate all bits in the mask so all bits from pivot to the left
     * will be 1, and then we use that mask to get a width multiple of align.
     * This give us the lower (floor) width nearest to the original 'width' and
     * multiple of align. To get the rounded nearest value we add align / 2 to
     * 'width'.
     * This is the equivalent of:
     *
     * align * round(width / align)
     */
    *owidth = (width + (align >> 1)) & ~(align - 1);

    /* Find the nearest width:
     *
     * round(height * owidth / width)
     */
    *oheight = (2 * height * *owidth + width) / (2 * width);
}

AkVCam::FourCC AkVCam::VideoFormat::fourccFromString(const std::string &fourccStr)
{
    auto vf = VideoFormatGlobals::byStr(fourccStr);

    return vf? vf->format: 0;
}

std::string AkVCam::VideoFormat::stringFromFourcc(AkVCam::FourCC fourcc)
{
    auto vf = VideoFormatGlobals::byPixelFormat(PixelFormat(fourcc));

    return vf? vf->str: std::string();
}

#ifdef _WIN32
#include <initguid.h> // For DEFINE_GUID

// Define MEDIASUBTYPE guids if not already available from dshow.h or similar
// This is a common practice to ensure they are defined.
// Note: In a real project, these would typically come from including <dshow.h> and <uuids.h>
// and this might cause redefinition errors if those headers are included elsewhere directly.
// However, for this tool's environment, explicitly defining them might be safer if headers are not fully processed.

// MEDIASUBTYPE_RGB32
DEFINE_GUID(MEDIASUBTYPE_RGB32, 0x00000016, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
// MEDIASUBTYPE_RGB24
DEFINE_GUID(MEDIASUBTYPE_RGB24, 0xe436eb7d, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70);
// MEDIASUBTYPE_RGB16 (usually RGB565)
DEFINE_GUID(MEDIASUBTYPE_RGB565,0xe436eb7e, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70);
// MEDIASUBTYPE_RGB15 (usually RGB555)
DEFINE_GUID(MEDIASUBTYPE_RGB555,0xe436eb7c, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70);
// MEDIASUBTYPE_YUY2
DEFINE_GUID(MEDIASUBTYPE_YUY2, 0x32595559, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
// MEDIASUBTYPE_UYVY
DEFINE_GUID(MEDIASUBTYPE_UYVY, 0x59565955, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
// MEDIASUBTYPE_NV12
DEFINE_GUID(MEDIASUBTYPE_NV12, 0x3231564e, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
// MEDIASUBTYPE_MJPG
DEFINE_GUID(MEDIASUBTYPE_MJPG, 0x47504A4D, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);


AkVCam::FourCC AkVCam::VideoFormat::guidToFourcc(const GUID &guid) {
    if (guid == MEDIASUBTYPE_RGB32) return PixelFormatRGB32;
    if (guid == MEDIASUBTYPE_RGB24) return PixelFormatRGB24;
    if (guid == MEDIASUBTYPE_RGB565) return PixelFormatRGB16; // Assuming RGB16 is RGB565
    if (guid == MEDIASUBTYPE_RGB555) return PixelFormatRGB15; // Assuming RGB15 is RGB555
    if (guid == MEDIASUBTYPE_YUY2) return PixelFormatYUY2;
    if (guid == MEDIASUBTYPE_UYVY) return PixelFormatUYVY;
    if (guid == MEDIASUBTYPE_NV12) return PixelFormatNV12;
    if (guid == MEDIASUBTYPE_MJPG) return AkVCam::FourCC_MJPG; // 'MJPG'

    // For direct FOURCC GUIDs like 'YUY2'
    if (guid.Data1 <= 0xFFFF && guid.Data2 == 0x0000 && guid.Data3 == 0x0010 &&
        guid.Data4[0] == 0x80 && guid.Data4[1] == 0x00 && guid.Data4[2] == 0x00 &&
        guid.Data4[3] == 0xaa && guid.Data4[4] == 0x00 && guid.Data4[5] == 0x38 &&
        guid.Data4[6] == 0x9b && guid.Data4[7] == 0x71) {
        return guid.Data1;
    }
    // A common way to represent FOURCCs in GUIDs is to have the FOURCC in Data1,
    // and the rest of the GUID fields be {0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}}.
    // This is essentially what MEDIASUBTYPE_YUY2, MEDIASUBTYPE_UYVY, etc. are.
    // Check if Data1 contains a valid ASCII representation if it's a FourCC.
    // This is a bit heuristic.
    // char c1 = (guid.Data1 >> 0) & 0xFF;
    // char c2 = (guid.Data1 >> 8) & 0xFF;
    // char c3 = (guid.Data1 >> 16) & 0xFF;
    // char c4 = (guid.Data1 >> 24) & 0xFF;
    // if (isprint(c1) && isprint(c2) && isprint(c3) && isprint(c4)) {
    //     return guid.Data1;
    // }

    AkLogWarning() << "guidToFourcc: Unknown GUID type." << std::endl;
    return 0; // Unknown
}

GUID AkVCam::VideoFormat::fourccToGuid(FourCC fourcc) {
    switch (fourcc) {
        case PixelFormatRGB32: return MEDIASUBTYPE_RGB32;
        case PixelFormatRGB24: return MEDIASUBTYPE_RGB24;
        case PixelFormatRGB16: return MEDIASUBTYPE_RGB565; // Assuming RGB16 is RGB565
        case PixelFormatRGB15: return MEDIASUBTYPE_RGB555; // Assuming RGB15 is RGB555
        case PixelFormatYUY2:  return MEDIASUBTYPE_YUY2;
        case PixelFormatUYVY:  return MEDIASUBTYPE_UYVY;
        case PixelFormatNV12:  return MEDIASUBTYPE_NV12;
        case AkVCam::FourCC_MJPG: return MEDIASUBTYPE_MJPG; // 'MJPG'
        default:
            // For other FOURCCs, construct the GUID directly if it's a standard representation
            // This is common for many FOURCCs like I420, YV12 etc.
            // The Data1 field holds the FOURCC.
            GUID guid = {fourcc, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
            return guid;
    }
}
#endif


AkVCam::VideoFormatPrivate::VideoFormatPrivate(FourCC fourcc,
                                               int width,
                                               int height,
                                               const std::vector<Fraction> &frameRates):
    m_fourcc(fourcc),
    m_width(width),
    m_height(height),
    m_frameRates(frameRates)
{
}

const std::vector<AkVCam::VideoFormatGlobals> &AkVCam::VideoFormatGlobals::formats()
{
    static const std::vector<VideoFormatGlobals> formats {
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
    };

    return formats;
}

const AkVCam::VideoFormatGlobals *AkVCam::VideoFormatGlobals::byPixelFormat(PixelFormat pixelFormat)
{
    for (auto &format: formats())
        if (format.format == pixelFormat)
            return &format;

    return nullptr;
}

const AkVCam::VideoFormatGlobals *AkVCam::VideoFormatGlobals::byStr(const std::string &str)
{
    for (auto &format: formats())
        if (format.str == str)
            return &format;

    return nullptr;
}

size_t AkVCam::VideoFormatGlobals::offsetNV(size_t plane, size_t width, size_t height)
{
    size_t offset[] = {
        0,
        align32(size_t(width)) * height,
        5 * align32(size_t(width)) * height / 4
    };

    return offset[plane];
}

size_t AkVCam::VideoFormatGlobals::byplNV(size_t plane, size_t width)
{
    UNUSED(plane);

    return align32(size_t(width));
}

std::ostream &operator <<(std::ostream &os, const AkVCam::VideoFormat &format)
{
    auto formatStr = AkVCam::VideoFormat::stringFromFourcc(format.fourcc());

    os << "VideoFormat("
       << formatStr
       << ' '
       << format.width()
       << 'x'
       << format.height()
       << ' '
       << format.minimumFrameRate()
       << ')';

    return os;
}

std::ostream &operator <<(std::ostream &os, const AkVCam::VideoFormats &formats)
{
    bool writeComma = false;
    os << "VideoFormats(";

    for (auto &format: formats) {
        if (writeComma)
            os << ", ";

        os << format;
        writeComma = true;
    }

    os << ')';

    return os;
}
