/**************************************************************************
	Matrix row/column sum optimization
***************************************************************************/

#include  <stdio.h>
#include  <stdlib.h>
#include  "rowcol.h"
#include  <math.h>

/* Column sum: row-major traversal + 4x loop unrolling */
void c_sum(matrix_t M, vector_t rowsum, vector_t colsum)
{
    int i, j;
    for (j = 0; j < N; j++)
	colsum[j] = 0;
    for (i = 0; i < N; i++) {
	for (j = 0; j < N; j += 4) {
	    int m0 = M[i][j];
	    int m1 = M[i][j + 1];
	    int m2 = M[i][j + 2];
	    int m3 = M[i][j + 3];
	    colsum[j]     += m0;
	    colsum[j + 1] += m1;
	    colsum[j + 2] += m2;
	    colsum[j + 3] += m3;
	}
    }
}


/* Row & Column sum: row-major traversal + local variable + 4x loop unrolling */
void rc_sum(matrix_t M, vector_t rowsum, vector_t colsum)
{
    int i, j;
    for (i = 0; i < N; i++)
	rowsum[i] = colsum[i] = 0;
    for (i = 0; i < N; i++) {
	int rsum = 0;
	for (j = 0; j < N; j += 4) {
	    int m0 = M[i][j];
	    int m1 = M[i][j + 1];
	    int m2 = M[i][j + 2];
	    int m3 = M[i][j + 3];
	    rsum += m0 + m1 + m2 + m3;
	    colsum[j]     += m0;
	    colsum[j + 1] += m1;
	    colsum[j + 2] += m2;
	    colsum[j + 3] += m3;
	}
	rowsum[i] = rsum;
    }
}


rc_fun_rec rc_fun_tab[] =
{
  {c_sum,  COL,    "Best column sum"},
  {rc_sum, ROWCOL, "Best row and column sum"},

  {NULL, ROWCOL, NULL}
};
