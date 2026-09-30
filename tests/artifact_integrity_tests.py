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


if __name__ == '__main__':
    unittest.main()
