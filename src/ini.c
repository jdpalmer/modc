/*
 * Shared INI dialect: [section], key = value, #/; comments.
 * Used by vendor (modc.ini / modc.lock) and package .pkg.meta.
 */
#include "ini.h"
#include "ast.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_os.h"

void
ini_free(IniFile* ini) {
	int i, j;

	if (ini == NULL)
		return;
	for (i = 0; i < ini->nsect; i++) {
		free(ini->sect[i].name);
		for (j = 0; j < ini->sect[i].nkv; j++) {
			free(ini->sect[i].kv[j].key);
			free(ini->sect[i].kv[j].val);
		}
	}
	ini->nsect = 0;
}

static char*
trim(char* s) {
	char* e;

	while (*s && isspace((unsigned char)*s))
		s++;
	if (*s == 0)
		return s;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		e--;
	*e = 0;
	return s;
}

const char*
ini_get(IniSect* s, const char* key) {
	int i;

	if (s == NULL || key == NULL)
		return NULL;
	for (i = 0; i < s->nkv; i++)
		if (s->kv[i].key && strcmp(s->kv[i].key, key) == 0)
			return s->kv[i].val;
	return NULL;
}

IniSect*
ini_find_sect(IniFile* ini, const char* name) {
	int i;

	if (ini == NULL || name == NULL)
		return NULL;
	for (i = 0; i < ini->nsect; i++)
		if (ini->sect[i].name && strcmp(ini->sect[i].name, name) == 0)
			return &ini->sect[i];
	return NULL;
}

static int
ini_add_kv(IniSect* s, const char* key, const char* val) {
	if (s->nkv >= MaxIniKey)
		return 1;
	s->kv[s->nkv].key = xstrdup(key);
	s->kv[s->nkv].val = xstrdup(val);
	s->nkv++;
	return 0;
}

static IniSect*
ini_add_sect(IniFile* ini, const char* name) {
	IniSect* s;

	if (ini->nsect >= MaxIniSect)
		return NULL;
	s = &ini->sect[ini->nsect++];
	s->name = xstrdup(name);
	s->nkv = 0;
	return s;
}

int
ini_parse(IniFile* ini, const char* text, const char* path) {
	char *copy, *p, *line, *eq, *sectname;
	IniSect* cur;

	(void)path;
	memset(ini, 0, sizeof(*ini));
	copy = xstrdup(text);
	cur = NULL;
	for (p = copy; *p;) {
		line = p;
		p = strchr(p, '\n');
		if (p) {
			*p = 0;
			p++;
		}
		line = trim(line);
		if (line[0] == 0 || line[0] == '#' || line[0] == ';')
			continue;
		if (line[0] == '[') {
			eq = strchr(line, ']');
			if (eq == NULL) {
				free(copy);
				return 1;
			}
			*eq = 0;
			sectname = trim(line + 1);
			cur = ini_add_sect(ini, sectname);
			if (cur == NULL) {
				free(copy);
				return 1;
			}
			continue;
		}
		if (cur == NULL)
			continue;
		eq = strchr(line, '=');
		if (eq == NULL)
			continue;
		*eq = 0;
		if (ini_add_kv(cur, trim(line), trim(eq + 1))) {
			free(copy);
			return 1;
		}
	}
	free(copy);
	return 0;
}

int
ini_load(IniFile* ini, const char* path) {
	char* text;
	int err;

	text = read_file(path, NULL);
	if (text == NULL)
		return 1;
	err = ini_parse(ini, text, path);
	free(text);
	return err;
}

int
ini_set(IniFile* ini, const char* sect, const char* key, const char* val) {
	IniSect* s;
	int i;

	if (ini == NULL || sect == NULL || key == NULL || val == NULL)
		return 1;
	s = ini_find_sect(ini, sect);
	if (s == NULL) {
		s = ini_add_sect(ini, sect);
		if (s == NULL)
			return 1;
	}
	for (i = 0; i < s->nkv; i++) {
		if (s->kv[i].key && strcmp(s->kv[i].key, key) == 0) {
			free(s->kv[i].val);
			s->kv[i].val = xstrdup(val);
			return 0;
		}
	}
	return ini_add_kv(s, key, val);
}

int
ini_save(IniFile* ini, const char* path) {
	char* buf;
	size_t cap, off;
	int i, j, n;
	int err;

	if (ini == NULL || path == NULL)
		return 1;
	cap = 256;
	buf = xmalloc(cap);
	off = 0;
	for (i = 0; i < ini->nsect; i++) {
		n = snprintf(NULL, 0, "%s[%s]\n", i ? "\n" : "", ini->sect[i].name);
		if (n < 0)
			goto fail;
		while (off + (size_t)n + 1 > cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		n = snprintf(buf + off, cap - off, "%s[%s]\n", i ? "\n" : "",
			     ini->sect[i].name);
		if (n < 0)
			goto fail;
		off += (size_t)n;
		for (j = 0; j < ini->sect[i].nkv; j++) {
			n = snprintf(NULL, 0, "%s = %s\n", ini->sect[i].kv[j].key,
				     ini->sect[i].kv[j].val);
			if (n < 0)
				goto fail;
			while (off + (size_t)n + 1 > cap) {
				cap *= 2;
				buf = xrealloc(buf, cap);
			}
			n = snprintf(buf + off, cap - off, "%s = %s\n",
				     ini->sect[i].kv[j].key, ini->sect[i].kv[j].val);
			if (n < 0)
				goto fail;
			off += (size_t)n;
		}
	}
	err = host_write_atomic(path, buf, off) != 0;
	free(buf);
	return err;
fail:
	free(buf);
	return 1;
}
