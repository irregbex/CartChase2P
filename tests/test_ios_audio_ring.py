"""The real-time audio handoff must preserve order and silence underruns."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from c_build import compile_model

ROOT = Path(__file__).resolve().parents[1]


class IOSAudioRingTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "Apple/Clang C11 atomic audio adapter")
    def test_stereo_overflow_underrun_and_reset(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "audio-ring"
            compile_model([ROOT / "tests/test_ios_audio_ring.c",
                           ROOT / "runner/ios/ios_audio_ring.c"],
                          [ROOT / "runner/ios"], executable)
            subprocess.run([str(executable)], check=True, capture_output=True)
