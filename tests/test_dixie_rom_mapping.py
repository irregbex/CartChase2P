"""Synthetic ROM-free test of the expanded variant's real cartridge mapper."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DixieRomMappingTests(unittest.TestCase):
    def test_expanded_data_banks_preserve_stock_aliases(self):
        src = ROOT / 'snesrecomp/runner/src'
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / ('mapper.exe' if os.name == 'nt' else 'mapper')
            sources = [ROOT/'tests/dixie_rom_mapping_test.c']
            if os.name == 'nt' and shutil.which('cl'):
                command = ['cl', '/nologo', '/std:c11', '/O1', '/W4', '/WX',
                           '/D_CRT_SECURE_NO_WARNINGS', '/I'+str(src),
                           *map(str, sources), '/Fe:'+str(output)]
            else:
                compiler = shutil.which(os.environ.get('CC', 'cc'))
                if compiler is None:
                    self.skipTest('a C compiler is required')
                command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                           '-I'+str(src), *map(str, sources), '-o', str(output)]
            built = subprocess.run(command, cwd=directory, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            subprocess.run([str(output)], check=True)
