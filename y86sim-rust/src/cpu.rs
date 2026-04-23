use crate::instruction::*;
use crate::memory::Memory;

const NUM_REGS: usize = 16;
const STACK_TOP: u64 = 0x1000;

pub struct Cpu {
    pub regs: [u64; NUM_REGS],
    pub pc: u64,
    pub cc: ConditionCodes,
    pub status: Status,
    pub mem: Memory,
    pub stat: u64,
}

impl Cpu {
    pub fn new(mem: Memory) -> Self {
        let mut regs = [0u64; NUM_REGS];
        regs[Register::Rsp.as_index()] = STACK_TOP;
        Cpu {
            regs,
            pc: 0,
            cc: ConditionCodes::default(),
            status: Status::Aok,
            mem,
            stat: 0,
        }
    }

    pub fn read_reg(&self, r: Register) -> u64 {
        self.regs[r.as_index()]
    }

    pub fn write_reg(&mut self, r: Register, val: u64) {
        self.regs[r.as_index()] = val;
    }

    pub fn run(&mut self) -> Result<u64, String> {
        let mut cycles = 0u64;
        loop {
            if self.status != Status::Aok {
                break;
            }
            self.step()?;
            cycles += 1;
            if cycles > 10_000_000 {
                return Err("执行超过最大周期数限制".to_string());
            }
        }
        Ok(cycles)
    }

    fn step(&mut self) -> Result<(), String> {
        let op_byte = self.mem.read_byte(self.pc)?;
        let opcode = Opcode::from_byte(op_byte)
            .ok_or_else(|| format!("无效操作码 0x{:02x} @ PC=0x{:04x}", op_byte, self.pc))?;

        let len = opcode.instruction_length();

        // 读取指令全部字节
        let mut bytes = [0u8; 10];
        for i in 0..len {
            bytes[i] = self.mem.read_byte(self.pc + i as u64)?;
        }

        // 译码：提取寄存器、立即数
        let (ra, rb) = if len >= 2 {
            let reg_byte = bytes[1];
            let hi = (reg_byte >> 4) & 0x0F;
            let lo = reg_byte & 0x0F;
            (Register::from_nibble(hi), Register::from_nibble(lo))
        } else {
            (None, None)
        };

        let val_v = if len == 10 {
            let v_bytes: [u8; 8] = bytes[2..10].try_into().unwrap();
            u64::from_le_bytes(v_bytes)
        } else if len == 9 {
            let d_bytes: [u8; 8] = bytes[1..9].try_into().unwrap();
            u64::from_le_bytes(d_bytes)
        } else {
            0
        };

        // 推进 PC
        self.pc += len as u64;

        // 执行
        match opcode {
            Opcode::Halt => {
                self.status = Status::Hlt;
            }
            Opcode::Nop => {}
            Opcode::Rrmovq | Opcode::Cmovle | Opcode::Cmovl | Opcode::Cmove
            | Opcode::Cmovne | Opcode::Cmovge | Opcode::Cmovg => {
                let r_src = ra.ok_or("rrmovq: 缺少源寄存器")?;
                let r_dst = rb.ok_or("rrmovq: 缺少目标寄存器")?;
                if r_src == Register::None || r_dst == Register::None {
                    return Err(format!("rrmovq: 无效寄存器 @ PC=0x{:04x}", self.pc));
                }
                if opcode == Opcode::Rrmovq || self.cc.evaluate(opcode) {
                    self.write_reg(r_dst, self.read_reg(r_src));
                }
            }
            Opcode::Irmovq => {
                let r_dst = rb.ok_or("irmovq: 缺少目标寄存器")?;
                if r_dst == Register::None {
                    return Err(format!("irmovq: 无效寄存器 @ PC=0x{:04x}", self.pc));
                }
                self.write_reg(r_dst, val_v);
            }
            Opcode::Rmmovq => {
                let r_src = ra.ok_or("rmmovq: 缺少源寄存器")?;
                let r_base = rb.ok_or("rmmovq: 缺少基址寄存器")?;
                let addr = self.read_reg(r_base).wrapping_add(val_v);
                self.mem.write_word(addr, self.read_reg(r_src))?;
            }
            Opcode::Mrmovq => {
                let r_dst = ra.ok_or("mrmovq: 缺少目标寄存器")?;
                let r_base = rb.ok_or("mrmovq: 缺少基址寄存器")?;
                let addr = self.read_reg(r_base).wrapping_add(val_v);
                let val = self.mem.read_word(addr)?;
                self.write_reg(r_dst, val);
            }
            Opcode::Addq | Opcode::Subq | Opcode::Andq | Opcode::Xorq => {
                let r_a = ra.ok_or("ALU: 缺少源寄存器")?;
                let r_b = rb.ok_or("ALU: 缺少目标寄存器")?;
                let a = self.read_reg(r_a);
                let b = self.read_reg(r_b);
                let result = match opcode {
                    Opcode::Addq => a.wrapping_add(b),
                    Opcode::Subq => b.wrapping_sub(a),
                    Opcode::Andq => a & b,
                    Opcode::Xorq => a ^ b,
                    _ => unreachable!(),
                };
                self.cc.update(result, b, a, opcode);
                self.write_reg(r_b, result);
            }
            Opcode::Jmp | Opcode::Jle | Opcode::Jl | Opcode::Je
            | Opcode::Jne | Opcode::Jge | Opcode::Jg => {
                if self.cc.evaluate(opcode) {
                    self.pc = val_v;
                }
            }
            Opcode::Call => {
                let rsp = self.read_reg(Register::Rsp);
                let new_rsp = rsp.wrapping_sub(8);
                self.mem.write_word(new_rsp, self.pc)?;
                self.write_reg(Register::Rsp, new_rsp);
                self.pc = val_v;
            }
            Opcode::Ret => {
                let rsp = self.read_reg(Register::Rsp);
                let ret_addr = self.mem.read_word(rsp)?;
                let new_rsp = rsp.wrapping_add(8);
                self.write_reg(Register::Rsp, new_rsp);
                self.pc = ret_addr;
            }
            Opcode::Pushq => {
                let r_src = ra.ok_or("pushq: 缺少源寄存器")?;
                let rsp = self.read_reg(Register::Rsp);
                let new_rsp = rsp.wrapping_sub(8);
                self.mem.write_word(new_rsp, self.read_reg(r_src))?;
                self.write_reg(Register::Rsp, new_rsp);
            }
            Opcode::Popq => {
                let r_dst = ra.ok_or("popq: 缺少目标寄存器")?;
                let rsp = self.read_reg(Register::Rsp);
                let val = self.mem.read_word(rsp)?;
                let new_rsp = rsp.wrapping_add(8);
                self.write_reg(r_dst, val);
                self.write_reg(Register::Rsp, new_rsp);
            }
        }

        Ok(())
    }

    pub fn print_registers(&self) {
        let names = [
            "%rax", "%rcx", "%rdx", "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
            "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14",
        ];
        println!("=== 寄存器状态 ===");
        for (i, name) in names.iter().enumerate() {
            println!(
                "  {:>5} = {:20} (0x{:016x})",
                name, self.regs[i] as i64, self.regs[i]
            );
        }
        println!("  PC   = 0x{:016x}", self.pc);
        println!(
            "  CC   : ZF={} SF={} OF={}",
            self.cc.zf, self.cc.sf, self.cc.of
        );
        println!("  状态 : {:?}", self.status);
    }
}
