#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/recognition_ensemble.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using fac_lpr::application::IPlateRecognizer;
using fac_lpr::application::ImageView;
using fac_lpr::application::OperationContext;
using fac_lpr::application::PixelFormat;
using fac_lpr::application::RecognitionEnsemble;
using fac_lpr::application::RecognizerRegistration;
using fac_lpr::domain::PlateCandidate;
using fac_lpr::domain::RecognitionEvidence;

struct Probe final {
    std::optional<std::chrono::steady_clock::time_point> deadline{};
    int calls{0};
};

class FakeRecognizer final : public IPlateRecognizer {
public:
    FakeRecognizer(
        std::string name,
        RecognitionEvidence result,
        std::shared_ptr<Probe> probe = {})
        : name_(std::move(name)), result_(std::move(result)), probe_(std::move(probe)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

    [[nodiscard]] RecognitionEvidence recognize(
        const ImageView&,
        const OperationContext& context) override {
        if (probe_) {
            ++probe_->calls;
            probe_->deadline = context.deadline;
        }
        return result_;
    }

private:
    std::string name_{};
    RecognitionEvidence result_{};
    std::shared_ptr<Probe> probe_{};
};

class FailingRecognizer final : public IPlateRecognizer {
public:
    explicit FailingRecognizer(std::string name) : name_(std::move(name)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] RecognitionEvidence recognize(
        const ImageView&,
        const OperationContext&) override {
        throw fac_lpr::application::ProviderError("synthetic provider failure");
    }
private:
    std::string name_{};
};

RecognitionEvidence recognized(std::string plate, const float confidence) {
    RecognitionEvidence evidence{};
    evidence.candidates.push_back(PlateCandidate{
        .text = std::move(plate),
        .confidence = confidence,
        .calibrated_confidence = 0.0F,
        .format_valid = true});
    return evidence;
}

ImageView image_fixture(std::vector<std::byte>& bytes) {
    bytes.assign(16U, std::byte{128});
    return ImageView{bytes, 4U, 4U, 4U, PixelFormat::gray8};
}

TEST(RecognitionEnsemble, OptionalProviderFailureDegradesButKeepsHealthyEvidence) {
    std::vector<RecognizerRegistration> registrations{};
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FailingRecognizer>("optional"),
        .weight = 1.0F,
        .timeout = std::chrono::milliseconds{100},
        .required = false});
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>(
            "primary", recognized("34A1234", 0.80F)),
        .weight = 1.0F,
        .timeout = std::chrono::milliseconds{100},
        .required = true});

    const RecognitionEnsemble ensemble{std::move(registrations)};
    std::vector<std::byte> bytes{};
    const auto result = ensemble.recognize(
        image_fixture(bytes), 0.75F, OperationContext{});

    EXPECT_TRUE(result.degraded);
    ASSERT_EQ(result.failures.size(), 1U);
    EXPECT_EQ(result.failures.front().provider, "optional");
    ASSERT_EQ(result.evidence.size(), 1U);
    EXPECT_EQ(result.evidence.front().source, "primary");
}

TEST(RecognitionEnsemble, RequiredProviderFailureIsFatal) {
    std::vector<RecognizerRegistration> registrations{};
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FailingRecognizer>("required"),
        .weight = 1.0F,
        .timeout = std::chrono::milliseconds{100},
        .required = true});
    const RecognitionEnsemble ensemble{std::move(registrations)};
    std::vector<std::byte> bytes{};
    EXPECT_THROW(
        ensemble.recognize(image_fixture(bytes), 0.5F, OperationContext{}),
        fac_lpr::application::ProviderError);
}

TEST(RecognitionEnsemble, ProviderWeightIsAppliedToCalibratedConfidence) {
    std::vector<RecognizerRegistration> registrations{};
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>(
            "weighted", recognized("34A1234", 0.80F)),
        .weight = 0.50F,
        .timeout = std::chrono::milliseconds{100}});
    const RecognitionEnsemble ensemble{std::move(registrations)};
    std::vector<std::byte> bytes{};
    const auto result = ensemble.recognize(
        image_fixture(bytes), 0.6F, OperationContext{});

    ASSERT_EQ(result.evidence.size(), 1U);
    ASSERT_EQ(result.evidence.front().candidates.size(), 1U);
    EXPECT_FLOAT_EQ(result.evidence.front().candidates.front().confidence, 0.80F);
    EXPECT_FLOAT_EQ(
        result.evidence.front().candidates.front().calibrated_confidence,
        0.40F);
}

