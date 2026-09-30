// Shared AST / Compiler definitions and the public surface of each module.
//
// Compilation pipeline: lex → pp → parse → type check → emit → QBE
//
#ifndef MODC_AST_H
#define MODC_AST_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Span Span;
typedef struct Tok Tok;
typedef struct Type Type;
typedef struct Symbol Symbol;
typedef struct Node Node;
typedef struct Field Field;
typedef struct Initializer Initializer;
typedef struct Compiler Compiler;
typedef struct Macro Macro;
typedef struct CompOpt CompOpt;
typedef struct CompDiag CompDiag;
typedef struct CompPaths CompPaths;
typedef struct CompLex CompLex;
typedef struct CompPp CompPp;
typedef struct CompTypes CompTypes;
typedef struct CompSyms CompSyms;
typedef struct CompUnit CompUnit;

// ---- Span / tokens ----

// Source location for diagnostics and AST nodes (1-based line/column).
struct Span {
	// Path of the source file (interned / stable for the compile).
	const char* file;
	// Start line.
	int line;
	// Start column.
	int col;
	// End column (exclusive) on the start line.
	int endcol;
};

// Lexer / preprocessor token kinds (Tok.kind).
enum {
	TkEof = 0,
	TkIdent,
	TkNumber,
	TkString,
	TkCharLit,
	TkKw,
	TkPunct,
	TkNewline,
	TkHeader,
	TkComment,
	TkEmbed,
};

// Keyword ids (Tok.kw when Tok.kind == TkKw). Order matches lex.c kwname[].
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

// Punctuator ids (Tok.punct when Tok.kind == TkPunct).
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

// One lexer / preprocessor token.
struct Tok {
	// Token kind (Tk*).
	int kind;
	// Keyword id (Kw*) when kind == TkKw; #embed byte length when kind == TkEmbed.
	int kw;
	// Punctuator id (Pn*) when kind == TkPunct.
	int punct;
	// Source location of this token.
	Span span;
	// Spelling for ident / string / number text (and similar).
	char* s;
	// Pending doc comment attached to this token.
	char* doc;
	// Numeric or character literal value; #embed strpool offset when kind == TkEmbed.
	int64_t int_val;
	// Beginning of line (preprocessor directive recognition).
	int bol;
	// Had whitespace before this token.
	int ws;
};

// ---- Types ----

// Type kinds (Type.kind).
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
	TyULLong,
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

// One struct/union field (linked list via next).
struct Field {
	// Field name; NULL for an anonymous embed.
	char* name;
	// Field type.
	Type* type;
	// Byte offset within the aggregate.
	int offset;
	// Static pointer field: package-private.
	int pkg_private;
	// Next field in the aggregate.
	Field* next;
};

// A type node (primitives, pointers, arrays, funcs, aggregates).
struct Type {
	// Type kind (Ty*).
	int kind;
	// Size in bytes (0 until laid out when applicable).
	int size;
	// Alignment in bytes.
	int align;
	// Integer types: unsigned.
	int is_unsigned;
	// Pointee / element / function return type.
	Type* base;
	// Array bound; -1 = incomplete / open.
	int64_t len;
	// Function parameter types.
	Type** params;
	// Function parameter names (may be NULL entries).
	char** param_names;
	// Number of function parameters.
	int params_len;
	// Function is variadic (...).
	int is_varargs;
	// Per-param: open/fixed array param (decays to pointer).
	char* param_array;
	// Per-param: N for fixed array param, else -1.
	int64_t* param_fixed_len;
	// Tag name for struct/union/enum.
	char* tag;
	// Struct/union fields (or enum members as fields in some paths).
	Field* fields;
	// Owning package directory for user struct/union.
	char* pkg_root;
	// Static struct/union/enum: package-private type.
	int pkg_private;
	// Aggregate/enum has a complete definition.
	int complete;
	// Size/align/offsets have been computed.
	int laid_out;
	// Ranged array T[..]; base is the element type.
	int is_ranged;
	// Multi-return anonymous struct (tuple).
	int is_tuple;
	// TyPtr: pointee read-only; ranged/array: element string-const.
	int is_readonly;
	// const? — call-site binds with arg constness; body treats as const.
	int is_poly;
	// Emit aggregate id; 0 = not yet assigned.
	int emit_id;
	// Next type in the compiler's type list.
	Type* next;
};

