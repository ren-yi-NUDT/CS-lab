#!/usr/bin/env python3
"""
patch_o.py - ELF Object File Patcher

本工具用于修补ELF目标文件(.o)，使其能顺利链接为共享库(.so)。

主要功能:
  1. 分析ELF文件中的重定位信息
  2. 检测可能导致TEXTREL的节区
  3. 修改节区头部标志以消除TEXTREL

用法:
  python3 patch_o.py <输入.o> [输出.o]
  
  如果不指定输出文件，默认直接修改输入文件。

提示：使用 readelf 分析 .o 文件的结构:
  readelf -a <file.o>    # 查看所有信息
  readelf -S <file.o>    # 查看节区头部
  readelf -r <file.o>    # 查看重定位信息
  readelf -s <file.o>    # 查看符号表
"""

import struct
import sys
import os


# ELF 常量
SHF_WRITE = 0x1          # 可写标志
SHF_ALLOC = 0x2          # 分配内存标志
SHF_EXECINSTR = 0x4      # 可执行标志
SHT_RELA = 4             # RELA类型重定位节

# ELF header 中关键字段的偏移
# 对于 ELF64:
#   e_shoff:     偏移 40 (8字节)
#   e_shentsize: 偏移 58 (2字节)
#   e_shnum:     偏移 60 (2字节)
#   e_shstrndx:  偏移 62 (2字节)

# 节区头部 (Elf64_Shdr) 结构:
#   sh_name:      偏移 0  (4字节) - 节区名在字符串表中的索引
#   sh_type:      偏移 4  (4字节)
#   sh_flags:     偏移 8  (8字节)
#   sh_addr:      偏移 16 (8字节)
#   sh_offset:    偏移 24 (8字节) - 节区在文件中的偏移
#   sh_size:      偏移 32 (8字节) - 节区大小
#   sh_link:      偏移 40 (4字节)
#   sh_info:      偏移 44 (4字节) - 对于RELA节区，指向被重定位的节区
#   sh_addralign: 偏移 48 (8字节)
#   sh_entsize:   偏移 56 (8字节)
#   总大小: 64 字节


def read_u16(data, offset):
    """从指定偏移读取2字节小端无符号整数"""
    return struct.unpack_from('<H', data, offset)[0]


def read_u32(data, offset):
    """从指定偏移读取4字节小端无符号整数"""
    return struct.unpack_from('<I', data, offset)[0]


def read_u64(data, offset):
    """从指定偏移读取8字节小端无符号整数"""
    return struct.unpack_from('<Q', data, offset)[0]


def write_u64(data, offset, val):
    """在指定偏移写入8字节小端无符号整数，返回修改后的数据"""
    struct.pack_into('<Q', data, offset, val)
    return data


def get_section_name(data, shdr_offset, shdr_entsize, shstrndx, sh_name_off):
    """
    从 .shstrtab 字符串表中查找节区名。
    
    参数:
      data:          ELF文件数据
      shdr_offset:   节区头部表偏移 (e_shoff)
      shdr_entsize:  节区头部大小 (e_shentsize)
      shstrndx:      .shstrtab 节区索引 (e_shstrndx)
      sh_name_off:   节区名在字符串表中的偏移
    
    返回:
      节区名字符串
    
    提示:
      1. 先定位 .shstrtab 的节区头部
      2. 从节区头部读取 sh_offset (文件偏移) 和 sh_size (大小)
      3. 在 .shstrtab 数据中，以 sh_name_off 为起点找到下一个 \\0 结束
    """
    # 定位 .shstrtab 节区头部
    shstrtab_hdr_off = shdr_offset + shstrndx * shdr_entsize
    shstrtab_offset = read_u64(data, shstrtab_hdr_off + 24)
    shstrtab_size = read_u64(data, shstrtab_hdr_off + 32)

    # 在字符串表中查找以 sh_name_off 为起点的节区名
    start = shstrtab_offset + sh_name_off
    end = data.index(b'\x00', start)
    return data[start:end].decode('ascii')


