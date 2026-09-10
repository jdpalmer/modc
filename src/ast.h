/*
 * Shared AST / Compiler definitions and the public surface of each module.
 *
 * Compilation pipeline: lex → pp → parse → type check → emit → QBE
 *
 * Compiler holds the token buffer, type pool, symbols, and collected funcs/globals
 * for one translation unit (or a package stitch of several).
 */
#ifndef MODC_AST_H
#define MODC_AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

typedef struct Span Span;
typedef struct Tok Tok;
typedef struct Type Type;
typedef struct Symbol Symbol;
typedef struct Node Node;
typedef struct Field Field;
typedef struct Initializer Initializer;
typedef struct Compiler Compiler;
typedef struct Macro Macro;
typedef struct MArg MArg;

struct Span {
	const char* file;
	int line;
	int col;
	int endcol;
};

enum {
	TkEof = 0,
	TkIdent,
	TkNumber,
	TkString,
	TkCharLit,
	TkKw,
	TkPunct,
	TkNewline,
	TkHeader,  /* after #include: <foo.h> or "foo.h" */
	TkComment, /* // or block comment; only when Compiler.keep_comments */
};

enum {
	KwAuto,
	KwBreak,
	KwCase,
	KwChar,
	KwConst,
	KwContinue,
	KwDefault,
	KwDefer,
	KwDo,
	KwDouble,
	KwElse,
	KwEnum,
	KwExtern,
	KwFallthrough,
	KwFloat,
	KwFor,
	KwGoto,
	KwIf,
	KwImport,
	KwInline,
	KwInt,
	KwLong,
	KwOverload,
	KwRegister,
	KwRestrict,
	KwReturn,
	KwShort,
	KwSigned,
	KwSizeof,
	KwStatic,
	KwStaticAssert,
	KwStruct,
	KwSwitch,
	KwTypedef,
	KwUnion,
	KwUnsigned,
	KwVoid,
	KwVolatile,
	KwWhile,
	KwBool,
	KwTrue,
	KwFalse,
	KwNkw
};

enum {
	PnPlus,
	PnMinus,
	PnStar,
	PnSlash,
	PnPercent,
	PnAmp,
	PnPipe,
	PnCaret,
	PnTilde,
	PnBang,
	PnEq,
	PnPlusEq,
	PnMinusEq,
	PnStarEq,
	PnSlashEq,
	PnPercentEq,
	PnAmpEq,
	PnPipeEq,
	PnCaretEq,
	PnShlEq,
	PnShrEq,
	PnEqEq,
	PnBangEq,
	PnLt,
	PnGt,
	PnLe,
	PnGe,
	PnShl,
	PnShr,
	PnAmpAmp,
	PnPipePipe,
	PnPlusPlus,
	PnMinusMinus,
	PnQuestion,
	PnColon,
	PnComma,
	PnSemi,
	PnLparen,
	PnRparen,
	PnLbrack,
	PnRbrack,
	PnLbrace,
	PnRbrace,
	PnDot,
	PnDotDot,
	PnArrow,
	PnEllipsis,
	PnHash,
	PnHashHash,
	PnCount
};

struct Tok {
	int kind;
	int kw;
	int punct;
	Span span;
	char* s;	 /* spelling for ident / string / number text */
	char* doc;	 /* pending doc comment attached to this token */
	int64_t int_val; /* numeric or character literal value */
	/* Short on purpose — high-traffic lexer flags: */
	int bol; /* beginning of line (directive recognition) */
	int ws;	 /* had whitespace before this token */
};

enum {
	TyVoid,
	TyChar,
	TyUChar,
	TyShort,
	TyUShort,
	TyInt,
	TyUInt,
	TyLong,
	TyULong,
	TyLLong,
	TyULLong, /* always 64-bit; int64_t / literal l / header long long */
	TyFloat,
	TyDouble,
	TyBool,
	TyPtr,
	TyArray,
	TyFunc,
	TyStruct,
	TyUnion,
	TyEnum
};

