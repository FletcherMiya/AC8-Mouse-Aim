"""Offline tests load the same Lua 5.4 modules that will be deployed."""
import argparse
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pattern', default='test_*.py')
    args = parser.parse_args()
    try:
        import lupa
        from lupa.lua54 import LuaRuntime
        if lupa.__version__ != '2.8':
            raise RuntimeError('Expected lupa 2.8; install requirements-dev.txt')
        runtime = LuaRuntime()
        assert runtime.eval('_VERSION') == 'Lua 5.4'
    except (ImportError, AssertionError, RuntimeError) as error:
        print(f'Test runtime unavailable: {error}', file=sys.stderr)
        return 2
    print(f'Python {sys.version.split()[0]}; Lupa {lupa.__version__}; {runtime.lua_implementation}', flush=True)
    suite = unittest.defaultTestLoader.discover(str(ROOT / 'tests'), pattern=args.pattern)
    if suite.countTestCases() == 0:
        print('No tests discovered; refusing to report success.', file=sys.stderr)
        return 2
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1

if __name__ == '__main__':
    sys.exit(main())
