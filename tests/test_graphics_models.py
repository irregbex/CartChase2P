"""Graphics preferences and color grading never mutate the guest's raw frame."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from c_build import compile_model
ROOT = Path(__file__).resolve().parents[1]
class GraphicsModels(unittest.TestCase):
    def test_settings_and_present_only_color(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / ('graphics.exe' if os.name=='nt' else 'graphics')
            compile_model([ROOT/p for p in ['tests/test_desktop_graphics.c',
                  'runner/desktop_graphics.c','runner/desktop_crt.c','runner/desktop_filter.c',
                  'snesrecomp/runner/src/snes/color_lut.c']],
                  [ROOT/'runner',ROOT/'snesrecomp/runner/src'],exe)
            subprocess.run([str(exe)],check=True,capture_output=True)