struct Field {
	char* name;
	Type* type;
	int offset;
	int pkg_private; /* static pointer field: package-private */
	Field* next;
};

struct Type {
	int kind;
	int size;
	int align;
	int is_unsigned;
	Type* base;  /* pointee / element / function return */
	int64_t len; /* array bound; -1 = incomplete */
	Type** params;
	char** param_names;
	int params_len;
	int is_varargs;
	char* param_array;	  /* per-param: open/fixed array param (decays to pointer) */
	int64_t* param_fixed_len; /* per-param: N for fixed array param, else -1 */
	char* tag;
	Field* fields;
	char* pkg_root; /* owning package dir for user struct/union */
	int complete;
	int laid_out;
	int is_ranged;	 /* ranged array T[..]; base is element type */
	int is_tuple;	 /* multi-return anonymous struct */
	int is_readonly; /* TyPtr: pointee read-only (header const T *) */
	int emit_id;	 /* emit aggregate id; 0 = not yet assigned */
	Type* next;
};

enum {
	SkNone,
	SkVar,
	SkFunc,
	SkTypedef,
	SkEnumCon,
	SkLabel,
	SkTag
};

enum {
	StNone,
	StExtern,
	StStatic,
	StLocal,
	StParam,
	StTypedef
};

struct Symbol {
	char* name;
	int kind;
	int storage;
	Type* type;
	Node* node;	 /* function body or initializer AST */
	int64_t int_val; /* enum constant, or emit label / static seq id */
	int offset;	 /* unused for stack; emit reuses for label numbers */
	int block;
	int defined;
	int emitted;
	int is_overload;
	int is_method;	 /* (T *r).name — linkname pkg_t_name */
	Type* recv_type; /* receiver type as written (T *) */
	char* recv_tag;	 /* struct tag for method lookup */
	int array_param;	 /* parameter written as array; type already a pointer */
	int64_t param_fixed_len; /* fixed array param element count, else -1 */
	int hidden;		 /* file-static after TU ends (package visibility) */
	int dead;		 /* left scope; lookup skips; kept for unused analysis */
	int used;		 /* name referenced (unused local/param diagnostics) */
	Span span;
	Symbol* owner;	/* enclosing function, if any */
	char* linkname; /* mangled name when overload */
	char* doc;	/* Go-style doc comment (file-scope API) */
	char* home;	/* defining file (header or .mc) */
	int header;	/* 1: from #include, visible only in home .mc */
	Symbol* shadow;
	Symbol* next;
	Symbol* hash_next; /* hash chain */
};

enum {
	NdLit,
	NdStr,
	NdName,
	NdBin,
	NdUn,
	NdPost,
	NdCall,
	NdMethod,
	NdIndex,
	NdSubrange,
	NdDot,
	NdArrow,
	NdAddr,
	NdDeref,
	NdCast,
	NdSizeof,
	NdSizeofT,
	NdCond,
	NdComma,
	NdAssign,
	NdStmtExpr,
	NdBlock,
	NdIf,
	NdWhile,
	NdDo,
	NdFor,
	NdSwitch,
	NdCase,
	NdDefault,
	NdBreak,
	NdContinue,
	NdReturn,
	NdGoto,
	NdLabel,
	NdDecl,
	NdInit,
	NdFunc,
	NdDefer,
	NdTupleLit,
	NdFallthrough,
	NdSkip
};

enum {
	IdNone,
	IdFieldDot, /* .field = */
	IdIndexEq   /* [n] = */
};

struct Initializer {
	Node* expr;
	Initializer* items;
	int items_len;
	int is_list;
	int designator;
	char** fields;
	int fields_len;
	int64_t index;
};

