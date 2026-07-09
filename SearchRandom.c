/*
	某个应用程序的随机数产生机制是这样的：
	1、首先产生一个0~0xFFFFFFFF之间的数，称为“密码”X
	2、用(X,0x29A)调用随机数更新函数GenerateRandomNumber，产生新的(Y,Z)
	3、用(Y,Z)调用随机数更新函数GenerateRandomNumber，产生新的(A,B)
	4、把B传给用户
	
	本程序（你的）目的，就是在知道这套机制、知道B的前提下，计算出X是多少？
	
	非常简单的思路：
	1、循环，从i=0~0xFFFFFFFF
	2、GenerateRandomNumber(i,0x29A)
	3、GenerateRandomNumber(A,B)
	4、判断B是否等于想要找的值
	
*/

#include <stdio.h>
#include <time.h>

/* 更新随机数 */
void GenerateRandomNumber(unsigned int *rand1_h,unsigned int *rand1_l)
{
	/*请忽略下列代码，知道是产生更新随机数即可，原理不必弄清楚*/
	unsigned long long x = (unsigned long long)*rand1_h;
	x *= 0x6AC690C5;
	x += *rand1_l;

	*rand1_h = (unsigned int)x;
	*rand1_l = (unsigned int)(x>>32);
}

void SearchRandomNumber(unsigned int begin_h,unsigned int begin_l, unsigned int search_val)
{
	/* 在begin_l~begin_h之间，计算B的值，看看是否和search_val相等*/
	unsigned int i;
	unsigned int h,l;

	for (i=begin_l;i<begin_h;i++)
	{
		h = i;
		l = 0x29A;
		GenerateRandomNumber(&h,&l);	// 第一次调用
		GenerateRandomNumber(&h,&l);	// 第二次调用
		if (l==search_val)						// 判断B是否和search_val相等
		{
			printf("找到啦~! 密码是%08X\n",i);	// 相等，找到了~~
		}
	}
}

int main()
{
	time_t begin_time,end_time;
	
	printf("这是一个标准、最慢的搜索程序，拼主频！\n");
	
	begin_time = time(NULL);							// 记录开始时刻
	SearchRandomNumber(0xFFFFFFFF,0,0x39A6FFBB);	// 在0~0xFFFFFFFF之间进行搜索
	// 0x39A6FFBB => 0x70FFFFFF
	end_time = time(NULL);								// 记录结束时刻

	printf("耗时%ld秒~~\n",end_time-begin_time);	// 结束-开始=耗时
  return 0;
}
