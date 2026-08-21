#include <fac_lpr/infrastructure/opencv_image_view.hpp>

#include <fac_lpr/application/error.hpp>

#include <limits>

namespace fac_lpr::infrastructure {
namespace {

[[nodiscard]] int cv_type(const application::PixelFormat format) {
    switch (format) {
        case application::PixelFormat::gray8:
            return CV_8UC1;
        case application::PixelFormat::bgr8:
        case application::PixelFormat::rgb8:
            return CV_8UC3;
    }
    throw application::InvalidImageError("unsupported pixel format for OpenCV adapter");
}

[[nodiscard]] int checked_dimension(const std::size_t value, const char* name) {
    if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw application::InvalidImageError(std::string{name} + " exceeds OpenCV int dimension limit");
    }
    return static_cast<int>(value);
}

} // namespace

OpenCvImageView::OpenCvImageView(const application::ValidatedImage& image)
    : matrix_(
          checked_dimension(image.view.height, "image height"),
          checked_dimension(image.view.width, "image width"),
          cv_type(image.view.format),
          const_cast<std::byte*>(image.view.bytes.data()),
          image.view.stride_bytes) {
    if (!matrix_.isContinuous() && image.view.stride_bytes == image.minimum_row_bytes) {
        throw application::InvalidImageError("OpenCV view continuity is inconsistent with packed image stride");
    }
}

} // namespace fac_lpr::infrastructure
