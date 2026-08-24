#include <fac_lpr/fac_lpr_engine.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

TEST(CApiThreadSafety, ConcurrentVersionQueriesAreReentrant) {
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads{};
    for (int thread_index = 0; thread_index < 8; ++thread_index) {
        threads.emplace_back([&] {
            for (int iteration = 0; iteration < 1000; ++iteration) {
                fac_lpr_version_info_v1 version = FAC_LPR_VERSION_INFO_V1_INIT;
                if (fac_lpr_get_version_v1(&version) != FAC_LPR_STATUS_OK ||
                    version.abi_major != FAC_LPR_ABI_VERSION_V1) {
                    failed = true;
                    return;
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_FALSE(failed.load());
}

TEST(CApiThreadSafety, DestroyRacingRecognizeNeverUsesFreedHandle) {
    for (int round = 0; round < 200; ++round) {
        fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
        fac_lpr_engine_handle* owner = nullptr;
        ASSERT_EQ(fac_lpr_engine_create_v1(&config, &owner), FAC_LPR_STATUS_OK);
        ASSERT_NE(owner, nullptr);

        auto* recognition_handle = owner;
        auto* destroy_handle = owner;
        const std::array<std::uint8_t, 3> pixel{0U, 0U, 0U};
        fac_lpr_image_view_v1 image = FAC_LPR_IMAGE_VIEW_V1_INIT;
        image.data = pixel.data();
        image.data_size = pixel.size();
        image.width = 1U;
        image.height = 1U;
        image.stride_bytes = 3U;
        image.pixel_format = FAC_LPR_PIXEL_FORMAT_BGR8;

        std::atomic<bool> start{false};
        std::atomic<bool> bad_status{false};
        std::thread recognizer([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int iteration = 0; iteration < 64; ++iteration) {
                std::size_t required = 0U;
                const auto status = fac_lpr_engine_recognize_v1(
                    recognition_handle, &image, nullptr, 0U, &required);
                if (status != FAC_LPR_STATUS_CONFIGURATION_ERROR) {
                    bad_status = true;
                    return;
                }
            }
        });
        std::thread destroyer([&] {
            start.store(true, std::memory_order_release);
            EXPECT_EQ(fac_lpr_engine_destroy_v1(&destroy_handle), FAC_LPR_STATUS_OK);
            EXPECT_EQ(destroy_handle, nullptr);
        });

        recognizer.join();
        destroyer.join();
        EXPECT_FALSE(bad_status.load());
        owner = nullptr;
    }
}

TEST(CApiThreadSafety, LastErrorIsThreadLocal) {
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads{};
    for (int thread_index = 0; thread_index < 8; ++thread_index) {
        threads.emplace_back([&, thread_index] {
            fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
            config.abi_version = static_cast<std::uint32_t>(100 + thread_index);
            fac_lpr_engine_handle* handle = nullptr;
            if (fac_lpr_engine_create_v1(&config, &handle) != FAC_LPR_STATUS_CONFIGURATION_ERROR) {
                failed = true;
                return;
            }
            std::size_t required = 0U;
            if (fac_lpr_get_last_error_v1(nullptr, 0U, &required) != FAC_LPR_STATUS_BUFFER_TOO_SMALL ||
                required <= 1U) {
                failed = true;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_FALSE(failed.load());
}

} // namespace
