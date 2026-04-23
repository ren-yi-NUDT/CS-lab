#!/usr/bin/env python3
"""Y86-64 模拟器 CLI 入口"""

import sys
from memory import Memory
from cpu import Cpu


def main():
    args = sys.argv[1:]
    if not args:
        print(f'用法: python3 {sys.argv[0]} <coe_file> [--dump <addr> <len>] [--mem]')
        sys.exit(1)

    coe_path = args[0]

    mem = Memory()
    loaded = mem.load_coe(coe_path)
    print(f'已加载 {loaded} 字节机器码')

    cpu = Cpu(mem)

    print('开始执行...')
    try:
        cycles = cpu.run()
        print(f'执行完成，共 {cycles} 个周期')
    except RuntimeError as e:
        print(f'执行错误: {e}', file=sys.stderr)

    cpu.print_registers()

    # 解析可选参数
    i = 1
    while i < len(args):
        if args[i] == '--dump' and i + 2 < len(args):
            addr = int(args[i + 1], 16)
            length = int(args[i + 2])
            print(f'\n=== 内存转储 0x{addr:04x} + {length} 字节 ===')
            cpu.mem.dump(addr, length)
            i += 3
        elif args[i] == '--mem':
            print('\n=== 非零内存内容 ===')
            cpu.mem.dump_nonzero()
            i += 1
        else:
            i += 1


if __name__ == '__main__':
    main()
