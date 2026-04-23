mod cpu;
mod instruction;
mod memory;

use cpu::Cpu;
use memory::Memory;
use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("用法: {} <coe_file> [--dump <addr> <len>]", args[0]);
        std::process::exit(1);
    }

    let coe_path = &args[1];

    let mut mem = Memory::new();
    match mem.load_from_coe(coe_path) {
        Ok(n) => println!("已加载 {} 字节机器码", n),
        Err(e) => {
            eprintln!("加载失败: {}", e);
            std::process::exit(1);
        }
    }

    let mut cpu = Cpu::new(mem);

    println!("开始执行...");
    match cpu.run() {
        Ok(cycles) => println!("执行完成，共 {} 个周期", cycles),
        Err(e) => eprintln!("执行错误: {}", e),
    }

    cpu.print_registers();

    // 检查是否有 --dump 参数
    let mut i = 2;
    while i < args.len() {
        if args[i] == "--dump" && i + 2 < args.len() {
            let addr = u64::from_str_radix(args[i + 1].trim_start_matches("0x"), 16)
                .unwrap_or(0);
            let len: usize = args[i + 2].parse().unwrap_or(64);
            println!("\n=== 内存转储 0x{:04x} + {} 字节 ===", addr, len);
            cpu.mem.dump(addr, len);
            i += 3;
        } else if args[i] == "--mem" {
            println!("\n=== 非零内存内容 ===");
            cpu.mem.dump_nonzero();
            i += 1;
        } else {
            i += 1;
        }
    }
}
