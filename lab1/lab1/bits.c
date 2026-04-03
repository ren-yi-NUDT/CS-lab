/*
 * CS:APP Data Lab
 *
 * bits.c - 你实验所需要修改本文件！
 *          你需要把此文件提交给老师/教辅。
 *
 */

#include "btest.h"
#include <limits.h>
#include <stdio.h>

/*
 * 学生做实验指导：
 *
 * 步骤1：将你的名字、学号填写如下面的结构中！
 */
team_struct team =
{
   /* Replace this with your full name */
   "任奕",

    /* Replace this with your Andrew login ID */
   "202402720028"
};

#if 0
/*
 * 步骤2：请仔细阅读下面的说明。
 */

你的数据实验，将通过编辑本文件（bits.c）中的各个函数，最后提交结果。

编程要求：

	将“return”语句替换成一行或者多行实现函数功能的C代码。
	你添加/修改的代码必须符合下面的风格：

  int Funct(arg1, arg2, ...) {
      /* 简要描述你是如何实现该函数功能的 */
      int var1 = Expr1;
      ...
      int varM = ExprM;

      varJ = ExprJ;
      ...
      varN = ExprN;
      return ExprR;
  }

	每一个“Expr”是一个仅仅使用下面内容的表达式：
  1. 0~255（0xFF）的整数常数。不允许使用大的常数，例如0xffffffff。
  2. 函数的参数和局部变量（不允许使用全局变量）。
  3. 规约的整数运算符" ! ~ "
  4. 二进制整数运算符“ & ^ | + << >> ”

	一些函数的实现，限制了能够使用的运算符。
	每一个“Expr”表达式可能包含多个运算符。没有要求你必须每行只用一个运算符。

	下列事项被严格禁止：
  1. 使用任何控制类的语句，例如if, do, while, for, switch, 等等。
  2. 定义或者使用任何宏。
  3. 在此文件中新增定义任何额外的函数。
  4. 调用任何函数。
  5. 使用任何其他的运算符，例如 &&, ||, -, 或者 ?:
  6. 使用任何形式的强制类型转换。

  你需要假设你的电脑：
  1. 使用2的补码，32位表示的整数。
  2. 右移运算是算术右移运算。
  3. 如果移位次数超过了字长，那么将会产生不可预测的结果。

可以接受的编程风格的示例：
  /*
   * pow2plus1 - returns 2^x + 1, where 0 <= x <= 31
   */
  int pow2plus1(int x) {
     /* exploit ability of shifts to compute powers of 2 */
     return (1 << x) + 1;
  }

  /*
   * pow2plus4 - returns 2^x + 4, where 0 <= x <= 31
   */
  int pow2plus4(int x) {
     /* exploit ability of shifts to compute powers of 2 */
     int result = (1 << x);
     result += 4;
     return result;
  }


注意：
  1. 首先使用dlc.exe检查你的bits.c是否符合编程要求。
  2. Each function has a maximum number of operators (! ~ & ^ | + << >>)
     that you are allowed to use for your implementation of the function.
     The max operator count is checked by dlc. Note that '=' is not
     counted; you may use as many of these as you want without penalty.
  3. 使用btest检查你的函数是否功能正确。
  4. The maximum number of ops for each function is given in the
     header comment for each function. If there are any inconsistencies
     between the maximum ops in the writeup and in this file, consider
     this file the authoritative source.
#endif

/*
 * 步骤3: 根据上面的编程要求，修改下面的函数。
 *
 *   重要！为了避免很差的成绩：
 *   1. 使用dlc.exe检查你的编程风格是否符合要求。
 *   2. 使用btest检查你的函数是否功能正确。请注意在Tmin和Tmax附近的特例是否正确。
 */



/*
 * bitAnd - x&y using only ~ and |
 *   Example: bitAnd(6, 5) = 4
 *   Legal ops: ~ |
 *   Max ops: 8
 *   Rating: 1
 */
int bitAnd(int x, int y) {
  return ~(~x | ~y);
}






/*
 * bitXor - x^y using only ~ and &
 *   Example: bitXor(4, 5) = 1
 *   Legal ops: ~ &
 *   Max ops: 14
 *   Rating: 2
 */
int bitXor(int x, int y) {

  return 2;

}






/*
 * evenBits - return word with all even-numbered bits set to 1
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 8
 *   Rating: 2
 */
int evenBits(void) {

  return 2;

}






/*
 * getByte - Extract byte n from word x
 *   Bytes numbered from 0 (LSB) to 3 (MSB)
 *   Examples: getByte(0x12345678,1) = 0x56
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 6
 *   Rating: 2
 */
int getByte(int x, int n) {

  return 2;

}






/*
 * bitMask - Generate a bitmask consisting of all 1's
 *   from lowbit to highbit and 0's everywhere else.
 *   Examples: bitMask(5,3) = 0x38
 *   Assume 0 <= lowbit <= 31, and 0 <= highbit <= 31
 *   If lowbit > highbit, then mask should be all 0's
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 16
 *   Rating: 3
 */
