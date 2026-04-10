// ============================================================
// 超级二进制炸弹2024版 — 反编译 C++ 可运行版本
// 编译: g++ -o bomb_decompiled bomb_decompiled.cpp -no-pie -z execstack
// 运行: ./bomb_decompiled 720028 < bomb_720028.txt
// ============================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <unistd.h>
#include <sys/mman.h>

// ============================================================
// 全局变量
// ============================================================

long long rand1_h;   // 0x408810
long long rand1_l;   // 0x408818
long long rand_div;  // 0x408820
int      result;     // 0x4087f8, Phase 5 使用

FILE *g_infile = nullptr;
char  g_linebuf[1024];

// 学号 720028 的随机数序列（从原炸弹用 GDB 逐个提取）
// 调用顺序: GenerateRandomNumber(0) → GenerateRandomNumber(2)*10 → ...
// 这里不还原算法，直接用 GDB 提取的值
struct RandomSequence {
    // Phase 1 用到的随机值: GenerateRandomString 生成 "yxYZRkQzJt"
    const char *phase1_secret;

    // Phase 2: 子关卡分发, a[0] 的约束
    int p2_sub;       // GenerateRandomNumber(16) → 13
    int p2_a0_rand;   // GenerateRandomNumber(50) → 28, a[0] = 28+16 = 44

    // Phase 3: 子关卡分发, val1 约束, char, val2
    int p3_sub;       // GenerateRandomNumber(14) → 13
    int p3_val1_rand; // GenerateRandomNumber(8) → 3, val1 = 3+130 = 133
    char p3_ch;       // 'E'
    int p3_val2;      // 106

    // Phase 4: 子关卡分发, 表索引
    int p4_sub;       // GenerateRandomNumber(20) → 14 (实际跳转到 phase_4_24)
    int p4_table_idx; // GenerateRandomNumber(7) → 5

    // Phase 5: XOR 校验, 跳转方式
    int p5_xor_rand;  // GenerateRandomNumber(0x400) → 540
    int p5_goto_rand; // GenerateRandomNumber(3) → 2
};

// 学号 720028 的提取结果
RandomSequence SEQ_720028 = {
    "yxYZRkQzJt",  // phase1_secret
    13, 28,         // phase 2
    13, 3, 'E', 106, // phase 3
    14, 5,          // phase 4
    540, 2          // phase 5
};

RandomSequence g_seq;  // 当前使用的序列

// ============================================================
// 工具函数
// ============================================================

void explode_bomb()
{
    printf("爆了爆了.....\n请重新再来！\n");
    exit(1);
}

char* read_line()
{
    if (fgets(g_linebuf, sizeof(g_linebuf), g_infile))
        return g_linebuf;
    explode_bomb();
    return nullptr;
}

void strip_newline(char *s)
{
    char *p = strchr(s, '\n'); if (p) *p = '\0';
    p = strchr(s, '\r'); if (p) *p = '\0';
}

long long GetTickCount_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ============================================================
// Phase 1: 字符串比较
//
// 反汇编: 0x401b53 <phase_1>
//   call GenerateRandomString  → 生成随机字符串
//   call strcmp(input, secret)
//   不相等 → explode_bomb
// ============================================================

void phase_1(char *input)
{
    strip_newline(input);
    if (strcmp(input, g_seq.phase1_secret) != 0)
        explode_bomb();
}

// ============================================================
// Phase 2: 六个整数 (随机子关卡)
//
// 反汇编: 0x401b8e <phase_2>
//   GenerateRandomNumber(16) → rand_div ∈ {0..15}
//   跳转表分发到 phase_2_0 ~ phase_2_15
//
// phase_2_13 (学号 720028):
//   read_six_numbers(input, a)
//   GenerateRandomNumber(50) → a[0] == rand_div + 16
//   所有 a[i] >= 0
//   a[i] > a[i-1]  (严格递增)
// ============================================================

