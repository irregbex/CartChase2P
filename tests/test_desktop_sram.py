"""Exercise real filesystem persistence in an isolated, disposable directory."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from c_build import compile_model

ROOT = Path(__file__).resolve().parents[1]


class DesktopSramTests(unittest.TestCase):
    def test_verifier_audio_cursor_layout_matches_engine(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / ('layout.exe' if os.name == 'nt' else 'layout')
            compile_model([ROOT / 'tests/ingame_snapshot_layout.c'],
                          [ROOT / 'snesrecomp/runner/src'], executable)
            subprocess.run([str(executable)], cwd=directory, check=True,
                           capture_output=True, text=True)

    def test_disk_roundtrip_and_failure_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / ('sram.exe' if os.name == 'nt' else 'sram')
            compile_model([ROOT / 'tests/test_desktop_sram.c',
                           ROOT / 'runner/desktop_sram.c'], [ROOT / 'runner'], executable)
            result = subprocess.run([str(executable)], cwd=directory, check=True,
                                    capture_output=True, text=True)
            self.assertIn('tests passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
