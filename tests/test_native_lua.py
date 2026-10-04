"""Execute the fork's actual Lua entrypoint with a mocked native bridge."""
from pathlib import Path
import unittest
from lupa.lua54 import LuaRuntime

SCRIPTS = Path(__file__).resolve().parents[1] / 'Payload/Game/Binaries/Win64/UE4SS/Mods/AC8MouseAim/Scripts'

class NativeLuaTests(unittest.TestCase):
    def environment(self, version=35):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().bridge_version = version
        lua.execute('''
            bindings = {}; events = {}; foreground = 1; EngineTickAvailable = false
            print = function() end
            local modules = { request=function() events.probe=(events.probe or 0)+1 end }
            dofile=function() return modules end
            RegisterKeyBind=function(key, callback)
                assert(math.type(key)=='integer', 'UE4SS requires integer key')
                bindings[key]=callback
            end
            package.loadlib=function(_, name)
                if name=='ac8_mouseaim_start' then return function() return bridge_version end end
                if name=='ac8_mouseaim_keys' then return function() return 123.0,124.0,125.0 end end
                if name=='ac8_mouseaim_foreground' then return function() return foreground end end
                return function() events[name]=(events[name] or 0)+1 end
            end
        ''')
        return lua

    def load_main(self, lua):
        lua.execute((SCRIPTS/'main.lua').read_text(encoding='utf-8'), name='@'+str(SCRIPTS/'main.lua'))

    def test_custom_registration_and_foreground(self):
        lua = self.environment()
        self.load_main(lua)
        lua.execute('for _, k in ipairs({123,124,125}) do bindings[k]() end')
        self.assertEqual(lua.globals().events['ac8_mouseaim_reload'], 1)
        self.assertEqual(lua.globals().events['probe'], 1)
        self.assertEqual(lua.globals().events['ac8_mouseaim_perf'], 1)
        lua.execute('foreground=0; for _, k in ipairs({123,124,125}) do bindings[k]() end')
        self.assertEqual(lua.globals().events['probe'], 1)
        self.assertEqual(lua.globals().events['ac8_mouseaim_reload'], 1)
        self.assertEqual(lua.globals().events['ac8_mouseaim_perf'], 1)
        self.assertIsNone(lua.globals().bindings[121]) # Old F10 is not registered.

    def test_original_dll_bridge_is_rejected(self):
        for version in (30, 31, 32, 33, 34):
            with self.subTest(version=version):
                lua = self.environment(version)
                with self.assertRaisesRegex(Exception, 'mismatched DLL/Lua'):
                    self.load_main(lua)
                self.assertEqual(list(lua.globals().bindings.items()), [])

    def test_all_lua_syntax(self):
        lua = self.environment()
        compile_lua = lua.eval('function(text,name) local f,e=load(text,name); assert(f,e) end')
        for file in SCRIPTS.glob('*.lua'):
            compile_lua(file.read_text(encoding='utf-8'), str(file))
