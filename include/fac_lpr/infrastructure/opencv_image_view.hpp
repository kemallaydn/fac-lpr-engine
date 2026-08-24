#pragma once

#include <fac_lpr/application/image_validation.hpp>

#include <opencv2/core/mat.hpp>

namespace fac_lpr::infrastructure {

// Non-owning adapter. The caller must keep ValidatedImage::view.bytes alive for
// the complete lifetime of this object. Only const access is exposed so the
// caller-owned image buffer cannot be mutated through the engine adapter.
class OpenCvImageView final {
public:
    explicit OpenCvImageView(const application::ValidatedImage& image);

    OpenCvImageView(const OpenCvImageView&) = default;
    OpenCvImageView& operator=(const OpenCvImageView&) = default;
    OpenCvImageView(OpenCvImageView&&) noexcept = default;
    OpenCvImageView& operator=(OpenCvImageView&&) noexcept = default;
    ~OpenCvImageView() = default;

    [[nodiscard]] const cv::Mat& mat() const noexcept { return matrix_; }

private:
    cv::Mat matrix_{};
};

} // namespace fac_lpr::infrastructure