/*
 * Node child layout (a / b / c / children[]):
 *
 *   NdBin/NdAssign/NdIndex/NdDot/NdArrow   a=lhs, b=rhs/index/field-expr
 *   NdUn/NdPost/NdAddr/NdDeref/NdCast      a=operand
 *   NdCall/NdMethod                     a=callee, children[]=args
 *   NdCond                             a=cond, b=then, c=else
 *   NdComma                            a=left, b=right
 *   NdSizeof                           a=expr;  NdSizeofT uses type only
 *   NdStmtExpr                         a=block
 *   NdBlock                            children[]=stmts
 *   NdIf                               a=cond, b=then, c=else
 *   NdWhile                            a=cond, b=body
 *   NdDo                               a=body, b=cond
 *   NdFor                              a=init, b=cond, c=step, children[0]=body
 *   NdSwitch                           a=expr, b=body
 *   NdCase                             a=low, b=high (range); children unused
 *   NdDefault/NdBreak/NdContinue/…       mostly leaf; NdLabel/NdDefer/NdReturn a=…
 *   NdDecl                             init=Initializer*; symbol set; optional a=…
 *   NdFunc                             a=body; symbol=function
 *   NdTupleLit                         children[]=elements
 *
 * Walkers that only visit "statements" still recurse into for-init/step
 * (a/c) when those hold expression statements.
 */
struct Node {
	int kind;
	Span span;
	Type* type;
	Symbol* symbol;
	int op;		 /* operator punct for bin/un/assign */
	int64_t int_val; /* literal, string-pool offset, block id, case value, … */
	char* s;
	Node *a, *b, *c;
	Node** children;
	int children_len;
	Initializer* init;
	int is_lvalue;
	int paren;	  /* wrapped in (…); assign-in-condition rules */
	int is_immutable; /* Auto-const: string literal provenance */
	int is_char_lit;  /* NdLit from '…' character constant */
};

struct MArg {
	char* name;
};

struct Macro {
	char* name;
	int func;
	char** params;
	int params_len;
	int varargs;
	Tok* body;
	int body_len;
	int hide;
	Macro* next;
	Macro* hash_next; /* hash chain */
};

enum { MaxErr = 20 };

struct Compiler {
	char* infile;
	char** incpaths;
	int incpaths_len;
	char** sysincpaths;
	int sysincpaths_len;
	char** syslibpaths; /* auto -L (e.g. Homebrew lib) for link */
	int syslibpaths_len;
	int no_system_includes;
	char* modc_include;

	Tok* tokens;
	int tokens_len, tokens_cap;

	int pos;
	int error_count;
	int fatal;
	int check_only;
	int quiet_pp; /* import scan: expand macros, mute diagnostics */

	Type *type_void, *type_char, *type_uchar, *type_short, *type_ushort;
	Type *type_int, *type_uint, *type_long, *type_ulong;
	Type *type_llong, *type_ullong; /* fixed 64-bit; not host long */
	Type *type_float, *type_double, *type_bool, *type_void_ptr;

	Type* type_list;
	Symbol* symbols;
	Symbol** symbol_tab; /* open chain by name hash */
	int symbol_tab_cap;  /* power-of-two capacity; 0 = empty */
	int symbols_len;
	int block;

	Node** funcs;
	int funcs_len, funcs_cap;
	Node** globals;
	int globals_len, globals_cap;

	Macro* macros;
	Macro** macro_tab; /* open chain by name hash */
	int macro_tab_cap;    /* power-of-two capacity; 0 = empty */
	int macros_len;
	char** once_files;
	int once_files_len;
	struct PpOnce** once_tab; /* hash of once_files paths */
	int once_tab_cap;
	struct PpInc** include_tab; /* #include path cache */
	int include_tab_cap;
	struct PpGuard** guard_tab; /* path → include-guard macro */
	int guard_tab_cap;
	struct Intern** intern_tab; /* string intern pool for idents/macros */
	int intern_tab_cap;
	int interns_len;
	uint32_t* macro_bits; /* approx set of defined macro name hashes */
	int macro_bits_cap;      /* uint32_t count (power of two) */
	uint32_t macro_start[8]; /* bitset: first chars of #define names */

