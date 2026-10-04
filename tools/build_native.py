"""Build/test the Windows x64 native module; never install or start the game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src'
BUILD = ROOT / 'build'
MOD = ROOT / 'Payload/Game/Binaries/Win64/UE4SS/Mods/AC8MouseAim'


def msvc_environment(explicit):
    if not explicit and os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() == 'x64' and shutil.which('cl.exe'):
        return {key.upper(): value for key, value in os.environ.items()}
    candidates = [Path(explicit)] if explicit else []
    if not explicit:
        vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        if vswhere.is_file():
            location = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
                'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
            if location:
                candidates.append(Path(location) / 'Common7/Tools/VsDevCmd.bat')
        if os.environ.get('VSINSTALLDIR'):
            candidates.append(Path(os.environ['VSINSTALLDIR']) / 'Common7/Tools/VsDevCmd.bat')
    devcmd = next((p.resolve() for p in candidates if p.is_file()), None)
    if not devcmd:
        raise RuntimeError('MSVC x64 tools unavailable; use an x64 developer shell or --vs-dev-cmd PATH')
    if any(c in str(devcmd) for c in '\r\n"%!'):
        raise RuntimeError('Unsupported shell characters in VS tools path')
    command = f'cmd.exe /d /s /c ""{devcmd}" -no_logo -arch=x64 -host_arch=x64 >nul && set"'
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    env = {key.upper(): value for key, value in os.environ.items()}
    for line in result.stdout.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key.upper()] = value
    return env


def run(args, env):
    executable = shutil.which(str(args[0]), path=env.get('PATH'))
    if not executable:
        raise RuntimeError(f'Tool not found: {args[0]}')
    print('+', Path(executable).name, subprocess.list2cmdline([str(a) for a in args[1:]]), flush=True)
    subprocess.run([executable, *map(str, args[1:])], cwd=BUILD, env=env, check=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vs-dev-cmd', help='Path to Visual Studio Common7/Tools/VsDevCmd.bat')
    parser.add_argument('--test-only', action='store_true', help='Run tests without rebuilding/copying the DLL')
    parser.add_argument('--update-payload', action='store_true', help='Copy the successfully built DLL into tracked Payload')
    args = parser.parse_args()
    if args.test_only and args.update_payload:
        parser.error('--test-only and --update-payload cannot be combined')
    BUILD.mkdir(exist_ok=True)
    env = msvc_environment(args.vs_dev_cmd)
    cpp_flags = ['/nologo', '/std:c++20', '/EHsc', '/O2', '/MT', '/W4', '/utf-8', '/Brepro',
                 f'/I{SRC}', f'/I{SRC / "vendor/ue4ss"}', f'/I{SRC / "vendor/lua"}']
    run(['cl.exe', *cpp_flags, ROOT/'tests/native/controller_tests.cpp', '/Fe:controller_tests.exe'], env)
    run([BUILD/'controller_tests.exe'], env)
    from native_harness import generate
    generate(SRC/'mouse_aim.cpp', BUILD/'runtime_tests.cpp')
    run(['cl.exe', *cpp_flags, BUILD/'runtime_tests.cpp', '/Fe:runtime_tests.exe', 'user32.lib'], env)
    run([BUILD/'runtime_tests.exe', MOD/'config.ini', MOD/'presets/f14d-pw5.ini'], env)
    subprocess.run([sys.executable, str(ROOT/'tools/run_tests.py')], cwd=ROOT, check=True)
    if args.test_only:
        return
    version = re.search(r'AC8_MOUSE_AIM_VERSION "([0-9.]+-pw\.[0-9]+)"', (SRC/'version.h').read_text()).group(1)
    run(['lib.exe', '/nologo', '/machine:x64', f'/def:{SRC / "ue4ss_lua.def"}', '/out:ue4ss_lua.lib'], env)
    minhook = SRC/'vendor/minhook/src'
    run(['cl.exe', '/nologo', '/O2', '/MT', '/W3', '/Brepro', '/c', minhook/'buffer.c', minhook/'hook.c',
         minhook/'trampoline.c', minhook/'hde/hde64.c'], env)
    numbers = version.replace('-pw.', '.').split('.')
    resource = f'''#include <windows.h>
1 VERSIONINFO
FILEVERSION {','.join(numbers)}
PRODUCTVERSION {','.join(numbers)}
FILEFLAGSMASK 0x3fL
FILEFLAGS VS_FF_PRERELEASE
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_DLL
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "FileDescription", "AC8 Mouse Aim local PW fork (upstream 0.2.30)\\0"
   VALUE "FileVersion", "{version}\\0"
   VALUE "ProductVersion", "{version}\\0"
   VALUE "ProductName", "AC8 Mouse Aim local fork\\0"
   VALUE "OriginalFilename", "ac8_mouse_aim_010.dll\\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x409, 1200
 END
END
'''
    (BUILD/'version.rc').write_text(resource, encoding='ascii')
    run(['rc.exe', '/nologo', '/fo', 'version.res', 'version.rc'], env)
    run(['cl.exe', *cpp_flags, '/LD', SRC/'mouse_aim.cpp', '/Fe:ac8_mouse_aim_010.dll',
         '/link', '/INCREMENTAL:NO', '/Brepro', 'ue4ss_lua.lib', 'buffer.obj', 'hook.obj',
         'trampoline.obj', 'hde64.obj', 'version.res'], env)
    dll = BUILD/'ac8_mouse_aim_010.dll'
    if args.update_payload:
        shutil.copyfile(dll, MOD/'Scripts/ac8_mouse_aim_010.dll')
    git = shutil.which('git')
    revision = subprocess.check_output([git, 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() if git else None
    dirty = bool(subprocess.check_output([git, 'status', '--porcelain'], cwd=ROOT, text=True).strip()) if git else None
    sources = [p for p in SRC.rglob('*') if p.is_file()]
    sources += list((MOD/'Scripts').glob('*.lua')) + [MOD/'config.ini', MOD/'presets/f14d-pw5.ini']
    sources += [p for folder in ('tools', 'tests') for p in (ROOT/folder).rglob('*')
                if p.is_file() and p.suffix in ('.py', '.cpp', '.h')]
    sources += [ROOT/'requirements-dev.txt']
    manifest = {'version': version, 'bridge_version': 35, 'source_commit': revision, 'working_tree_dirty': dirty,
        'msvc': env.get('VCTOOLSVERSION'), 'windows_sdk': env.get('WINDOWSSDKVERSION'),
        'dll_sha256': sha(dll), 'payload_updated': args.update_payload,
        'source_sha256': {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(sources)}}
    (BUILD/'BUILD-MANIFEST.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(f'Built {version}: {dll}\nDLL SHA256: {sha(dll)}\nPayload updated: {args.update_payload}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Native build failed: {error}', file=sys.stderr)
        sys.exit(1)
