"""Y86-64 指令定义：操作码、寄存器、条件码"""

from enum import Enum, auto

MEM_SIZE = 65536
STACK_TOP = 0x1000

class Opcode(Enum):
    HALT    = 0x00
    NOP     = 0x10
    RRMOVQ  = 0x20
    CMOVLE  = 0x21
    CMOVL   = 0x22
    CMOVE   = 0x23
    CMOVNE  = 0x24
    CMOVGE  = 0x25
    CMOVG   = 0x26
    IRMOVQ  = 0x30
    RMMOVQ  = 0x40
    MRMOVQ  = 0x50
    ADDQ    = 0x60
    SUBQ    = 0x61
    ANDQ    = 0x62
    XORQ    = 0x63
    JMP     = 0x70
    JLE     = 0x71
    JL      = 0x72
    JE      = 0x73
    JNE     = 0x74
    JGE     = 0x75
    JG      = 0x76
    CALL    = 0x80
    RET     = 0x90
    PUSHQ   = 0xA0
    POPQ    = 0xB0

_OPCODE_MAP = {op.value: op for op in Opcode}

_INSTR_LEN = {
    1: (Opcode.HALT, Opcode.NOP, Opcode.RET),
    2: (Opcode.RRMOVQ, Opcode.CMOVLE, Opcode.CMOVL, Opcode.CMOVE,
        Opcode.CMOVNE, Opcode.CMOVGE, Opcode.CMOVG,
        Opcode.ADDQ, Opcode.SUBQ, Opcode.ANDQ, Opcode.XORQ,
        Opcode.PUSHQ, Opcode.POPQ),
    9: (Opcode.JMP, Opcode.JLE, Opcode.JL, Opcode.JE,
        Opcode.JNE, Opcode.JGE, Opcode.JG, Opcode.CALL),
    10: (Opcode.IRMOVQ, Opcode.RMMOVQ, Opcode.MRMOVQ),
}
LEN_BY_OPCODE = {}
for length, opcodes in _INSTR_LEN.items():
    for op in opcodes:
        LEN_BY_OPCODE[op] = length

ALU_OPS = {Opcode.ADDQ, Opcode.SUBQ, Opcode.ANDQ, Opcode.XORQ}
JXX_OPS = {Opcode.JMP, Opcode.JLE, Opcode.JL, Opcode.JE,
           Opcode.JNE, Opcode.JGE, Opcode.JG}
CMOV_OPS = {Opcode.CMOVLE, Opcode.CMOVL, Opcode.CMOVE,
            Opcode.CMOVNE, Opcode.CMOVGE, Opcode.CMOVG}

REG_NAMES = [
    "%rax", "%rcx", "%rdx", "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
    "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14",
]
# 寄存器编号 0-14 有效，15 = NONE
NONE_REG = 0xF


def decode_opcode(byte):
    return _OPCODE_MAP.get(byte)


def decode_regs(byte):
    """返回 (rA, rB) 各为 0-F"""
    return (byte >> 4) & 0xF, byte & 0xF


class ConditionCodes:
    __slots__ = ('zf', 'sf', 'of')

    def __init__(self):
        self.zf = False
        self.sf = False
        self.of = False

    def update(self, result, a, b, op):
        self.zf = (result == 0)
        self.sf = bool((result >> 63) & 1)
        if op == Opcode.ADDQ:
            self.of = (((~(a ^ b)) & (a ^ result)) >> 63) & 1
        elif op == Opcode.SUBQ:
            self.of = (((a ^ b) & (a ^ result)) >> 63) & 1
        else:
            self.of = 0

    def evaluate(self, op):
        sf, of, zf = self.sf, self.of, self.zf
        if op == Opcode.JMP:
            return True
        if op in (Opcode.JLE, Opcode.CMOVLE):
            return sf ^ of
        if op in (Opcode.JL, Opcode.CMOVL):
            return sf and not of
        if op in (Opcode.JE, Opcode.CMOVE):
            return zf
        if op in (Opcode.JNE, Opcode.CMOVNE):
            return not zf
        if op in (Opcode.JGE, Opcode.CMOVGE):
            return not (sf ^ of)
        if op in (Opcode.JG, Opcode.CMOVG):
            return not zf and not (sf ^ of)
        return False
