use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Opcode {
    Halt,
    Nop,
    Rrmovq,
    Cmovle,
    Cmovl,
    Cmove,
    Cmovne,
    Cmovge,
    Cmovg,
    Irmovq,
    Rmmovq,
    Mrmovq,
    Addq,
    Subq,
    Andq,
    Xorq,
    Jmp,
    Jle,
    Jl,
    Je,
    Jne,
    Jge,
    Jg,
    Call,
    Ret,
    Pushq,
    Popq,
}

impl Opcode {
    pub fn from_byte(byte: u8) -> Option<Self> {
        match byte {
            0x00 => Some(Opcode::Halt),
            0x10 => Some(Opcode::Nop),
            0x20 => Some(Opcode::Rrmovq),
            0x21 => Some(Opcode::Cmovle),
            0x22 => Some(Opcode::Cmovl),
            0x23 => Some(Opcode::Cmove),
            0x24 => Some(Opcode::Cmovne),
            0x25 => Some(Opcode::Cmovge),
            0x26 => Some(Opcode::Cmovg),
            0x30 => Some(Opcode::Irmovq),
            0x40 => Some(Opcode::Rmmovq),
            0x50 => Some(Opcode::Mrmovq),
            0x60 => Some(Opcode::Addq),
            0x61 => Some(Opcode::Subq),
            0x62 => Some(Opcode::Andq),
            0x63 => Some(Opcode::Xorq),
            0x70 => Some(Opcode::Jmp),
            0x71 => Some(Opcode::Jle),
            0x72 => Some(Opcode::Jl),
            0x73 => Some(Opcode::Je),
            0x74 => Some(Opcode::Jne),
            0x75 => Some(Opcode::Jge),
            0x76 => Some(Opcode::Jg),
            0x80 => Some(Opcode::Call),
            0x90 => Some(Opcode::Ret),
            0xA0 => Some(Opcode::Pushq),
            0xB0 => Some(Opcode::Popq),
            _ => None,
        }
    }

    pub fn instruction_length(self) -> usize {
        match self {
            Opcode::Halt | Opcode::Nop | Opcode::Ret => 1,
            Opcode::Rrmovq
            | Opcode::Cmovle
            | Opcode::Cmovl
            | Opcode::Cmove
            | Opcode::Cmovne
            | Opcode::Cmovge
            | Opcode::Cmovg
            | Opcode::Addq
            | Opcode::Subq
            | Opcode::Andq
            | Opcode::Xorq
            | Opcode::Pushq
            | Opcode::Popq => 2,
            Opcode::Irmovq | Opcode::Rmmovq | Opcode::Mrmovq => 10,
            Opcode::Jmp
            | Opcode::Jle
            | Opcode::Jl
            | Opcode::Je
            | Opcode::Jne
            | Opcode::Jge
            | Opcode::Jg
            | Opcode::Call => 9,
        }
    }

    pub fn is_alu(self) -> bool {
        matches!(
            self,
            Opcode::Addq | Opcode::Subq | Opcode::Andq | Opcode::Xorq
        )
    }

    pub fn is_jxx(self) -> bool {
        matches!(
            self,
            Opcode::Jmp
                | Opcode::Jle
                | Opcode::Jl
                | Opcode::Je
                | Opcode::Jne
                | Opcode::Jge
                | Opcode::Jg
        )
    }

    pub fn is_cmov(self) -> bool {
        matches!(
            self,
            Opcode::Cmovle
                | Opcode::Cmovl
                | Opcode::Cmove
                | Opcode::Cmovne
                | Opcode::Cmovge
                | Opcode::Cmovg
        )
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Register {
    Rax,
    Rcx,
    Rdx,
    Rbx,
    Rsp,
    Rbp,
    Rsi,
    Rdi,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    None,
}

impl Register {
    pub fn from_nibble(nibble: u8) -> Option<Self> {
        match nibble {
            0x0 => Some(Register::Rax),
            0x1 => Some(Register::Rcx),
            0x2 => Some(Register::Rdx),
            0x3 => Some(Register::Rbx),
            0x4 => Some(Register::Rsp),
            0x5 => Some(Register::Rbp),
            0x6 => Some(Register::Rsi),
            0x7 => Some(Register::Rdi),
            0x8 => Some(Register::R8),
            0x9 => Some(Register::R9),
            0xA => Some(Register::R10),
            0xB => Some(Register::R11),
            0xC => Some(Register::R12),
            0xD => Some(Register::R13),
            0xE => Some(Register::R14),
            0xF => Some(Register::None),
            _ => None,
        }
    }

    pub fn as_index(self) -> usize {
        match self {
            Register::Rax => 0,
            Register::Rcx => 1,
            Register::Rdx => 2,
            Register::Rbx => 3,
            Register::Rsp => 4,
            Register::Rbp => 5,
            Register::Rsi => 6,
            Register::Rdi => 7,
            Register::R8 => 8,
            Register::R9 => 9,
            Register::R10 => 10,
            Register::R11 => 11,
            Register::R12 => 12,
            Register::R13 => 13,
            Register::R14 => 14,
            Register::None => 15,
        }
    }
}

impl fmt::Display for Register {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let name = match self {
            Register::Rax => "%rax",
            Register::Rcx => "%rcx",
            Register::Rdx => "%rdx",
            Register::Rbx => "%rbx",
            Register::Rsp => "%rsp",
            Register::Rbp => "%rbp",
            Register::Rsi => "%rsi",
            Register::Rdi => "%rdi",
            Register::R8 => "%r8",
            Register::R9 => "%r9",
            Register::R10 => "%r10",
            Register::R11 => "%r11",
            Register::R12 => "%r12",
            Register::R13 => "%r13",
            Register::R14 => "%r14",
            Register::None => "%none",
        };
        write!(f, "{name}")
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Aok,
    Hlt,
    Adr,
    Ins,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct ConditionCodes {
    pub zf: bool,
    pub sf: bool,
    pub of: bool,
}

impl ConditionCodes {
    pub fn update(&mut self, result: u64, a: u64, b: u64, op: Opcode) {
        self.zf = result == 0;
        self.sf = (result as i64) < 0;
        match op {
            Opcode::Addq => {
                self.of = ((a as i64) > 0 && (b as i64) > 0 && (result as i64) < 0)
                    || ((a as i64) < 0 && (b as i64) < 0 && (result as i64) > 0);
            }
            Opcode::Subq => {
                self.of = ((a as i64) > 0 && (b as i64) < 0 && (result as i64) < 0)
                    || ((a as i64) < 0 && (b as i64) > 0 && (result as i64) > 0);
            }
            _ => {
                self.of = false;
            }
        }
    }

    pub fn evaluate(self, op: Opcode) -> bool {
        match op {
            Opcode::Jmp => true,
            Opcode::Jle => self.sf ^ self.of,
            Opcode::Jl => self.sf && !self.of,
            Opcode::Je => self.zf,
            Opcode::Jne => !self.zf,
            Opcode::Jge => !(self.sf ^ self.of),
            Opcode::Jg => !self.zf && !(self.sf ^ self.of),
            // cmovXX uses same condition logic
            Opcode::Cmovle => self.sf ^ self.of,
            Opcode::Cmovl => self.sf && !self.of,
            Opcode::Cmove => self.zf,
            Opcode::Cmovne => !self.zf,
            Opcode::Cmovge => !(self.sf ^ self.of),
            Opcode::Cmovg => !self.zf && !(self.sf ^ self.of),
            _ => false,
        }
    }
}