TEST(RecognitionEnsemble, ProviderReceivesBoundedChildDeadline) {
    auto probe = std::make_shared<Probe>();
    std::vector<RecognizerRegistration> registrations{};
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>(
            "deadline", recognized("34A1234", 0.8F), probe),
        .weight = 1.0F,
        .timeout = std::chrono::milliseconds{50}});
    const RecognitionEnsemble ensemble{std::move(registrations)};
    std::vector<std::byte> bytes{};
    const auto before = std::chrono::steady_clock::now();
    (void)ensemble.recognize(image_fixture(bytes), 0.6F, OperationContext{});
    const auto after = std::chrono::steady_clock::now();

    ASSERT_TRUE(probe->deadline.has_value());
    EXPECT_GE(*probe->deadline, before);
    EXPECT_LE(*probe->deadline, after + std::chrono::milliseconds{50});
}

TEST(RecognitionEnsemble, DisabledAndZeroWeightProvidersAreNotInvoked) {
    auto disabled_probe = std::make_shared<Probe>();
    auto zero_weight_probe = std::make_shared<Probe>();
    auto active_probe = std::make_shared<Probe>();

    std::vector<RecognizerRegistration> registrations{};
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>(
            "disabled", recognized("34A1234", 0.9F), disabled_probe),
        .weight = 1.0F,
        .timeout = std::chrono::milliseconds{100},
        .enabled = false});
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>(
            "zero-weight", recognized("34B1234", 0.9F), zero_weight_probe),
        .weight = 0.0F,
        .timeout = std::chrono::milliseconds{100}});
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>(
            "active", recognized("34C1234", 0.8F), active_probe),
        .weight = 1.0F,
        .timeout = std::chrono::milliseconds{100}});

    const RecognitionEnsemble ensemble{std::move(registrations)};
    std::vector<std::byte> bytes{};
    const auto result = ensemble.recognize(
        image_fixture(bytes), 0.7F, OperationContext{});

    EXPECT_EQ(disabled_probe->calls, 0);
    EXPECT_EQ(zero_weight_probe->calls, 0);
    EXPECT_EQ(active_probe->calls, 1);
    ASSERT_EQ(result.evidence.size(), 1U);
    EXPECT_EQ(result.evidence.front().source, "active");
}

TEST(RecognitionEnsemble, InvalidCalibratedConfidenceDegradesOptionalProvider) {
    auto invalid = recognized("34A1234", 0.8F);
    invalid.candidates.front().calibrated_confidence = 1.5F;

    std::vector<RecognizerRegistration> registrations{};
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>("invalid", invalid),
        .weight = 1.0F,
        .timeout = std::chrono::milliseconds{100},
        .required = false});
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>(
            "healthy", recognized("06AB123", 0.75F)),
        .weight = 1.0F,
        .timeout = std::chrono::milliseconds{100},
        .required = true});

    const RecognitionEnsemble ensemble{std::move(registrations)};
    std::vector<std::byte> bytes{};
    const auto result = ensemble.recognize(
        image_fixture(bytes), 0.7F, OperationContext{});

    EXPECT_TRUE(result.degraded);
    ASSERT_EQ(result.failures.size(), 1U);
    EXPECT_EQ(result.failures.front().provider, "invalid");
    EXPECT_EQ(result.failures.front().code, fac_lpr::application::EngineErrorCode::provider);
    ASSERT_EQ(result.evidence.size(), 1U);
    EXPECT_EQ(result.evidence.front().source, "healthy");
}

TEST(RecognitionEnsemble, DuplicateProviderNamesFailFast) {
    std::vector<RecognizerRegistration> registrations{};
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>("same", recognized("34A1234", 0.8F))});
    registrations.push_back(RecognizerRegistration{
        .provider = std::make_shared<FakeRecognizer>("same", recognized("06AB123", 0.8F))});
    EXPECT_THROW(
        RecognitionEnsemble{std::move(registrations)},
        fac_lpr::application::ConfigurationError);
}

} // namespace
