#!/usr/bin/env python3
"""Create a signed, profile-free iOS IPA for recipient-side re-signing.

The normal provisioned device build stays untouched. Only explicit application
resources enter the public package; profiles, device IDs and private inputs do
not. This is a GitHub sideload artifact, not an App Store/ad hoc export.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = {
    'DKC1Recomp', 'Info.plist', 'PkgInfo', 'Assets.car', 'ios_upscaling.txt',
    'AppIcon60x60@2x.png', 'AppIcon76x76@2x~ipad.png',
    'Licenses/LICENSE', 'Licenses/THIRD_PARTY_NOTICES.md',
    'Licenses/snesrecomp-LICENSE', 'Licenses/snesrecomp-THIRD_PARTY_ATTRIBUTION.md',
}
PRIVATE_SIGNING = {'embedded.mobileprovision', '_CodeSignature/CodeResources'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def text_section(data):
    """Hash executable instructions independently of changed signing metadata."""
    if data[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError('Expected a thin arm64 Mach-O')
    _, cpu, _, _, count, _, _, _ = struct.unpack_from('<8I', data)
    if cpu != 0x0100000C:
        raise ValueError('Expected arm64')
    offset = 32
    for _ in range(count):
        cmd, size = struct.unpack_from('<II', data, offset)
        if size < 8 or offset+size > len(data):
            raise ValueError('Invalid load command')
        if cmd == 0x19:  # LC_SEGMENT_64
            section_count = struct.unpack_from('<I', data, offset+64)[0]
            for index in range(section_count):
                section = offset+72+index*80
                name, segment, _, length, start = struct.unpack_from('<16s16sQQI', data, section)
                if name.rstrip(b'\0') == b'__text' and segment.rstrip(b'\0') == b'__TEXT':
                    return sha(data[start:start+length])
        offset += size
    raise ValueError('Missing executable text section')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--identity', required=True, help='Installed Apple Distribution identity or SHA-1')
    args = parser.parse_args()
    version = args.version.removeprefix('v')
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        parser.error('version must be major.minor.patch')
    if command('git', 'status', '--porcelain'):
        parser.error('commit the source before packaging')
    app = args.app.resolve()
    info = plistlib.loads((app/'Info.plist').read_bytes())
    if info.get('CFBundleShortVersionString') != version or info.get('CFBundleVersion') != version:
        parser.error('bundle version does not match the requested release')
    commit = command('git', 'rev-parse', 'HEAD')
    original = (app/'DKC1Recomp').read_bytes()
    if command('git', 'rev-parse', '--short', 'HEAD').encode() not in original:
        parser.error('rebuild the app from the committed source before packaging')
    if b'DKC1_IOS_QA_UPSCALER' in original or b'--verify-graphics' in original:
        parser.error('diagnostic app cannot be released')
    files = {str(p.relative_to(app)) for p in app.rglob('*') if p.is_file()}
    if files - RESOURCES - PRIVATE_SIGNING or RESOURCES - files:
        parser.error(f'unexpected/missing bundle resources: {sorted(files-RESOURCES-PRIVATE_SIGNING)} / {sorted(RESOURCES-files)}')
    if (app/'ios_upscaling.txt').read_bytes() != (ROOT/'runner/macos_graphics.metal').read_bytes():
        parser.error('bundled shader differs from source')
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    entitlement_data = subprocess.check_output(['codesign', '-d', '--entitlements', ':-', str(app)], stderr=subprocess.DEVNULL)
    entitlements = plistlib.loads(entitlement_data)
    # The public signature identifies the publisher. Recipients replace it and
    # supply their own provisioning profile through their sideloading tool.
    entitlements['get-task-allow'] = False
    args.out.mkdir(parents=True, exist_ok=True)
    stem = f'DKC1Recomp-v{version}-iOS-arm64-sideload'
    archive = args.out/(stem+'.ipa')
    if archive.exists():
        parser.error(f'output already exists: {archive}')
    with tempfile.TemporaryDirectory(prefix='dkc1-ios-package-') as temporary:
        staging = Path(temporary)
        signed = staging/'Payload/DKC1Recomp.app'
        for filename in sorted(RESOURCES):
            source, destination = app/filename, signed/filename
            if source.is_symlink():
                raise ValueError(f'Unexpected symlink: {filename}')
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        entitlement_file = staging/'entitlements.plist'
        entitlement_file.write_bytes(plistlib.dumps(entitlements))
        subprocess.run(['codesign', '--force', '--sign', args.identity, '--timestamp',
                        '--generate-entitlement-der', '--entitlements', str(entitlement_file), str(signed)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(signed)], check=True)
        signature = subprocess.run(['codesign', '-dvv', str(signed)], check=True, capture_output=True, text=True).stderr
        if 'Authority=Apple Distribution:' not in signature:
            raise ValueError('Release must use an Apple Distribution certificate')
        public_binary = (signed/'DKC1Recomp').read_bytes()
        code_hash = text_section(original)
        if code_hash != text_section(public_binary):
            raise ValueError('Re-signing changed executable instructions')
        public_files = {str(p.relative_to(signed)): p.read_bytes() for p in signed.rglob('*') if p.is_file()}
        if set(public_files) != RESOURCES | {'_CodeSignature/CodeResources'}:
            raise ValueError('Unexpected signed payload')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zip_file:
            for filename in sorted(public_files):
                zip_file.write(signed/filename, 'Payload/DKC1Recomp.app/'+filename)
        with zipfile.ZipFile(archive) as zip_file:
            if zip_file.testzip() is not None:
                raise ValueError('Archive CRC failure')
            for filename, data in public_files.items():
                if zip_file.read('Payload/DKC1Recomp.app/'+filename) != data:
                    raise ValueError(f'Archive mismatch: {filename}')
            # Verify the actual extracted artifact, not only its staging input.
            extracted = staging/'extracted'
            zip_file.extractall(extracted)
        subprocess.run(['codesign', '--verify', '--deep', '--strict',
                        str(extracted/'Payload/DKC1Recomp.app')], check=True)
        digest = sha(archive.read_bytes())
        metadata = {
            'version': 'v'+version, 'commit': commit,
            'engine_commit': command('git', '-C', 'snesrecomp', 'rev-parse', 'HEAD'),
            'bundle_id': info['CFBundleIdentifier'], 'minimum_ios': info['MinimumOSVersion'],
            'architecture': 'arm64', 'diagnostics': False,
            'signing': 'Apple Distribution; profile-free; recipient must re-sign for their device',
            'provisioned_device_build_binary_sha256': sha(original),
            'public_binary_sha256': sha(public_binary), 'executable_text_sha256': code_hash,
            'ipa_sha256': digest,
            'files': {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in sorted(public_files.items())},
        }
        (args.out/(stem+'.BUILDINFO.json')).write_text(json.dumps(metadata, indent=2)+'\n')
        (args.out/(stem+'.ipa.sha256')).write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    print(json.dumps({'archive': str(archive), 'sha256': digest, 'commit': commit}, indent=2))


if __name__ == '__main__':
    main()
