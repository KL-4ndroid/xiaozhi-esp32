import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
BOARD = ROOT / "main" / "boards" / "espressif" / "esp-hi"


class EspHiDogMotionGuardrailsTest(unittest.TestCase):
    def test_safe_action_allowlist_is_documented_and_parseable(self):
        header = (BOARD / "dog_motion.h").read_text(encoding="utf-8")
        board = (BOARD / "esp_hi.cc").read_text(encoding="utf-8")
        allowed = {
            "forward",
            "backward",
            "turn_left",
            "turn_right",
            "lay_down",
            "bow",
            "lean_back",
            "bow_lean",
            "sway_back_forth",
            "sway",
            "shake_hand",
            "shake_back_legs",
            "retract_legs",
            "stop",
            "home",
        }

        parsed = set(re.findall(r'if \(name == "([a-z_]+)"\)', header))
        self.assertEqual(parsed, allowed)
        for action in allowed:
            self.assertIn(action, board)

    def test_high_risk_actions_are_rejected(self):
        source = (BOARD / "dog_motion.cc").read_text(encoding="utf-8")
        for action in ("jump_forward", "jump_backward", "poke", "installation"):
            self.assertIn(f'!DogMotion::Parse("{action}")', source)

    def test_cycles_are_bounded_and_servo_parameters_are_not_exposed(self):
        board = (BOARD / "esp_hi.cc").read_text(encoding="utf-8")
        tool_start = board.index('"self.dog.action"')
        tool_end = board.index("// 灯光控制", tool_start)
        tool = board[tool_start:tool_end]

        self.assertIn('Property("cycles", kPropertyTypeInteger, 1, 1, 5)', tool)
        self.assertNotIn('Property("speed"', tool)
        self.assertNotIn('Property("angle"', tool)
        self.assertNotIn('Property("servo"', tool)


if __name__ == "__main__":
    unittest.main()
