#!/usr/bin/env python3
"""链接炸弹测试框架 - Link Bomb Test Framework

使用:
  python3 bomb_tester.py <level_dir> <lib.so>
  
示例:
  python3 bomb_tester.py level1 ./libbomb.so
  
每关返回码:
  0 - 过关
  1 - 炸弹爆炸（测试失败）
  2 - 炸弹爆炸（链接失败或其他错误）
"""

import sys
import os
import subprocess
import ctypes
import struct
import json
from pathlib import Path

BOOM = """
    ██████   ██████  ██████  ███    ███
   ██    ██ ██      ██       ████  ████
   ██    ██ ██      ██       ██ ████ ██
   ██    ██ ██████  ██████   ██  ██  ██
   ██    ██      ██ ██       ██      ██
   ██    ██ ██    ██ ██       ██      ██
    ██████   ██████  ██████  ██████ ██████
   💥     BOOOOOOOM!     💥
"""

PASS = """
    ██████  █████  ███████ ███████ 
   ██      ██   ██ ██      ██      
   ██      ███████ ███████ ███████ 
   ██      ██   ██      ██      ██
    ██████ ██   ██ ███████ ███████ 
   🎉   LEVEL DEFUSED!   🎉
"""


class LinkBombTester:
    COLORS = {
        'red': '\033[91m',
        'green': '\033[92m',
        'yellow': '\033[93m',
        'cyan': '\033[96m',
        'bold': '\033[1m',
        'end': '\033[0m',
    }

    def __init__(self, level_dir):
        self.level_dir = Path(level_dir)
        config_path = self.level_dir / 'config.json'
        if not config_path.exists():
            self.error(f"Missing config: {config_path}")
            sys.exit(2)
        with open(config_path) as f:
            self.config = json.load(f)

    def c(self, color, text):
        """彩色输出"""
        return f"{self.COLORS.get(color, '')}{text}{self.COLORS['end']}"

    def header(self, text):
        print(f"\n{'='*50}")
        print(f"  {self.c('bold', text)}")
        print(f"{'='*50}")

    def check_elf(self, so_path):
        """检查文件是否为合法的ELF共享库"""
        if not os.path.exists(so_path):
            return False, f"File not found: {so_path}"
        with open(so_path, 'rb') as f:
            magic = f.read(4)
            if magic != b'\x7fELF':
                return False, "Not a valid ELF file"
        result = self._readelf(['-h', so_path])
        if result.returncode != 0:
            return False, "Cannot read ELF header"
        if 'DYN' not in result.stdout:
            return False, "Not a shared object (DYN)"
        result2 = self._readelf(['-d', so_path])
        if result2.returncode != 0:
            return False, "Cannot read dynamic section"
        return True, None

    def _readelf(self, args, **kwargs):
        """Run readelf with LANG=C for consistent English output"""
        env = os.environ.copy()
        env['LANG'] = 'C'
        return subprocess.run(['readelf'] + args, capture_output=True, text=True, env=env, **kwargs)

    def check_textrel(self, so_path):
        """检查是否有TEXTREL"""
        result = self._readelf(['-d', so_path])
        return 'TEXTREL' in result.stdout

    def check_notext(self, so_path):
        """检查是否无TEXTREL（返回True表示无TEXTREL）"""
        return not self.check_textrel(so_path)

    def dlopen_test(self, so_path):
        """用ctypes加载.so并运行函数测试"""
        tests = self.config.get('tests', [])
        try:
            lib = ctypes.CDLL(so_path, use_errno=True)
        except Exception as e:
            return False, f"Cannot load .so: {e}"
        for test in tests:
            func_name = test['name']
            arg_types = test.get('arg_types', [])
            ret_type = test.get('ret_type', 'int')
            args = test.get('args', [])
            expected = test.get('expected')

            try:
                func = getattr(lib, func_name)
            except AttributeError as e:
                return False, f"Missing function: {func_name}: {e}"

            try:
                if arg_types:
                    func.argtypes = [
                        {'int': ctypes.c_int,
                         'char': ctypes.c_char,
                         'ptr': ctypes.c_void_p}.get(t, ctypes.c_int)
                        for t in arg_types
                    ]
                func.restype = {
                    'int': ctypes.c_int,
                    'void': None,
                    'ptr': ctypes.c_void_p
                }.get(ret_type, ctypes.c_int)

                result = func(*args)
            except Exception as e:
                return False, f"Call {func_name}({','.join(map(str,args))}) failed: {e}"

            if expected is not None and result != expected:
                return False, (
                    f"{func_name}({','.join(map(str,args))}) "
                    f"→ {result} (expected {expected})"
                )
        return True, None

    def run(self, so_path):
        so_path = str(Path(so_path).resolve())
        self.header(f"🧨 Link Bomb - {self.config.get('name', 'Unknown Level')}")
        print(f"  Target: {so_path}")
        print(f"  Description: {self.config.get('desc', '')}")
        print()

        checks = self.config.get('checks', [])

        for check in checks:
            check_name = check.get('check', '')
            check_desc = check.get('desc', '')
            print(f"  ⏳ {check_desc}... ", end='', flush=True)

            if check_name == 'elf_valid':
                ok, msg = self.check_elf(so_path)
                if ok:
                    print(self.c('green', '✓ OK'))
                else:
                    print(self.c('red', '✗ FAIL'))
                    print(f"\n  {self.c('red', 'Reason:')} {msg}")
                    self.boom()
                    return False

            elif check_name == 'no_textrel':
                if self.check_textrel(so_path):
                    print(self.c('red', '✗ TEXTREL DETECTED!'))
                    print(f"\n  {self.c('yellow', 'Hint:')} The .so contains TEXTREL.")
                    print("  Binary must be position-independent for shared library.")
                    self.boom()
                    return False
                else:
                    print(self.c('green', '✓ No TEXTREL'))

            elif check_name == 'func_test':
                ok, msg = self.dlopen_test(so_path)
                if ok:
                    print(self.c('green', '✓ All tests pass'))
                else:
                    print(self.c('red', '✗ FAIL'))
                    print(f"\n  {self.c('red', '💥 BOOM:')} {msg}")
                    self.boom()
                    return False

        print()
        self.defused()
        return True

    def boom(self):
        print(self.c('red', BOOM))
        print(self.c('red', '  The bomb exploded!\n'))

    def defused(self):
        print(self.c('green', PASS))
        print(self.c('cyan', f"  + {self.config.get('score', 0)} points\n"))


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 bomb_tester.py <level_dir> <lib.so>")
        print("Example: python3 bomb_tester.py level1 ./libbomb.so")
        sys.exit(2)

    level_dir = sys.argv[1]
    so_path = sys.argv[2]

    tester = LinkBombTester(level_dir)
    if tester.run(so_path):
        sys.exit(0)
    else:
        sys.exit(1)


if __name__ == '__main__':
    main()