	char** src_files;
	char** src_text;
	int src_files_len;

	int strpool_len;
	unsigned char* strpool;
	int strpool_cap;

	Symbol* current_fn;
	int static_seq;

	char** pkgpaths; /* -M (explicit); MODC_PATH is env-only in resolve */
	int pkgpaths_len;
	char* modc_pkg; /* stdlib package root: install or in-tree */
	char** c_libs; /* #pragma modc c_libs */
	int c_libs_len;
	char** framework_paths; /* -F / SDK Frameworks */
	int framework_paths_len;
	char** frameworks; /* #pragma modc frameworks → -framework */
	int frameworks_len;
	char** csources; /* #pragma modc c_sources → host compile at link */
	int csources_len;
	char** cli_defs; /* -D from CLI; reapplied per TU */
	int cli_defs_len;
	char** unit_files; /* TU roots in this compile (user_source) */
	int unit_files_len;
	char* pending_doc; /* lexer: doc comment pending for next token */
	int keep_comments; /* lexer: emit TkComment (for modc format) */
};

/* ---- diag.c ---- */
// Allocate n bytes zero-filled; n==0 is bumped to 1 so we never hand back a zero-size block.
void* xmalloc(size_t n);
// Allocate n bytes uninitialized (faster for large Tok buffers).
void* xmalloc_raw(size_t n);
// Resize an existing block; OOM is fatal because the compiler has no recovery path.
void* xrealloc(void* p, size_t n);
// Heap-copy a C string; NULL in yields NULL out.
char* xstrdup(const char* s);
// FNV-1a 32-bit hash of a NUL-terminated string (0 if s is NULL).
unsigned str_hash(const char* s);
// Intern a string for the Compiler lifetime; equal spellings share one pointer.
char* str_intern(Compiler* c, const char* s);
// Intern a length-bounded slice (does not require s[n] == 0).
char* str_intern_n(Compiler* c, const char* s, size_t n);
// Copy at most n bytes from s and NUL-terminate; unlike strndup, does not require s to be longer than n.
char* xstrndup(const char* s, size_t n);
// User-facing error at a source span: message, caret underline, and fatal after MaxErr.
void error_at(Compiler* c, Span sp, const char* fmt, ...);
// error_at with the span taken from a token (or a dummy location when t is NULL).
void error_tok(Compiler* c, Tok* t, const char* fmt, ...);
// Internal fatal error (OOM, impossible state): print and exit; not counted in Compiler.error_count.
void die(const char* fmt, ...);

/* ---- lex.c ---- */
void lex_file(Compiler* c, const char* path, const char* text, int bol_start);
char* read_file(const char* path, size_t* outlen);
int keyword(const char* s);
const char* punct_spell(int p);

/* ---- pp.c ---- */
void pp_init(Compiler* c);
void pp_clear_macros(Compiler* c); /* drop macro list + hash (does not free Macro*) */
void pp_clear_once(Compiler* c);
void pp_define(Compiler* c, const char* def);
void pp_define_cli(Compiler* c, const char* def);
void pp_run(Compiler* c);
int pp_defined(Compiler* c, const char* name);

/* ---- parse.c ---- */
void prescan_unit(Compiler* c);	     /* types then func sigs (one file) */
void prescan_unit_type_names(Compiler* c); /* incomplete tags/typedef stubs */
void prescan_unit_type_bodies(Compiler* c); /* typedefs/tags bodies */
void prescan_unit_types(Compiler* c); /* names then bodies (one file) */
void prescan_unit_funcs(Compiler* c); /* file-scope func/method signatures */
void parse_unit(Compiler* c);
Tok* peek(Compiler* c);	    /* current Tok*; does not advance */
Tok* peekn(Compiler* c, int n); /* lookahead; peekn(c,0) == peek(c) */
Tok* take(Compiler* c);	    /* consume current, return it */
int at(Compiler* c, int punct);
int atkw(Compiler* c, int kw);
int eat(Compiler* c, int punct); /* take if matches; else 0 */
int eatkw(Compiler* c, int kw);

