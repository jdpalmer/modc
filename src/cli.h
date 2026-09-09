/*
 * CLI internals shared by main.c, cli_build.c, and cli_cmd.c.
 */
#ifndef MODC_CLI_H
#define MODC_CLI_H

#include "ast.h"
#include "host_os.h"

#ifndef PATH_MAX
#define PATH_MAX HOST_PATH_MAX
#endif

#define MODC_VERSION "0.1.0-dev"

/* Installed layout: $PREFIX/bin/modc + $PREFIX/lib/modc/{include,pkg}/ */
#define MODC_REL_INCLUDE "../lib/modc/include"
#define MODC_REL_PKG "../lib/modc/pkg"

typedef struct {
	int check_only;
	int verbose;
	int no_system_includes;
	int corpus;
	char* output;
	char** incpaths;
	int incpaths_len;
	char** files;
	int files_len;
	char** linkargv;
	int linkargv_len;
} CliOpts;

void usage(const char* sub);
void version(void);
void add_file(CliOpts* o, char* path);
void apply_cli(Compiler* c, CliOpts* o);
int parse_common(Compiler* c, CliOpts* o, int* i, int argc, char** argv, int need_out);

void reset_comp_state(Compiler* c);
int compile_file(Compiler* c, const char* path, FILE* outf);
int emit_one(Compiler* c, CliOpts* o, const char* path);
int compile_link_exe(Compiler* c, CliOpts* o, const char* path, const char* dir, const char* outpath);
char* default_out_name(const char* path);
void ensure_build_root(CliOpts* o);
void cleanup_tmpdir(const char* dir, const char* a, const char* b, const char* c);
int build_and_run_root(Compiler* c, CliOpts* o, const char* path);

int cmd_check(Compiler* c, CliOpts* o, int argc, char** argv);
int cmd_emit(Compiler* c, CliOpts* o, int argc, char** argv);
int cmd_build(Compiler* c, CliOpts* o, int argc, char** argv);
int cmd_run(Compiler* c, CliOpts* o, int argc, char** argv);
int cmd_test(Compiler* c, CliOpts* o, int argc, char** argv);
int cmd_doc(Compiler* c, CliOpts* o, int argc, char** argv);
int cmd_format(Compiler* c, CliOpts* o, int argc, char** argv);
int cmd_clean(CliOpts* o, int argc, char** argv);

#endif
