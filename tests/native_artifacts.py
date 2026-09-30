"""Verify target-native artifacts and safe release startup without credentials/network."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import tarfile
import tempfile
import zipfile


def output(*args):
    return subprocess.check_output(list(args), text=True, encoding='utf-8', errors='strict')


def pe_imports(binary):
    data = binary.read_bytes()
    assert data[:2] == b'MZ', 'missing PE DOS header'
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0'
    machine, sections = struct.unpack_from('<HH', data, pe+4)
    assert machine == 0x8664, 'Windows artifact must be x64'
    optional = pe + 24
    assert struct.unpack_from('<H', data, optional)[0] == 0x20b, 'PE32+ required'
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    table = optional + optional_size
    def offset(rva):
        for index in range(sections):
            section = table + index * 40
            virtual_size, virtual, size, raw = struct.unpack_from('<IIII', data, section+8)
            if virtual <= rva < virtual + max(size, virtual_size):
                return raw + rva - virtual
        raise AssertionError('invalid PE RVA')
    imports_rva = struct.unpack_from('<I', data, optional+120)[0]
    if not imports_rva: return []
    cursor = offset(imports_rva)
    result = []
    while any(data[cursor:cursor+20]):
        name = offset(struct.unpack_from('<I', data, cursor+12)[0])
        result.append(data[name:data.index(b'\0', name)].decode('ascii').lower())
        cursor += 20
    return result


def inspect(binary, target):
    if target == 'macos-arm64':
        assert output('lipo', '-archs', str(binary)).strip() == 'arm64'
        for line in output('otool', '-L', str(binary)).splitlines()[1:]:
            library = line.strip().split(' (')[0]
            assert library.startswith(('/usr/lib/', '/System/Library/')), (binary, library)
        assert re.search(r'minos\s+14\.0\b', output('otool', '-l', str(binary)))
        subprocess.run(['codesign', '--verify', str(binary)], check=True)
    elif target == 'windows-x64':
        system = {'kernel32.dll','advapi32.dll','bcrypt.dll','crypt32.dll','normaliz.dll','secur32.dll','shell32.dll','user32.dll','ws2_32.dll','wldap32.dll','iphlpapi.dll','ntdll.dll','ole32.dll','ucrtbase.dll','msvcrt.dll','version.dll','comdlg32.dll','gdi32.dll','winmm.dll'}
        for dll in pe_imports(binary):
            assert dll in system or dll.startswith(('api-ms-win-', 'ext-ms-win-')), (binary, 'non-system DLL import', dll)
    elif target == 'linux-x64':
        data = binary.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data,18)[0] == 62, 'ELF x86_64 required'
        dynamic = output('readelf', '-d', str(binary))
        allowed = {'libc.so.6','libm.so.6','libgcc_s.so.1','libstdc++.so.6','libpthread.so.0','libdl.so.2','librt.so.1','ld-linux-x86-64.so.2'}
        needed = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic)
        assert all(lib in allowed for lib in needed), ('unexpected dynamic dependency', needed)
        assert not re.search(r'\((?:RPATH|RUNPATH)\)', dynamic), 'host-dependent search path'
        versions = re.findall(r'GLIBC_(\d+)\.(\d+)', output('readelf', '--version-info', str(binary)))
        assert all((int(major), int(minor)) <= (2,35) for major,minor in versions), 'requires glibc newer than Ubuntu 22.04'
    else:
        raise AssertionError('unknown target')
    if target != 'windows-x64': assert os.access(binary, os.X_OK)


def smoke(root, target):
    suffix = '.exe' if target == 'windows-x64' else ''
    for name in ('main','setup'): inspect(root/(name+suffix),target)
    with tempfile.TemporaryDirectory(prefix='itu-smoke-ü-') as empty:
        main = subprocess.run([str(root/('main'+suffix)), '--test','--dry-run','--local'], cwd=empty,capture_output=True,text=True,encoding='utf-8',timeout=5)
        assert main.returncode == 1 and 'config.json not found' in main.stderr, main
        setup = subprocess.run([str(root/('setup'+suffix))],cwd=empty,input='',capture_output=True,text=True,encoding='utf-8',timeout=5)
        assert setup.returncode != 0 and 'terminal' in setup.stderr.lower(),setup


def archive(path, requested):
    with tempfile.TemporaryDirectory(prefix='itu-archive-ü-') as folder:
        destination = Path(folder)
        if zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as package:
                names = package.namelist()
                assert all(not (item.external_attr >> 16) & 0o170000 == 0o120000 for item in package.infolist()), 'archive symlink'
                validate_names(names)
                package.extractall(destination)
        else:
            with tarfile.open(path, 'r:gz') as package:
                members = package.getmembers()
                names = [item.name for item in members]
                validate_names(names)
                assert all(item.isfile() or item.isdir() for item in members), 'archive special entry'
                package.extractall(destination, filter='data')
        roots = list(destination.iterdir())
        assert len(roots) == 1 and roots[0].is_dir(), 'one archive root required'
        root = roots[0]
        manifest = json.loads((root/'build-manifest.json').read_text(encoding='utf-8'))
        target = manifest['target']
        if requested: assert requested == target
        assert all(key in manifest for key in ('version','compiler','dependencies','revision'))
        suffix = '.exe' if target == 'windows-x64' else ''
        actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
        mandatory = {'main'+suffix,'setup'+suffix,'data/example_config.json','README.md','build-manifest.json','SHA256SUMS'}
        assert mandatory <= actual, ('missing archive entries',mandatory-actual)
        extras = actual - mandatory
        assert extras and all(name.startswith('licenses/') for name in extras), ('archive allowlist',extras)
        checks = {}
        for line in (root/'SHA256SUMS').read_text(encoding='utf-8').splitlines():
            digest,relative=line.split('  ',1)
            assert relative not in checks, 'duplicate checksum'
            assert relative in actual and relative != 'SHA256SUMS', 'unexpected checksum entry'
            assert hashlib.sha256((root/relative).read_bytes()).hexdigest() == digest,relative
            checks[relative] = digest
        assert set(checks) == actual - {'SHA256SUMS'}, 'incomplete checksum coverage'
        smoke(root,target)


def validate_names(names):
    assert names and len(names) == len(set(names)), 'empty/duplicate archive paths'
    assert all(not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts and '\\' not in name and ':' not in name for name in names), 'unsafe archive path'
    assert len({PurePosixPath(name).parts[0] for name in names}) == 1


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('path',type=Path)
    parser.add_argument('--target',choices=('macos-arm64','windows-x64','linux-x64'))
    args=parser.parse_args()
    path=args.path.resolve()
    if path.is_dir():
        assert args.target, '--target required for binary directory'
        smoke(path,args.target)
    else: archive(path,args.target)
    print('Native architecture, runtime dependencies, archive contents/checksums and safe startup passed.')
