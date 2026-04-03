#include <stdio.h>
#include <limits.h>

// 函数声明
int greatestBitPos(int x);
int leastBitPos(int x);
int isNegative(int x);
int isGreater(int x, int y);
int tmin(void);
int plusOne(int x);

int main() {
    printf("=== 测试 greatestBitPos ===\n");
    printf("greatestBitPos(0) = 0x%x (期望: 0x0)\n", greatestBitPos(0));
    printf("greatestBitPos(96) = 0x%x (期望: 0x40)\n", greatestBitPos(96));
    printf("greatestBitPos(255) = 0x%x (期望: 0x80)\n", greatestBitPos(255));
    printf("greatestBitPos(-1) = 0x%x (期望: 0x80000000)\n", greatestBitPos(-1));

    printf("\n=== 测试 leastBitPos ===\n");
    printf("leastBitPos(0) = 0x%x (期望: 0x0)\n", leastBitPos(0));
    printf("leastBitPos(96) = 0x%x (期望: 0x20)\n", leastBitPos(96));
    printf("leastBitPos(1) = 0x%x (期望: 0x1)\n", leastBitPos(1));
    printf("leastBitPos(-1) = 0x%x (期望: 0x1)\n", leastBitPos(-1));

    printf("\n=== 测试 isNegative ===\n");
    printf("isNegative(-1) = %d (期望: 1)\n", isNegative(-1));
    printf("isNegative(0) = %d (期望: 0)\n", isNegative(0));
    printf("isNegative(1) = %d (期望: 0)\n", isNegative(1));
    printf("isNegative(0x80000000) = %d (期望: 1)\n", isNegative(INT_MIN));

    printf("\n=== 测试 isGreater ===\n");
    printf("isGreater(4, 5) = %d (期望: 0)\n", isGreater(4, 5));
    printf("isGreater(5, 4) = %d (期望: 1)\n", isGreater(5, 4));
    printf("isGreater(5, 5) = %d (期望: 0)\n", isGreater(5, 5));
    printf("isGreater(-1, 1) = %d (期望: 0)\n", isGreater(-1, 1));
    printf("isGreater(1, -1) = %d (期望: 1)\n", isGreater(1, -1));

    printf("\n=== 测试 tmin ===\n");
    printf("tmin() = 0x%x (期望: 0x80000000)\n", tmin());

    printf("\n=== 测试 plusOne ===\n");
    printf("plusOne(5) = %d (期望: 6)\n", plusOne(5));
    printf("plusOne(-1) = %d (期望: 0)\n", plusOne(-1));
    printf("plusOne(0) = %d (期望: 1)\n", plusOne(0));

    return 0;
}
