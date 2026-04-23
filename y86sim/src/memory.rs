const MEM_SIZE: usize = 65536;

pub struct Memory {
    data: Vec<u8>,
}

impl Memory {
    pub fn new() -> Self {
        Memory {
            data: vec![0; MEM_SIZE],
        }
    }

    pub fn load_from_coe(&mut self, path: &str) -> Result<usize, String> {
        let content = std::fs::read_to_string(path)
            .map_err(|e| format!("无法读取文件 '{}': {}", path, e))?;

        let mut radix = 16u32;
        let mut data_section = false;
        let mut loaded = 0usize;
        let mut addr = 0usize;

        for line in content.lines() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }

            let lower = line.to_lowercase();
            if lower.starts_with("memory_initialization_radix") {
                let parts: Vec<&str> = line.split('=').collect();
                if parts.len() == 2 {
                    radix = parts[1].trim().trim_end_matches(';').trim().parse::<u32>()
                        .map_err(|_| format!("无效的 radix: {}", parts[1].trim()))?;
                }
                continue;
            }

            if lower.starts_with("memory_initialization_vector") {
                data_section = true;
                // handle values on same line after '='
                if let Some(pos) = line.find('=') {
                    let rest = &line[pos + 1..];
                    if !rest.trim().is_empty() {
                        self.parse_data_values(rest, radix, &mut addr, &mut loaded)?;
                    }
                }
                continue;
            }

            if data_section {
                self.parse_data_values(line, radix, &mut addr, &mut loaded)?;
            }
        }

        Ok(loaded)
    }

    fn parse_data_values(
        &mut self,
        text: &str,
        radix: u32,
        addr: &mut usize,
        loaded: &mut usize,
    ) -> Result<(), String> {
        let text = text.trim();
        if text.is_empty() {
            return Ok(());
        }

        let is_end = text.ends_with(';');
        let text = text.trim_end_matches(';');

        for token in text.split(',') {
            let token = token.trim();
            if token.is_empty() {
                continue;
            }
            let value = u64::from_str_radix(token, radix)
                .map_err(|_| format!("无法解析数据项 '{}' (radix={})", token, radix))?;

            if *addr < MEM_SIZE {
                self.data[*addr] = value as u8;
                *addr += 1;
                *loaded += 1;
            }
        }

        let _ = is_end;
        Ok(())
    }

    pub fn read_byte(&self, addr: u64) -> Result<u8, String> {
        let a = addr as usize;
        if a >= MEM_SIZE {
            return Err(format!("地址越界: 0x{:x}", addr));
        }
        Ok(self.data[a])
    }

    pub fn write_byte(&mut self, addr: u64, val: u8) -> Result<(), String> {
        let a = addr as usize;
        if a >= MEM_SIZE {
            return Err(format!("地址越界: 0x{:x}", addr));
        }
        self.data[a] = val;
        Ok(())
    }

    pub fn read_word(&self, addr: u64) -> Result<u64, String> {
        let a = addr as usize;
        if a + 8 > MEM_SIZE {
            return Err(format!("地址越界: 0x{:x}", addr));
        }
        let bytes: [u8; 8] = self.data[a..a + 8].try_into().unwrap();
        Ok(u64::from_le_bytes(bytes))
    }

    pub fn write_word(&mut self, addr: u64, val: u64) -> Result<(), String> {
        let a = addr as usize;
        if a + 8 > MEM_SIZE {
            return Err(format!("地址越界: 0x{:x}", addr));
        }
        self.data[a..a + 8].copy_from_slice(&val.to_le_bytes());
        Ok(())
    }

    pub fn dump(&self, start: u64, len: usize) {
        let s = start as usize;
        let end = std::cmp::min(s + len, MEM_SIZE);
        for i in (s..end).step_by(16) {
            let mut hex = String::new();
            let mut ascii = String::new();
            for j in 0..16 {
                if i + j < end {
                    hex.push_str(&format!("{:02x} ", self.data[i + j]));
                    let c = self.data[i + j];
                    ascii.push(if c.is_ascii_graphic() || c == b' ' {
                        c as char
                    } else {
                        '.'
                    });
                } else {
                    hex.push_str("   ");
                }
            }
            println!("0x{:04x}: {} {}", i, hex, ascii);
        }
    }

    pub fn dump_nonzero(&self) {
        let mut found = false;
        let mut i = 0;
        while i < MEM_SIZE {
            if self.data[i] != 0 {
                let start = i;
                while i < MEM_SIZE && self.data[i] != 0 {
                    i += 1;
                }
                println!("内存 [0x{:04x} - 0x{:04x}]:", start, i - 1);
                for row in (start..i).step_by(16) {
                    let end = std::cmp::min(row + 16, i);
                    let hex: Vec<String> = (row..end).map(|j| format!("{:02x}", self.data[j])).collect();
                    println!("  0x{:04x}: {}", row, hex.join(" "));
                }
                found = true;
            } else {
                i += 1;
            }
        }
        if !found {
            println!("内存全为零");
        }
    }
}