/* ---- type.c ---- */
void type_init(Compiler* c);
Type* type_new(Compiler* c, int kind);
Type* type_ptr(Compiler* c, Type* base); /* fresh T*; e.g. type_ptr(c, c->type_int) */
Type* type_array(Compiler* c, Type* base, int64_t len);
Type* type_func(Compiler* c, Type* ret, Type** params, int n, int va);
Type* type_struct(Compiler* c, int kind, char* tag, Span sp);
Type* type_ranged(Compiler* c, Type* elem); /* interned ranged array T[..] */
Type* type_tuple(Compiler* c, Type** elts, int n);
void type_layout(Compiler* c, Type* t);
void type_layout_pending(Compiler* c); /* finish aggregates deferred across files */
int type_size(Compiler* c, Type* t);
int type_align(Compiler* c, Type* t);
int is_int(Type* t);
int is_arith(Type* t);
int is_scalar(Type* t);
int is_ptr(Type* t);
int is_func(Type* t);
int is_array(Type* t);
int is_aggr(Type* t);
int is_ranged(Type* t);
Node* maybe_ranged_conv(Compiler* c, Type* dst, Node* src);
Node* maybe_ranged_decay(Compiler* c, Type* dst, Node* src);
Node* apply_implicit_conversions(Compiler* c, Type* dst, Node* src);
int is_tuple(Type* t);
int is_signed_int(Type* t);
int is_null_expr(Node* n);
int is_void_ptr(Type* t);
int expr_is_immutable(Node* n);
Type* decay(Compiler* c, Type* t);
Type* usual_arith(Compiler* c, Type* a, Type* b);
int type_eq(Type* a, Type* b);
int type_compat(Type* a, Type* b);
int user_source(Compiler* c, Span sp);
int anon_embed_offset(Type* outer, Type* inner, int* off);
int anon_embed_unique_ranged(Type* outer, Type** ranged, int* off);
int conv_implicit_ok(Compiler* c, Type* dst, Type* src, Node* expr);
Node* maybe_embed_project(Compiler* c, Type* dst, Node* src);
Node* maybe_embed_upcast(Compiler* c, Type* dst, Node* src);
void check_implicit_conv(Compiler* c, Span sp, Type* dst, Node* src);
void check_shift_count(Compiler* c, Span sp, Type* lhs, Node* count);
void check_sign_compare(Compiler* c, Span sp, Node* a, Node* b);
void check_call_args(Compiler* c, Span sp, Type* fn, Node** args, int args_len);
const char* type_name(Type* t);
Type* promote(Compiler* c, Type* t);
char qbe_class(Type* t); /* 'w'/'l'/'s'/'d' or '@' aggregate */
/* Decode escapes into the string pool. Returns offset; *out_len (if non-NULL)
 * receives the decoded byte count including the terminating NUL. */
int intern_str(Compiler* c, const char* raw, int* out_len);
int eval_const(Compiler* c, Node* n, int64_t* out);
Node* type_expr(Compiler* c, Node* n);
void mark_symbol_used(Node* n);
Field* find_field(Type* t, const char* name, int* off);
Type* field_lhs(Type* t);

/* ---- check.c ---- */
void type_check_unit(Compiler* c);

