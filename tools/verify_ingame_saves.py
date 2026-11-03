#!/usr/bin/env python3
"""Prove a cartridge save survives desktop exit and a cold boot without a state.

Supply an immutable pre-entry state, fixed save inputs, and an input/wait-only
restart route. Headless execution supplies the guest-state oracle; the desktop
uses isolated user directories. No ROM, state, or SRAM is written to the repo's
normal user slots. Output must be a new directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def digest(data):
    return hashlib.sha256(data).hexdigest()


def snapshot_sram(path):
    data = path.read_bytes()
    if struct.unpack_from('<II', data) != (0x52544c53, 9):
        raise ValueError('Only native snapshot v9 is supported')
    marker = b'PPI0' + struct.pack('<I', 38)
    if data.count(marker) != 1:
        raise ValueError('Missing or ambiguous PPU internal-state block')
    # snes_saveload: PPU internal block is followed by 2048-byte cart RAM.
    offset = data.index(marker) + 46
    ram = data[offset:offset + 2048]
    if len(ram) != 2048:
        raise ValueError('Truncated cartridge RAM')
    return ram


def comparable_state(path, cold_start=False):
    # Only the host audio consumer cursor is non-deterministic across SDL and
    # headless playback. No guest CPU, WRAM, PPU, SPC, DSP synthesis or SRAM
    # bytes are excluded. tests/ingame_snapshot_layout.c verifies this offset
    # against the pinned engine's actual C structure layouts and save order.
    snapshot_sram(path)
    data = path.read_bytes()
    if data.index(b'PPI0' + struct.pack('<I', 38)) + 46 != 166213:
        raise ValueError('Snapshot layout changed; re-verify audio cursor offset')
    # SDL's unthrottled startup route also resets the host PCM output queue
    # before opening audio. The DSP synthesis state is before this region.
    start = 0x103bc if cold_start else 0x183c0
    return data[:start] + bytes(0x183c4 - start) + data[0x183c4:]


def environment(directory, **options):
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('DKC1_', 'SNESRECOMP_'))}
    env.update(DKC1_USER_DIR=str(directory), DKC1_WIDESCREEN='0',
               DKC1_ASPECT='4:3', DKC1_HAPTICS='0', DKC1_MSU1_DISABLE='1')
    env.update({k: str(v) for k, v in options.items()})
    return env


def run(executable, rom, directory, frames=None, **options):
    directory.mkdir(parents=True, exist_ok=True)
    args = [str(executable), str(rom)]
    if frames is not None:
        args.append(str(frames))
    with (directory / 'stdout.log').open('wb') as stdout, \
            (directory / 'stderr.log').open('wb') as stderr:
        completed = subprocess.run(args, cwd=directory,
                                   env=environment(directory, **options),
                                   stdout=stdout, stderr=stderr, timeout=90,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    if completed.returncode:
        raise RuntimeError(f'{directory}: runner exited {completed.returncode}; see stderr.log')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('rom', 'exe', 'headless', 'entry-state', 'input', 'restart-script', 'out'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--frames', required=True, type=int)
    args = parser.parse_args()
    for name in ('rom', 'exe', 'headless', 'entry_state', 'input', 'restart_script'):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    rom_hash = digest(args.rom.read_bytes())
    if rom_hash != 'fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15':
        raise ValueError('Unsupported ROM checksum')
    if not 0 < args.frames <= 30000:
        raise ValueError('Save sequence must contain 1..30000 frames')
    oracle_dir = args.out / 'save-oracle'
    oracle = oracle_dir / 'final.state'
    run(args.headless, args.rom, oracle_dir, args.frames,
        DKC1_SAVESTATE_INPUT=args.entry_state, SNESRECOMP_INPUT_PLAY=args.input,
        DKC1_SAVESTATE_OUTPUT=oracle, DKC1_FRAME_PPM=oracle_dir / 'final.ppm')
    expected = snapshot_sram(oracle)
    if not any(expected) or expected == snapshot_sram(args.entry_state):
        raise ValueError('Save route did not change SRAM; no cartridge-save evidence')
    expected_path = args.out / 'expected.srm'
    expected_path.write_bytes(expected)
    restart_dir = args.out / 'restart-oracle'
    restart_dir.mkdir()
    restart_script = restart_dir / 'with-final-frame.dks'
    restart_script.write_text(args.restart_script.read_text() + '\n0 * 1\n')
    restart_oracle = restart_dir / 'final.state'
    run(args.headless, args.rom, restart_dir, 30000,
        DKC1_SRAM_INPUT=expected_path, DKC1_SCRIPT=restart_script,
        DKC1_SAVESTATE_OUTPUT=restart_oracle, DKC1_FRAME_PPM=restart_dir / 'final.ppm')
    neutral = args.out / 'neutral.input'
    neutral.write_text('0 * 1\n')
    repeats = []
    for index in range(1, 4):
        directory = args.out / f'repeat-{index}'
        saved = directory / 'saved.state'
        run(args.exe, args.rom, directory,
            DKC1_SMOKE_TEST_HIDDEN=1, DKC1_SMOKE_TEST_FRAMES=args.frames,
            DKC1_SAVESTATE_INPUT=args.entry_state, SNESRECOMP_INPUT_PLAY=args.input,
            DKC1_SAVESTATE_OUTPUT=saved)
        battery = directory / 'saves/save.srm'
        if battery.read_bytes() != expected or snapshot_sram(saved) != expected:
            raise ValueError(f'Repeat {index}: disk/guest SRAM differs from cartridge oracle')
        if comparable_state(saved) != comparable_state(oracle):
            raise ValueError(f'Repeat {index}: full saved machine state differs from headless oracle')
        save_log = (directory / 'stderr.log').read_text(errors='replace')
        if not 0 <= save_log.find('[sram] saved') < save_log.find('[smoke] complete'):
            raise ValueError('Battery save was not flushed during play before shutdown')
        (directory / 'save-stderr.log').write_bytes((directory / 'stderr.log').read_bytes())
        resumed = directory / 'resumed.state'
        # Deliberately no DKC1_SAVESTATE_INPUT. The game can only find disk SRAM.
        run(args.exe, args.rom, directory,
            DKC1_SMOKE_TEST_HIDDEN=1, DKC1_SMOKE_TEST_FRAMES=1,
            DKC1_STARTUP_SCRIPT=args.restart_script, SNESRECOMP_INPUT_PLAY=neutral,
            DKC1_SAVESTATE_OUTPUT=resumed)
        if comparable_state(resumed, True) != comparable_state(restart_oracle, True):
            raise ValueError(f'Repeat {index}: cold restart differs from explicit SRAM import oracle')
        log = (directory / 'stderr.log').read_text(errors='replace')
        if '[sram] loaded saves/save.srm (2048 bytes)' not in log:
            raise ValueError('Desktop did not acknowledge loading battery RAM')
        repeats.append(dict(repeat=index, saved_state_sha256=digest(saved.read_bytes()),
                            resumed_state_sha256=digest(resumed.read_bytes()),
                            comparable_saved_sha256=digest(comparable_state(saved)),
                            comparable_resumed_sha256=digest(comparable_state(resumed, True)),
                            sram_sha256=digest(expected), passed=True))
        print(f'Repeat {index}: cartridge save, disk bytes and cold restart match', flush=True)
    report = dict(schema='dkc1.ingame-save-verification.v1', passed=True,
                  rom_sha256=rom_hash, executable_sha256=digest(args.exe.read_bytes()),
                  headless_sha256=digest(args.headless.read_bytes()),
                  entry_state_sha256=digest(args.entry_state.read_bytes()),
                  input_sha256=digest(args.input.read_bytes()), frames=args.frames,
                  restart_script_sha256=digest(args.restart_script.read_bytes()),
                  comparison_excludes=[dict(field='Dsp.sampleRead', offset=0x183c0, size=4,
                                             leg='save', reason='host audio consumption cursor'),
                                       dict(field='Dsp.sampleBuffer/sampleWrite/sampleRead',
                                            offset=0x103bc, size=0x8008, leg='cold restart',
                                            reason='startup resets host PCM output queue')],
                  results=repeats)
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {args.out / "report.json"}')


if __name__ == '__main__':
    main()
