/**************************************************************************
	多项式计算函数，只需要编辑本文件
	1. 实现不同版本的多项式计算函数
	2. 编辑peval_fun_rec peval_fun_tab数组，将你写的代码
		最小CPE、最小C10放在前面
***************************************************************************/



#include  <stdio.h>
#include  <stdlib.h>
typedef int (*peval_fun)(int*, int, int);

typedef struct {
  peval_fun f;
  char *descr;
} peval_fun_rec, *peval_fun_ptr;


/* 常系数多项式：完全展开的 Horner 法，degree=3 */
int const_poly_eval(int *a, int degree, int x)
{
    return a[0] + x * (a[1] + x * (a[2] + x * a[3]));
}

/* CPE 优化版：4x4a — 4路独立累加器 + Horner(x^4) 产生指令级并行 */
int poly_eval_cpe(int *a, int degree, int x)
{
    int i;
    int x2 = x * x;
    int x4 = x2 * x2;
    int acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;

    for (i = degree; i >= 3; i -= 4) {
	acc3 = acc3 * x4 + a[i];
	acc2 = acc2 * x4 + a[i - 1];
	acc1 = acc1 * x4 + a[i - 2];
	acc0 = acc0 * x4 + a[i - 3];
    }

    /* 组合4路累加器: acc0 + acc1*x + acc2*x^2 + acc3*x^3 */
    int result = acc3;
    result = result * x + acc2;
    result = result * x + acc1;
    result = result * x + acc0;

    /* 折叠剩余低阶项 */
    while (i >= 0) {
	result = result * x + a[i];
	i--;
    }

    return result;
}

/* C(10) 优化版：Horner 法 + 4x1a 循环展开 */
int poly_eval_c10(int *a, int degree, int x)
{
    int i;
    int result = a[degree];

    for (i = degree - 1; i >= 3; i -= 4) {
	result = result * x + a[i];
	result = result * x + a[i - 1];
	result = result * x + a[i - 2];
	result = result * x + a[i - 3];
    }

    for (; i >= 0; i--) {
	result = result * x + a[i];
    }

    return result;
}

/* 参考实现 */
int poly_eval(int *a, int degree, int x)
{
    int result = a[degree];
    int i;
    for (i = degree - 1; i >= 0; i--) {
	result = result * x + a[i];
    }
    return result;
}


peval_fun_rec peval_fun_tab[] =
{
  /* 第一项，应该放你写的用于最小CPE的函数实现 */
 {poly_eval_cpe, "CPE"},
  /* 第二项，应该放你写的用于最小10阶时执行时间性能的实现 */
 {poly_eval_c10, "C(10)"},

 {poly_eval, "poly_eval"},

 /* 后面的代码不要修改或删除，用于结束列表 */
 {NULL, ""}
};
