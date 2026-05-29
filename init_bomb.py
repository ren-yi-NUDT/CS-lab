#!/usr/bin/env python3
"""
🔐 链接炸弹初始化器 (Link Bomb Initializer)
============================================

为每位学生生成专属的链接炸弹实验包。

用法:
  python3 init_bomb.py
  python3 init_bomb.py <6位学号>

示例:
  python3 init_bomb.py            # 交互式输入学号
  python3 init_bomb.py 202401     # 直接指定学号
"""

import sys
import os
import struct
import hashlib
import json
import random
import subprocess
import shutil
from pathlib import Path


# ─── 确定性参数生成 ───────────────────────────────────────

def seed_from_id(student_id):
    h = hashlib.sha256(str(student_id).encode())
    return int(h.hexdigest()[:8], 16)


def gen_params(student_id):
    seed = seed_from_id(student_id)
    rng = random.Random(seed)
    params = {}

    params['l1_add'] = (rng.randint(1, 20), rng.randint(1, 10))
    params['l1_sub'] = (rng.randint(1, 20), rng.randint(1, 10))
    params['l1_mul'] = (rng.randint(1, 20), rng.randint(1, 10))
    params['l1_mod'] = (rng.randint(1, 20), rng.randint(1, 10))

    params['l2_gvar'] = rng.randint(50, 200)
    params['l3_counter'] = rng.randint(10, 99)
    params['l4_base'] = rng.randint(50, 500)

    params['l5_gvar'] = rng.randint(100, 999)
    params['l5_counter'] = rng.randint(0, 50)
    d0 = rng.randint(50, 200)
    d1 = rng.randint(50, 200)
    d2 = rng.randint(50, 200)
    params['l5_data'] = (d0, d1, d2)

    return params


# ─── 配置生成 ─────────────────────────────────────────────

def make_config(name, desc, score, checks, tests):
    return {
        "name": name, "desc": desc, "score": score,
        "checks": checks, "tests": tests
    }


def build_configs(params):
    a1, b1 = params['l1_add']
    a2, b2 = params['l1_sub']
    a3, b3 = params['l1_mul']
    a4, b4 = params['l1_mod']

    return {
        'level1': make_config(
            "Level 1 - 蒸汽时代 (Steam Age)",
            "请将 bomb1.a 转换为 .so，通过算术函数测试",
            10,
            [{"check": "elf_valid"}, {"check": "func_test"}],
            [
                {"name": "add", "args": [a1, b1], "expected": a1 + b1},
                {"name": "sub", "args": [a2, b2], "expected": a2 - b2},
                {"name": "mul", "args": [a3, b3], "expected": a3 * b3},
                {"name": "mod", "args": [a4, b4], "expected": a4 % b4},
            ]
        ),
        'level2': make_config(
            "Level 2 - 全局危机 (Global Crisis)",
            "全局变量 gvar=%d 的取地址操作导致 R_X86_64_32/32S" % params['l2_gvar'],
            20,
            [{"check": "elf_valid"}, {"check": "no_textrel"}, {"check": "func_test"}],
            [
                {"name": "read_var", "args": [], "expected": params['l2_gvar']},
                {"name": "get_elem", "args": [2], "expected": 30},
                {"name": "get_elem", "args": [4], "expected": 50},
                {"name": "get_addr", "args": [], "expected": None},
            ]
        ),
        'level3': make_config(
            "Level 3 - 数据陷阱 (Data Trap)",
            "全局符号 counter=%d 的 PC-relative 引用被拒绝" % params['l3_counter'],
            20,
            [{"check": "elf_valid"}, {"check": "no_textrel"}, {"check": "func_test"}],
            [
                {"name": "next_val", "args": [], "expected": params['l3_counter']},
                {"name": "next_val", "args": [], "expected": params['l3_counter'] + 1},
                {"name": "next_val", "args": [], "expected": params['l3_counter'] + 2},
                {"name": "next_val", "args": [], "expected": params['l3_counter'] + 3},
            ]
        ),
        'level4': make_config(
            "Level 4 - 二进制手术师 (Binary Surgeon)",
            "base_val=%d, .rodata 重定位导致 TEXTREL" % params['l4_base'],
            25,
            [{"check": "elf_valid"}, {"check": "no_textrel"}, {"check": "func_test"}],
            [
                {"name": "compute", "args": [5], "expected": params['l4_base'] + 5},
                {"name": "compute", "args": [20], "expected": params['l4_base'] + 20},
            ]
        ),
        'level5': make_config(
            "Level 5 - 终极链接炸弹 (Ultimate Link Bomb)",
            "综合关卡: 需要 PIC 替代 + objcopy + TEXTREL 修补",
            50,
            [{"check": "elf_valid"}, {"check": "no_textrel"}, {"check": "func_test"}],
            [
                {"name": "read_shared", "args": [], "expected": params['l5_gvar']},
                {"name": "next_val", "args": [], "expected": params['l5_counter']},
                {"name": "next_val", "args": [], "expected": params['l5_counter'] + 1},
                {"name": "next_val", "args": [], "expected": params['l5_counter'] + 2},
                {"name": "sum_data", "args": [], "expected": sum(params['l5_data'])},
                {"name": "helper_double", "args": [7], "expected": 14},
                {"name": "helper_square", "args": [7], "expected": 49},
                {"name": "shared_addr", "args": [], "expected": None},
            ]
        ),
    }


