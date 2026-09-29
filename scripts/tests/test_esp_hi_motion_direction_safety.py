#!/usr/bin/env python3

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "patches" / "espfriends-servo-dog-ctrl-0.2.0-motion-direction-safety.patch"
SEND_PATCH = ROOT / "patches" / "espfriends-servo-dog-ctrl-0.2.0-motion-send-safety.patch"
WEB_STOP_PATCH = ROOT / "patches" / "espfriends-servo-dog-ctrl-0.2.0-motion-web-stop-safety.patch"
QUEUE_PATCH = ROOT / "patches" / "espfriends-servo-dog-ctrl-0.2.0-motion-queue-safety.patch"
BUILD_SCRIPT = ROOT / "scripts" / "build.py"


class EspHiMotionDirectionSafetyPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text(encoding="utf-8")
        cls.send_patch = SEND_PATCH.read_text(encoding="utf-8")
        cls.web_stop_patch = WEB_STOP_PATCH.read_text(encoding="utf-8")
        cls.queue_patch = QUEUE_PATCH.read_text(encoding="utf-8")
        cls.build_script = BUILD_SCRIPT.read_text(encoding="utf-8")

    def test_observed_front_back_actions_are_remapped(self):
        self.assertIn("X(DOG_STATE_FORWARD, servo_dog_backward", self.patch)
        self.assertIn("X(DOG_STATE_BACKWARD, servo_dog_forward", self.patch)
        self.assertIn("X(DOG_STATE_BOW, servo_dog_lean_back", self.patch)
        self.assertIn("X(DOG_STATE_LEAN_BACK, servo_dog_bow", self.patch)
        self.assertIn("X(DOG_STATE_JUMP_FORWARD, servo_dog_jump_backward", self.patch)
        self.assertIn("X(DOG_STATE_JUMP_BACKWARD, servo_dog_jump_forward", self.patch)

    def test_turn_trajectories_are_played_in_reverse(self):
        self.assertIn("for (int i = 39; i >= 0; i--)", self.patch)
        self.assertGreaterEqual(self.patch.count("for (int i = 39; i >= 0; i--)"), 4)
        self.assertIn("Play the same path in reverse", self.patch)

    def test_motion_queue_keeps_only_latest_command(self):
        self.assertIn("xQueueCreate(1, sizeof(servo_dog_action_msg_t))", self.queue_patch)
        self.assertIn("xQueueOverwrite(g_servo_dog->dog_action_queue, &msg)", self.send_patch)
        self.assertIn("state < 0 || state >= DOG_STATE_MAX", self.send_patch)
        self.assertIn("args->repeat_count != NOT_USE", self.send_patch)

    def test_web_joystick_sends_stop_in_both_locales(self):
        self.assertEqual(self.patch.count("const SEND_INTERVAL = 1500"), 2)
        self.assertEqual(self.patch.count("this.sendMove('S')"), 2)
        self.assertGreaterEqual(self.patch.count("stopMove()"), 2)
        self.assertIn("case 'S':", self.web_stop_patch)
        self.assertIn("servo_dog_ctrl_send(DOG_STATE_IDLE, NULL)", self.web_stop_patch)

    def test_canonical_builder_applies_direction_patch(self):
        self.assertIn(PATCH.name, self.build_script)
        self.assertIn(SEND_PATCH.name, self.build_script)
        self.assertIn(WEB_STOP_PATCH.name, self.build_script)
        self.assertIn(QUEUE_PATCH.name, self.build_script)


if __name__ == "__main__":
    unittest.main()
