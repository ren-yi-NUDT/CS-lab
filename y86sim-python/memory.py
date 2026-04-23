"""内存模块：COE 文件解析与字节/字读写"""

import struct
from instruction import MEM_SIZE


class Memory:
    def __init__(self):
        self.data = bytearray(MEM_SIZE)

    def load_coe(self, path):
        with open(path, 'r') as f:
            content = f.read()

        radix = 16
        data_section = False
        addr = 0

        for raw_line in content.splitlines():
            line = raw_line.strip()
            if not line:
                continue

            lower = line.lower()
            if lower.startswith('memory_initialization_radix'):
                parts = line.split('=')
                if len(parts) == 2:
                    radix = int(parts[1].strip().rstrip(';').strip())
                continue

            if lower.startswith('memory_initialization_vector'):
                data_section = True
                eq_pos = line.find('=')
                if eq_pos >= 0:
                    rest = line[eq_pos + 1:].strip()
                    if rest:
                        addr = self._parse_values(rest, radix, addr)
                continue

            if data_section:
                addr = self._parse_values(line, radix, addr)

        return addr

    def _parse_values(self, text, radix, addr):
        text = text.strip().rstrip(';').strip()
        if not text:
            return addr
        for token in text.split(','):
            token = token.strip()
            if not token:
                continue
            val = int(token, radix)
            if addr < MEM_SIZE:
                self.data[addr] = val & 0xFF
                addr += 1
        return addr

    def read_byte(self, addr):
        if addr >= MEM_SIZE:
            raise ValueError(f"地址越界: 0x{addr:x}")
        return self.data[addr]

    def write_byte(self, addr, val):
        if addr >= MEM_SIZE:
            raise ValueError(f"地址越界: 0x{addr:x}")
        self.data[addr] = val & 0xFF

    def read_word(self, addr):
        if addr + 8 > MEM_SIZE:
            raise ValueError(f"地址越界: 0x{addr:x}")
        return struct.unpack_from('<Q', self.data, addr)[0]

    def write_word(self, addr, val):
        if addr + 8 > MEM_SIZE:
            raise ValueError(f"地址越界: 0x{addr:x}")
        struct.pack_into('<Q', self.data, addr, val)

    def dump(self, start, length=64):
        end = min(start + length, MEM_SIZE)
        for row in range(start, end, 16):
            chunk = self.data[row:min(row + 16, end)]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            print(f'0x{row:04x}: {hex_str:<48s} {ascii_str}')

    def dump_nonzero(self):
        i = 0
        found = False
        while i < MEM_SIZE:
            if self.data[i] != 0:
                start = i
                while i < MEM_SIZE and self.data[i] != 0:
                    i += 1
                print(f'内存 [0x{start:04x} - 0x{i - 1:04x}]:')
                for row in range(start, i, 16):
                    end = min(row + 16, i)
                    hex_str = ' '.join(f'{self.data[j]:02x}' for j in range(row, end))
                    print(f'  0x{row:04x}: {hex_str}')
                found = True
            else:
                i += 1
        if not found:
            print('内存全为零')