# ─── 模板 ─────────────────────────────────────────────────

TEMPLATES = {
    'level1.c': """int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }
int mod(int a, int b) { return a % b; }
""",
    'level2.c': """int gvar = __GVAR__;
int get_addr(void) { return (int)(unsigned long)&gvar; }
int read_var(void) { return gvar; }
static int arr[] = {10, 20, 30, 40, 50};
int get_elem(int i) { return arr[i]; }
""",
    'level3.c': """int counter = __COUNTER__;
int next_val(void) {
    int tmp = counter;
    counter++;
    return tmp;
}
""",
    'level4.c': """static int base_val = __BASE__;
int * const ptr = &base_val;
int compute(int x) {
    return *ptr + x;
}
""",
    'level5_helper.c': """int helper_double(int x) { return x * 2; }
int helper_square(int x) { return x * x; }
""",
    'level5_state.c': """int gvar = __GVAR5__;
int counter = __COUNTER5__;
int next_val(void) { int t = counter; counter++; return t; }
static int data[] = {__D0__, __D1__, __D2__};
int * const pdata = data;
int sum_data(void) {
    int s = 0;
    for (int i = 0; i < 3; i++) s += pdata[i];
    return s;
}
""",
    'level5_tricky.c': """extern int gvar;
int shared_addr(void) { return (int)(unsigned long)&gvar; }
int read_shared(void) { return gvar; }
""",
}


# ─── 炸弹编译 ─────────────────────────────────────────────

def compile_c(src_text, obj_name, work_dir):
    src_path = work_dir / obj_name.replace('.o', '.c')
    src_path.write_text(src_text)
    obj_path = work_dir / obj_name
    r = subprocess.run(
        ['gcc', '-c', '-O1', '-fno-pic', '-o', str(obj_path), str(src_path)],
        capture_output=True, text=True
    )
    if r.returncode != 0:
        print("  编译失败:", r.stderr)
        return None
    return obj_path


def make_level1(work_dir, params):
    src = TEMPLATES['level1.c']
    o = compile_c(src, 'lv1.o', work_dir)
    if o is None: return False
    subprocess.run(['ar', 'crs', 'bomb1.a', 'lv1.o'], cwd=work_dir)
    return True


def make_level2(work_dir, params):
    src = TEMPLATES['level2.c'].replace('__GVAR__', str(params['l2_gvar']))
    o = compile_c(src, 'lv2.o', work_dir)
    if o is None: return False
    subprocess.run(['ar', 'crs', 'bomb2.a', 'lv2.o'], cwd=work_dir)
    return True


def make_level3(work_dir, params):
    src = TEMPLATES['level3.c'].replace('__COUNTER__', str(params['l3_counter']))
    o = compile_c(src, 'lv3.o', work_dir)
    if o is None: return False
    subprocess.run(['ar', 'crs', 'bomb3.a', 'lv3.o'], cwd=work_dir)
    return True


def make_level4(work_dir, params):
    src = TEMPLATES['level4.c'].replace('__BASE__', str(params['l4_base']))
    o = compile_c(src, 'lv4.o', work_dir)
    if o is None: return False
    subprocess.run(['ar', 'crs', 'bomb4.a', 'lv4.o'], cwd=work_dir)
    return True


