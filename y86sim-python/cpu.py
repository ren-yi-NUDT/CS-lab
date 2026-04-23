"""CPU 核心：多周期取指-译码-执行"""

from instruction import (
    Opcode, LEN_BY_OPCODE, ALU_OPS, NONE_REG,
    REG_NAMES, ConditionCodes, decode_opcode, decode_regs,
)
from instruction import STACK_TOP
from memory import Memory

NUM_REGS = 15

MASK64 = (1 << 64) - 1


def to_i64(v):
    """将无符号 64 位值转为有符号"""
    v &= MASK64
    return v - (1 << 64) if v >= (1 << 63) else v


class Cpu:
    def __init__(self, mem: Memory):
        self.regs = [0] * NUM_REGS
        self.regs[4] = STACK_TOP  # %rsp
        self.pc = 0
        self.cc = ConditionCodes()
        self.status = 'AOK'
        self.mem = mem

    def _read_reg(self, r):
        if r == NONE_REG or r >= NUM_REGS:
            return 0
        return self.regs[r]

    def _write_reg(self, r, val):
        if r != NONE_REG and r < NUM_REGS:
            self.regs[r] = val & MASK64

    def run(self, max_cycles=10_000_000):
        cycles = 0
        while self.status == 'AOK':
            self._step()
            cycles += 1
            if cycles >= max_cycles:
                raise RuntimeError('执行超过最大周期数限制')
        return cycles

    def _step(self):
        op_byte = self.mem.read_byte(self.pc)
        opcode = decode_opcode(op_byte)
        if opcode is None:
            raise RuntimeError(f'无效操作码 0x{op_byte:02x} @ PC=0x{self.pc:04x}')

        length = LEN_BY_OPCODE[opcode]

        # 取指令全部字节
        raw = bytes(self.mem.data[self.pc:self.pc + length])

        # 译码
        rA = rB = NONE_REG
        if length >= 2:
            rA, rB = decode_regs(raw[1])

        val_v = 0
        if length == 10:
            val_v = int.from_bytes(raw[2:10], 'little')
        elif length == 9:
            val_v = int.from_bytes(raw[1:9], 'little')

        # 推进 PC
        self.pc += length

        # 执行
        if opcode == Opcode.HALT:
            self.status = 'HLT'

        elif opcode == Opcode.NOP:
            pass

        elif opcode == Opcode.RRMOVQ or opcode in (
                Opcode.CMOVLE, Opcode.CMOVL, Opcode.CMOVE,
                Opcode.CMOVNE, Opcode.CMOVGE, Opcode.CMOVG):
            if rA == NONE_REG or rB == NONE_REG:
                raise RuntimeError(f'rrmovq: 无效寄存器 @ PC=0x{self.pc:04x}')
            if opcode == Opcode.RRMOVQ or self.cc.evaluate(opcode):
                self._write_reg(rB, self._read_reg(rA))

        elif opcode == Opcode.IRMOVQ:
            if rB == NONE_REG:
                raise RuntimeError(f'irmovq: 无效寄存器 @ PC=0x{self.pc:04x}')
            self._write_reg(rB, val_v)

        elif opcode == Opcode.RMMOVQ:
            addr = (self._read_reg(rB) + val_v) & MASK64
            self.mem.write_word(addr, self._read_reg(rA))

        elif opcode == Opcode.MRMOVQ:
            addr = (self._read_reg(rB) + val_v) & MASK64
            self._write_reg(rA, self.mem.read_word(addr))

        elif opcode in ALU_OPS:
            a = self._read_reg(rA)
            b = self._read_reg(rB)
            if opcode == Opcode.ADDQ:
                result = (a + b) & MASK64
            elif opcode == Opcode.SUBQ:
                result = (b - a) & MASK64
            elif opcode == Opcode.ANDQ:
                result = a & b
            else:  # XORQ
                result = a ^ b
            self.cc.update(result, b, a, opcode)
            self._write_reg(rB, result)

        elif opcode in (Opcode.JMP, Opcode.JLE, Opcode.JL, Opcode.JE,
                        Opcode.JNE, Opcode.JGE, Opcode.JG):
            if self.cc.evaluate(opcode):
                self.pc = val_v

        elif opcode == Opcode.CALL:
            rsp = (self._read_reg(4) - 8) & MASK64
            self.mem.write_word(rsp, self.pc)
            self._write_reg(4, rsp)
            self.pc = val_v

        elif opcode == Opcode.RET:
            rsp = self._read_reg(4)
            ret_addr = self.mem.read_word(rsp)
            self._write_reg(4, (rsp + 8) & MASK64)
            self.pc = ret_addr

        elif opcode == Opcode.PUSHQ:
            rsp = (self._read_reg(4) - 8) & MASK64
            self.mem.write_word(rsp, self._read_reg(rA))
            self._write_reg(4, rsp)

        elif opcode == Opcode.POPQ:
            rsp = self._read_reg(4)
            val = self.mem.read_word(rsp)
            self._write_reg(rA, val)
            self._write_reg(4, (rsp + 8) & MASK64)

    def print_registers(self):
        print('=== 寄存器状态 ===')
        for i, name in enumerate(REG_NAMES):
            v = self.regs[i]
            print(f'  {name:>5} = {to_i64(v):>20} (0x{v:016x})')
        print(f'  PC   = 0x{self.pc:016x}')
        print(f'  CC   : ZF={self.cc.zf} SF={self.cc.sf} OF={self.cc.of}')
        print(f'  状态 : {self.status}')
