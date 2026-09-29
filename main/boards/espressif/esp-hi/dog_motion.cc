#include "dog_motion.h"

#include <esp_err.h>
#include <esp_timer.h>

#include "servo_dog_ctrl.h"

namespace {

// The controller has no completion callback yet. These windows deliberately
// outlast the underlying preset so unrelated actions are not queued on top of it.
constexpr int64_t kMovementBusyWindowPerCycleUs = 3000 * 1000;
constexpr int64_t kShortPoseBusyWindowUs = 1500 * 1000;
constexpr int64_t kDanceBusyWindowUs = 2500 * 1000;
constexpr int64_t kHandshakeBusyWindowUs = 4500 * 1000;
constexpr int64_t kStretchBusyWindowUs = 3500 * 1000;

static_assert(DogMotion::Parse("forward") == DogMotionAction::kForward);
static_assert(DogMotion::Parse("backward") == DogMotionAction::kBackward);
static_assert(DogMotion::Parse("turn_left") == DogMotionAction::kTurnLeft);
static_assert(DogMotion::Parse("turn_right") == DogMotionAction::kTurnRight);
static_assert(DogMotion::Parse("lay_down") == DogMotionAction::kLayDown);
static_assert(DogMotion::Parse("bow") == DogMotionAction::kBow);
static_assert(DogMotion::Parse("lean_back") == DogMotionAction::kLeanBack);
static_assert(DogMotion::Parse("bow_lean") == DogMotionAction::kBowLean);
static_assert(DogMotion::Parse("sway_back_forth") == DogMotionAction::kSwayBackForth);
static_assert(DogMotion::Parse("sway") == DogMotionAction::kSway);
static_assert(DogMotion::Parse("shake_hand") == DogMotionAction::kShakeHand);
static_assert(DogMotion::Parse("shake_back_legs") == DogMotionAction::kShakeBackLegs);
static_assert(DogMotion::Parse("retract_legs") == DogMotionAction::kRetractLegs);
static_assert(DogMotion::Parse("stop") == DogMotionAction::kStop);
static_assert(DogMotion::Parse("home") == DogMotionAction::kHome);
static_assert(!DogMotion::Parse(""));
static_assert(!DogMotion::Parse("Forward"));
static_assert(!DogMotion::Parse("jump_forward"));
static_assert(!DogMotion::Parse("jump_backward"));
static_assert(!DogMotion::Parse("poke"));
static_assert(!DogMotion::Parse("installation"));

bool SupportsCycles(DogMotionAction action)
{
    return action == DogMotionAction::kForward || action == DogMotionAction::kBackward ||
           action == DogMotionAction::kTurnLeft || action == DogMotionAction::kTurnRight;
}

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
    case DogMotionResult::kInvalidCycles:
        return "cycles must be 1 for poses and gestures, or from 1 to 5 for walking and turning";
    case DogMotionResult::kControllerUnavailable:
        return "Dog motion controller is unavailable";
    case DogMotionResult::kSendFailed:
        return "Failed to send dog motion command";
    case DogMotionResult::kOk:
        return "";
    }
    return "Unknown dog motion error";
}

DogMotionResult DogMotion::Execute(DogMotionAction action, int cycles)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (cycles < 1 || cycles > 5 || (!SupportsCycles(action) && cycles != 1)) {
        return DogMotionResult::kInvalidCycles;
    }

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
    dog_action_args_t args = {
        .repeat_count = NOT_USE,
        .speed = NOT_USE,
        .hold_time_ms = NOT_USE,
        .angle_offset = NOT_USE,
    };
    int64_t busy_window_us = kShortPoseBusyWindowUs;
    switch (action) {
    case DogMotionAction::kForward:
        state = DOG_STATE_FORWARD;
        args.repeat_count = cycles;
        args.speed = 80;
        busy_window_us = kMovementBusyWindowPerCycleUs * cycles;
        break;
    case DogMotionAction::kBackward:
        state = DOG_STATE_BACKWARD;
        args.repeat_count = cycles;
        args.speed = 80;
        busy_window_us = kMovementBusyWindowPerCycleUs * cycles;
        break;
    case DogMotionAction::kTurnLeft:
        state = DOG_STATE_TURN_LEFT;
        args.repeat_count = cycles;
        args.speed = 80;
        busy_window_us = kMovementBusyWindowPerCycleUs * cycles;
        break;
    case DogMotionAction::kTurnRight:
        state = DOG_STATE_TURN_RIGHT;
        args.repeat_count = cycles;
        args.speed = 80;
        busy_window_us = kMovementBusyWindowPerCycleUs * cycles;
        break;
    case DogMotionAction::kLayDown:
        state = DOG_STATE_LAY_DOWN;
        break;
    case DogMotionAction::kBow:
        state = DOG_STATE_BOW;
        args.speed = 80;
        args.hold_time_ms = 500;
        break;
    case DogMotionAction::kLeanBack:
        state = DOG_STATE_LEAN_BACK;
        args.speed = 80;
        args.hold_time_ms = 500;
        break;
    case DogMotionAction::kBowLean:
        state = DOG_STATE_BOW_LEAN;
        args.repeat_count = 1;
        args.speed = 80;
        busy_window_us = kDanceBusyWindowUs;
        break;
    case DogMotionAction::kSwayBackForth:
        state = DOG_STATE_SWAY_BACK_FORTH;
        busy_window_us = kDanceBusyWindowUs;
        break;
    case DogMotionAction::kSway:
        state = DOG_STATE_SWAY;
        args.repeat_count = 1;
        args.speed = 40;
        args.angle_offset = 20;
        busy_window_us = kDanceBusyWindowUs;
        break;
    case DogMotionAction::kShakeHand:
        state = DOG_STATE_SHAKE_HAND;
        args.repeat_count = 3;
        args.hold_time_ms = 1000;
        busy_window_us = kHandshakeBusyWindowUs;
        break;
    case DogMotionAction::kShakeBackLegs:
        state = DOG_STATE_SHAKE_BACK_LEGS;
        args.angle_offset = 0;
        busy_window_us = kStretchBusyWindowUs;
        break;
    case DogMotionAction::kRetractLegs:
        state = DOG_STATE_RETRACT_LEGS;
        break;
    case DogMotionAction::kStop:
    case DogMotionAction::kHome:
        return DogMotionResult::kSendFailed;
    default:
        return DogMotionResult::kSendFailed;
    }

    const DogMotionResult result = SendMotion(state, &args);
    if (result == DogMotionResult::kOk) {
        busy_until_us_ = now_us + busy_window_us;
    }
    return result;
}
