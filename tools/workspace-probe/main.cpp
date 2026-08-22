#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <cstdlib>
#include <iostream>

int main() {
    using fac_lpr::application::PixelFormat;
    using fac_lpr::infrastructure::native_image::NativeImageWorkspace;

    NativeImageWorkspace workspace{};
    (void)workspace.prepare_tensor(3U * 960U * 960U);
    (void)workspace.prepare_image(1280U, 720U, PixelFormat::bgr8);
    const auto warmed = workspace.stats();

    constexpr int iterations = 1000;
    for (int index = 0; index < iterations; ++index) {
        (void)workspace.prepare_tensor(3U * 960U * 960U);
        (void)workspace.prepare_image(1280U, 720U, PixelFormat::bgr8);
    }

    const auto after = workspace.stats();
    std::cout
        << "iterations=" << iterations << '\n'
        << "tensor_capacity=" << after.tensor_capacity << '\n'
        << "scratch_capacity=" << after.scratch_capacity << '\n'
        << "tensor_growth_warm=" << warmed.tensor_growth_count << '\n'
        << "tensor_growth_after=" << after.tensor_growth_count << '\n'
        << "scratch_growth_warm=" << warmed.scratch_growth_count << '\n'
        << "scratch_growth_after=" << after.scratch_growth_count << '\n';

    const auto stable =
        after.tensor_growth_count == warmed.tensor_growth_count &&
        after.scratch_growth_count == warmed.scratch_growth_count;
    return stable ? EXIT_SUCCESS : EXIT_FAILURE;
}
