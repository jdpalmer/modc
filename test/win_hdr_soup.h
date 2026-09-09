/* Simulated SDK header for interop tests */
typedef float FLOAT;
typedef FLOAT *PFLOAT;

#define near
#define far
typedef unsigned char BYTE;
typedef BYTE near *PBYTE;

typedef signed char INT8, *PINT8;

typedef unsigned __int64 UINT_PTR_TEST;
typedef UINT_PTR_TEST HANDLE64;
typedef HANDLE64 *PHANDLE64;

struct WinSoupS {
	unsigned a : 1;
	unsigned b : 2;
	int x, *p;
};

typedef struct WinSoupS *PWinSoupS;