def find_sections_with_textrel(data):
    """
    分析 ELF .o 文件中可能导致 TEXTREL 的节区。
    
    TEXTREL 产生原因：共享库中，重定位信息位于只读节区（如 .rodata），
    动态链接器需要修改这些位置，但只读页面不允许写入。
    
    检测方法：
      1. 遍历所有节区头部
      2. 找到类型为 SHT_RELA 的节区
      3. 通过 sh_info 找到被重定位的目标节区
      4. 检查目标节区是否：已分配 (SHF_ALLOC) 但不可写 (非 SHF_WRITE)
      5. 排除可执行节区 (.text)，避免制造 W+X 安全风险
    
    返回:
      需要修补的 (节区索引, 节区名, 当前标志) 列表
    """
    if data[:4] != b'\x7fELF':
        print("ERROR: Not a valid ELF file")
        return []

    # 检查是否为 ELF64
    if data[4] != 2:
        print("ERROR: Only 64-bit ELF is supported")
        return []

    # 从 ELF header 读取节区头部表信息
    shdr_offset = read_u64(data, 40)
    shdr_entsize = read_u16(data, 58)
    shdr_count = read_u16(data, 60)
    shstrndx = read_u16(data, 62)

    if shdr_offset is None:
        print("ERROR: Failed to parse ELF header")
        return []

    results = []
    for i in range(shdr_count):
        hdr_off = shdr_offset + i * shdr_entsize
        
        # 读取节区类型 (sh_type)
        sh_type = read_u32(data, hdr_off + 4)
        
        if sh_type != SHT_RELA:
            continue
        
        # 从 sh_info 获取目标节区索引
        target_idx = read_u32(data, hdr_off + 44)
        
        # TODO: 解析目标节区的信息
        target_hdr = shdr_offset + target_idx * shdr_entsize
        tgt_name_off = read_u32(data, target_hdr)
        tgt_name = get_section_name(data, shdr_offset, shdr_entsize, shstrndx, tgt_name_off)
        
        # 读取目标节区的标志 (sh_flags)
        tgt_flags = read_u64(data, target_hdr + 8)
        
        # 判断是否为只读已分配节区（非可执行）
        is_readonly_alloc = (tgt_flags & SHF_ALLOC) and not (tgt_flags & SHF_WRITE)
        is_text = bool(tgt_flags & SHF_EXECINSTR)
        
        if is_readonly_alloc and not is_text:
            results.append((target_idx, tgt_name, tgt_flags))
            print(f"  [!] Found TEXTREL risk: '{tgt_name}' (idx {target_idx}, flags=0x{tgt_flags:x})")
    
    return results


def fix_textrel(obj_path, output_path=None):
    """
    修补 ELF .o 文件以消除 TEXTREL。
    
    修补方法：
      对检测到的只读数据节区添加 SHF_WRITE 标志，
      使链接器将其视为可写节区，避免产生 TEXTREL。
    """
    if output_path is None:
        output_path = obj_path

    with open(obj_path, 'rb') as f:
        data = bytearray(f.read())

    problems = find_sections_with_textrel(bytes(data))

    if not problems:
        print("  No TEXTREL issues found (already clean)")
        with open(output_path, 'wb') as f:
            f.write(data)
        return True

    print(f"  Found {len(problems)} section(s) with TEXTREL risk")

    shdr_offset = read_u64(data, 40)
    shdr_entsize = read_u16(data, 58)

    for idx, name, flags in problems:
        hdr_off = shdr_offset + idx * shdr_entsize
        new_flags = flags | SHF_WRITE
        write_u64(data, hdr_off + 8, new_flags)
        print(f"  Patched '{name}': flags 0x{flags:x} -> 0x{new_flags:x}")

    with open(output_path, 'wb') as f:
        f.write(data)
    print(f"  TEXTREL fix applied to {len(problems)} section(s)")
    return True


def print_elf_info(obj_path):
    """打印 ELF 文件的基本信息（辅助调试）"""
    with open(obj_path, 'rb') as f:
        data = f.read()
    
    if data[:4] != b'\x7fELF':
        print("Not a valid ELF file")
        return
    
    is_64bit = data[4] == 2
    print(f"ELF{64 if is_64bit else 32} file")
    
    if not is_64bit:
        print("Only 64-bit ELF is supported")
        return
    
    # 从 ELF header 读取并打印关键信息
    shdr_offset = read_u64(data, 40)
    shdr_entsize = read_u16(data, 58)
    shdr_count = read_u16(data, 60)
    shstrndx = read_u16(data, 62)
    print(f"  Section header offset: {shdr_offset}")
    print(f"  Section header entry size: {shdr_entsize}")
    print(f"  Number of sections: {shdr_count}")
    print(f"  String table index: {shstrndx}")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input.o> [output.o]")
        print()
        print("Options:")
        print("  --info <file.o>   打印 ELF 文件信息")
        sys.exit(1)

    if sys.argv[1] == '--info':
        if len(sys.argv) < 3:
            print("Usage: patch_o.py --info <file.o>")
            sys.exit(1)
        print_elf_info(sys.argv[2])
        return

    obj_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else obj_path

    if not os.path.exists(obj_path):
        print(f"ERROR: File not found: {obj_path}")
        sys.exit(1)

    print(f"Patching {obj_path} → {output_path}")
    if fix_textrel(obj_path, output_path):
        print("Done!")
    else:
        print("ERROR: Failed to patch")
        sys.exit(1)


if __name__ == '__main__':
    main()
