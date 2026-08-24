#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace fac_lpr::application {

enum class EngineErrorCode {
    configuration,
    model_load,
    inference,
    invalid_image,
    provider,
    cancelled,
    timeout,
    resource_exhausted,
    internal
};

class EngineError : public std::runtime_error {
public:
    EngineError(EngineErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    [[nodiscard]] EngineErrorCode code() const noexcept {
        return code_;
    }

private:
    EngineErrorCode code_;
};

class ConfigurationError final : public EngineError {
public:
    explicit ConfigurationError(std::string message)
        : EngineError(EngineErrorCode::configuration, std::move(message)) {}
};

class ModelLoadError final : public EngineError {
public:
    explicit ModelLoadError(std::string message)
        : EngineError(EngineErrorCode::model_load, std::move(message)) {}
};

class InferenceError final : public EngineError {
public:
    explicit InferenceError(std::string message)
        : EngineError(EngineErrorCode::inference, std::move(message)) {}
};

class InvalidImageError final : public EngineError {
public:
    explicit InvalidImageError(std::string message)
        : EngineError(EngineErrorCode::invalid_image, std::move(message)) {}
};

class ProviderError final : public EngineError {
public:
    explicit ProviderError(std::string message)
        : EngineError(EngineErrorCode::provider, std::move(message)) {}
};

class CancelledError final : public EngineError {
public:
    explicit CancelledError(std::string message)
        : EngineError(EngineErrorCode::cancelled, std::move(message)) {}
};

class TimeoutError final : public EngineError {
public:
    explicit TimeoutError(std::string message)
        : EngineError(EngineErrorCode::timeout, std::move(message)) {}
};

class ResourceExhaustedError final : public EngineError {
public:
    explicit ResourceExhaustedError(std::string message)
        : EngineError(EngineErrorCode::resource_exhausted, std::move(message)) {}
};

class InternalError final : public EngineError {
public:
    explicit InternalError(std::string message)
        : EngineError(EngineErrorCode::internal, std::move(message)) {}
};

} // namespace fac_lpr::application
