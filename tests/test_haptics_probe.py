"""Compile the real stomp detector for both cartridge variants."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class HapticsProbeTests(unittest.TestCase):
    def test_stock_and_dixie_stomps_reject_other_impulses(self):
        for dixie in (False,True):
            with self.subTest(dixie=dixie), tempfile.TemporaryDirectory() as temp:
                exe=Path(temp)/('probe.exe' if os.name=='nt' else 'probe')
                sources=[str(ROOT/'tests/haptics_probe_test.c'),str(ROOT/'runner/dkc1_haptics.c')]
                if os.name=='nt' and shutil.which('cl'):
                    cmd=['cl','/nologo','/std:c11','/W4','/WX','/I'+str(ROOT/'runner'),
                         *(['/DDKC1_DIXIE_VARIANT'] if dixie else []),*sources,'/Fe:'+str(exe)]
                else:
                    cc=shutil.which(os.environ.get('CC','cc'))
                    if not cc:self.skipTest('C compiler required')
                    cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'runner'),
                         *(['-DDKC1_DIXIE_VARIANT'] if dixie else []),*sources,'-o',str(exe)]
                p=subprocess.run(cmd,cwd=temp,capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                subprocess.run([str(exe)],check=True)
