"""Archive integrity checks run before any packaged executable is started."""
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import warnings
import zipfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from release_checksums import release_checksums

import native_artifacts


class ArchiveIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='itu-integrity-ü-')
        self.addCleanup(self.directory.cleanup)
        self.folder = Path(self.directory.name)
        self.smoke = self.enterContext(patch.object(native_artifacts, 'smoke'))

    def package(self, target='macos-arm64', mutate=None, extra_entries=()):
        suffix = '.exe' if target == 'windows-x64' else ''
        files = {
            'main' + suffix: b'opaque test executable',
            'setup' + suffix: b'opaque test executable',
            'README.md': b'fixture',
            'data/example_config.json': json.dumps({
                'time': {'lead_millisecond': 0}, 'courses': {'crn': [], 'scrn': []}
            }).encode(),
            'build-manifest.json': json.dumps({
                'version': '1.0.0', 'target': target, 'compiler': {},
                'dependencies': {}, 'revision': '0' * 40
            }).encode(),
            'licenses/nlohmann-json.txt': b'fixture license',
        }
        if target != 'macos-arm64':
            files.update({f'licenses/{name}.txt': b'fixture license' for name in ('curl', 'nghttp2', 'zlib')})
        if target == 'linux-x64': files['licenses/openssl.txt'] = b'fixture license'
        if mutate: mutate(files)
        files['SHA256SUMS'] = ''.join(
            f'{hashlib.sha256(content).hexdigest()}  {name}\n'
            for name, content in sorted(files.items())
        ).encode()
        root = f'itu-ders-bot-1.0.0-{target}'
        entries = [(root + '/' + name, content, False) for name, content in files.items()]
        entries.extend(extra_entries)
        archive = self.folder / (root + ('.zip' if suffix else '.tar.gz'))
        if suffix:
            with warnings.catch_warnings(), zipfile.ZipFile(archive, 'w') as package:
                warnings.simplefilter('ignore', UserWarning)
                for name, content, symlink in entries:
                    info = zipfile.ZipInfo(name)
                    info.create_system = 3
                    info.external_attr = (0o120777 if symlink else 0o100644) << 16
                    package.writestr(info, content)
        else:
            with tarfile.open(archive, 'w:gz') as package:
                for name, content, symlink in entries:
                    info = tarfile.TarInfo(name)
                    if symlink:
                        info.type = tarfile.SYMTYPE
                        info.linkname = content.decode()
                        package.addfile(info)
                    else:
                        info.size = len(content)
                        package.addfile(info, io.BytesIO(content))
        self.outer_checksum(archive)
        return archive

    def outer_checksum(self, archive):
        archive.with_name(archive.name + '.sha256').write_text(
            f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n', encoding='utf-8'
        )

    def rejected(self, archive, message, target=None):
        with self.assertRaisesRegex(AssertionError, message):
            native_artifacts.archive(archive, target)
        self.smoke.assert_not_called()

    def test_valid_archives_reach_native_smoke_for_each_target(self):
        for target in ('macos-arm64', 'windows-x64', 'linux-x64'):
            with self.subTest(target=target):
                native_artifacts.archive(self.package(target), target)
                self.assertEqual(self.smoke.call_args.args[1], target)
                self.smoke.reset_mock()

    def test_outer_checksum_rejects_tampering(self):
        archive = self.package()
        with archive.open('ab') as output: output.write(b'tampered')
        self.rejected(archive, 'outer archive checksum mismatch')

    def test_outer_checksum_requires_matching_filename(self):
        archive = self.package()
        checksum = archive.with_name(archive.name + '.sha256')
        checksum.write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  other.tar.gz\n')
        self.rejected(archive, 'invalid outer checksum entry')

    def test_personal_courses_and_positive_lead_are_rejected(self):
        for target in ('macos-arm64', 'windows-x64'):
            for courses, lead, message in (({'crn': ['001'], 'scrn': []}, 0, 'courses must be empty'),
                                           ({'crn': [], 'scrn': []}, 1, 'lead must be zero')):
                with self.subTest(target=target, message=message):
                    def mutate(files):
                        files['data/example_config.json'] = json.dumps({
                            'time': {'lead_millisecond': lead}, 'courses': courses
                        }).encode()
                    self.rejected(self.package(target, mutate), message)

    def test_unexpected_files_are_rejected_even_with_valid_checksums(self):
        for name in ('.env', 'data/config.json', 'licenses/unlisted.txt'):
            with self.subTest(name=name):
                self.rejected(self.package(mutate=lambda files: files.update({name: b'synthetic'})), 'archive allowlist')

    def test_required_license_cannot_be_omitted(self):
        self.rejected(self.package('linux-x64', lambda files: files.pop('licenses/openssl.txt')), 'archive allowlist')

    def test_target_mismatch_is_rejected(self):
        self.rejected(self.package(), '', 'linux-x64')

    def test_unsafe_and_duplicate_paths_are_rejected_before_extraction(self):
        for target in ('macos-arm64', 'windows-x64'):
            root = f'itu-ders-bot-1.0.0-{target}'
            for name, message in ((root + '/../../escape', 'unsafe archive path'),
                                  (root + '/C:escape', 'unsafe archive path'),
                                  (root + '/README.md', 'duplicate archive paths')):
                with self.subTest(target=target, name=name):
                    self.rejected(self.package(target, extra_entries=[(name, b'synthetic', False)]), message)
            self.rejected(self.package(target, extra_entries=[(root + '/link', b'README.md', True)]),
                          'archive (?:symlink|special entry)')

    def test_release_checksums_accepts_consistent_archives(self):
        for target in ('windows-x64', 'macos-arm64', 'linux-x64'):
            self.package(target)
        release_checksums(self.folder, 'v1.0.0')
        self.assertTrue((self.folder / 'SHA256SUMS').is_file())

    def test_release_checksums_rejects_mismatched_revisions(self):
        self.package('windows-x64', mutate=lambda f: f.update({
            'build-manifest.json': json.dumps({
                'version': '1.0.0', 'target': 'windows-x64', 'compiler': {},
                'dependencies': {}, 'revision': '1' * 40
            }).encode()
        }))
        self.package('macos-arm64')
        self.package('linux-x64')
        with self.assertRaisesRegex(SystemExit, 'Archive revision mismatch'):
            release_checksums(self.folder, 'v1.0.0')

    def test_release_checksums_rejects_dirty_manifest(self):
        for target in ('windows-x64', 'macos-arm64'):
            self.package(target)
        self.package('linux-x64', mutate=lambda f: f.update({
            'build-manifest.json': json.dumps({
                'version': '1.0.0', 'target': 'linux-x64', 'compiler': {},
                'dependencies': {}, 'revision': '0' * 40, 'dirty': True
            }).encode()
        }))
        with self.assertRaisesRegex(SystemExit, 'dirty working tree'):
            release_checksums(self.folder, 'v1.0.0')

    def test_release_checksums_rejects_invalid_revision(self):
        for bad_rev in ('unknown', '123', 'g' * 40):
            with self.subTest(bad_rev=bad_rev):
                for target in ('windows-x64', 'macos-arm64'):
                    self.package(target)
                self.package('linux-x64', mutate=lambda f: f.update({
                    'build-manifest.json': json.dumps({
                        'version': '1.0.0', 'target': 'linux-x64', 'compiler': {},
                        'dependencies': {}, 'revision': bad_rev
                    }).encode()
                }))
                with self.assertRaisesRegex(SystemExit, 'Invalid or unknown revision in manifest'):
                    release_checksums(self.folder, 'v1.0.0')

    def test_release_checksums_rejects_missing_checksum_file(self):
        for target in ('windows-x64', 'macos-arm64', 'linux-x64'):
            self.package(target)
        (self.folder / 'itu-ders-bot-1.0.0-linux-x64.tar.gz.sha256').unlink()
        with self.assertRaisesRegex(SystemExit, 'Missing checksum file'):
            release_checksums(self.folder, 'v1.0.0')

    def test_release_checksums_accepts_sha256_git_revisions(self):
        rev = 'a' * 64
        for target in ('windows-x64', 'macos-arm64', 'linux-x64'):
            self.package(target, mutate=lambda f: f.update({
                'build-manifest.json': json.dumps({
                    'version': '1.0.0', 'target': target, 'compiler': {},
                    'dependencies': {}, 'revision': rev
                }).encode()
            }))
        release_checksums(self.folder, 'v1.0.0')
        self.assertTrue((self.folder / 'SHA256SUMS').is_file())

    def test_linux_abi_checker_rejects_newer_glibcxx_and_cxxabi(self):
        fake_elf = self.folder / 'fake.elf'
        fake_elf.write_bytes(b'\x7fELF\x02\x01' + b'\x00' * 12 + (62).to_bytes(2, 'little') + b'\x00' * 100)
        with patch.object(native_artifacts, 'output') as mock_output:
            mock_output.side_effect = lambda *cmd: (
                '(NEEDED) [libc.so.6]\n(NEEDED) [libstdc++.so.6]' if cmd[1] == '-d'
                else 'GLIBC_2.35\nGLIBCXX_3.4.32\nCXXABI_1.3.13'
            )
            with self.assertRaisesRegex(AssertionError, 'requires libstdc\\+\\+ newer than Ubuntu 22.04: GLIBCXX_3.4.32'):
                native_artifacts.inspect(fake_elf, 'linux-x64')

            mock_output.side_effect = lambda *cmd: (
                '(NEEDED) [libc.so.6]\n(NEEDED) [libstdc++.so.6]' if cmd[1] == '-d'
                else 'GLIBC_2.35\nGLIBCXX_3.4.30\nCXXABI_1.3.15'
            )
            with self.assertRaisesRegex(AssertionError, 'requires CXXABI newer than Ubuntu 22.04: CXXABI_1.3.15'):
                native_artifacts.inspect(fake_elf, 'linux-x64')


if __name__ == '__main__':
    unittest.main()
