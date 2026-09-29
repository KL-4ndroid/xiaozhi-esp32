#include "dog_motion.h"

#include <esp_err.h>
#include <esp_timer.h>

#include "servo_dog_ctrl.h"

namespace {

// The controller has no completion callback yet. Keep this deliberately longer
// than the measured single forward cycle so a second movement is never queued
// immediately after the first one.
constexpr int64_t kMovementBusyWindowUs = 3000 * 1000;

static_assert(DogMotion::Parse("forward") == DogMotionAction::kForward);
static_assert(DogMotion::Parse("backward") == DogMotionAction::kBackward);
static_assert(DogMotion::Parse("turn_left") == DogMotionAction::kTurnLeft);
static_assert(DogMotion::Parse("turn_right") == DogMotionAction::kTurnRight);
static_assert(DogMotion::Parse("stop") == DogMotionAction::kStop);
static_assert(DogMotion::Parse("home") == DogMotionAction::kHome);
static_assert(!DogMotion::Parse(""));
static_assert(!DogMotion::Parse("Forward"));
static_assert(!DogMotion::Parse("jump_forward"));

DogMotionResult SendMotion(servo_dog_state_t state, dog_action_args_t* args)
{
    const esp_err_t result = servo_dog_ctrl_send(state, args);
    if (result == ESP_OK) {
        return DogMotionResult::kOk;
    }
    if (result == ESP_ERR_INVALID_STATE) {
        return DogMotionResult::kControllerUnavailable;
    }
    return DogMotionResult::kSendFailed;
}

}  // namespace

DogMotion& DogMotion::GetInstance()
{
    static DogMotion instance;
    return instance;
}

const char* DogMotion::ErrorMessage(DogMotionResult result)
{
    switch (result) {
    case DogMotionResult::kBusy:
        return "Dog motion is busy; use stop or wait before sending another movement";
    case DogMotionResult::kControllerUnavailable:
        return "Dog motion controller is unavailable";
    case DogMotionResult::kSendFailed:
        return "Failed to send dog motion command";
    case DogMotionResult::kOk:
        return "";
    }
    return "Unknown dog motion error";
}

DogMotionResult DogMotion::Execute(DogMotionAction action)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (action == DogMotionAction::kStop || action == DogMotionAction::kHome) {
        const DogMotionResult result = SendMotion(DOG_STATE_IDLE, nullptr);
        if (result == DogMotionResult::kOk) {
            busy_until_us_ = 0;
        }
        return result;
    }

    const int64_t now_us = esp_timer_get_time();
    if (now_us < busy_until_us_) {
        return DogMotionResult::kBusy;
    }

    servo_dog_state_t state = DOG_STATE_IDLE;
    switch (action) {
    case DogMotionAction::kForward:
        state = DOG_STATE_FORWARD;
        break;
    case DogMotionAction::kBackward:
        state = DOG_STATE_BACKWARD;
        break;
    case DogMotionAction::kTurnLeft:
        state = DOG_STATE_TURN_LEFT;
        break;
    case DogMotionAction::kTurnRight:
        state = DOG_STATE_TURN_RIGHT;
        break;
    case DogMotionAction::kStop:
    case DogMotionAction::kHome:
        return DogMotionResult::kSendFailed;
    default:
        return DogMotionResult::kSendFailed;
    }

    dog_action_args_t args = {
        .repeat_count = 1,
        .speed = 80,
        .hold_time_ms = NOT_USE,
        .angle_offset = NOT_USE,
    };
    const DogMotionResult result = SendMotion(state, &args);
    if (result == DogMotionResult::kOk) {
        busy_until_us_ = now_us + kMovementBusyWindowUs;
    }
    return result;
}
