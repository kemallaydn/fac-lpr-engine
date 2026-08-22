#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/model/atomic_model_slot.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>

namespace {
using fac_lpr::infrastructure::model::AtomicModelSlot;

struct Session final {
    explicit Session(int value, std::atomic<int>* destroyed = nullptr)
        : value(value), destroyed(destroyed) {}
    ~Session() {
        if (destroyed != nullptr) {
            ++(*destroyed);
        }
    }
    int value{};
    std::atomic<int>* destroyed{};
};

TEST(AtomicModelSlot, PublishesValidatedReplacementAtomically) {
    AtomicModelSlot<Session> slot{std::make_shared<Session>(1)};

    ASSERT_TRUE(slot.reload(
        [] { return std::make_shared<Session>(2); },
        [](const auto& candidate) { return candidate->value == 2; }));

    EXPECT_EQ(slot.acquire()->value, 2);
}

TEST(AtomicModelSlot, FailedLoadKeepsCurrentSession) {
    AtomicModelSlot<Session> slot{std::make_shared<Session>(1)};

    EXPECT_FALSE(slot.reload(
        []() -> std::shared_ptr<Session> { throw std::runtime_error("load failed"); },
        [](const auto&) { return true; }));
    EXPECT_EQ(slot.acquire()->value, 1);
}

TEST(AtomicModelSlot, FailedSelfTestKeepsCurrentSession) {
    AtomicModelSlot<Session> slot{std::make_shared<Session>(1)};

    EXPECT_FALSE(slot.reload(
        [] { return std::make_shared<Session>(2); },
        [](const auto&) { return false; }));
    EXPECT_EQ(slot.acquire()->value, 1);
}

TEST(AtomicModelSlot, InFlightSnapshotSurvivesSwapAndOldSessionIsReleasedAfterward) {
    std::atomic<int> destroyed{0};
    AtomicModelSlot<Session> slot{std::make_shared<Session>(1, &destroyed)};
    auto in_flight = slot.acquire();

    ASSERT_TRUE(slot.reload(
        [&] { return std::make_shared<Session>(2, &destroyed); },
        [](const auto&) { return true; }));

    EXPECT_EQ(in_flight->value, 1);
    EXPECT_EQ(slot.acquire()->value, 2);
    EXPECT_EQ(destroyed.load(), 0);

    in_flight.reset();
    EXPECT_EQ(destroyed.load(), 1);
}

TEST(AtomicModelSlot, BackgroundReloadPublishesOnlyAfterValidation) {
    AtomicModelSlot<Session> slot{std::make_shared<Session>(1)};

    auto result = slot.reload_async(
        [] { return std::make_shared<Session>(3); },
        [](const auto& candidate) { return candidate->value == 3; });

    EXPECT_TRUE(result.get());
    EXPECT_EQ(slot.acquire()->value, 3);
}

TEST(AtomicModelSlot, RejectsNullInitialSession) {
    EXPECT_THROW(
        (AtomicModelSlot<Session>{std::shared_ptr<Session>{}}),
        fac_lpr::application::ConfigurationError);
}

} // namespace