/* ---- pkg.c ---- */
void pkg_add_search_path(Compiler* c, const char* dir);
void pkg_add_clib(Compiler* c, const char* lib);
void pkg_add_framework(Compiler* c, const char* name);
void pkg_add_csource(Compiler* c, const char* from_file, const char* relpath);
void comp_add_framework_path(Compiler* c, const char* dir);
int pkg_discover(Compiler* c, const char* root, char*** out_files, int* out_n);
int pkg_is_test_src(const char* path);
void pkg_file_root(const char* mcfile, char* out, size_t out_len);
void pkg_mangle_from_file(const char* mcfile, char* out, size_t out_len);
int pkg_list_tests(const char* root, char*** out, int* out_len);
int pkg_resolve_spec(Compiler* c, const char* spec, char* out, size_t out_len);

/* ---- vendor.c ---- */
int vendor_cmd(int argc, char** argv);

/* ---- cache.c ---- */
uint64_t cache_hash_bytes(const void* data, size_t n);
uint64_t cache_hash_str(const char* s);
uint64_t cache_hash_file(const char* path);
uint64_t cache_hash_mix(uint64_t a, uint64_t b);
void cache_hash_hex(uint64_t h, char* out, size_t out_len);
int cache_root_for(const char* entry, char* out, size_t out_len);
/* Directory that owns .modc-cache (parent of cache_root_for). */
int cache_project_root(const char* entry, char* out, size_t out_len);
/* Path for cache keys: relative to projroot when under it, else absolute. */
void cache_path_key(const char* path, const char* projroot, char* out, size_t out_len);
int cache_mkdir_p(const char* dir);
void cache_log(int verbose, const char* hitmiss, const char* what);
int cache_copy_file(const char* src, const char* dst);
int cache_write_bytes(const char* path, const void* data, size_t n);
int cache_write_str(const char* path, const char* s);
int cache_read_str(const char* path, char* out, size_t out_len);

/* ---- emit.c ---- */
int emit_qbe(Compiler* c, FILE* out);
/* Emit only funcs/globals whose span.file is under pkg_dir (or equals pkg_file).
 * str_symbol is the QBE data symbol for the string pool (e.g. "__string_ab12"). */
int emit_qbe_pkg(Compiler* c, FILE* out, const char* pkg_dir, const char* str_symbol);

/* ---- fmt.c ---- */
char* fmt_source(Compiler* c); /* malloc'd formatted text from c->tokens; NULL on error */

/* ---- symbol.c ---- */
Symbol* symbol_lookup(Compiler* c, const char* name);
Symbol* symbol_lookup_tag(Compiler* c, const char* name);
Symbol* symbol_define(Compiler* c, const char* name, int kind, Type* t, int storage, Span sp);
Symbol* symbol_define_func(Compiler* c, const char* name, Type* t, int storage, Span sp, int isoverload);
Symbol* symbol_define_method(Compiler* c, const char* name, Type* recv, const char* recv_tag, Type* t, int storage, Span sp);
Symbol* symbol_find_method(Compiler* c, const char* recv_tag, const char* name);
Symbol* symbol_resolve_method_call(Compiler* c, Type* recv_ty, const char* method, Span sp);
int symbol_has_overload(Compiler* c, const char* name);
Symbol* symbol_resolve_overload(Compiler* c, const char* name, Node** args, int args_len, Span sp);
Symbol* symbol_resolve_range_count(Compiler* c, Type* range_ty, Type** elem_out, Span sp);
Symbol* symbol_resolve_range_at(Compiler* c, Type* range_ty, Type* elem, Span sp);
void symbol_push_block(Compiler* c);
void symbol_pop_block(Compiler* c);
void symbol_hide_file_statics(Compiler* c);

// Allocate a fresh AST node with kind and source span; children are added via node_add.
Node* node(int kind, Span sp);
// node with one child pointer stored in ->a.
Node* node1(int kind, Span sp, Node* a);
// node with two children in ->a and ->b.
Node* node2(int kind, Span sp, Node* a, Node* b);
// Append a child to n->children; grows the children array in steps of eight.
void node_add(Node* n, Node* k);

int is_typename_tok(Compiler* c, Tok* t);

#endif
