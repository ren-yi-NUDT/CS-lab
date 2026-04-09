/* Testing Code */

#include <limits.h>




int test_bitAnd(int x, int y)
{
  return x&y;
}






int test_bitXor(int x, int y)
{
  return x^y;
}






int test_evenBits(void) {
  int result = 0;
  int i;
  for (i = 0; i < 32; i+=2)
    result |= 1<<i;
  return result;
}






int test_getByte(int x, int n)
{
  union  {
    int word;
    unsigned char bytes[4];
  } u;
  int test = 1;
  int littleEndian = (int) *(char *) &test;
  u.word = x;
  return littleEndian ? (unsigned) u.bytes[n] : (unsigned) u.bytes[3-n];
}






int test_bitMask(int highbit, int lowbit)
{
  int result = 0;
  int i;
  for (i = lowbit; i <= highbit; i++)
    result |= 1 << i;
  return result;
}






int test_reverseBytes(int x)
{
  union U {
    int result;
    char byte[4];
  };
  union U u;
  int temp;
  u.result = x;
  temp = u.byte[0];
  u.byte[0] = u.byte[3];
  u.byte[3] = temp;
  temp = u.byte[1];
  u.byte[1] = u.byte[2];
  u.byte[2] = temp;
  return u.result;
}






int test_leastBitPos(int x) {
  int mask = 1;

  if (x == 0)
    return 0;
  while (!(mask & x)) {
    mask = mask << 1;
  }
  return mask;
}






int test_logicalNeg(int x)
{
  return !x;
}







int test_minusOne(void) {
  return -1;
}






int test_tmax(void) {
  return 0x7FFFFFFF;
}






int test_negate(int x) {
  return -x;
}






int test_isPositive(int x) {
  return x > 0;
}






int test_isLess(int x, int y)
{
  return x < y;
}






int test_sm2tc(int x) {
  int sign = x < 0;
  int mag  = x & LONG_MAX;
  return sign ? -mag : mag;
}


int test_greatestBitPos(int x) {
  unsigned mask = 1 << 31;
  if (x == 0) return 0;
  while (!(x & mask)) {
    mask >>= 1;
  }
  return mask;
}


int test_isNegative(int x) {
  return x < 0;
}


int test_isGreater(int x, int y) {
  return x > y;
}


int test_tmin(void) {
  return 0x80000000;
}


int test_plusOne(int x) {
  return x + 1;
}



