/* Shared INI parse for modc.ini, modc.lock, and .pkg.meta. */
#ifndef MODC_INI_H
#define MODC_INI_H

#include <stddef.h>

enum { MaxIniSect = 128, MaxIniKey = 256 };

typedef struct {
	char* key;
	char* val;
} IniKV;

typedef struct {
	char* name;
	IniKV kv[MaxIniKey];
	int nkv;
} IniSect;

typedef struct {
	IniSect sect[MaxIniSect];
	int nsect;
} IniFile;

void ini_free(IniFile* ini);
const char* ini_get(IniSect* s, const char* key);
IniSect* ini_find_sect(IniFile* ini, const char* name);
int ini_parse(IniFile* ini, const char* text, const char* path);
int ini_load(IniFile* ini, const char* path);
/* Append key=value under section (creates section if needed). 0 ok. */
int ini_set(IniFile* ini, const char* sect, const char* key, const char* val);
/* Write INI text to path atomically. 0 ok. */
int ini_save(IniFile* ini, const char* path);

#endif