def make_level5(work_dir, params):
    p = params
    s1 = TEMPLATES['level5_helper.c']
    s2 = (TEMPLATES['level5_state.c']
          .replace('__GVAR5__', str(p['l5_gvar']))
          .replace('__COUNTER5__', str(p['l5_counter']))
          .replace('__D0__', str(p['l5_data'][0]))
          .replace('__D1__', str(p['l5_data'][1]))
          .replace('__D2__', str(p['l5_data'][2])))
    s3 = TEMPLATES['level5_tricky.c']
    o1 = compile_c(s1, 'boss_helper.o', work_dir)
    o2 = compile_c(s2, 'boss_state.o', work_dir)
    o3 = compile_c(s3, 'boss_tricky.o', work_dir)
    if any(x is None for x in [o1, o2, o3]): return False
    subprocess.run(
        ['ar', 'crs', 'bomb5.a', 'boss_helper.o', 'boss_state.o', 'boss_tricky.o'],
        cwd=work_dir
    )
    return True


# ─── 主流程 ───────────────────────────────────────────────

COLOR = {
    'red': '\033[91m', 'green': '\033[92m',
    'yellow': '\033[93m', 'cyan': '\033[96m',
    'bold': '\033[1m', 'end': '\033[0m',
}


def banner():
    print(COLOR['cyan'] + """
   ╔══════════════════════════════════════════╗
   ║     💣  链接炸弹 - Link Bomb            ║
   ║     计算机系统 第7章(链接) 配套实验      ║
   ╚══════════════════════════════════════════╝
    """ + COLOR['end'])


def prompt_student_id():
    while True:
        sid = input(COLOR['bold'] + "  请输入你的学号（后6位）: " + COLOR['end']).strip()
        if len(sid) == 6 and sid.isdigit():
            return sid
        print(COLOR['red'] + "  ❌ 学号必须是6位数字，请重新输入" + COLOR['end'])


def check_dependencies():
    missing = []
    for cmd in ['gcc', 'ar', 'readelf', 'objcopy']:
        if shutil.which(cmd) is None:
            missing.append(cmd)
    if missing:
        print(COLOR['red'] + "  ❌ 缺少必要工具: " + ", ".join(missing) + COLOR['end'])
        print("     请安装 build-essential / binutils 等开发工具包")
        return False
    return True


def main():
    banner()

    if not check_dependencies():
        sys.exit(1)

    if len(sys.argv) > 1:
        sid = sys.argv[1]
        if not (len(sid) == 6 and sid.isdigit()):
            print(COLOR['red'] + f"  ❌ 学号 '{sid}' 不是6位数字" + COLOR['end'])
            sys.exit(1)
    else:
        sid = prompt_student_id()

    print(f"\n  学号: {COLOR['bold']}{sid}{COLOR['end']}")
    print(f"  种子: {seed_from_id(sid)}")
    print()

    params = gen_params(sid)
    configs = build_configs(params)
    levels = ['level1', 'level2', 'level3', 'level4', 'level5']
    makers = [make_level1, make_level2, make_level3, make_level4, make_level5]

    # 创建炸弹文件
    all_ok = True
    for level_name, maker, config in zip(levels, makers, [configs[l] for l in levels]):
        work_dir = Path(level_name)
        work_dir.mkdir(parents=True, exist_ok=True)
        print(f"  🔨 生成 {level_name}... ", end='', flush=True)
        if maker(work_dir, params):
            with open(work_dir / 'config.json', 'w') as f:
                json.dump(config, f, indent=2, ensure_ascii=False)
            # 清理中间 .o 和 .c
            for f in work_dir.iterdir():
                if f.suffix in ('.o', '.c'):
                    f.unlink()
            print(COLOR['green'] + "OK" + COLOR['end'])
        else:
            print(COLOR['red'] + "FAIL" + COLOR['end'])
            all_ok = False

    print()
    if not all_ok:
        print(COLOR['red'] + "  ❌ 炸弹生成失败，请检查编译环境" + COLOR['end'])
        sys.exit(1)

    print(COLOR['green'] + COLOR['bold'] + """
   ╔══════════════════════════════════════════╗
   ║     ✅  炸弹已生成！                     ║
   ║     目录结构:                           ║
   ║                                        ║
   ║      level1/bomb1.a + config.json      ║
   ║      level2/bomb2.a + config.json      ║
   ║      level3/bomb3.a + config.json      ║
   ║      level4/bomb4.a + config.json      ║
   ║      level5/bomb5.a + config.json      ║
   ║                                        ║
   ║  总分: 125 分                          ║
   ╚══════════════════════════════════════════╝
    """ + COLOR['end'])

    print("  开始拆弹：")
    print("    cd level1 && ar -x bomb1.a && gcc -shared -o libbomb.so *.o")
    print("    python3 ../bomb_tester.py . libbomb.so")
    print()
    print("  详细说明请阅读 README.md")


if __name__ == '__main__':
    main()