// ---- Symbols ----

// Symbol kinds (Symbol.kind).
enum {
	SkNone,
	SkVar,
	SkFunc,
	SkTypedef,
	SkEnumCon,
	SkLabel,
	SkTag
};

// Storage classes (Symbol.storage).
enum {
	StNone,
	StExtern,
	StStatic,
	StLocal,
	StParam,
	StTypedef
};

// A named entity in the symbol table (var, func, typedef, tag, …).
struct Symbol {
	// Identifier spelling.
	char* name;
	// Symbol kind (Sk*).
	int kind;
	// Storage class (St*).
	int storage;
	// Declared type.
	Type* type;
	// Function body or initializer AST.
	Node* node;
	// Enum constant value, or emit label / static seq id.
	int64_t int_val;
	// Unused for stack; emit reuses for label numbers.
	int offset;
	// Lexical block nesting depth at definition.
	int block;
	// Definition seen (vs declaration only).
	int defined;
	// Already emitted to QBE.
	int emitted;
	// Member of an overload set.
	int is_overload;
	// Method (T *r).name — linkname pkg_t_name.
	int is_method;
	// Lexical block containing a label.
	Node* label_scope;
	// Method receiver type as written (T *).
	Type* recv_type;
	// Struct tag for method lookup.
	char* recv_tag;
	// Parameter written as array; type already a pointer.
	int array_param;
	// Fixed array param element count, else -1.
	int64_t param_fixed_len;
	// Reserved; package-private uses storage == StStatic.
	int hidden;
	// Left scope; lookup skips; kept for unused analysis.
	int dead;
	// Name referenced (unused local/param diagnostics).
	int used;
	// Definition / declaration source location.
	Span span;
	// Enclosing function, if any.
	Symbol* owner;
	// Mangled name when overload / method.
	char* linkname;
	// Go-style doc comment (file-scope API).
	char* doc;
	// Defining file (header or .mc).
	char* home;
	// From #include; visible only in home .mc.
	int header;
	// Previous symbol shadowed by this one.
	Symbol* shadow;
	// Next in scope chain.
	Symbol* next;
	// Next in hash bucket.
	Symbol* hash_next;
};

// ---- AST nodes ----

// AST node kinds (Node.kind).
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

// Initializer designator kinds (Initializer.designator).
enum {
	IdNone,
	IdFieldDot,
	IdIndexEq
};

// One initializer: a scalar expr, or a brace list of nested Initializers.
struct Initializer {
	// Scalar / string expression when not a brace list.
	Node* expr;
	// Nested elements when is_list.
	Initializer* items;
	// Length of items.
	int items_len;
	// Brace-enclosed list ({ … }).
	int is_list;
	// Designator kind (Id*).
	int designator;
	// Field path for IdFieldDot (.a.b =).
	char** fields;
	// Length of fields.
	int fields_len;
	// Index for IdIndexEq ([n] =).
	int64_t index;
};

// AST node. Child slots a/b/c/children[] by kind:
//   NdBin/NdAssign/NdIndex/NdDot/NdArrow  a=lhs, b=rhs/index/field-expr
//   NdUn/NdPost/NdAddr/NdDeref/NdCast     a=operand
//   NdCall/NdMethod                       a=callee, children[]=args
//   NdCond                                a=cond, b=then, c=else
//   NdComma                               a=left, b=right
//   NdSizeof                              a=expr; NdSizeofT uses type only
//   NdStmtExpr                            a=block
//   NdBlock                               children[]=stmts
//   NdIf                                  a=cond, b=then, c=else
//   NdWhile                               a=cond, b=body
//   NdDo                                  a=body, b=cond
//   NdFor                                 a=init, b=cond, c=step, children[0]=body
//   NdSwitch                              a=expr, b=body
//   NdCase                                a=low, b=high (range); children unused
//   NdDefault/NdBreak/NdContinue/…        mostly leaf; NdLabel/NdDefer/NdReturn a=…
//   NdDecl                                init=Initializer*; symbol set; optional a=…
//   NdFunc                                a=body; symbol=function
//   NdTupleLit                            children[]=elements
// Statement walkers still recurse into for-init/step (a/c) when those hold exprs.
struct Node {
	// Node kind (Nd*).
	int kind;
	// Source location.
	Span span;
	// Type after checking (or known earlier for some literals).
	Type* type;
	// Bound symbol (name, decl, func, …).
	Symbol* symbol;
	// Operator punct (Pn*) for bin/un/assign.
	int op;
	// Literal value, string-pool offset, block id, case value, ….
	int64_t int_val;
	// Spelling or other string payload when needed.
	char* s;
	// Child slot (meaning depends on kind; see layout above).
	Node* a;
	// Child slot (meaning depends on kind; see layout above).
	Node* b;
	// Child slot (meaning depends on kind; see layout above).
	Node* c;
	// Containing lexical block; for NdBlock, its parent.
	Node* scope;
	// Variable-length children (args, stmts, tuple elements, …).
	Node** children;
	// Length of children.
	int children_len;
	// Initializer for NdDecl / related.
	Initializer* init;
	// Expression is an lvalue.
	int is_lvalue;
	// Wrapped in (…); assign-in-condition rules.
	int paren;
	// NdLit from a '…' character constant.
	int is_char_lit;
	// NdStr from #embed (raw bytes; no synthetic NUL).
	int is_embed;
	// Compiler-built node (e.g. ranged → pointer decay).
	int is_synth;
	// Unnecessary-cast diagnostic already emitted.
	int cast_checked;
};

