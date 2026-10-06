"""Exercise the real coexist installer in disposable build/fixtures directories."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = ROOT/'Payload'
REL_MOD = Path('Game/Binaries/Win64/UE4SS/Mods/AC8MouseAim')
REL_CORE = Path('Game/Binaries/Win64/UE4SS/UE4SS.dll')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(root):
    return {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}


def install(game, success=True):
    result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', str(ROOT/'Install.ps1'), '-GamePath', str(game), '-NoPause'], capture_output=True)
    if (result.returncode == 0) != success:
        raise RuntimeError((result.stdout+result.stderr).decode('utf-8', errors='replace'))


def verify_mod(game, preserved=None):
    for path in (PAYLOAD/REL_MOD).rglob('*'):
        if path.is_file():
            rel = path.relative_to(PAYLOAD/REL_MOD)
            actual = game/REL_MOD/rel
            expected = preserved if rel.as_posix() == 'config.ini' and preserved is not None else path.read_bytes()
            assert actual.read_bytes() == expected, str(rel)


def main():
    manifest = json.loads((ROOT/'build/BUILD-MANIFEST.json').read_text(encoding='utf-8'))
    assert sha(PAYLOAD/REL_MOD/'Scripts/ac8_mouse_aim_010.dll') == manifest['dll_sha256'], 'Rebuild with --update-payload first'
    fixtures = ROOT/'build/fixtures'/uuid.uuid4().hex
    fixtures.mkdir(parents=True)
    # Keep fixtures under ignored build/ for inspection; never visit a real game directory.
    def game(name):
        target = fixtures/name
        (target/'Game/Binaries/Win64').mkdir(parents=True)
        (target/'EasyAntiCheat').mkdir()
        (target/'Game/Binaries/Win64/AceCombat8.exe').write_bytes(b'INSTALLER FIXTURE ONLY')
        return target

    fresh = game('fresh')
    install(fresh)
    verify_mod(fresh)
    assert sha(fresh/REL_CORE) == sha(PAYLOAD/REL_CORE)
    assert 'AC8MouseAim : 1' in (fresh/REL_MOD.parent/'mods.txt').read_text(encoding='utf-8')
    print('PASS fresh install: matching DLL/Lua, defaults, preset and runtime copied')

    reuse = game('reuse')
    core = reuse/REL_CORE
    core.parent.mkdir(parents=True)
    shutil.copyfile(PAYLOAD/REL_CORE, core)
    mod = reuse/REL_MOD
    (mod/'Scripts').mkdir(parents=True)
    custom = b'; user configuration\r\n[control]\r\nsensitivity=0.23\r\nmax_bank=65\r\n'
    (mod/'config.ini').write_bytes(custom)
    (mod/'Scripts/ac8_mouse_aim_010.dll').write_bytes(b'old module fixture')
    shared = {
        reuse/'Game/Binaries/Win64/dwmapi.dll': b'existing shared loader',
        core.parent/'UE4SS-settings.ini': b'[Hooks]\r\nHookEngineTick=1\r\n',
        mod.parent/'OtherMod/Scripts/main.lua': b'-- another mod\n',
    }
    for p, data in shared.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    (mod.parent/'mods.txt').write_text('OtherMod : 1\nAC8MouseAim : 0\n', encoding='utf-8')
    others = [{'mod_name': 'OtherMod', 'mod_enabled': True, 'custom': 7}]
    (mod.parent/'mods.json').write_text(json.dumps({'keep': 'metadata', 'mods': others}), encoding='utf-8')
    install(reuse)
    verify_mod(reuse, custom)
    for p, data in shared.items():
        assert p.read_bytes() == data, str(p)
    assert sha(core) == sha(PAYLOAD/REL_CORE)
    assert 'OtherMod : 1' in (mod.parent/'mods.txt').read_text(encoding='utf-8')
    updated = json.loads((mod.parent/'mods.json').read_text(encoding='utf-8'))
    assert updated['keep'] == 'metadata' and updated['mods'][0] == others[0]
    assert updated['mods'][1] == {'mod_name': 'AC8MouseAim', 'mod_enabled': True}
    backups = list((reuse/'AC8MouseAim-Backups').glob('install-*'))
    assert len(backups) == 1
    assert (backups[0]/'AC8MouseAim/config.ini').read_bytes() == custom
    assert (backups[0]/'AC8MouseAim/Scripts/ac8_mouse_aim_010.dll').read_bytes() == b'old module fixture'
    print('PASS coexist upgrade: custom INI, shared loader/settings, other mods and backup preserved')

    unknown = game('unknown-runtime')
    (unknown/REL_CORE).parent.mkdir(parents=True)
    (unknown/REL_CORE).write_bytes(b'unknown runtime fixture')
    before = snapshot(unknown)
    install(unknown, success=False)
    assert snapshot(unknown) == before
    print('PASS unknown runtime: rejected without modifying fixture files')
    print(f'Fixtures retained for review: {fixtures}')


if __name__ == '__main__':
    main()
