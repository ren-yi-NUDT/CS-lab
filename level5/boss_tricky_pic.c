extern int gvar;
int shared_addr(void) { return (int)(unsigned long)&gvar; }
int read_shared(void) { return gvar; }
