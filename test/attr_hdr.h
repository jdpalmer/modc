/* Headers: MSVC/GNU attributes and calling conventions are ignored. */
#pragma once

__declspec(dllimport) int attr_dllimport_fn(void);
__declspec(dllexport) int attr_dllexport_fn(int x);

int __stdcall attr_stdcall_fn(int x);
int __cdecl attr_cdecl_fn(int x);

__attribute__((noreturn)) void attr_gnu_noreturn(void);

typedef __declspec(align(8)) struct AttrAligned {
	int x;
} AttrAligned;
