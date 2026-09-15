#include <stdio.h>

/* sum of a and b */
int   add( int a,int b )
{
if(a>b){
return a;
}else{
return b;
}
}

int mul(int*p,int n){
int i;int s;
s=0;
for(i=0;i<n;i++){
s=s+p[i];
}
return s;
}

enum { One = 1 };
enum Color { Red = 1, Green = 2, Blue = 3 };
union U { int x; };
union V { int a; char b; };
static int z={ 0 };
static int zs[3]={1,2,3};
Fs * blob = NULL;
static Fs * p = NULL;
Fs * * q = NULL;
char[..] * s = NULL;
#ifdef X
int * skip = NULL;
#else
FsFd * after_else = NULL;
#endif