void phase_2(char *input)
{
    // 此处简化: 直接用提取的子关卡参数
    if (g_seq.p2_sub != 13) {
        printf("进入了 phase_2_%d（本程序仅实现 phase_2_13）\n", g_seq.p2_sub);
        explode_bomb();
    }

    int a[6];
    if (sscanf(input, "%d %d %d %d %d %d",
               &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6)
        explode_bomb();

    // 约束 1: a[0] == rand_div(50) + 16
    if (a[0] != g_seq.p2_a0_rand + 16)
        explode_bomb();

    // 约束 2: 所有元素 >= 0
    for (int i = 0; i < 6; i++)
        if (a[i] < 0) explode_bomb();

    // 约束 3: 严格递增
    for (int i = 1; i < 6; i++)
        if (a[i] <= a[i - 1]) explode_bomb();
}

// ============================================================
// Phase 3: 整数+字符+整数 (随机子关卡)
//
// 反汇编: 0x4028b4 <phase_3> 分发
//
// phase_3_13 (学号 720028):
//   sscanf(input, "%d %c %d", &val1, &ch, &val2)
//   val1 == rand_div(8) + 130
//   switch(val1 - 130) 生成 expected_ch 和 expected_val2
//   ch == expected_ch, val2 == expected_val2
// ============================================================

void phase_3(char *input)
{
    if (g_seq.p3_sub != 13) {
        printf("进入了 phase_3_%d（本程序仅实现 phase_3_13）\n", g_seq.p3_sub);
        explode_bomb();
    }

    int val1;
    char ch;
    int val2;
    if (sscanf(input, "%d %c %d", &val1, &ch, &val2) != 3)
        explode_bomb();

    // 约束 1: val1 == rand_div(8) + 130
    int expected_val1 = g_seq.p3_val1_rand + 130;
    if (val1 != expected_val1)
        explode_bomb();

    // 约束 2: ch 和 val2 由 switch case 内部的 GenerateRandomNumber 决定
    if (ch != g_seq.p3_ch) explode_bomb();
    if (val2 != g_seq.p3_val2) explode_bomb();
}

// ============================================================
// Phase 4: 阶乘校验 (随机子关卡)
//
// 反汇编: 0x4045bb <phase_4> 分发
//   rand_div=14 → phase_4_24 (跳转表陷阱!)
//
// phase_4_24:
//   sscanf(input, "%d", &x)
//   x > 0 且 x > 499  (即 x >= 500)
//   quotient = x / 500  (魔数除法 0x10624dd3)
//   func4_2(quotient) == table[GenerateRandomNumber(7)]
//
// func4_2: if n<=1 return 1; else return n * func4_2(n-1);  即 n!
// ============================================================

long long factorial(long long n)
{
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

void phase_4(char *input)
{
    int table[7] = {24, 120, 720, 5040, 40320, 362880, 3628800};  // 4!~10!

    int x;
    if (sscanf(input, "%d", &x) != 1) explode_bomb();

    // 约束 1: x > 0
    if (x <= 0) explode_bomb();

    // 约束 2: x > 499  (原汇编: cmp $0x1f3; jg)
    if (x <= 499) explode_bomb();

    // 约束 3: quotient = x / 500
    int quotient = x / 500;

    // 约束 4: factorial(quotient) == table[rand_div(7)]
    long long fact = factorial(quotient);
    if (fact != table[g_seq.p4_table_idx])
        explode_bomb();
}

// ============================================================
// Phase 5: 不可能任务 — Shellcode 注入
//
// 反汇编: 0x401777 <phase_impossible>
//
// 流程:
//   1. strlen(input) > 9 且 <= 768
//   2. tohex(input) → buf[256]  (十六进制字符串转原始字节)
//   3. check_buf_valid: XOR(buf[0..255]) == rand_div & 0xFF
//   4. goto_buf_X(buf): 跳转到 buf 地址执行 shellcode
//   5. shellcode 跳回后: result == GenerateRandomNumber(0x400)
//   6. 执行时间 <= 1000ms
//
// tohex 反汇编: 0x401913
//   逐字符处理, 每两个 hex 字符合成一个字节
//
// check_buf_valid 反汇编: 0x401a41
//   xor_result = 0; for(i=0;i<256;i++) xor_result ^= buf[i];
//   return xor_result == (rand_div & 0xFF);
// ============================================================

void tohex(unsigned char *buf, const char *hex_str)
{
    int pos = 0, high_nibble = 1, value = 0;
    for (int i = 0; hex_str[i] && hex_str[i] != '\n' && hex_str[i] != '\r'; i++) {
        char c = hex_str[i];
        if (!isprint(c)) break;
        int digit;
        if (c >= '0' && c <= '9')       digit = c - '0';
        else if (c >= 'A' && c <= 'F')  digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')  digit = c - 'a' + 10;
        else continue;
        if (high_nibble) { value = digit; high_nibble = 0; }
        else { buf[pos++] = (value << 4) | digit; high_nibble = 1; }
    }
}

int check_buf_valid(unsigned char *buf, int target)
{
    unsigned char x = 0;
    for (int i = 0; i < 256; i++) x ^= buf[i];
    return x == (unsigned char)(target & 0xFF);
}

// phase_secret — 隐藏彩蛋（死代码, 正常流程不会调用）
// 反汇编: 0x401a8b <phase_secret>
void phase_secret()
{
    printf("不可能的...不可能的...指令和数据的世界已经混乱...SOS...\n");
}

void phase_impossible(char *input)
{
    strip_newline(input);
    long long start_time = GetTickCount_ms();

    // 1. 长度校验
    size_t len = strlen(input);
    if (len <= 9 || len > 0x300)
        explode_bomb();

    // 2. hex → 字节
    unsigned char *buf = new unsigned char[256]();
    tohex(buf, input);

    // 3. XOR 校验
    if (!check_buf_valid(buf, g_seq.p5_xor_rand)) {
        printf("对不起，输入的攻击指令，没有通过数据校验....\n");
        explode_bomb();
    }

    // ---- 原炸弹从这里开始执行 shellcode ----
    // 原炸弹流程:
    //   goto_buf_2(buf)  →  跳到 buf 地址执行 shellcode
    //   shellcode 内容:
    //     call GenerateRandomNumber(0x400)  → rand_div 更新
    //     result = rand_div
    //     call phase_secret()               → 打印 SOS 消息 (隐藏彩蛋)
    //     jmp 0x4018a0                       → 跳回主程序比较逻辑
    //
    //   shellcode 返回后 (0x4018a0):
    //     GenerateRandomNumber(0x400)
    //     if (result != rand_div) explode_bomb()
    //     if (耗时 > 1000ms)      explode_bomb()
    //
    // 在 C++ 中模拟 shellcode 的效果:
    phase_secret();     // ★ 触发隐藏彩蛋 (原炸弹靠 shellcode call 触发)

    // 模拟 GenerateRandomNumber(0x400) 的效果
    // shellcode 调一次, 主程序再调一次, 两次 rand_div 应该相同 = 540
    // (实际上 shellcode 的结果已经通过 GDB 预知)

    // 4. 超时检测
    if (GetTickCount_ms() - start_time > 1000) {
        printf("攻击超时了，重来吧....\n");
        explode_bomb();
    }

    delete[] buf;
}

// ============================================================
// main 函数
// 反汇编: 0x4012d0 <main>
// ============================================================

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("用法: %s <学号后6位>\n例如: %s 720028 < bomb_720028.txt\n", argv[0], argv[0]);
        return 1;
    }

    int sid = atoi(argv[1]);
    g_seq = SEQ_720028;  // 目前只实现了 720028 的随机数

    // 打开答案文件
    char filename[256];
    sprintf(filename, "bomb_%d.txt", sid);
    g_infile = fopen(filename, "r");
    if (g_infile) {
        printf("嗯嗯，找到%s文件....从里面读取以前的密码...\n", filename);
    } else {
        g_infile = stdin;
    }

    printf("    超级二进制炸弹2024版，欢迎你！\n");
    printf("============================\n");
    printf("\t欢迎你！%d\n", sid);
    printf("============================\n");

    // Phase 1
    printf("请输入第1级的密码：");
    phase_1(read_line());
    printf("牛刀小试~你已经通过了第1级考验！\n");

    // Phase 2
    printf("请输入第2级的密码（6个数字，用空格隔开，例如 1 2 3 4 5 6）：");
    phase_2(read_line());
    printf("不错不错~你已经通过了第2级考验！\n");

    // Phase 3
    printf("请输入第3级的密码：");
    phase_3(read_line());
    printf("今夜没加班？ 你已经通过了第3级考验！\n");

    // Phase 4
    printf("请输入第4级的密码：");
    phase_4(read_line());
    printf("完美了~你已经通过了第4级考验！\n");

    // Phase 5 入口
    printf("二进制炸弹之不可能任务，你的选择是继续前行（Y），或者放弃（N）：");
    char choice;
    sscanf(read_line(), "%c", &choice);
    if (choice == 'N' || choice == 'n') {
        printf("知足常乐啊？拜拜！\n");
        if (g_infile != stdin) fclose(g_infile);
        return 0;
    }

    printf("友情提示：后面的代码，涉及到反调试、动态生成指令、执行超时检测等，所以你要有思想准备......\n");
    printf("不可能任务，请输入突防指令：");
    phase_impossible(read_line());

    printf("你已经通过了第5级考验，完成了不可能完成任务（终极考验）！\n");
    printf("温馨提示：炸弹还有隐藏彩蛋哦...\n");

    if (g_infile != stdin) fclose(g_infile);
    return 0;
}
