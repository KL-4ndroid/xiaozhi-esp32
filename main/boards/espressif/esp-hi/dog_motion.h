#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

enum class DogMotionAction {
    kForward,
    kBackward,
    kTurnLeft,
    kTurnRight,
    kStop,
    kHome,
};

enum class DogMotionResult {
    kOk,
    kBusy,
    kControllerUnavailable,
    kSendFailed,
};

class DogMotion {
public:
    static DogMotion& GetInstance();

    static constexpr std::optional<DogMotionAction> Parse(std::string_view name)
    {
        if (name == "forward") {
            return DogMotionAction::kForward;
        }
        if (name == "backward") {
            return DogMotionAction::kBackward;
        }
        if (name == "turn_left") {
            return DogMotionAction::kTurnLeft;
        }
        if (name == "turn_right") {
            return DogMotionAction::kTurnRight;
        }
        if (name == "stop") {
            return DogMotionAction::kStop;
        }
        if (name == "home") {
            return DogMotionAction::kHome;
        }
        return std::nullopt;
    }

    static const char* ErrorMessage(DogMotionResult result);

    DogMotionResult Execute(DogMotionAction action);

private:
    DogMotion() = default;

    std::mutex mutex_;
    int64_t busy_until_us_ = 0;
};