// ---- Macros ----

// Preprocessor macro (#define).
struct Macro {
	// Macro name.
	char* name;
	// Function-like (1) vs object-like (0).
	int func;
	// Parameter names for function-like macros.
	char** params;
	// Number of parameters.
	int params_len;
	// Has ... / __VA_ARGS__.
	int varargs;
	// Replacement token list.
	Tok* body;
	// Length of body.
	int body_len;
	// Hidden during its own expansion (prevents recursion).
	int hide;
	// Next in the linear macro list.
	Macro* next;
	// Next in the name hash bucket.
	Macro* hash_next;
};

// ---- Compiler session ----

// Hard compiler limits (params, control nesting, switch arms, …).
enum {
	MaxErr = 20,
	MaxParams = 64,
	MaxControlDepth = 32,
	MaxDeferDepth = 64,
	MaxDefersPerScope = 64,
	MaxSwitchCases = 128
};

// Codegen / ABI target (--target=…; TargetHost = build machine).
enum {
	TargetHost = 0,
	TargetWindows,
	TargetMacos,
	TargetLinux
};

// Driver / session options (often mirrored from CliOpts).
struct CompOpt {
	// Check only; do not emit/link.
	int check_only;
	// Insert runtime index bounds traps.
	int bounds_check;
	// Codegen / ABI target (Target*).
	int target;
};

// Diagnostics and error state.
struct CompDiag {
	// Number of errors reported so far.
	int error_count;
	// Stop the current phase after a fatal diagnostic.
	int fatal;
	// Suppress preprocessor diagnostics.
	int quiet_pp;
	// Suppress stderr diagnostics (capture to diag_log instead).
	int quiet_diag;
	// Captured diagnostic text when quiet_diag (selftest needles).
	char* diag_log;
	// Length of diag_log.
	size_t diag_log_len;
	// Capacity of diag_log.
	size_t diag_log_cap;
};

// Include / package / link search paths and related driver knobs.
struct CompPaths {
	// Current input file path (stable; spans alias it).
	char* infile;
	// -I user include paths.
	char** incpaths;
	int incpaths_len;
	// System include paths.
	char** sysincpaths;
	int sysincpaths_len;
	// System library search paths.
	char** syslibpaths;
	int syslibpaths_len;
	// Do not add default system includes.
	int no_system_includes;
	// Hosted stub include root (lib/modc/include).
	char* modc_include;
	// -M package search paths.
	char** pkgpaths;
	int pkgpaths_len;
	// Nearest modc.ini directory, else entry directory.
	char project_root[1024];
	// Stdlib package root (install or in-tree).
	char* modc_pkg;
	// #pragma modc c_libs
	char** c_libs;
	int c_libs_len;
	// -F / SDK Frameworks paths.
	char** framework_paths;
	int framework_paths_len;
	// #pragma modc frameworks → -framework
	char** frameworks;
	int frameworks_len;
	// #pragma modc c_sources → host compile at link
	char** csources;
	int csources_len;
	// -D from CLI; reapplied per TU
	char** cli_defs;
	int cli_defs_len;
};

