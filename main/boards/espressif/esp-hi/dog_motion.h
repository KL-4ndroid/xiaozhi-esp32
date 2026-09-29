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
    kLayDown,
    kBow,
    kLeanBack,
    kBowLean,
    kSwayBackForth,
    kSway,
    kShakeHand,
    kShakeBackLegs,
    kRetractLegs,
    kStop,
    kHome,
};

enum class DogMotionResult {
    kOk,
    kBusy,
    kInvalidCycles,
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
        if (name == "lay_down") {
            return DogMotionAction::kLayDown;
        }
        if (name == "bow") {
            return DogMotionAction::kBow;
        }
        if (name == "lean_back") {
            return DogMotionAction::kLeanBack;
        }
        if (name == "bow_lean") {
            return DogMotionAction::kBowLean;
        }
        if (name == "sway_back_forth") {
            return DogMotionAction::kSwayBackForth;
        }
        if (name == "sway") {
            return DogMotionAction::kSway;
        }
        if (name == "shake_hand") {
            return DogMotionAction::kShakeHand;
        }
        if (name == "shake_back_legs") {
            return DogMotionAction::kShakeBackLegs;
        }
        if (name == "retract_legs") {
            return DogMotionAction::kRetractLegs;
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

    DogMotionResult Execute(DogMotionAction action, int cycles = 1);

private:
    DogMotion() = default;

    std::mutex mutex_;
    int64_t busy_until_us_ = 0;
};
