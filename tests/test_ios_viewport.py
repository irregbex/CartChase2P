"""Check real mobile viewport geometry at physical device resolutions."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from c_build import compile_model

ROOT = Path(__file__).resolve().parents[1]


class IOSViewportTests(unittest.TestCase):
    def test_native_proportions_and_phone_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "viewport"
            compile_model([ROOT / "tests/test_ios_viewport.c",
                           ROOT / "runner/ios/ios_viewport.c"],
                          [ROOT / "runner/ios", ROOT / "runner"], executable)
            subprocess.run([str(executable)], check=True, capture_output=True)