// Lexer token stream and format-mode flags.
struct CompLex {
	// Token buffer after lex / pp.
	Tok* tokens;
	int tokens_len;
	int tokens_cap;
	// Parse cursor into tokens.
	int pos;
	// Lexer: doc comment pending for next token.
	char* pending_doc;
	// Lexer: emit TkComment (for modc format).
	int keep_comments;
};

// Preprocessor tables and string intern pool.
struct CompPp {
	// Defined macros (list + hash).
	Macro* macros;
	Macro** macro_tab;
	int macro_tab_cap;
	int macros_len;
	// #pragma once paths.
	char** once_files;
	int once_files_len;
	struct PpOnce** once_tab;
	int once_tab_cap;
	// #include path cache.
	struct PpInc** include_tab;
	int include_tab_cap;
	// String intern pool for idents/macros.
	struct Intern** intern_tab;
	int intern_tab_cap;
	int interns_len;
	// Approx set of defined macro name hashes.
	uint32_t* macro_bits;
	int macro_bits_cap;
	// Bitset: first chars of #define names.
	uint32_t macro_start[8];
};

// Builtin and allocated types.
struct CompTypes {
	Type *type_void, *type_char, *type_uchar, *type_short, *type_ushort;
	Type *type_int, *type_uint, *type_long, *type_ulong;
	Type *type_llong, *type_ullong;
	Type *type_float, *type_double, *type_bool, *type_void_ptr;
	// All heap types (never freed individually).
	Type* type_list;
};

// Symbol table and lexical scope.
struct CompSyms {
	// All symbols (list + hash).
	Symbol* symbols;
	Symbol** symbol_tab;
	int symbol_tab_cap;
	int symbols_len;
	// Current lexical block nesting depth.
	int block;
	// Current lexical block node.
	Node* current_scope;
	// Function being parsed/checked/emitted.
	Symbol* current_fn;
};

// Per-translation-unit AST, sources, and string/embed pool.
struct CompUnit {
	// Function definitions.
	Node** funcs;
	int funcs_len;
	int funcs_cap;
	// File-scope variable declarations.
	Node** globals;
	int globals_len;
	int globals_cap;
	// Opened source paths and texts (includes + embeds).
	char** src_files;
	char** src_text;
	int src_files_len;
	// src_files index where the current compile began.
	int unit_src_start;
	// Compile-time string / #embed byte pool.
	int strpool_len;
	unsigned char* strpool;
	int strpool_cap;
	// Static local sequence counter for unique QBE names.
	int static_seq;
	// TU roots in this compile (user_source).
	char** unit_files;
	int unit_files_len;
};

// Compilation session: options plus nested phase state.
struct Compiler {
	// Driver / session options.
	CompOpt opt;
	// Diagnostics.
	CompDiag diag;
	// Search paths and package/link metadata.
	CompPaths paths;
	// Token stream.
	CompLex lex;
	// Preprocessor + intern.
	CompPp pp;
	// Types.
	CompTypes types;
	// Symbols.
	CompSyms syms;
	// Current unit outputs and pools.
	CompUnit unit;
};

// ---- Public API ----

// Memory, strings, diagnostics, AST helpers
void* xmalloc(size_t n);
void* xmalloc_raw(size_t n);
void* xrealloc(void* p, size_t n);
char* xstrdup(const char* s);
char* xstrndup(const char* s, size_t n);
unsigned str_hash(const char* s);
char* str_intern(Compiler* c, const char* s);
char* str_intern_n(Compiler* c, const char* s, size_t n);
void error_at(Compiler* c, Span sp, const char* fmt, ...);
void error_tok(Compiler* c, Tok* t, const char* fmt, ...);
void die(const char* fmt, ...);
Node* node(int kind, Span sp);
Node* node1(int kind, Span sp, Node* a);
Node* node2(int kind, Span sp, Node* a, Node* b);
void node_add(Node* n, Node* k);

// Lex
void lex_file(Compiler* c, const char* path, const char* text, int bol_start);
char* read_file(const char* path, size_t* outlen);
int keyword(const char* s);
const char* punct_spell(int p);

