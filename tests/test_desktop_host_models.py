"""Compile and exercise the backend-independent Mac host adapters."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from c_build import compile_model

ROOT = Path(__file__).resolve().parents[1]


class DesktopHostModels(unittest.TestCase):
    def test_models(self):
        with tempfile.TemporaryDirectory() as directory:
            for module in ('refresh', 'audio_rate', 'input', 'rewind', 'crt'):
                with self.subTest(module=module):
                    executable = Path(directory) / (module + ('.exe' if os.name=='nt' else ''))
                    compile_model([ROOT/f'tests/test_desktop_{module}.c',
                                   ROOT/f'runner/desktop_{module}.c'],[ROOT/'runner'],executable)
                    result = subprocess.run([str(executable)], check=True,
                                            capture_output=True, text=True)
                    self.assertIn('ok' if module == 'crt' else 'passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
