/**************************************************************************
	多项式计算函数。按下面的要求编辑此文件：
	1. 将你的学号、姓名，以注释的方式写到下面；
	2. 实现不同版本的多项式计算函数；
	3. 编辑peval_fun_rec peval_fun_tab数组，将你的最好的答案
		（最小CPE、最小C10）作为数组的前两项
***************************************************************************/
   
/*
	学号：201209054233
	姓名：夜半加班狂
*/



#include  <stdio.h>
#include  <stdlib.h>
typedef int (*peval_fun)(int*, int, int);

typedef struct {
  peval_fun f;
  char *descr;
} peval_fun_rec, *peval_fun_ptr;


/**************************************************************************
 Edit this comment to indicate your name and Andrew ID
#ifdef ASSIGN
   Submission by Harry Q. Bovik, bovik@andrew.cmu.edu
#else
   Instructor's version.
   Created by Randal E. Bryant, Randy.Bryant@cs.cmu.edu, 10/07/02
#endif
***************************************************************************/

/*
	实现一个指定的常系数多项式计算
	第一次，请直接运行程序，以便获知你需要实现的常系数是啥
*/
int const_poly_eval(int *not_use, int not_use2, int x)
{
    int result = 0;
/*    int i;
    int xpwr = 1; // x的幂次
    int a[4] = {21,90,42,88};
    for (i = 0; i <= 3; i++) {
	result += a[i]*xpwr;
	xpwr   *= x;
    }
*/
// 90 = 64 + 32 - 4 - 2
// 42 = 32 + 8 + 2
// 88 = 64 + 16 + 8
	int x64,x32,x16,x8,x4,x2;
	
	x64 = x << 6;
	x32 = x << 5;
	x16 = x << 4;
	x8 = x << 3;
	x4 = x << 2;
	x2 = x << 1;
	result = 21 + x64+x32-x4-x2 + ((x32+x8+x2) + (x64+x16+x8)*x)*x;
    return result;
}



/* 多项式计算函数。注意：这个只是一个参考实现，你需要实现自己的版本 */

/*
	友情提示：lcc支持ATT格式的嵌入式汇编，例如
	
	_asm("movl %eax,%ebx");
	_asm("pushl %edx");
	
	可以在lcc中project->configuration->Compiler->Code Generation->Generate .asm，
	将其选中后，可以在lcc目录下面生成对应程序的汇编代码实现。通过查看汇编文件，
	你可以了解编译器是如何实现你的代码的。有些实现可能非常低效。
	你可以在适当的地方加入嵌入式汇编，来大幅度提高计算性能。
*/

int poly_eval(int *a, int degree, int x)
{
    int result = 0;
    int i;
    int xpwr = 1; /* x的幂次 */
//    printf("阶=%d\n",degree);
    for (i = 0; i <= degree; i++) {
	result += a[i]*xpwr;
	xpwr   *= x;
    }
    return result;
}




/*
	这个表格包含多个数组元素，每一组元素（函数名字, "描述字符串"）
	将你认为最好的两个实现，放在最前面。
	比如：
	{my_poly_eval1, "超级垃圾实现"},
	{my_poly_eval2, "好一点的实现"},
*/
   
peval_fun_rec peval_fun_tab[] = 
{

  /* 第一项，应当是你写的最好CPE的函数实现 */
 {poly_eval, "夜半加班狂的CPE"},
  /* 第二项，应当是你写的在10阶时具有最好性能的实现 */
 {poly_eval, "夜半加班狂的10阶实现"},

 {poly_eval, "poly_eval: 参考实现"},

 /* 下面的代码不能修改或者删除！！表明数组列表结束 */
 {NULL, ""}
};







