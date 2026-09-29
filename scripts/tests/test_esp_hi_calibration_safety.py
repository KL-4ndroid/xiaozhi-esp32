#!/usr/bin/env python3

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "patches" / "espfriends-servo-dog-ctrl-0.2.0-calibration-safety.patch"
BUILD_SCRIPT = ROOT / "scripts" / "build.py"


class EspHiCalibrationSafetyPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text(encoding="utf-8")
        cls.build_script = BUILD_SCRIPT.read_text(encoding="utf-8")

    def test_all_servo_commands_are_clamped_before_driver_call(self):
        self.assertIn("static void servo_set_angle(servo_id_t servo_id, int angle)", self.patch)
        self.assertIn("safe_angle = SERVO_MIN_ANGLE", self.patch)
        self.assertIn("safe_angle = SERVO_MAX_ANGLE", self.patch)
        self.assertIn("iot_servo_write_angle(g_servo_dog->servos[servo_id], safe_angle)", self.patch)

    def test_calibration_api_has_read_only_route_and_server_side_limits(self):
        self.assertIn('.uri = "/calibration"', self.patch)
        self.assertIn("return send_calibration_values(req);", self.patch)
        self.assertIn("Calibration mode is not active", self.patch)
        self.assertIn("forward_servo && leg_value >= 0", self.patch)
        self.assertIn("reverse_servo && leg_value >= -CALIBRATION_OFFSET_LIMIT", self.patch)

    def test_web_ui_uses_leg_specific_limits_in_both_locales(self):
        self.assertEqual(self.patch.count("const CALIBRATION_LIMITS = Object.freeze"), 2)
        self.assertEqual(self.patch.count("this.cfg = await sendRequest('/calibration')"), 2)
        self.assertEqual(self.patch.count("this.minus.disabled"), 2)
        self.assertEqual(self.patch.count("this.plus.disabled"), 2)

    def test_canonical_builder_applies_version_pinned_patch(self):
        self.assertIn("_apply_managed_component_patches()", self.build_script)
        self.assertIn('"0.2.0"', self.build_script)
        self.assertIn('"--recount"', self.build_script)
        self.assertIn('"--reverse", "--check"', self.build_script)


if __name__ == "__main__":
    unittest.main()
