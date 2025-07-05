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

#ifndef AKVCAMUTILS_VIDEOFORMAT_H
#define AKVCAMUTILS_VIDEOFORMAT_H

#include <string>
#include <vector>
#include <ostream> // Required for std::ostream operator<< declarations

#include "videoformattypes.h" // Defines AkVCam::FourCC, AkVCam::PixelFormat
#include "fraction.h"

#ifdef _WIN32
#include <guiddef.h> // Provides GUID definition
#endif

namespace AkVCam
{
    class VideoFormat; // Forward declaration for operator<<
    class VideoFormatPrivate;
    using VideoFormats = std::vector<VideoFormat>;

    class VideoFormat
    {
        public:
            VideoFormat();
            VideoFormat(FourCC fourcc,
                        int width,
                        int height,
                        const std::vector<Fraction> &frameRates={});
            VideoFormat(const VideoFormat &other);
            ~VideoFormat();
            VideoFormat &operator =(const VideoFormat &other);
            bool operator ==(const VideoFormat &other) const;
            bool operator !=(const VideoFormat &other) const;
            operator bool() const;

            FourCC fourcc() const;
            FourCC &fourcc();
            int width() const;
            int &width();
            int height() const;
            int &height();
            std::vector<Fraction> frameRates() const;
            std::vector<Fraction> &frameRates();
            std::vector<FractionRange> frameRateRanges() const;
            Fraction minimumFrameRate() const;
            size_t bpp() const;
            size_t bypl(size_t plane) const;
            size_t size() const;
            size_t planes() const;
            size_t offset(size_t plane) const;
            size_t planeSize(size_t plane) const;
            bool isValid() const;
            void clear();
            VideoFormat nearest(const std::vector<VideoFormat> &formats) const;

            static void roundNearest(int width, int height,
                                     int *owidth, int *oheight,
                                     int align=32);
            static FourCC fourccFromString(const std::string &fourccStr);
            static std::string stringFromFourcc(FourCC fourcc);
#ifdef _WIN32
            static AkVCam::FourCC guidToFourcc(const GUID &guid); // Use GUID
            static GUID fourccToGuid(AkVCam::FourCC fourcc);   // Use GUID
#endif

        private:
            VideoFormatPrivate *d;
    };
} // namespace AkVCam

// operator<< declarations should be outside the AkVCam namespace if they take AkVCam::VideoFormat
// or correctly namespaced if they are friends or defined within.
// For now, assuming global namespace or correct handling elsewhere.
std::ostream &operator <<(std::ostream &os, const AkVCam::VideoFormat &format);
std::ostream &operator <<(std::ostream &os, const AkVCam::VideoFormats &formats);

#endif // AKVCAMUTILS_VIDEOFORMAT_H