// Preprocessor
void pp_init(Compiler* c);
void pp_clear_macros(Compiler* c);
void pp_clear_once(Compiler* c);
void pp_define(Compiler* c, const char* def);
void pp_define_cli(Compiler* c, const char* def);
void pp_run(Compiler* c);

// Parse
void prescan_unit_type_names(Compiler* c);
void prescan_unit_type_bodies(Compiler* c);
void prescan_unit_funcs(Compiler* c);
void parse_unit(Compiler* c);

// Symbols
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

// Types
void type_init(Compiler* c);
Type* type_new(Compiler* c, int kind);
Type* type_ptr(Compiler* c, Type* base);
Type* type_array(Compiler* c, Type* base, int64_t len);
Type* type_func(Compiler* c, Type* ret, Type** params, int n, int va);
Type* type_struct(Compiler* c, int kind, char* tag, Span sp, int storage);
Type* type_ranged_full(Compiler* c, Type* elem, int readonly, int poly);
Type* type_tuple(Compiler* c, Type** elts, int n);
void type_layout(Compiler* c, Type* t);
void type_layout_pending(Compiler* c);
int type_size(Compiler* c, Type* t);
int type_align(Compiler* c, Type* t);
const char* type_name(Type* t);
int type_eq(Type* a, Type* b);
int is_int(Type* t);
int is_scalar(Type* t);
int is_ptr(Type* t);
int is_func(Type* t);
int is_array(Type* t);
int is_aggr(Type* t);
int is_ranged(Type* t);
int is_tuple(Type* t);
int is_null_expr(Node* n);
Type* decay(Compiler* c, Type* t);
Type* promote(Compiler* c, Type* t);
char qbe_class(Type* t);
int user_source(Compiler* c, Span sp);
int anon_embed_offset(Type* outer, Type* inner, int* off);
int anon_embed_unique_ranged(Type* outer, Type** ranged, int* off);
int conv_implicit_ok(Compiler* c, Type* dst, Type* src, Node* expr);
Node* apply_implicit_conversions(Compiler* c, Type* dst, Node* src);
void check_implicit_conv(Compiler* c, Span sp, Type* dst, Node* src);
void check_format_call(Compiler* c, Node* call, Type* ft);
int intern_str(Compiler* c, const char* raw, int* out_len);
int intern_bytes(Compiler* c, const void* bytes, int nbytes);
int eval_const(Compiler* c, Node* n, int64_t* out);
int eval_float_const(Compiler* c, Node* n, double* out);
Node* type_expr(Compiler* c, Node* n);
void mark_symbol_used(Node* n);
Field* find_field(Type* t, const char* name, int* off);
Type* field_lhs(Type* t);

// Check
void type_check_unit(Compiler* c);

// Emit
int emit_qbe(Compiler* c, FILE* out);
int emit_qbe_pkg(Compiler* c, FILE* out, const char* pkg_dir, const char* str_symbol);

// Packages, vendor, cache, format (driver)
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
/* Resolve top-level import "spec" strings in path to absolute package roots. */
int pkg_import_roots(Compiler* c, const char* path, char*** out_roots, int* out_n);
int vendor_cmd(int argc, char** argv);
uint64_t cache_hash_str(const char* s);
uint64_t cache_hash_file(const char* path);
uint64_t cache_hash_mix(uint64_t a, uint64_t b);
void cache_hash_hex(uint64_t h, char* out, size_t out_len);
int cache_root_for(const char* entry, char* out, size_t out_len);
int cache_project_root(const char* entry, char* out, size_t out_len);
void cache_path_key(const char* path, const char* projroot, char* out, size_t out_len);
void cache_log(int verbose, const char* hitmiss, const char* what);
int cache_copy_file(const char* src, const char* dst);
int cache_write_bytes(const char* path, const void* data, size_t n);
int cache_write_str(const char* path, const char* s);
int cache_read_str(const char* path, char* out, size_t out_len);
int cache_write_deps(const char* path, char** files, int nfiles);
int cache_deps_valid(const char* path);
int cache_depfile_to_deps(const char* depfile, const char* path);
/* Write public API of package pkg_dir from Compiler into path. 0 ok. */
int export_write_pkg(Compiler* c, const char* pkg_dir, const char* path);
/* Load export file into Compiler (symbols/types). 0 ok. */
int export_load(Compiler* c, const char* path);
char* fmt_source(Compiler* c);

#endif
