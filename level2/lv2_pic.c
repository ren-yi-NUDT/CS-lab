int gvar = 108;
int get_addr(void) { return (int)(unsigned long)&gvar; }
int read_var(void) { return gvar; }
static int arr[] = {10, 20, 30, 40, 50};
int get_elem(int i) { return arr[i]; }
