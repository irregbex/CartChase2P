"""Verify simultaneous run/jump and double-tap hold behavior."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from c_build import compile_model

ROOT = Path(__file__).resolve().parents[1]


class IOSTouchButtonTests(unittest.TestCase):
    def test_chords_double_tap_and_cancellation(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "touch-button"
            compile_model([ROOT / "tests/test_ios_touch_button.c",
                           ROOT / "runner/ios/ios_touch_button.c"],
                          [ROOT / "runner/ios"], executable)
            subprocess.run([str(executable)], check=True, capture_output=True)