int bitMask(int highbit, int lowbit) {

  return 2;

}






/*
 * reverseBytes - reverse the bytes of x
 *   Example: reverseBytes(0x01020304) = 0x04030201
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 25
 *   Rating: 3
 */
int reverseBytes(int x) {
  return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24);
}






/*
 * leastBitPos - return a mask that marks the position of the
 *               least significant 1 bit. If x == 0, return 0
 *   Example: leastBitPos(96) = 0x20
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 6
 *   Rating: 4
 */
int leastBitPos(int x) {
  /* 利用 x & (-x) 可以得到最低位的1 */
  /* -x = ~x + 1 */
  return x & (~x + 1);
}






/*
 * logicalNeg - implement the ! operator, using all of
 *              the legal operators except !
 *   Examples: logicalNeg(3) = 0, logicalNeg(0) = 1
 *   Legal ops: ~ & ^ | + << >>
 *   Max ops: 12
 *   Rating: 4
 */
int logicalNeg(int x) {
  return ((x | (~x + 1)) >> 31) + 1;
}







/*
 * minusOne - return a value of -1
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 2
 *   Rating: 1
 */
int minusOne(void) {

  return 2;

}






/*
 * TMax - return maximum two's complement integer
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 4
 *   Rating: 1
 */
int tmax(void) {
  return ~(1 << 31);
}






/*
 * negate - return -x
 *   Example: negate(1) = -1.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 5
 *   Rating: 2
 */
int negate(int x) {

  return 2;

}






/*
 * isPositive - return 1 if x > 0, return 0 otherwise
 *   Example: isPositive(-1) = 0.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 8
 *   Rating: 3
 */
int isPositive(int x) {
  return (!(x >> 31)) & (!!x);
}






/*
 * isLess - if x < y  then return 1, else return 0
 *   Example: isLess(4,5) = 1.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 24
 *   Rating: 3
 */
int isLess(int x, int y) {

  return 2;

}






/*
 * sm2tc - Convert from sign-magnitude to two's complement
 *   where the MSB is the sign bit
 *   Example: sm2tc(0x80000005) = -5.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 15
 *   Rating: 4
 */
int sm2tc(int x) {

  return 2;

}


/*
 * greatestBitPos - return a mask that marks the position of the
 *                  most significant 1 bit. If x == 0, return 0
 *   Example: greatestBitPos(96) = 0x40
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 70
 *   Rating: 4
 */
int greatestBitPos(int x) {
  /* 将所有1位向右传播,然后取异或得到最高位 */
  int temp = x;
  temp = temp | (temp >> 1);
  temp = temp | (temp >> 2);
  temp = temp | (temp >> 4);
  temp = temp | (temp >> 8);
  temp = temp | (temp >> 16);
  /* temp 现在从最高位1到最低位都是1 */
  /* temp ^ (temp >> 1) 会留下最高位,但要处理负数情况 */
  return temp ^ ((temp >> 1) & ~(1 << 31));
}


/*
 * isNegative - return 1 if x < 0, return 0 otherwise
 *   Example: isNegative(-1) = 1.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 6
 *   Rating: 2
 */
int isNegative(int x) {
  /* 负数的符号位(第31位)为1 */
  return (x >> 31) & 1;
}


/*
 * isGreater - if x > y  then return 1, else return 0
 *   Example: isGreater(4,5) = 0, isGreater(5,4) = 1
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 24
 *   Rating: 3
 */
int isGreater(int x, int y) {
  /* x > y 等价于 y < x,可以利用 y - x 的结果 */
  /* 但需要考虑溢出情况 */
  int sign_x = (x >> 31) & 1;
  int sign_y = (y >> 31) & 1;
  int diff = y + (~x + 1);  /* y - x */
  int sign_diff = (diff >> 31) & 1;

  /* 如果符号相同,不会溢出,diff的符号决定结果 */
  /* 如果符号不同,需要特殊处理 */
  /* x > y 当且仅当:
     1. x正y负: 直接返回1
     2. x负y正: 直接返回0
     3. 同号: y - x < 0 且 y != x
  */
  int same_sign = !(sign_x ^ sign_y);
  int not_equal = !!diff;

  return ((same_sign & sign_diff & not_equal) | (!same_sign & !sign_x)) & 1;
}


/*
 * tmin - return minimum two's complement integer
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 4
 *   Rating: 1
 */
int tmin(void) {
  /* 最小补码整数是 0x80000000 */
  return 1 << 31;
}


/*
 * plusOne - return x + 1
 *   Example: plusOne(5) = 6
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 2
 *   Rating: 1
 */
int plusOne(int x) {
  /* 简单的加1操作 */
  return x + 1;
}




