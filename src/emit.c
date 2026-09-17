/*
 * QBE backend: typed AST → IL text.
 *
 * Compilation pipeline: lex → pp → parse → type check → [emit] → QBE
 *
 * Phases in emit_qbe: (1) collect aggregates/locals referenced by functions,
 * (2) emit type definitions + string pool + globals, (3) emit each function.
 * Values are Val { text, cls, type } — either a temp (%tN) or an immediate.
 * Aggregates are QBE :suN types; scalars use w/l/s/d. Defer frames nest with
 * blocks: return/break/goto run pending defers *after* the return value is
 * evaluated so the deferred code cannot clobber it.
 */
#include "ast.h"
#include "host_os.h"

typedef struct Val Val;
struct Val {
	char text[96];
	char cls; /* w l s d @ */
	Type* type;
};

enum {
	MaxLoop = 32,
	MaxCase = 128,
	MaxDefer = 64,
	MaxDeferStmt = 64,
	MaxInlineMap = 32,
	MaxInlineStack = 16,
	InlineNodeBudget = 32
};

typedef struct DeferFrame DeferFrame;
struct DeferFrame {
	Node* stmts[MaxDeferStmt];
	int stmts_len;
	Node* scope;
};

/*
 * Prefix inl_ — call-site auto-inline (budget InlineNodeBudget nodes).
 * Remaps callee locals into caller QBE slots; InlineSite is registered during
 * collect, expanded in try_inline_call. Cross-package inlines are skipped when
 * emit_qbe_pkg filters by package (callee statics stay in the other .o).
 *
 * Prefix v* — Val helpers: vtmp = fresh %tN SSA temp, vimm = integer immediate.
 */
/* Call-site auto-inline: remap callee locals into caller slots (QBE allocs at entry). */
typedef struct InlineMap InlineMap;
struct InlineMap {
	Symbol* symbol;
	char name[48];
};

typedef struct InlineSite InlineSite;
struct InlineSite {
	Node* call;
	Symbol* callee;
	InlineMap map[MaxInlineMap];
	int map_len;
	char retname[48];
	int has_ret;
};

typedef struct InlineCtx InlineCtx;
struct InlineCtx {
	int active;
	InlineSite* site;
	int endlbl;
	Symbol* stack[MaxInlineStack];
	int stack_len;
};

static FILE* outf;
static int tempno, lblno;
static const char* emit_str_symbol = "__string"; /* QBE data symbol for string pool */
static const char* emit_pkg_filter; /* non-NULL during emit_qbe_pkg */
static Symbol** locals;
static Type** localty;
static int* localparam;
static char** localslot; /* QBE slot basename; uniquified when names collide */
static int locals_len;
static int locals_cap;
static int loopbrk[MaxLoop], loopcont[MaxLoop], loop_defer[MaxLoop], loops_len;
static DeferFrame deferstk[MaxDefer];
static int defers_len;
static InlineSite* isites;
static int isites_len;
static int isites_cap;
static InlineCtx inl;
static Node* emit_curfn;

static Val emitexpr(Compiler* c, Node* n);
static Val emitlval(Compiler* c, Node* n);
static void collect(Compiler* c, Node* n);
static int emitstmt_ret(Compiler* c, Node* n);
static void emit_defers_frame(Compiler* c, int fi);
static void pop_defer_frame(Compiler* c);
static void emit_defers_until(Compiler* c, int target);
static void emit_all_defers(Compiler* c);
static void push_defer_frame(Node* scope);
static void add_defer(Compiler* c, Node* stmt);
static int inline_eligible(Symbol* s);
static void register_inline_sites(Compiler* c, Node* n);
static InlineSite* find_inline_site(Node* call);
static const char* slot_basename(Symbol* s);
static int try_inline_call(Compiler* c, Node* n, Val* out);
static int node_in_pkg(Node* n, const char* pkg_dir);
static Type* emit_field_path(Compiler* c, Type* t, Initializer* it, int* totoff);

// Allocate the next QBE SSA temporary name (%tN).
static int
newtmp(void) {
	return ++tempno;
}

// Allocate the next basic-block label id (@LN).
static int
newlbl(void) {
	return ++lblno;
}

// Emit a QBE label definition.
static void
emitlbl(int id) {
	fprintf(outf, "@L%d\n", id);
}

// Unconditional branch to a label.
static void
emitjmp(int id) {
	fprintf(outf, "\tjmp @L%d\n", id);
}

// Branch to t when v is non-zero, otherwise to f.
static void
emitjnz(const char* v, int t, int f) {
	fprintf(outf, "\tjnz %s, @L%d, @L%d\n", v, t, f);
}

// QBE aggregate type name (:tag or :suN) for struct/union/ranged/tuple refs.
static const char*
aggregate_name(Type* t) {
	static char buf[64];
	if (t && t->tag && t->tag[0])
		return t->tag;
	if (t) {
		snprintf(buf, sizeof(buf), "su%d", t->emit_id >= 0 ? t->emit_id : 0);
		return buf;
	}
	return "su";
}

// Peel T, T[N], T[N][M], … down to the element type.
static Type*
array_elem(Type* t) {
	while (t && t->kind == TyArray)
		t = t->base;
	return t;
}

// Assign a stable emit_id and recurse into nested aggregates before first use in IL.
static void
ensure_aggregate(Type* t) {
	static int nextid = 1;
	Field* f;
	Type* e;

	if (!is_aggr(t) || t->emit_id != 0)
		return;
	t->emit_id = nextid++;
	for (f = t->fields; f; f = f->next) {
		if (is_aggr(f->type))
			ensure_aggregate(f->type);
		else {
			/* T[N] / T[N][M] fields: QBE prints :Inner N, so Inner must be defined first. */
			e = array_elem(f->type);
			if (is_aggr(e))
				ensure_aggregate(e);
		}
	}
}

// Byte size used for alloc/blit; incomplete types default to 4.
static int
storewidth(Compiler* c, Type* t) {
	int s;

	s = type_size(c, t);
	return s > 0 ? s : 4;
}

// QBE store instruction for a ModC type (storeb/h/w/l/s/d).
static const char*
storeop(Compiler* c, Type* t) {
	int w;

	w = storewidth(c, t);
	if (t) {
		switch (t->kind) {
		case TyChar:
		case TyUChar:
		case TyBool:
			return "storeb";
		case TyShort:
		case TyUShort:
			return "storeh";
		case TyLong:
		case TyULong:
		case TyLLong:
		case TyULLong:
		case TyPtr:
		case TyArray:
		case TyFunc:
			return t->size == 4 ? "storew" : "storel";
		case TyFloat:
			return "stores";
		case TyDouble:
			return "stored";
		default:
			break;
		}
	}
	if (w == 1)
		return "storeb";
	if (w == 2)
		return "storeh";
	if (w == 8)
		return "storel";
	return "storew";
}

// QBE load instruction for a ModC type (loadub/sh/uh/w/l/s/d).
static const char*
loadop(Type* t) {
	if (t == NULL)
		return "loadw";
	switch (t->kind) {
	case TyChar:
	case TyUChar:
	case TyBool:
		return "loadub";
	case TyShort:
		return "loadsh";
	case TyUShort:
		return "loaduh";
	case TyLong:
	case TyULong:
	case TyLLong:
	case TyULLong:
	case TyPtr:
	case TyArray:
	case TyFunc:
		if (t->size == 4)
			return t->is_unsigned ? "loaduw" : "loadsw";
		return "loadl";
	case TyFloat:
		return "loads";
	case TyDouble:
		return "loadd";
	default:
		return "loadw";
	}
}

// Print a single QBE value class for function signatures and returns.
static void
print_abi_type(Type* t) {
	if (t && (t->kind == TyPtr || t->kind == TyArray || t->kind == TyFunc)) {
		fputc('l', outf);
		return;
	}
	if (is_aggr(t)) {
		ensure_aggregate(t);
		fprintf(outf, ":%s", aggregate_name(t));
		return;
	}
	fputc(qbe_class(t), outf);
}

// Emit one field type inside a QBE aggregate definition (with array repeat count).
static void
emit_subtype(Compiler* c, Type* t) {
	int n;

	if (t == NULL) {
		fputc('w', outf);
		return;
	}
	if (t->kind == TyArray) {
		n = t->len > 0 ? (int)t->len : 0;
		emit_subtype(c, t->base);
		if (n > 1)
			fprintf(outf, " %d", n);
		return;
	}
	if (is_aggr(t)) {
		ensure_aggregate(t);
		fprintf(outf, ":%s", aggregate_name(t));
		return;
	}
	switch (t->kind) {
	case TyChar:
	case TyUChar:
	case TyBool:
		fputc('b', outf);
		break;
	case TyShort:
	case TyUShort:
		fputc('h', outf);
		break;
	case TyFloat:
		fputc('s', outf);
		break;
	case TyDouble:
		fputc('d', outf);
		break;
	case TyLong:
	case TyULong:
	case TyLLong:
	case TyULLong:
	case TyPtr:
		fputc(t->size == 4 ? 'w' : 'l', outf);
		break;
	default:
		fputc('w', outf);
		break;
	}
}

// Emit a full QBE struct/union type line, including padding bytes between fields.
static void
emitsutype(Compiler* c, Type* t) {
	Field* f;
	int pos, gap, first, al;

	if (t == NULL || t->emit_id < 0)
		return;
	if (t->emit_id < 0)
		return;
	al = type_align(c, t);
	if (t->kind == TyUnion) {
		fprintf(outf, "type :%s = align %d { b %d }\n\n",
			aggregate_name(t), al > 0 ? al : 1, storewidth(c, t));
		return;
	}
	fprintf(outf, "type :%s = { ", aggregate_name(t));
	pos = 0;
	first = 1;
	for (f = t->fields; f; f = f->next) {
		gap = f->offset - pos;
		if (gap > 0) {
			if (!first)
				fputs(", ", outf);
			fprintf(outf, "b %d", gap);
			first = 0;
			pos += gap;
		}
		if (!first)
			fputs(", ", outf);
		emit_subtype(c, f->type);
		first = 0;
		pos = f->offset + type_size(c, f->type);
	}
	gap = type_size(c, t) - pos;
	if (gap > 0) {
		if (!first)
			fputs(", ", outf);
		fprintf(outf, "b %d", gap);
	}
	fputs(" }\n\n", outf);
}

// Function-local statics (block scope, int_val seq) use synthetic linker names.
static int
is_static_local(Symbol* s) {
	return s && s->kind == SkVar && s->storage == StStatic && s->int_val > 0;
}

// Linker symbol for a global, static, overload, or ordinary name.
static const char*
symbol_link_name(Symbol* s) {
	static char buf[96];

	if (s == NULL)
		return "g";
	if (s->linkname)
		return s->linkname;
	if (is_static_local(s)) {
		snprintf(buf, sizeof(buf), "__st%d", (int)s->int_val);
		return buf;
	}
	return s->name;
}

// True for file-scope or extern variables (not stack slots).
static int
is_global_symbol(Symbol* s) {
	if (s == NULL || s->kind != SkVar)
		return 0;
	if (s->storage == StParam || s->storage == StLocal)
		return 0;
	return 1;
}

// Register a stack slot to emit at @start; dedupes repeated collect walks.
static void
ensure_locals_cap(int need) {
	if (need <= locals_cap)
		return;
	if (locals_cap < 128)
		locals_cap = 128;
	while (locals_cap < need)
		locals_cap *= 2;
	locals = xrealloc(locals, (size_t)locals_cap * sizeof(*locals));
	localty = xrealloc(localty, (size_t)locals_cap * sizeof(*localty));
	localparam = xrealloc(localparam, (size_t)locals_cap * sizeof(*localparam));
	localslot = xrealloc(localslot, (size_t)locals_cap * sizeof(*localslot));
}

/* QBE %name.addr must be unique per function; block-scoped shadows share C names. */
static char*
unique_local_slot(const char* name) {
	char buf[96];
	int n = 0;
	int i;

	if (name == NULL || name[0] == 0)
		name = "tmp";
	for (i = 0; i < locals_len; i++) {
		if (locals[i] && locals[i]->name && strcmp(locals[i]->name, name) == 0)
			n++;
	}
	if (n == 0)
		return xstrdup(name);
	snprintf(buf, sizeof(buf), "%s__%d", name, n);
	return xstrdup(buf);
}

static const char*
slot_for_local(Symbol* s) {
	int i;

	if (s == NULL)
		return "g";
	for (i = 0; i < locals_len; i++) {
		if (locals[i] == s)
			return localslot[i] ? localslot[i] : (s->name ? s->name : "g");
	}
	return s->name ? s->name : "g";
}

static void
addlocal(Symbol* s, Type* t, int isparam) {
	int i;

	if (s == NULL || is_global_symbol(s) || is_static_local(s))
		return;
	if (s->kind != SkVar)
		return;
	for (i = 0; i < locals_len; i++)
		if (locals[i] == s) {
			if (isparam)
				localparam[i] = 1;
			return;
		}
	ensure_locals_cap(locals_len + 1);
	localparam[locals_len] = isparam;
	localty[locals_len] = t ? t : s->type;
	localslot[locals_len] = unique_local_slot(s->name);
	locals[locals_len++] = s;
}

// Give each ranged() temp a stable __rngN local for store/load and inline remapping.
static void
ensure_ranged_symbol(Node* n) {
	static int nrngtmp;
	char buf[32];

	if (n == NULL || n->kind != NdCall || n->a == NULL || n->a->kind != NdName || n->a->s == NULL || strcmp(n->a->s, "ranged") != 0 || n->type == NULL || !is_aggr(n->type))
		return;
	if (n->symbol != NULL)
		return;
	snprintf(buf, sizeof(buf), "__rng%d", nrngtmp++);
	n->symbol = xmalloc(sizeof(*n->symbol));
	n->symbol->name = xstrdup(buf);
	n->symbol->kind = SkVar;
	n->symbol->storage = StLocal;
	n->symbol->type = n->type;
}

// Pre-pass: find locals, ranged temps, and aggregates referenced by a function body.
static void collect_init(Compiler* c, Initializer* in);

static void
collect(Compiler* c, Node* n) {
	int i;

	if (n == NULL)
		return;
	if (n->type && is_aggr(n->type))
		ensure_aggregate(n->type);
	if (n->kind == NdCall && n->a && n->a->kind == NdName && n->a->s && strcmp(n->a->s, "ranged") == 0 && n->type && is_aggr(n->type)) {
		ensure_ranged_symbol(n);
		addlocal(n->symbol, n->type, 0);
	}
	if (n->kind == NdName && n->symbol)
		addlocal(n->symbol, n->type, n->symbol->storage == StParam);
	if (n->kind == NdDecl && n->symbol)
		addlocal(n->symbol, n->type, n->symbol->storage == StParam);
	if (n->kind == NdDecl && n->init)
		collect_init(c, n->init);
	collect(c, n->a);
	collect(c, n->b);
	collect(c, n->c);
	for (i = 0; i < n->children_len; i++)
		collect(c, n->children[i]);
}

static void
collect_init(Compiler* c, Initializer* in) {
	int i;

	if (in == NULL)
		return;
	if (in->expr)
		collect(c, in->expr);
	for (i = 0; i < in->items_len; i++)
		collect_init(c, &in->items[i]);
}

// Count AST nodes and reject constructs that cannot be duplicated at inline sites.
static int inline_body_walk_init(Initializer* in, int* nnodes);

static int
inline_body_walk(Node* n, int* nnodes) {
	int i;

	if (n == NULL)
		return 1;
	(*nnodes)++;
	if (*nnodes > InlineNodeBudget)
		return 0;
	switch (n->kind) {
	case NdDefer:
	case NdGoto:
	case NdLabel:
	case NdSwitch:
	case NdFunc:
		return 0;
	default:
		break;
	}
	if (n->kind == NdDecl && n->init && !inline_body_walk_init(n->init, nnodes))
		return 0;
	if (!inline_body_walk(n->a, nnodes))
		return 0;
	if (!inline_body_walk(n->b, nnodes))
		return 0;
	if (!inline_body_walk(n->c, nnodes))
		return 0;
	for (i = 0; i < n->children_len; i++)
		if (!inline_body_walk(n->children[i], nnodes))
			return 0;
	return 1;
}

static int
inline_body_walk_init(Initializer* in, int* nnodes) {
	int i;

	if (in == NULL)
		return 1;
	if (in->expr && !inline_body_walk(in->expr, nnodes))
		return 0;
	for (i = 0; i < in->items_len; i++)
		if (!inline_body_walk_init(&in->items[i], nnodes))
			return 0;
	return 1;
}

// Small non-varargs functions without aggregate returns may be expanded at call sites.
static int
inline_eligible(Symbol* s) {
	Node *fn, *body;
	Type *ty, *ret;
	int nnodes;

	if (s == NULL || s->kind != SkFunc)
		return 0;
	fn = s->node;
	if (fn == NULL || fn->kind != NdFunc)
		return 0;
	body = fn->a;
	if (body == NULL)
		return 0;
	ty = s->type ? s->type : fn->type;
	if (ty == NULL || !is_func(ty) || ty->is_varargs)
		return 0;
	ret = ty->base;
	if (ret && is_aggr(ret))
		return 0;
	nnodes = 0;
	return inline_body_walk(body, &nnodes);
}

// Detect recursive inline (f calls f) to avoid infinite expansion.
static int
inline_on_stack(Symbol* s) {
	int i;

	for (i = 0; i < inl.stack_len; i++)
		if (inl.stack[i] == s)
			return 1;
	return 0;
}

// Map callee locals to ilN_* slots when emitting an inlined body.
static const char*
slot_basename(Symbol* s) {
	int i;

	if (s == NULL)
		return "g";
	if (inl.active && inl.site) {
		for (i = 0; i < inl.site->map_len; i++)
			if (inl.site->map[i].symbol == s)
				return inl.site->map[i].name;
	}
	return slot_for_local(s);
}

// Lookup the pre-registered InlineSite for a call node.
static InlineSite*
find_inline_site(Node* call) {
	int i;

	for (i = 0; i < isites_len; i++)
		if (isites[i].call == call)
			return &isites[i];
	return NULL;
}

// Reserve a caller-stack slot and record sym→renamed mapping for one inline site.
static void
inl_add_map(Compiler* c, InlineSite* site, Symbol* s, int id) {
	Symbol* ls;
	char buf[48];
	int i;

	(void)c;
	if (s == NULL || s->kind != SkVar)
		return;
	if (is_global_symbol(s) || is_static_local(s))
		return;
	for (i = 0; i < site->map_len; i++)
		if (site->map[i].symbol == s)
			return;
	if (site->map_len >= MaxInlineMap)
		return;
	snprintf(buf, sizeof(buf), "il%d_%s", id, s->name ? s->name : "x");
	site->map[site->map_len].symbol = s;
	snprintf(site->map[site->map_len].name, sizeof(site->map[site->map_len].name), "%s", buf);
	site->map_len++;
	ls = xmalloc(sizeof(*ls));
	memset(ls, 0, sizeof(*ls));
	ls->name = xstrdup(buf);
	ls->kind = SkVar;
	ls->storage = StLocal;
	ls->type = s->type;
	addlocal(ls, s->type, 0);
}

// Walk callee body to collect locals and ranged temps that need remapping.
static void inl_collect_map_init(Compiler* c, InlineSite* site, Initializer* in, int id);

static void
inl_collect_map(Compiler* c, InlineSite* site, Node* n, int id) {
	int i;

	if (n == NULL)
		return;
	if (n->type && is_aggr(n->type))
		ensure_aggregate(n->type);
	if (n->kind == NdCall && n->a && n->a->kind == NdName && n->a->s && strcmp(n->a->s, "ranged") == 0 && n->type && is_aggr(n->type)) {
		ensure_ranged_symbol(n);
		inl_add_map(c, site, n->symbol, id);
	}
	if (n->kind == NdName && n->symbol)
		inl_add_map(c, site, n->symbol, id);
	if (n->kind == NdDecl && n->symbol)
		inl_add_map(c, site, n->symbol, id);
	if (n->kind == NdDecl && n->init)
		inl_collect_map_init(c, site, n->init, id);
	inl_collect_map(c, site, n->a, id);
	inl_collect_map(c, site, n->b, id);
	inl_collect_map(c, site, n->c, id);
	for (i = 0; i < n->children_len; i++)
		inl_collect_map(c, site, n->children[i], id);
}

static void
inl_collect_map_init(Compiler* c, InlineSite* site, Initializer* in, int id) {
	int i;

	if (in == NULL)
		return;
	if (in->expr)
		inl_collect_map(c, site, in->expr, id);
	for (i = 0; i < in->items_len; i++)
		inl_collect_map_init(c, site, &in->items[i], id);
}

// Find a parameter symbol by name in the callee AST (for inline arg binding).
static Symbol* inl_find_param_in_init(Initializer* in, const char* name);

static Symbol*
inl_find_param_symbol(Node* n, const char* name) {
	Symbol* s;
	int i;

	if (n == NULL || name == NULL)
		return NULL;
	if (n->kind == NdName && n->symbol && n->symbol->name && strcmp(n->symbol->name, name) == 0 && (n->symbol->storage == StParam || n->symbol->kind == SkVar))
		return n->symbol;
	if (n->kind == NdDecl && n->init) {
		s = inl_find_param_in_init(n->init, name);
		if (s)
			return s;
	}
	s = inl_find_param_symbol(n->a, name);
	if (s)
		return s;
	s = inl_find_param_symbol(n->b, name);
	if (s)
		return s;
	s = inl_find_param_symbol(n->c, name);
	if (s)
		return s;
	for (i = 0; i < n->children_len; i++) {
		s = inl_find_param_symbol(n->children[i], name);
		if (s)
			return s;
	}
	return NULL;
}

static Symbol*
inl_find_param_in_init(Initializer* in, const char* name) {
	Symbol* s;
	int i;

	if (in == NULL)
		return NULL;
	if (in->expr) {
		s = inl_find_param_symbol(in->expr, name);
		if (s)
			return s;
	}
	for (i = 0; i < in->items_len; i++) {
		s = inl_find_param_in_init(&in->items[i], name);
		if (s)
			return s;
	}
	return NULL;
}

// Plan one call-site inline: param/ret slots and callee-local rename table.
static void
register_inline_site(Compiler* c, Node* call, Symbol* callee) {
	InlineSite* site;
	Type *ty, *ret;
	Node* body;
	Symbol* ps;
	int id, i;

	if (call == NULL || callee == NULL || !inline_eligible(callee))
		return;
	/* Per-package .o emit: inlining a foreign callee can pull $__stN /
	 * $__fN_* locals that are defined only in the callee's object. */
	if (emit_pkg_filter && callee->node && !node_in_pkg(callee->node, emit_pkg_filter))
		return;
	if (find_inline_site(call))
		return;
	if (isites_len >= isites_cap) {
		isites_cap = isites_cap ? isites_cap * 2 : 64;
		isites = xrealloc(isites, (size_t)isites_cap * sizeof(*isites));
	}
	id = isites_len;
	site = &isites[isites_len++];
	memset(site, 0, sizeof(*site));
	site->call = call;
	site->callee = callee;
	ty = callee->type;
	body = callee->node && callee->node->kind == NdFunc ? callee->node->a : NULL;
	if (ty) {
		for (i = 0; i < ty->params_len; i++) {
			if (!ty->param_names || !ty->param_names[i])
				continue;
			ps = inl_find_param_symbol(body, ty->param_names[i]);
			if (ps == NULL) {
				ps = xmalloc(sizeof(*ps));
				memset(ps, 0, sizeof(*ps));
				ps->name = ty->param_names[i];
				ps->kind = SkVar;
				ps->storage = StParam;
				ps->type = ty->params[i];
			}
			inl_add_map(c, site, ps, id);
		}
	}
	inl_collect_map(c, site, body, id);
	ret = ty ? ty->base : NULL;
	if (ret && ret->kind != TyVoid) {
		site->has_ret = 1;
		snprintf(site->retname, sizeof(site->retname), "il%d__ret", id);
		ps = xmalloc(sizeof(*ps));
		memset(ps, 0, sizeof(*ps));
		ps->name = xstrdup(site->retname);
		ps->kind = SkVar;
		ps->storage = StLocal;
		ps->type = ret;
		addlocal(ps, ret, 0);
	}
}

// Walk a function body and register every eligible call for inlining.
static void register_inline_sites_init(Compiler* c, Initializer* in);

static void
register_inline_sites(Compiler* c, Node* n) {
	Symbol* cal;
	int i;

	if (n == NULL)
		return;
	if (n->kind == NdCall && n->a && n->a->kind == NdName && n->a->symbol && n->a->symbol->kind == SkFunc) {
		cal = n->a->symbol;
		{
			int skip;

			skip = 0;
			if (n->a->s && strncmp(n->a->s, "__builtin_", 10) == 0)
				skip = 1;
			else if (n->a->s &&
				 (strcmp(n->a->s, "len") == 0 || strcmp(n->a->s, "ranged") == 0) &&
				 n->a->symbol == NULL)
				skip = 1;
			if (!skip)
				register_inline_site(c, n, cal);
		}
	}
	if (n->kind == NdDecl && n->init)
		register_inline_sites_init(c, n->init);
	register_inline_sites(c, n->a);
	register_inline_sites(c, n->b);
	register_inline_sites(c, n->c);
	for (i = 0; i < n->children_len; i++)
		register_inline_sites(c, n->children[i]);
}

static void
register_inline_sites_init(Compiler* c, Initializer* in) {
	int i;

	if (in == NULL)
		return;
	if (in->expr)
		register_inline_sites(c, in->expr);
	for (i = 0; i < in->items_len; i++)
		register_inline_sites_init(c, &in->items[i]);
}

// Fresh SSA temporary (%tN) with QBE class and optional ModC type.
static Val
vtmp(char cls, Type* t) {
	Val v;

	memset(&v, 0, sizeof(v));
	v.cls = cls;
	v.type = t;
	snprintf(v.text, sizeof(v.text), "%%t%d", newtmp());
	return v;
}

// SSA value holding a compile-time integer literal.
static Val
vimm(char cls, int64_t n, Type* t) {
	Val v;

	memset(&v, 0, sizeof(v));
	v.cls = cls;
	v.type = t;
	snprintf(v.text, sizeof(v.text), "%" PRId64, (int64_t)n);
	return v;
}

// Insert casts/extensions so a value matches the target QBE class.
static Val
coerce(Val v, char cls, Type* to) {
	Val r;
	const char* op;
	char* end;
	long long lit;

	if (v.cls == cls || v.cls == '@')
		return v;
	/* Integer literals: rewrite w↔l without an extend instruction. */
	if ((v.cls == 'w' || v.cls == 'l') && (cls == 'w' || cls == 'l') &&
	    v.text[0] != '%' && v.text[0] != '$') {
		lit = strtoll(v.text, &end, 10);
		if (end != v.text && *end == '\0')
			return vimm(cls, (int64_t)lit, to ? to : v.type);
	}
	r = vtmp(cls, to);
	if (v.cls == 'w' && cls == 'l') {
		op = (to && to->is_unsigned) || (v.type && v.type->is_unsigned) ? "extuw" : "extsw";
		fprintf(outf, "\t%s =l %s %s\n", r.text, op, v.text);
		return r;
	}
	if (v.cls == 'l' && cls == 'w') {
		fprintf(outf, "\t%s =w copy %s\n", r.text, v.text);
		return r;
	}
	if ((v.cls == 'w' || v.cls == 'l') && cls == 's') {
		fprintf(outf, "\t%s =s %s %s\n", r.text, v.cls == 'l' ? "sltof" : "swtof", v.text);
		return r;
	}
	if ((v.cls == 'w' || v.cls == 'l') && cls == 'd') {
		fprintf(outf, "\t%s =d %s %s\n", r.text, v.cls == 'l' ? "sltof" : "swtof", v.text);
		return r;
	}
	fprintf(outf, "\t%s =%c copy %s\n", r.text, cls, v.text);
	return r;
}

// Non-constant shift counts are masked to the type width (defined behavior).
static Val
mask_shift_count(Compiler* c, Val r, Type* ty) {
	int bits, mask;
	Val m;

	if (ty == NULL || !is_int(ty))
		return r;
	ty = promote(c, ty);
	bits = type_size(c, ty) * 8;
	if (bits <= 1)
		return r;
	mask = bits - 1;
	m = vtmp(r.cls, ty);
	fprintf(outf, "\t%s =%c and %s, %d\n", m.text, r.cls, r.text, mask);
	return m;
}

// Build a QBE floating-point constant name from a lexer literal (strip suffixes).
static void
fpconst(Val* v, Type* t, const char* raw) {
	char num[64];
	size_t n, i;
	const char* p;

	p = raw ? raw : "0";
	n = strlen(p);
	if (n >= sizeof(num))
		n = sizeof(num) - 1;
	memcpy(num, p, n);
	num[n] = 0;
	while (n > 0) {
		char c = num[n - 1];
		if (c == 'f' || c == 'F' || c == 'l' || c == 'L' || c == 'u' || c == 'U')
			num[--n] = 0;
		else
			break;
	}
	if (t && t->kind == TyFloat) {
		v->cls = 's';
		snprintf(v->text, sizeof(v->text), "s_%s", num);
	} else {
		v->cls = 'd';
		snprintf(v->text, sizeof(v->text), "d_%s", num);
	}
	(void)i;
}

// Normalize any scalar to a word-sized 0/1 for branches and logic ops.
static Val
asbool(Val v) {
	Val r, z;

	if (v.cls == 'w')
		return v;
	r = vtmp('w', NULL);
	if (v.cls == 'd') {
		z = vtmp('d', NULL);
		snprintf(z.text, sizeof(z.text), "d_0");
		fprintf(outf, "\t%s =w cned %s, %s\n", r.text, v.text, z.text);
		return r;
	}
	if (v.cls == 's') {
		z = vtmp('s', NULL);
		snprintf(z.text, sizeof(z.text), "s_0");
		fprintf(outf, "\t%s =w cnes %s, %s\n", r.text, v.text, z.text);
		return r;
	}
	fprintf(outf, "\t%s =w cnel %s, 0\n", r.text, v.text);
	return r;
}

// Map a relational punctuator to the matching QBE compare instruction.
static const char*
cmpinst(int op, char cls, int uns) {
	if (cls == 'd') {
		switch (op) {
		case PnEqEq:
			return "ceqd";
		case PnBangEq:
			return "cned";
		case PnLt:
			return "cltd";
		case PnLe:
			return "cled";
		case PnGt:
			return "cgtd";
		case PnGe:
			return "cged";
		default:
			return "ceqd";
		}
	}
	if (cls == 's') {
		switch (op) {
		case PnEqEq:
			return "ceqs";
		case PnBangEq:
			return "cnes";
		case PnLt:
			return "clts";
		case PnLe:
			return "cles";
		case PnGt:
			return "cgts";
		case PnGe:
			return "cges";
		default:
			return "ceqs";
		}
	}
	if (cls == 'l') {
		switch (op) {
		case PnEqEq:
			return "ceql";
		case PnBangEq:
			return "cnel";
		case PnLt:
			return uns ? "cultl" : "csltl";
		case PnLe:
			return uns ? "culel" : "cslel";
		case PnGt:
			return uns ? "cugtl" : "csgtl";
		case PnGe:
			return uns ? "cugel" : "csgel";
		default:
			return "ceql";
		}
	}
	switch (op) {
	case PnEqEq:
		return "ceqw";
	case PnBangEq:
		return "cnew";
	case PnLt:
		return uns ? "cultw" : "csltw";
	case PnLe:
		return uns ? "culew" : "cslew";
	case PnGt:
		return uns ? "cugtw" : "csgtw";
	case PnGe:
		return uns ? "cugew" : "csgew";
	default:
		return "ceqw";
	}
}

// Copy n bytes between two addresses (struct/array assignment).
static void
emitblit(Val dst, Val src, int n) {
	if (n > 0)
		fprintf(outf, "\tblit %s, %s, %d\n", src.text, dst.text, n);
}

// True when a global var lives outside this object (dylib / other TU): QBE needs
// `extern $name` so arm64_apple uses GOT (@gotpage) instead of @page/@pageoff.
static int
needs_qbe_extern(Symbol* s) {
	return s && s->kind == SkVar && s->storage == StExtern && !s->defined;
}

// Address of a global, string literal, or function symbol ($name or extern $name).
static Val
emitgaddr(Compiler* c, Node* n) {
	Val v;
	int off;
	const char* name;

	memset(&v, 0, sizeof(v));
	v.cls = 'l';
	v.type = n->type;
	if (n->kind == NdStr || (n->symbol == NULL && n->kind == NdName && n->s && n->s[0] == 0)) {
		off = (int)n->int_val;
		if (off == 0)
			snprintf(v.text, sizeof(v.text), "$%s", emit_str_symbol);
		else {
			v = vtmp('l', n->type);
			fprintf(outf, "\t%s =l add $%s, %d\n", v.text, emit_str_symbol, off);
		}
		return v;
	}
	if (n->kind == NdName && n->symbol && n->symbol->kind == SkFunc) {
		snprintf(v.text, sizeof(v.text), "$%s", n->symbol->name);
		return v;
	}
	name = n->symbol ? symbol_link_name(n->symbol) : "g";
	if (needs_qbe_extern(n->symbol))
		snprintf(v.text, sizeof(v.text), "extern $%s", name);
	else
		snprintf(v.text, sizeof(v.text), "$%s", name);
	(void)c;
	return v;
}

// True for ordinary stack locals (not globals or static locals).
static int
isslot(Node* n) {
	return n && n->kind == NdName && n->symbol && !is_global_symbol(n->symbol) && !is_static_local(n->symbol) && n->symbol->kind == SkVar;
}

/* ---- expressions ---- */

// Address of an lvalue: slot, global, deref, field, or indexed element.
static Val
emitlval(Compiler* c, Node* n) {
	Val v, b, i, s;
	Type* bt;
	int64_t step;

	memset(&v, 0, sizeof(v));
	v.cls = 'l';
	if (n == NULL) {
		strcpy(v.text, "0");
		return v;
	}
	v.type = n->type;
	if (isslot(n)) {
		snprintf(v.text, sizeof(v.text), "%%%s.addr", slot_basename(n->symbol));
		return v;
	}
	if (n->kind == NdName && n->symbol && (is_global_symbol(n->symbol) || is_static_local(n->symbol) || n->symbol->kind == SkFunc))
		return emitgaddr(c, n);
	if (n->kind == NdStr)
		return emitgaddr(c, n);
	if (n->kind == NdDeref)
		return emitexpr(c, n->a);
	if (n->kind == NdDot || n->kind == NdArrow) {
		if (n->kind == NdArrow)
			b = emitexpr(c, n->a);
		else if (n->a && is_aggr(n->a->type))
			b = emitlval(c, n->a);
		else
			b = emitexpr(c, n->a);
		if (n->int_val == 0)
			return b;
		v = vtmp('l', n->type);
		fprintf(outf, "\t%s =l add %s, %d\n", v.text, b.text, (int)n->int_val);
		return v;
	}
	if (n->kind == NdIndex) {
		if (n->a && is_ranged(n->a->type) && n->a->type->base) {
			b = emitexpr(c, n->a);
			if (b.cls != 'l' && b.cls != '@')
				b = coerce(b, 'l', c->type_void_ptr);
			{
				Val p;

				p = vtmp('l', type_ptr(c, n->a->type->base));
				fprintf(outf, "\t%s =l loadl %s\n", p.text, b.text);
				b = p;
			}
			i = emitexpr(c, n->b);
			step = type_size(c, n->a->type->base);
			if (i.cls != 'l')
				i = coerce(i, 'l', c->type_llong);
			if (step != 1) {
				s = vtmp('l', c->type_llong);
				if (step > 0 && (step & (step - 1)) == 0) {
					int sh = 0;
					int64_t st = step;

					while (st > 1) {
						st >>= 1;
						sh++;
					}
					fprintf(outf, "\t%s =l shl %s, %d\n", s.text, i.text, sh);
				} else
					fprintf(outf, "\t%s =l mul %s, %" PRId64 "\n", s.text, i.text, (int64_t)step);
				i = s;
			}
			v = vtmp('l', n->type);
			fprintf(outf, "\t%s =l add %s, %s\n", v.text, b.text, i.text);
			return v;
		}
		b = emitexpr(c, n->a);
		i = emitexpr(c, n->b);
		bt = n->a && n->a->type ? n->a->type : NULL;
		if (bt && (bt->kind == TyArray || bt->kind == TyPtr) && bt->base)
			step = type_size(c, bt->base);
		else if (n->type)
			step = type_size(c, n->type);
		else
			step = 4;
		if (i.cls != 'l')
			i = coerce(i, 'l', c->type_llong);
		if (b.cls != 'l')
			b = coerce(b, 'l', c->type_void_ptr);
		if (step != 1) {
			s = vtmp('l', c->type_llong);
			if (step > 0 && (step & (step - 1)) == 0) {
				int sh = 0;
				int64_t st = step;

				while (st > 1) {
					st >>= 1;
					sh++;
				}
				fprintf(outf, "\t%s =l shl %s, %d\n", s.text, i.text, sh);
			} else
				fprintf(outf, "\t%s =l mul %s, %" PRId64 "\n", s.text, i.text, (int64_t)step);
			i = s;
		}
		v = vtmp('l', n->type);
		fprintf(outf, "\t%s =l add %s, %s\n", v.text, b.text, i.text);
		return v;
	}
	strcpy(v.text, "0");
	return v;
}

// Emit ++/-- on a modifiable lvalue; return pre- or post-update value.
// Evaluate the address once so a[i]++ does not recompute a[i].
static Val
emitinc(Compiler* c, Node* n, int pre, int plus) {
	Val addr, cur, neu;
	int step;
	Type* t;

	t = n->a ? n->a->type : NULL;
	step = 1;
	if (t && t->kind == TyPtr && t->base)
		step = type_size(c, t->base);
	addr = emitlval(c, n->a);
	if (is_aggr(t)) {
		cur = emitexpr(c, n->a);
		neu = vtmp(cur.cls, t);
		fprintf(outf, "\t%s =%c %s %s, %d\n",
			neu.text, cur.cls, plus ? "add" : "sub", cur.text, step);
		emitblit(addr, neu, storewidth(c, t));
		return pre ? neu : cur;
	}
	cur = vtmp(qbe_class(t), t);
	fprintf(outf, "\t%s =%c %s %s\n", cur.text, cur.cls, loadop(t), addr.text);
	neu = vtmp(cur.cls, t);
	fprintf(outf, "\t%s =%c %s %s, %d\n",
		neu.text, cur.cls, plus ? "add" : "sub", cur.text, step);
	fprintf(outf, "\t%s %s, %s\n", storeop(c, t), neu.text, addr.text);
	return pre ? neu : cur;
}

// Index of the ... in a varargs function type, or -1 if not varargs.
static int
fixedparams(Type* t) {
	if (t && t->kind == TyPtr && t->base && t->base->kind == TyFunc)
		t = t->base;
	if (t && t->kind == TyFunc && t->is_varargs)
		return t->params_len;
	return -1;
}

// Map compound-assignment punctuators to QBE arithmetic ops.
static const char*
qbe_arith_op(int punct, int is_unsigned) {
	switch (punct) {
	case PnPlus:
	case PnPlusEq:
		return "add";
	case PnMinus:
	case PnMinusEq:
		return "sub";
	case PnStar:
	case PnStarEq:
		return "mul";
	case PnSlash:
	case PnSlashEq:
		return is_unsigned ? "udiv" : "div";
	case PnPercent:
	case PnPercentEq:
		return is_unsigned ? "urem" : "rem";
	case PnAmp:
	case PnAmpEq:
		return "and";
	case PnPipe:
	case PnPipeEq:
		return "or";
	case PnCaret:
	case PnCaretEq:
		return "xor";
	case PnShl:
	case PnShlEq:
		return "shl";
	case PnShr:
	case PnShrEq:
		return is_unsigned ? "shr" : "sar";
	default:
		return "add";
	}
}

// Evaluate = and compound assignments, storing through the lhs lvalue.
// Compound assigns evaluate the lhs address once (no double a[i] for a[i] += k).
static Val
emitexpr_assign(Compiler* c, Node* n) {
	Val v, l, r, addr;
	const char* op;
	int uns;

	if (n->op != PnEq && n->a && !is_aggr(n->a->type)) {
		r = emitexpr(c, n->b);
		addr = emitlval(c, n->a);
		l = vtmp(qbe_class(n->a->type), n->a->type);
		fprintf(outf, "\t%s =%c %s %s\n", l.text, l.cls, loadop(n->a->type), addr.text);
		uns = n->a->type && n->a->type->is_unsigned;
		op = qbe_arith_op(n->op, uns);
		if (r.cls != l.cls)
			r = coerce(r, l.cls, n->a->type);
		if (n->op == PnShlEq || n->op == PnShrEq)
			r = mask_shift_count(c, r, n->a->type);
		v = vtmp(l.cls, n->a->type);
		fprintf(outf, "\t%s =%c %s %s, %s\n", v.text, l.cls, op, l.text, r.text);
		if (v.cls != qbe_class(n->a->type))
			v = coerce(v, qbe_class(n->a->type), n->a->type);
		fprintf(outf, "\t%s %s, %s\n", storeop(c, n->a->type), v.text, addr.text);
		return v;
	}
	r = emitexpr(c, n->b);
	if (is_aggr(n->a ? n->a->type : NULL)) {
		l = emitlval(c, n->a);
		emitblit(l, r, storewidth(c, n->a->type));
		return r;
	}
	if (r.cls != qbe_class(n->a->type) && !is_aggr(n->a->type))
		r = coerce(r, qbe_class(n->a->type), n->a->type);
	l = emitlval(c, n->a);
	fprintf(outf, "\t%s %s, %s\n", storeop(c, n->a->type), r.text, l.text);
	return r;
}

// Expand a small eligible call in place; bind args and jump to shared exit label.
static int
try_inline_call(Compiler* c, Node* n, Val* out) {
	Symbol* callee;
	InlineSite* site;
	Type *ty, *pt, *ret;
	Node* body;
	Val arg, addr, v;
	int i, j, endlbl, fallen;
	const char* bn;

	memset(&v, 0, sizeof(v));
	if (inl.active)
		return 0;
	if (n == NULL || n->a == NULL || n->a->kind != NdName || n->a->symbol == NULL)
		return 0;
	callee = n->a->symbol;
	if (callee->kind != SkFunc || !inline_eligible(callee) || inline_on_stack(callee))
		return 0;
	site = find_inline_site(n);
	if (site == NULL || site->callee != callee)
		return 0;
	ty = callee->type;
	body = callee->node->a;
	ret = ty ? ty->base : n->type;
	if (inl.stack_len >= MaxInlineStack)
		return 0;

	for (i = 0; i < n->children_len; i++) {
		pt = n->children[i]->type;
		if (ty && is_func(ty) && i < ty->params_len)
			pt = ty->params[i];
		arg = emitexpr(c, n->children[i]);
		if (!is_aggr(pt))
			arg = coerce(arg, qbe_class(pt), pt);
		bn = NULL;
		if (ty && ty->param_names && i < ty->params_len && ty->param_names[i]) {
			for (j = 0; j < site->map_len; j++)
				if (site->map[j].symbol && site->map[j].symbol->name && strcmp(site->map[j].symbol->name, ty->param_names[i]) == 0) {
					bn = site->map[j].name;
					break;
				}
		}
		if (bn == NULL)
			continue;
		snprintf(addr.text, sizeof(addr.text), "%%%s.addr", bn);
		addr.cls = 'l';
		addr.type = pt;
		if (is_aggr(pt))
			emitblit(addr, arg, storewidth(c, pt));
		else
			fprintf(outf, "\t%s %s, %s\n", storeop(c, pt), arg.text, addr.text);
	}

	endlbl = newlbl();
	inl.stack[inl.stack_len++] = callee;
	inl.active = 1;
	inl.site = site;
	inl.endlbl = endlbl;
	fallen = emitstmt_ret(c, body);
	if (!fallen)
		emitjmp(endlbl);
	emitlbl(endlbl);
	inl.active = 0;
	inl.site = NULL;
	inl.stack_len--;

	if (ret && ret->kind == TyVoid) {
		strcpy(v.text, "0");
		v.cls = 'w';
		v.type = ret;
		*out = v;
		return 1;
	}
	if (site->has_ret) {
		v = vtmp(qbe_class(ret), ret);
		fprintf(outf, "\t%s =%c %s %%%s.addr\n",
			v.text, v.cls, loadop(ret), site->retname);
		*out = v;
		return 1;
	}
	strcpy(v.text, "0");
	v.cls = qbe_class(ret);
	v.type = ret;
	*out = v;
	return 1;
}

// Emit a call (or builtin va_*); evaluate args and return the call result value.
static Val
emitexpr_call(Compiler* c, Node* n, Val v) {
	Val l, r, tgt;
	Type *ft, *want;
	char* bn;
	int i, fixed;

	bn = (n->a && n->a->kind == NdName && n->a->s) ? n->a->s : NULL;
	if (bn && strcmp(bn, "__builtin_va_start") == 0) {
		if (n->children_len >= 1) {
			l = emitexpr(c, n->children[0]);
			fprintf(outf, "\tvastart %s\n", l.text);
		}
		strcpy(v.text, "0");
		v.cls = 'w';
		return v;
	}
	if (bn && strcmp(bn, "__builtin_va_end") == 0) {
		if (n->children_len >= 1)
			emitexpr(c, n->children[0]);
		strcpy(v.text, "0");
		v.cls = 'w';
		return v;
	}
	if (bn && strcmp(bn, "__builtin_va_copy") == 0) {
		if (n->children_len >= 2) {
			l = emitexpr(c, n->children[0]);
			r = emitexpr(c, n->children[1]);
			/* amd64_sysv :valist is 32 bytes; amd64_win is one pointer. */
#ifdef _WIN32
			emitblit(l, r, 8);
#else
			emitblit(l, r, 32);
#endif
		}
		strcpy(v.text, "0");
		v.cls = 'w';
		return v;
	}
	if (bn && strcmp(bn, "__builtin_va_arg") == 0) {
		l = emitexpr(c, n->children[0]);
		want = n->type ? n->type : c->type_int;
		v = vtmp(qbe_class(want), want);
		fprintf(outf, "\t%s =%c vaarg %s\n", v.text, v.cls, l.text);
		return v;
	}
	if (bn && strcmp(bn, "ranged") == 0 && !(n->a && n->a->symbol)) {
		Val p, ln, cp, slot;
		int lenoff, capoff;
		int64_t alen;
		Field* f;

		ensure_aggregate(n->type);
		if (n->symbol)
			snprintf(slot.text, sizeof(slot.text), "%%%s.addr", slot_basename(n->symbol));
		else {
			slot = vtmp('l', n->type);
			fprintf(outf, "\t%s =l alloc8 %d\n", slot.text, storewidth(c, n->type));
		}
		slot.cls = 'l';
		if (n->children_len >= 1)
			p = emitexpr(c, n->children[0]);
		else {
			p.text[0] = '0';
			p.text[1] = 0;
			p.cls = 'l';
		}
		if (p.cls != 'l')
			p = coerce(p, 'l', c->type_void_ptr);
		fprintf(outf, "\tstorel %s, %s\n", p.text, slot.text);
		lenoff = 8;
		capoff = 16;
		for (f = n->type ? n->type->fields : NULL; f; f = f->next) {
			if (f->name && strcmp(f->name, "len") == 0)
				lenoff = f->offset;
			if (f->name && strcmp(f->name, "cap") == 0)
				capoff = f->offset;
		}
		ln = vtmp('l', c->type_ullong);
		fprintf(outf, "\t%s =l add %s, %d\n", ln.text, slot.text, lenoff);
		alen = 0;
		if (n->children_len == 1 && n->children[0] && n->children[0]->kind == NdStr && n->children[0]->type && n->children[0]->type->base && (n->children[0]->type->base->kind == TyChar || n->children[0]->type->base->kind == TyUChar) && n->children[0]->type->len > 0)
			alen = n->children[0]->type->len - 1;
		else if (n->children_len == 1 && n->children[0] && is_array(n->children[0]->type))
			alen = n->children[0]->type->len;
		if (n->children_len == 1 && alen > 0)
			r = vimm('l', alen, c->type_ullong);
		else if (n->children_len >= 2) {
			r = emitexpr(c, n->children[1]);
			if (r.cls != 'l')
				r = coerce(r, 'l', c->type_ullong);
		} else
			r = vimm('l', 0, c->type_ullong);
		fprintf(outf, "\tstorel %s, %s\n", r.text, ln.text);
		/* cap: third arg, else same as len (full view). */
		cp = vtmp('l', c->type_ullong);
		fprintf(outf, "\t%s =l add %s, %d\n", cp.text, slot.text, capoff);
		if (n->children_len >= 3) {
			Val cv;

			cv = emitexpr(c, n->children[2]);
			if (cv.cls != 'l')
				cv = coerce(cv, 'l', c->type_ullong);
			fprintf(outf, "\tstorel %s, %s\n", cv.text, cp.text);
		} else
			fprintf(outf, "\tstorel %s, %s\n", r.text, cp.text);
		snprintf(v.text, sizeof(v.text), "%s", slot.text);
		v.cls = '@';
		v.type = n->type;
		return v;
	}
	if (bn && strcmp(bn, "len") == 0 && !(n->a && n->a->symbol)) {
		Node* x;
		Val base, off, ln;
		int lenoff;
		Field* f;

		x = n->children_len >= 1 ? n->children[0] : NULL;
		if (x && is_ranged(x->type)) {
			base = emitexpr(c, x);
			if (base.cls != 'l' && base.cls != '@')
				base = coerce(base, 'l', c->type_void_ptr);
			lenoff = 8;
			for (f = x->type->fields; f; f = f->next)
				if (f->name && strcmp(f->name, "len") == 0)
					lenoff = f->offset;
			off = vtmp('l', c->type_void_ptr);
			fprintf(outf, "\t%s =l add %s, %d\n", off.text, base.text, lenoff);
			ln = vtmp('l', c->type_ullong);
			fprintf(outf, "\t%s =l loadl %s\n", ln.text, off.text);
			return ln;
		}
		if (x && is_array(x->type) && x->type->len >= 0)
			return vimm('l', x->type->len, c->type_ullong);
		if (x && x->kind == NdName && x->symbol && x->symbol->array_param && x->symbol->param_fixed_len >= 0)
			return vimm('l', x->symbol->param_fixed_len, c->type_ullong);
		if (x) {
			Type* ag;

			ag = field_lhs(x->type);
			if (ag && anon_embed_unique_ranged(ag, NULL, NULL) == 1) {
				int lenoff;

				if (find_field(ag, "len", &lenoff)) {
					if (x->type && is_aggr(x->type))
						base = emitlval(c, x);
					else
						base = emitexpr(c, x);
					if (base.cls != 'l' && base.cls != '@')
						base = coerce(base, 'l', c->type_void_ptr);
					off = vtmp('l', c->type_void_ptr);
					fprintf(outf, "\t%s =l add %s, %d\n", off.text, base.text, lenoff);
					ln = vtmp('l', c->type_ullong);
					fprintf(outf, "\t%s =l loadl %s\n", ln.text, off.text);
					return ln;
				}
			}
		}
		v.text[0] = '0';
		v.text[1] = 0;
		v.cls = 'l';
		return v;
	}
	if (bn && strcmp(bn, "cap") == 0 && !(n->a && n->a->symbol)) {
		Node* x;
		Val base, off, cv;
		int capoff;
		Field* f;

		x = n->children_len >= 1 ? n->children[0] : NULL;
		if (x && is_ranged(x->type)) {
			base = emitexpr(c, x);
			if (base.cls != 'l' && base.cls != '@')
				base = coerce(base, 'l', c->type_void_ptr);
			capoff = 16;
			for (f = x->type->fields; f; f = f->next)
				if (f->name && strcmp(f->name, "cap") == 0)
					capoff = f->offset;
			off = vtmp('l', c->type_void_ptr);
			fprintf(outf, "\t%s =l add %s, %d\n", off.text, base.text, capoff);
			cv = vtmp('l', c->type_ullong);
			fprintf(outf, "\t%s =l loadl %s\n", cv.text, off.text);
			return cv;
		}
		if (x && is_array(x->type) && x->type->len >= 0)
			return vimm('l', x->type->len, c->type_ullong);
		if (x && x->kind == NdName && x->symbol && x->symbol->array_param && x->symbol->param_fixed_len >= 0)
			return vimm('l', x->symbol->param_fixed_len, c->type_ullong);
		if (x) {
			Type* ag;

			ag = field_lhs(x->type);
			if (ag && anon_embed_unique_ranged(ag, NULL, NULL) == 1) {
				if (find_field(ag, "cap", &capoff)) {
					if (x->type && is_aggr(x->type))
						base = emitlval(c, x);
					else
						base = emitexpr(c, x);
					if (base.cls != 'l' && base.cls != '@')
						base = coerce(base, 'l', c->type_void_ptr);
					off = vtmp('l', c->type_void_ptr);
					fprintf(outf, "\t%s =l add %s, %d\n", off.text, base.text, capoff);
					cv = vtmp('l', c->type_ullong);
					fprintf(outf, "\t%s =l loadl %s\n", cv.text, off.text);
					return cv;
				}
			}
		}
		v.text[0] = '0';
		v.text[1] = 0;
		v.cls = 'l';
		return v;
	}
	if (bn && strcmp(bn, "ptr") == 0 && !(n->a && n->a->symbol)) {
		Node* x;
		Val base, p;
		int ptroff;
		Field* f;

		x = n->children_len >= 1 ? n->children[0] : NULL;
		if (x && is_ranged(x->type)) {
			base = emitexpr(c, x);
			if (base.cls != 'l' && base.cls != '@')
				base = coerce(base, 'l', c->type_void_ptr);
			ptroff = 0;
			for (f = x->type->fields; f; f = f->next)
				if (f->name && strcmp(f->name, "ptr") == 0)
					ptroff = f->offset;
			if (ptroff == 0) {
				p = vtmp('l', n->type ? n->type : c->type_void_ptr);
				fprintf(outf, "\t%s =l loadl %s\n", p.text, base.text);
				return p;
			}
			{
				Val off;

				off = vtmp('l', c->type_void_ptr);
				fprintf(outf, "\t%s =l add %s, %d\n", off.text, base.text, ptroff);
				p = vtmp('l', n->type ? n->type : c->type_void_ptr);
				fprintf(outf, "\t%s =l loadl %s\n", p.text, off.text);
				return p;
			}
		}
		if (x && is_array(x->type)) {
			p = emitexpr(c, x);
			if (p.cls != 'l')
				p = coerce(p, 'l', n->type ? n->type : c->type_void_ptr);
			return p;
		}
		if (x) {
			Type* ag;

			ag = field_lhs(x->type);
			if (ag && anon_embed_unique_ranged(ag, NULL, NULL) == 1) {
				if (find_field(ag, "ptr", &ptroff)) {
					if (x->type && is_aggr(x->type))
						base = emitlval(c, x);
					else
						base = emitexpr(c, x);
					if (base.cls != 'l' && base.cls != '@')
						base = coerce(base, 'l', c->type_void_ptr);
					{
						Val off;

						off = vtmp('l', c->type_void_ptr);
						fprintf(outf, "\t%s =l add %s, %d\n", off.text, base.text, ptroff);
						p = vtmp('l', n->type ? n->type : c->type_void_ptr);
						fprintf(outf, "\t%s =l loadl %s\n", p.text, off.text);
						return p;
					}
				}
			}
		}
		v.text[0] = '0';
		v.text[1] = 0;
		v.cls = 'l';
		return v;
	}
	if (try_inline_call(c, n, &v))
		return v;
	ft = n->a ? n->a->type : NULL;
	if (ft && is_ptr(ft) && is_func(ft->base))
		ft = ft->base;
	if (ft && is_func(ft) == 0 && n->a && n->a->kind == NdName && n->a->symbol)
		ft = n->a->symbol->type;
	if (n->a && n->a->kind == NdName && n->a->symbol && n->a->symbol->kind == SkFunc)
		snprintf(tgt.text, sizeof(tgt.text), "$%s", symbol_link_name(n->a->symbol));
	else
		tgt = emitexpr(c, n->a);
	fixed = fixedparams(ft);
	{
		Val args[MaxParams];
		int na = n->children_len < MaxParams ? n->children_len : MaxParams;
		for (i = 0; i < na; i++) {
			Type* pt;

			pt = n->children[i]->type;
			if (ft && is_func(ft) && i < ft->params_len)
				pt = ft->params[i];
			args[i] = emitexpr(c, n->children[i]);
			args[i] = coerce(args[i], qbe_class(pt), pt);
		}
		if (n->type && n->type->kind == TyVoid) {
			strcpy(v.text, "0");
			v.cls = 'w';
			fprintf(outf, "\tcall %s(", tgt.text);
		} else if (is_aggr(n->type)) {
			ensure_aggregate(n->type);
			v = vtmp('@', n->type);
			fprintf(outf, "\t%s =:%s call %s(", v.text, aggregate_name(n->type), tgt.text);
		} else {
			v = vtmp(v.cls, n->type);
			fprintf(outf, "\t%s =%c call %s(", v.text, v.cls, tgt.text);
		}
		for (i = 0; i < na; i++) {
			Type* pt;

			if (i)
				fputs(", ", outf);
			if (fixed >= 0 && i == fixed)
				fputs("..., ", outf);
			pt = n->children[i]->type;
			if (ft && is_func(ft) && i < ft->params_len)
				pt = ft->params[i];
			print_abi_type(pt);
			fprintf(outf, " %s", args[i].text);
		}
		if (fixed >= 0 && n->children_len <= fixed) {
			if (n->children_len > 0)
				fputs(", ", outf);
			fputs("...", outf);
		}
		fputs(")\n", outf);
		return v;
	}
}

// Binary expressions: short-circuit logic, compares, ptr arithmetic, arithmetic.
static Val
emitexpr_bin(Compiler* c, Node* n) {
	Val v, l, r;
	char cls;
	int ttrue, tfalse, tjoin, tright;
	const char* op;

	if (n->op == PnAmpAmp || n->op == PnPipePipe) {
		ttrue = newlbl();
		tfalse = newlbl();
		tjoin = newlbl();
		tright = newlbl();
		l = asbool(emitexpr(c, n->a));
		if (n->op == PnAmpAmp)
			emitjnz(l.text, tright, tfalse);
		else
			emitjnz(l.text, ttrue, tright);
		emitlbl(tright);
		r = asbool(emitexpr(c, n->b));
		emitjnz(r.text, ttrue, tfalse);
		emitlbl(ttrue);
		emitjmp(tjoin);
		emitlbl(tfalse);
		emitjmp(tjoin);
		emitlbl(tjoin);
		v = vtmp('w', c->type_bool);
		fprintf(outf, "\t%s =w phi @L%d 1, @L%d 0\n", v.text, ttrue, tfalse);
		return v;
	}
	if (n->op == PnEqEq || n->op == PnBangEq || n->op == PnLt || n->op == PnGt || n->op == PnLe || n->op == PnGe) {
		l = emitexpr(c, n->a);
		r = emitexpr(c, n->b);
		if (n->type && n->type->kind == TyDouble)
			cls = 'd';
		else if (n->type && n->type->kind == TyFloat)
			cls = 's';
		else if (r.cls == 'l' || l.cls == 'l' || is_ptr(n->a->type) || is_ptr(n->b->type))
			cls = 'l';
		else
			cls = l.cls;
		if (l.cls != cls)
			l = coerce(l, cls, n->a->type);
		if (r.cls != cls)
			r = coerce(r, cls, n->b->type);
		v = vtmp('w', c->type_bool);
		{
			int uns = (n->a->type && (n->a->type->is_unsigned || is_ptr(n->a->type))) || (n->b->type && (n->b->type->is_unsigned || is_ptr(n->b->type)));
			fprintf(outf, "\t%s =w %s %s, %s\n",
				v.text, cmpinst(n->op, cls, uns), l.text, r.text);
		}
		return v;
	}
	l = emitexpr(c, n->a);
	r = emitexpr(c, n->b);
	if (n->op == PnPlus || n->op == PnMinus) {
		if (is_ptr(n->a->type) && is_int(n->b->type)) {
			int step = n->a->type->base ? type_size(c, n->a->type->base) : 1;
			if (r.cls != 'l')
				r = coerce(r, 'l', c->type_llong);
			if (step != 1) {
				Val sc = vtmp('l', c->type_llong);
				fprintf(outf, "\t%s =l mul %s, %d\n", sc.text, r.text, step);
				r = sc;
			}
			if (l.cls != 'l')
				l = coerce(l, 'l', n->a->type);
			v = vtmp('l', n->type);
			fprintf(outf, "\t%s =l %s %s, %s\n",
				v.text, n->op == PnPlus ? "add" : "sub", l.text, r.text);
			return v;
		}
		if (n->op == PnPlus && is_int(n->a->type) && is_ptr(n->b->type)) {
			int step = n->b->type->base ? type_size(c, n->b->type->base) : 1;
			if (l.cls != 'l')
				l = coerce(l, 'l', c->type_llong);
			if (step != 1) {
				Val sc = vtmp('l', c->type_llong);
				fprintf(outf, "\t%s =l mul %s, %d\n", sc.text, l.text, step);
				l = sc;
			}
			if (r.cls != 'l')
				r = coerce(r, 'l', n->b->type);
			v = vtmp('l', n->type);
			fprintf(outf, "\t%s =l add %s, %s\n", v.text, r.text, l.text);
			return v;
		}
		if (n->op == PnMinus && is_ptr(n->a->type) && is_ptr(n->b->type)) {
			int step = n->a->type->base ? type_size(c, n->a->type->base) : 1;
			if (l.cls != 'l')
				l = coerce(l, 'l', n->a->type);
			if (r.cls != 'l')
				r = coerce(r, 'l', n->b->type);
			v = vtmp('l', c->type_llong);
			fprintf(outf, "\t%s =l sub %s, %s\n", v.text, l.text, r.text);
			if (step > 1) {
				Val q = vtmp('l', c->type_llong);
				fprintf(outf, "\t%s =l div %s, %d\n", q.text, v.text, step);
				v = q;
			}
			return v;
		}
	}
	cls = qbe_class(n->type);
	if (l.cls != cls)
		l = coerce(l, cls, n->type);
	if (r.cls != cls)
		r = coerce(r, cls, n->type);
	if (n->op == PnShl || n->op == PnShr)
		r = mask_shift_count(c, r, n->type);
	op = qbe_arith_op(n->op, n->type && n->type->is_unsigned);
	v = vtmp(cls, n->type);
	fprintf(outf, "\t%s =%c %s %s, %s\n", v.text, cls, op, l.text, r.text);
	return v;
}

// Tuple literal: alloc aggregate on stack and store each field.
static Val
emitexpr_tuple(Compiler* c, Node* n) {
	Val v, slot, r, off;
	Field* f;
	int i, w;

	ensure_aggregate(n->type);
	w = storewidth(c, n->type);
	slot = vtmp('l', n->type);
	fprintf(outf, "\t%s =l alloc8 %d\n", slot.text, w);
	for (f = n->type->fields, i = 0; f && i < n->children_len; f = f->next, i++) {
		off = vtmp('l', c->type_void_ptr);
		fprintf(outf, "\t%s =l add %s, %d\n", off.text, slot.text, f->offset);
		r = emitexpr(c, n->children[i]);
		if (is_array(f->type) || is_aggr(f->type)) {
			r.cls = '@';
			emitblit(off, r, storewidth(c, f->type));
		} else {
			if (r.cls != qbe_class(f->type))
				r = coerce(r, qbe_class(f->type), f->type);
			fprintf(outf, "\t%s %s, %s\n", storeop(c, f->type), r.text, off.text);
		}
	}
	v.cls = '@';
	v.type = n->type;
	snprintf(v.text, sizeof(v.text), "%s", slot.text);
	return v;
}

// Main expression dispatcher: literals, loads, casts, calls, control-flow exprs.
static Val
emitexpr(Compiler* c, Node* n) {
	Val v, l, r;
	char cls;
	int ttrue, tfalse, tjoin;

	memset(&v, 0, sizeof(v));
	if (n == NULL) {
		strcpy(v.text, "0");
		v.cls = 'w';
		return v;
	}
	v.type = n->type;
	if (is_aggr(n->type))
		v.cls = '@';
	else
		v.cls = qbe_class(n->type);

	switch (n->kind) {
	case NdLit:
		if (n->type && (n->type->kind == TyFloat || n->type->kind == TyDouble)) {
			fpconst(&v, n->type, n->s);
			return v;
		}
		snprintf(v.text, sizeof(v.text), "%" PRId64, (int64_t)n->int_val);
		return v;
	case NdStr:
		return emitgaddr(c, n);
	case NdName:
		if (n->symbol && n->symbol->kind == SkFunc) {
			snprintf(v.text, sizeof(v.text), "$%s", n->symbol->name);
			v.cls = 'l';
			return v;
		}
		if (n->type && n->type->kind == TyArray) {
			v.cls = 'l';
			if (isslot(n)) {
				snprintf(v.text, sizeof(v.text), "%%%s.addr", slot_basename(n->symbol));
				return v;
			}
			return emitgaddr(c, n);
		}
		if (is_aggr(n->type)) {
			v.cls = '@';
			if (isslot(n)) {
				snprintf(v.text, sizeof(v.text), "%%%s.addr", slot_basename(n->symbol));
				return v;
			}
			return emitgaddr(c, n);
		}
		if (isslot(n)) {
			snprintf(v.text, sizeof(v.text), "%%t%d", newtmp());
			fprintf(outf, "\t%s =%c %s %%%s.addr\n",
				v.text, v.cls, loadop(n->type), slot_basename(n->symbol));
			return v;
		}
		if (n->symbol && (is_global_symbol(n->symbol) || is_static_local(n->symbol))) {
			l = emitgaddr(c, n);
			snprintf(v.text, sizeof(v.text), "%%t%d", newtmp());
			fprintf(outf, "\t%s =%c %s %s\n", v.text, v.cls, loadop(n->type), l.text);
			return v;
		}
		strcpy(v.text, "0");
		return v;
	case NdSizeof:
	case NdSizeofT:
		snprintf(v.text, sizeof(v.text), "%d", (int)n->int_val);
		v.cls = 'w';
		return v;
	case NdCast:
		if (n->type && n->type->kind == TyVoid) {
			/* (void)name; — no load (unused silence) */
			if (n->a && n->a->kind != NdName)
				emitexpr(c, n->a);
			strcpy(v.text, "0");
			v.cls = 'w';
			return v;
		}
		l = emitexpr(c, n->a);
		cls = qbe_class(n->type);
		if (is_ptr(n->type) && l.cls == 'w')
			return coerce(l, 'l', n->type);
		if (cls != l.cls)
			return coerce(l, cls, n->type);
		l.type = n->type;
		return l;
	case NdAddr:
		return emitlval(c, n->a);
	case NdDeref:
		if (n->a && n->a->type) {
			Type* pt = decay(c, n->a->type);
			if (pt && is_ptr(pt) && is_func(pt->base))
				return emitexpr(c, n->a);
		}
		if (n->type && (n->type->kind == TyArray || n->type->kind == TyFunc || is_aggr(n->type))) {
			v = emitexpr(c, n->a);
			v.cls = is_aggr(n->type) ? '@' : 'l';
			return v;
		}
		l = emitexpr(c, n->a);
		v = vtmp(v.cls, n->type);
		fprintf(outf, "\t%s =%c %s %s\n", v.text, v.cls, loadop(n->type), l.text);
		return v;
	case NdDot:
	case NdArrow:
	case NdIndex:
		if (n->type && (n->type->kind == TyArray || is_aggr(n->type))) {
			v = emitlval(c, n);
			v.cls = is_aggr(n->type) ? '@' : 'l';
			v.type = n->type;
			return v;
		}
		l = emitlval(c, n);
		v = vtmp(v.cls, n->type);
		fprintf(outf, "\t%s =%c %s %s\n", v.text, v.cls, loadop(n->type), l.text);
		return v;
	case NdSubrange: {
		Val slot, base, ptr, lenv, lo, hi, tmp, off;
		Type *bt, *elem;
		int stride, lenoff;
		Field* f;

		ensure_aggregate(n->type);
		elem = n->type && n->type->base ? n->type->base : c->type_int;
		stride = type_size(c, elem);
		if (stride < 1)
			stride = 1;
		slot = vtmp('l', n->type);
		fprintf(outf, "\t%s =l alloc8 %d\n", slot.text, storewidth(c, n->type));
		bt = n->a ? n->a->type : NULL;
		if (is_ranged(bt) && bt->base) {
			base = emitexpr(c, n->a);
			if (base.cls != 'l' && base.cls != '@')
				base = coerce(base, 'l', c->type_void_ptr);
			ptr = vtmp('l', type_ptr(c, bt->base));
			fprintf(outf, "\t%s =l loadl %s\n", ptr.text, base.text);
			lenoff = 8;
			if (bt->fields && bt->fields->next)
				lenoff = bt->fields->next->offset;
			off = vtmp('l', c->type_void_ptr);
			fprintf(outf, "\t%s =l add %s, %d\n", off.text, base.text, lenoff);
			lenv = vtmp('l', c->type_ullong);
			fprintf(outf, "\t%s =l loadl %s\n", lenv.text, off.text);
		} else if (is_array(bt) && bt->base) {
			ptr = emitexpr(c, n->a);
			if (ptr.cls != 'l')
				ptr = coerce(ptr, 'l', type_ptr(c, bt->base));
			lenv = vimm('l', bt->len >= 0 ? bt->len : 0, c->type_ullong);
		} else {
			ptr = emitexpr(c, n->a);
			lenv = vimm('l', 0, c->type_ullong);
		}
		if (n->b) {
			lo = emitexpr(c, n->b);
			if (lo.cls != 'l')
				lo = coerce(lo, 'l', c->type_ullong);
		} else
			lo = vimm('l', 0, c->type_ullong);
		if (n->c) {
			hi = emitexpr(c, n->c);
			if (hi.cls != 'l')
				hi = coerce(hi, 'l', c->type_ullong);
		} else
			hi = lenv;
		tmp = vtmp('l', c->type_ullong);
		if (stride != 1)
			fprintf(outf, "\t%s =l mul %s, %d\n", tmp.text, lo.text, stride);
		else
			fprintf(outf, "\t%s =l copy %s\n", tmp.text, lo.text);
		off = vtmp('l', type_ptr(c, elem));
		fprintf(outf, "\t%s =l add %s, %s\n", off.text, ptr.text, tmp.text);
		tmp = vtmp('l', c->type_ullong);
		fprintf(outf, "\t%s =l sub %s, %s\n", tmp.text, hi.text, lo.text);
		fprintf(outf, "\tstorel %s, %s\n", off.text, slot.text);
		lenoff = 8;
		for (f = n->type->fields; f; f = f->next)
			if (f->name && strcmp(f->name, "len") == 0)
				lenoff = f->offset;
		off = vtmp('l', c->type_void_ptr);
		fprintf(outf, "\t%s =l add %s, %d\n", off.text, slot.text, lenoff);
		fprintf(outf, "\tstorel %s, %s\n", tmp.text, off.text);
		/* Subrange is a closed view: cap == len (no parent spare). */
		{
			int capoff = 16;
			Val cpoff;

			for (f = n->type->fields; f; f = f->next)
				if (f->name && strcmp(f->name, "cap") == 0)
					capoff = f->offset;
			cpoff = vtmp('l', c->type_void_ptr);
			fprintf(outf, "\t%s =l add %s, %d\n", cpoff.text, slot.text, capoff);
			fprintf(outf, "\tstorel %s, %s\n", tmp.text, cpoff.text);
		}
		snprintf(v.text, sizeof(v.text), "%s", slot.text);
		v.cls = '@';
		v.type = n->type;
		return v;
	}
	case NdTupleLit:
		return emitexpr_tuple(c, n);
	case NdAssign:
		return emitexpr_assign(c, n);
	case NdUn:
		if (n->op == PnPlusPlus)
			return emitinc(c, n, 1, 1);
		if (n->op == PnMinusMinus)
			return emitinc(c, n, 1, 0);
		l = emitexpr(c, n->a);
		if (n->op == PnBang) {
			l = asbool(l);
			v = vtmp('w', c->type_bool);
			fprintf(outf, "\t%s =w ceqw %s, 0\n", v.text, l.text);
			return v;
		}
		if (n->op == PnPlus)
			return l;
		v = vtmp(l.cls, n->type);
		if (n->op == PnMinus)
			fprintf(outf, "\t%s =%c sub 0, %s\n", v.text, l.cls, l.text);
		else if (n->op == PnTilde)
			fprintf(outf, "\t%s =%c xor %s, -1\n", v.text, l.cls, l.text);
		else
			return l;
		return v;
	case NdPost:
		return emitinc(c, n, 0, n->op == PnPlusPlus);
	case NdCall:
		return emitexpr_call(c, n, v);
	case NdBin:
		return emitexpr_bin(c, n);
	case NdCond:
		ttrue = newlbl();
		tfalse = newlbl();
		tjoin = newlbl();
		cls = qbe_class(n->type);
		l = asbool(emitexpr(c, n->a));
		emitjnz(l.text, ttrue, tfalse);
		emitlbl(ttrue);
		r = emitexpr(c, n->b);
		if (r.cls != cls && r.cls != '@')
			r = coerce(r, cls, n->type);
		emitjmp(tjoin);
		emitlbl(tfalse);
		l = emitexpr(c, n->c);
		if (l.cls != cls && l.cls != '@')
			l = coerce(l, cls, n->type);
		emitjmp(tjoin);
		emitlbl(tjoin);
		v = vtmp(cls, n->type);
		fprintf(outf, "\t%s =%c phi @L%d %s, @L%d %s\n",
			v.text, cls, ttrue, r.text, tfalse, l.text);
		return v;
	case NdComma:
		emitexpr(c, n->a);
		return emitexpr(c, n->b);
	default:
		strcpy(v.text, "0");
		return v;
	}
}

// Push break/continue targets and the defer depth at loop entry.
static void
pushloop(int brk, int cont) {
	if (loops_len < MaxLoop) {
		loop_defer[loops_len] = defers_len;
		loopbrk[loops_len] = brk;
		loopcont[loops_len] = cont;
		loops_len++;
	}
}

// Pop the innermost loop's break/continue labels.
static void
poploop(void) {
	if (loops_len > 0)
		loops_len--;
}

// One defer stack frame per compound statement.
static void
push_defer_frame(Node* scope) {
	if (defers_len >= MaxDefer)
		return;
	deferstk[defers_len].stmts_len = 0;
	deferstk[defers_len].scope = scope;
	defers_len++;
}

// Queue a defer statement on the current compound's frame.
static void
add_defer(Compiler* c, Node* stmt) {
	DeferFrame* f;

	(void)c;
	if (defers_len <= 0 || stmt == NULL)
		return;
	f = &deferstk[defers_len - 1];
	if (f->stmts_len >= MaxDeferStmt)
		return;
	f->stmts[f->stmts_len++] = stmt;
}

// Run deferred statements for one frame in reverse registration order.
static void
emit_defers_frame(Compiler* c, int fi) {
	int i;

	if (fi < 0 || fi >= defers_len)
		return;
	for (i = deferstk[fi].stmts_len - 1; i >= 0; i--)
		(void)emitstmt_ret(c, deferstk[fi].stmts[i]);
}

// Run and discard the innermost defer frame (leaving a block).
static void
pop_defer_frame(Compiler* c) {
	if (defers_len <= 0)
		return;
	emit_defers_frame(c, defers_len - 1);
	defers_len--;
}

// Emit defers for a control transfer without changing lexical emission state.
static void
emit_defers_until(Compiler* c, int target) {
	int i;

	for (i = defers_len - 1; i >= target; i--)
		emit_defers_frame(c, i);
}

// Run every queued defer before function return.
static void
emit_all_defers(Compiler* c) {
	emit_defers_until(c, 0);
}

// True when ancestor is scope itself or one of its lexical parents.
static int
scope_contains(Node* ancestor, Node* scope) {
	for (; scope; scope = scope->scope)
		if (scope == ancestor)
			return 1;
	return 0;
}

// Emit defer frames belonging to scopes exited by a goto.
static void
emit_defers_for_goto(Compiler* c, Node* label_scope) {
	int i;

	for (i = defers_len - 1; i >= 0; i--) {
		if (label_scope && scope_contains(deferstk[i].scope, label_scope))
			break;
		emit_defers_frame(c, i);
	}
}

// Branch on a scalar condition without materializing a bool temporary when possible.
static void
emitbooljmp(Compiler* c, Node* n, int t, int f) {
	Val v;

	v = asbool(emitexpr(c, n));
	emitjnz(v.text, t, f);
}

typedef struct Casearm Casearm;
struct Casearm {
	int64_t lo;
	int64_t hi;
	int lbl;
};

// Gather switch case arms and default label from the case-list subtree.
static void
collectcases(Node* n, Casearm* arms, int* narm, int* def) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NdCase) {
		if (*narm < MaxCase) {
			arms[*narm].lo = n->int_val;
			arms[*narm].hi = (n->b && n->b->kind == NdLit) ? n->b->int_val : n->int_val;
			if (n->op == 0)
				n->op = newlbl(); /* reuse op as label */
			arms[*narm].lbl = n->op;
			(*narm)++;
		}
		return;
	}
	if (n->kind == NdDefault) {
		if (n->op == 0)
			n->op = newlbl();
		*def = n->op;
		return;
	}
	collectcases(n->a, arms, narm, def);
	collectcases(n->b, arms, narm, def);
	collectcases(n->c, arms, narm, def);
	for (i = 0; i < n->children_len; i++)
		collectcases(n->children[i], arms, narm, def);
}

// Label id for a case/default arm (stored in node->op).
static int
caselbl(Node* n) {
	if (n->op == 0)
		n->op = newlbl();
	return n->op;
}

/* ---- statements ---- */

// Store zeros into [addr, addr+n). Large regions call memset; small use stores.
static void
emit_zero_mem(Val addr, int n) {
	Val cur, nxt, tmp;
	int chunk;

	if (n >= 64) {
		tmp = vtmp('l', NULL);
		fprintf(outf, "\t%s =l call $memset(l %s, w 0, l %d)\n", tmp.text, addr.text, n);
		return;
	}
	cur = addr;
	while (n > 0) {
		if (n >= 8)
			chunk = 8;
		else if (n >= 4)
			chunk = 4;
		else
			chunk = 1;
		if (chunk == 8)
			fprintf(outf, "\tstorel 0, %s\n", cur.text);
		else if (chunk == 4)
			fprintf(outf, "\tstorew 0, %s\n", cur.text);
		else
			fprintf(outf, "\tstoreb 0, %s\n", cur.text);
		n -= chunk;
		if (n <= 0)
			break;
		nxt = vtmp('l', NULL);
		fprintf(outf, "\t%s =l add %s, %d\n", nxt.text, cur.text, chunk);
		cur = nxt;
	}
}

// Address of base + byte offset (identity when off == 0).
static Val
emit_addr_off(Val base, int off) {
	Val v;

	if (off == 0)
		return base;
	v = vtmp('l', NULL);
	fprintf(outf, "\t%s =l add %s, %d\n", v.text, base.text, off);
	return v;
}

static void emit_local_init(Compiler* c, Val base, Type* t, Initializer* in, int off);

// Initialize a local character array without reading beyond the literal.
static void
emit_local_string_init(Compiler* c, Val dest, Type* t, Node* str) {
	Val src;
	int dstlen, srclen, n;

	dstlen = type_size(c, t);
	srclen = str && str->type ? type_size(c, str->type) : 0;
	emit_zero_mem(dest, dstlen);
	n = srclen < dstlen ? srclen : dstlen;
	if (n > 0) {
		src = emitexpr(c, str);
		emitblit(dest, src, n);
	}
}

// Return the positional cursor immediately after a field designator's top field.
static int
field_designator_cursor(Type* t, Initializer* in) {
	Field* f;
	int pos;

	if (t == NULL || in == NULL || in->fields_len == 0)
		return 0;
	for (f = t->fields, pos = 0; f; f = f->next, pos++)
		if ((f->name && strcmp(f->name, in->fields[0]) == 0) ||
		    (!f->name && is_aggr(f->type) &&
		     find_field(f->type, in->fields[0], NULL)))
			return pos + 1;
	return 0;
}

// Emit a braced initializer list into a local/aggregate at base+off.
static void
emit_local_init_list(Compiler* c, Val base, Type* t, Initializer* in, int off) {
	int i, nextpos, inner, w, j;
	Type* ft;
	Field* f;

	nextpos = 0;
	for (i = 0; i < in->items_len; i++) {
		Initializer* it = &in->items[i];

		if (it->designator == IdIndexEq) {
			if (t->kind != TyArray)
				continue;
			w = type_size(c, t->base);
			emit_local_init(c, base, t->base, it, off + (int)(it->index * w));
			nextpos = (int)it->index + 1;
		} else if (it->designator == IdFieldDot) {
			ft = emit_field_path(c, t, it, &inner);
			if (ft)
				emit_local_init(c, base, ft, it, off + inner);
			nextpos = field_designator_cursor(t, it);
		} else if (t->kind == TyArray) {
			w = type_size(c, t->base);
			emit_local_init(c, base, t->base, it, off + nextpos * w);
			nextpos++;
		} else if (is_aggr(t)) {
			for (f = t->fields, j = 0; f && j < nextpos; j++)
				f = f->next;
			if (f)
				emit_local_init(c, base, f->type, it, off + f->offset);
			nextpos++;
		} else
			emit_local_init(c, base, t, it, off);
	}
}

// Emit one local initializer (scalar expr, string, or nested brace list).
static void
emit_local_init(Compiler* c, Val base, Type* t, Initializer* in, int off) {
	Val dest, r;
	char cls;
	int w, i;

	if (t == NULL || in == NULL)
		return;
	if (in->is_list) {
		emit_local_init_list(c, base, t, in, off);
		return;
	}
	if (t->kind == TyArray) {
		w = t->base ? type_size(c, t->base) : 4;
		if (in->expr && in->expr->kind == NdStr && t->base && type_size(c, t->base) == 1) {
			dest = emit_addr_off(base, off);
			emit_local_string_init(c, dest, t, in->expr);
			return;
		}
		emit_local_init(c, base, t->base, in, off);
		for (i = 1; i < t->len; i++)
			; /* remaining elements already zeroed by caller */
		(void)w;
		return;
	}
	if (!in->expr)
		return;
	dest = emit_addr_off(base, off);
	if (is_aggr(t) || is_array(t)) {
		/*
		 * `T x = {0}` / `T a[N] = {0}`: the `0` is a zero-init filler, not a
		 * value of type T. The object was already cleared by emit_zero_mem.
		 */
		if (in->expr->type == NULL ||
		    (!is_aggr(in->expr->type) && !is_array(in->expr->type)))
			return;
		r = emitexpr(c, in->expr);
		emitblit(dest, r, storewidth(c, t));
		return;
	}
	r = emitexpr(c, in->expr);
	cls = qbe_class(t);
	if (r.cls != cls)
		r = coerce(r, cls, t);
	fprintf(outf, "\t%s %s, %s\n", storeop(c, t), r.text, dest.text);
}

// Emit one statement; returns 1 if control cannot fall through (return/break/goto).
static int
emitstmt_ret(Compiler* c, Node* n) {
	int t, t2, t3, t4, i, def, narm, fallen, defer_base;
	Val v;
	Casearm arms[MaxCase];

	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NdSkip:
		return 0;
	case NdDecl:
		if (n->init && n->symbol && n->symbol->storage == StLocal) {
			Val addr;

			snprintf(addr.text, sizeof(addr.text), "%%%s.addr", slot_basename(n->symbol));
			addr.cls = 'l';
			addr.type = n->type;
			if (n->init->expr) {
				Val r;

				if (is_array(n->type) && n->init->expr->kind == NdStr &&
				    n->type->base && type_size(c, n->type->base) == 1)
					emit_local_string_init(c, addr, n->type, n->init->expr);
				else {
					r = emitexpr(c, n->init->expr);
					if (is_aggr(n->type) || is_array(n->type))
						emitblit(addr, r, storewidth(c, n->type));
					else {
						char cls;

						cls = qbe_class(n->type);
						if (r.cls != cls)
							r = coerce(r, cls, n->type);
						fprintf(outf, "\t%s %s, %s\n", storeop(c, n->type), r.text, addr.text);
					}
				}
			} else if (n->init->is_list || n->init->items_len > 0) {
				/* C `{0}` / partial lists: zero the object, then store members. */
				emit_zero_mem(addr, storewidth(c, n->type));
				emit_local_init(c, addr, n->type, n->init, 0);
			}
		}
		return 0;
	case NdBlock:
		defer_base = defers_len;
		push_defer_frame(n);
		fallen = 0;
		for (i = 0; i < n->children_len; i++) {
			if (n->children[i]->kind == NdDefer)
				add_defer(c, n->children[i]->a);
			else
				fallen = emitstmt_ret(c, n->children[i]) || fallen;
		}
		if (!fallen)
			pop_defer_frame(c);
		else
			defers_len = defer_base;
		return fallen;
	case NdIf:
		t = newlbl();
		t2 = newlbl();
		t3 = n->c ? newlbl() : t2;
		emitbooljmp(c, n->a, t, t3);
		emitlbl(t);
		if (!emitstmt_ret(c, n->b))
			emitjmp(t2);
		if (n->c) {
			emitlbl(t3);
			if (!emitstmt_ret(c, n->c))
				emitjmp(t2);
		}
		emitlbl(t2);
		return 0;
	case NdWhile:
		t = newlbl();
		t2 = newlbl();
		t3 = newlbl();
		emitlbl(t);
		emitbooljmp(c, n->a, t2, t3);
		emitlbl(t2);
		pushloop(t3, t);
		if (!emitstmt_ret(c, n->b))
			emitjmp(t);
		poploop();
		emitlbl(t3);
		return 0;
	case NdDo:
		t = newlbl();
		t2 = newlbl();
		t3 = newlbl();
		emitlbl(t);
		pushloop(t3, t2);
		emitstmt_ret(c, n->a);
		poploop();
		emitlbl(t2);
		emitbooljmp(c, n->b, t, t3);
		emitlbl(t3);
		return 0;
	case NdFor:
		if (n->a) {
			if (n->a->kind == NdDecl)
				emitstmt_ret(c, n->a);
			else
				emitexpr(c, n->a);
		}
		t = newlbl();
		t2 = newlbl();
		t3 = newlbl();
		t4 = newlbl();
		emitlbl(t);
		if (n->b)
			emitbooljmp(c, n->b, t2, t3);
		else
			emitjmp(t2);
		emitlbl(t2);
		pushloop(t3, t4);
		if (n->children_len > 0)
			emitstmt_ret(c, n->children[0]);
		emitlbl(t4);
		if (n->c)
			emitexpr(c, n->c);
		emitjmp(t);
		poploop();
		emitlbl(t3);
		return 0;
	case NdBreak:
		if (loops_len > 0)
			emit_defers_until(c, loop_defer[loops_len - 1]);
		if (loops_len > 0)
			emitjmp(loopbrk[loops_len - 1]);
		return 1;
	case NdContinue:
		if (loops_len > 0)
			emit_defers_until(c, loop_defer[loops_len - 1]);
		if (loops_len > 0)
			emitjmp(loopcont[loops_len - 1]);
		return 1;
	case NdDefer:
		add_defer(c, n->a);
		return 0;
	case NdReturn:
		/*
		 * Evaluate the return value before running defers so
		 * `defer free(p)` cannot free something still needed for ret.
		 */
		if (inl.active) {
			if (n->a && inl.site && inl.site->has_ret) {
				Val addr;
				Type* rt;

				rt = inl.site->callee && inl.site->callee->type
					 ? inl.site->callee->type->base
					 : n->a->type;
				v = emitexpr(c, n->a);
				snprintf(addr.text, sizeof(addr.text), "%%%s.addr", inl.site->retname);
				addr.cls = 'l';
				if (is_aggr(rt))
					emitblit(addr, v, storewidth(c, rt));
				else {
					char cls;

					cls = qbe_class(rt);
					if (v.cls != cls)
						v = coerce(v, cls, rt);
					fprintf(outf, "\t%s %s, %s\n",
						storeop(c, rt), v.text, addr.text);
				}
			} else if (n->a)
				emitexpr(c, n->a);
			emitjmp(inl.endlbl);
			return 1;
		}
		if (n->a) {
			Type* rt;

			rt = emit_curfn && emit_curfn->type ? emit_curfn->type->base : NULL;
			v = emitexpr(c, n->a);
			if (rt && !is_aggr(rt)) {
				char cls;

				cls = qbe_class(rt);
				if (v.cls != cls)
					v = coerce(v, cls, rt);
			}
			emit_all_defers(c);
			fprintf(outf, "\tret %s\n", v.text);
		} else {
			emit_all_defers(c);
			fprintf(outf, "\tret\n");
		}
		return 1;
	case NdGoto:
		if (n->symbol)
			emit_defers_for_goto(c, n->symbol->label_scope);
		if (n->symbol) {
			if (n->symbol->offset == 0)
				n->symbol->offset = newlbl();
			emitjmp(n->symbol->offset);
		}
		return 1;
	case NdLabel:
		if (n->symbol) {
			if (n->symbol->offset == 0)
				n->symbol->offset = newlbl();
			emitlbl(n->symbol->offset);
		}
		return emitstmt_ret(c, n->a);
	case NdCase:
		emitlbl(caselbl(n));
		return 0;
	case NdDefault:
		emitlbl(caselbl(n));
		return 0;
	case NdFallthrough:
		return 0;
	case NdSwitch: {
		narm = 0;
		def = 0;
		collectcases(n->b, arms, &narm, &def);
		t3 = newlbl();
		if (def == 0)
			def = t3;
		v = emitexpr(c, n->a);
		for (i = 0; i < narm; i++) {
			Val cmp, tlo, thi;
			char cls = v.cls;

			t = newlbl();
			if (arms[i].lo == arms[i].hi) {
				cmp = vtmp('w', c->type_int);
				if (cls == 'l')
					fprintf(outf, "\t%s =w ceql %s, %" PRId64 "\n",
						cmp.text, v.text, (int64_t)arms[i].lo);
				else
					fprintf(outf, "\t%s =w ceqw %s, %" PRId64 "\n",
						cmp.text, v.text, (int64_t)arms[i].lo);
				emitjnz(cmp.text, arms[i].lbl, t);
			} else {
				/* x >= lo && x <= hi (signed); value first, imm second */
				tlo = vtmp('w', c->type_int);
				thi = vtmp('w', c->type_int);
				cmp = vtmp('w', c->type_int);
				if (cls == 'l') {
					fprintf(outf, "\t%s =w csgel %s, %" PRId64 "\n",
						tlo.text, v.text, (int64_t)arms[i].lo);
					fprintf(outf, "\t%s =w cslel %s, %" PRId64 "\n",
						thi.text, v.text, (int64_t)arms[i].hi);
				} else {
					fprintf(outf, "\t%s =w csgew %s, %" PRId64 "\n",
						tlo.text, v.text, (int64_t)arms[i].lo);
					fprintf(outf, "\t%s =w cslew %s, %" PRId64 "\n",
						thi.text, v.text, (int64_t)arms[i].hi);
				}
				fprintf(outf, "\t%s =w and %s, %s\n",
					cmp.text, tlo.text, thi.text);
				emitjnz(cmp.text, arms[i].lbl, t);
			}
			emitlbl(t);
		}
		emitjmp(def);
		pushloop(t3, loops_len > 0 ? loopcont[loops_len - 1] : 0);
		fallen = emitstmt_ret(c, n->b);
		if (!fallen)
			emitjmp(t3);
		poploop();
		emitlbl(t3);
		return 0;
	}
	default:
		emitexpr(c, n);
		return 0;
	}
}

// Emit @start stack slots for locals and spill register params into them.
static void
emitallocs(Compiler* c) {
	int i, w, align;
	Symbol* s;
	Type* t;
	const char* sn;

	for (i = 0; i < locals_len; i++) {
		s = locals[i];
		t = localty[i];
		sn = localslot[i] ? localslot[i] : (s->name ? s->name : "g");
		if (localparam[i] && is_aggr(t)) {
			/* Aggregate param arrives as a pointer; slot may be renamed. */
			fprintf(outf, "\t%%%s.addr =l copy %%%s\n", sn, s->name);
			continue;
		}
		w = storewidth(c, t);
		align = type_align(c, t);
		if (align < 4)
			align = 4;
		if (align >= 8)
			fprintf(outf, "\t%%%s.addr =l alloc8 %d\n", sn, w);
		else
			fprintf(outf, "\t%%%s.addr =l alloc4 %d\n", sn, w);
	}
	for (i = 0; i < locals_len; i++) {
		if (!localparam[i] || is_aggr(localty[i]))
			continue;
		s = locals[i];
		sn = localslot[i] ? localslot[i] : (s->name ? s->name : "g");
		/* SSA param name stays as declared; .addr uses uniquified slot. */
		fprintf(outf, "\t%s %%%s, %%%s.addr\n",
			storeop(c, localty[i]), s->name, sn);
	}
}

// Emit one function: signature, @start allocs, body, and implicit ret if needed.
static void
emitfunc(Compiler* c, Node* fn) {
	Type *ty, *ret;
	int i, first, returned;
	Symbol* s;

	s = fn->symbol;
	ty = fn->type;
	ret = ty ? ty->base : c->type_int;
	tempno = 0;
	lblno = 0;
	locals_len = 0;
	loops_len = 0;
	defers_len = 0;
	isites_len = 0;
	inl.active = 0;
	inl.site = NULL;
	inl.stack_len = 0;
	collect(c, fn->a);
	register_inline_sites(c, fn->a);
	for (i = 0; i < (ty ? ty->params_len : 0); i++) {
		if (ty->param_names && ty->param_names[i]) {
			Symbol* p = NULL;
			int j;
			/* find from locals already collected; if missing, dummy */
			for (j = 0; j < locals_len; j++)
				if (strcmp(locals[j]->name, ty->param_names[i]) == 0)
					p = locals[j];
			(void)p;
		}
	}
	if (is_aggr(ret))
		ensure_aggregate(ret);
	fputs("export function ", outf);
	if (ret && ret->kind == TyVoid)
		fprintf(outf, "$%s(", symbol_link_name(s));
	else {
		print_abi_type(ret);
		fprintf(outf, " $%s(", symbol_link_name(s));
	}
	first = 1;
	if (ty) {
		for (i = 0; i < ty->params_len; i++) {
			if (!ty->param_names || !ty->param_names[i])
				continue;
			if (!first)
				fputs(", ", outf);
			print_abi_type(ty->params[i]);
			fprintf(outf, " %%%s", ty->param_names[i]);
			first = 0;
			{
				/* ensure local slot */
				int j, found = 0;
				for (j = 0; j < locals_len; j++)
					if (strcmp(locals[j]->name, ty->param_names[i]) == 0)
						found = 1;
				if (!found) {
					Symbol* ps = xmalloc(sizeof(*ps));
					ps->name = ty->param_names[i];
					ps->kind = SkVar;
					ps->storage = StParam;
					ps->type = ty->params[i];
					addlocal(ps, ty->params[i], 1);
				}
			}
		}
		if (ty->is_varargs) {
			if (!first)
				fputs(", ", outf);
			fputs("...", outf);
		}
	}
	fputs(") {\n@start\n", outf);
	emit_curfn = fn;
	emitallocs(c);
	returned = emitstmt_ret(c, fn->a);
	emit_curfn = NULL;
	if (!returned) {
		if (ret && ret->kind == TyVoid)
			fputs("\tret\n", outf);
		else
			fputs("\tret 0\n", outf);
	}
	fputs("}\n\n", outf);
}

typedef struct GInit GInit;
struct GInit {
	int off, w, kind;
	int64_t val;
	int stroff;
	Symbol* symbol;
};

static GInit* ginits;
static int ginits_len, ginits_cap;

// Sort global init fragments by byte offset for sequential data emission.
static int
ginit_cmp(const void* a, const void* b) {
	const GInit *ga, *gb;

	ga = a;
	gb = b;
	return ga->off - gb->off;
}

// Record one byte/word of a compile-time global initializer.
static void
addgi(int off, int w, int kind, int64_t val, Symbol* sym, int stroff) {
	if (ginits_len >= ginits_cap) {
		ginits_cap = ginits_cap ? ginits_cap * 2 : 256;
		ginits = xrealloc(ginits, (size_t)ginits_cap * sizeof(*ginits));
	}
	ginits[ginits_len].off = off;
	ginits[ginits_len].w = w;
	ginits[ginits_len].kind = kind;
	ginits[ginits_len].val = val;
	ginits[ginits_len].symbol = sym;
	ginits[ginits_len].stroff = stroff;
	ginits_len++;
}

static int global_reloc(Compiler* c, Node* n, Symbol** sym, int64_t* addend);

// Resolve the address of a global lvalue into a linker symbol and byte addend.
static int
global_lvalue_reloc(Compiler* c, Node* n, Symbol** sym, int64_t* addend) {
	int64_t index;

	if (n == NULL)
		return 0;
	if (n->kind == NdName && n->symbol &&
	    (is_global_symbol(n->symbol) || is_static_local(n->symbol) ||
	     n->symbol->kind == SkFunc)) {
		*sym = n->symbol;
		*addend = 0;
		return 1;
	}
	if (n->kind == NdDeref)
		return global_reloc(c, n->a, sym, addend);
	if (n->kind == NdIndex && global_reloc(c, n->a, sym, addend) &&
	    eval_const(c, n->b, &index)) {
		*addend += index * type_size(c, n->type);
		return 1;
	}
	if (n->kind == NdDot && global_lvalue_reloc(c, n->a, sym, addend)) {
		*addend += n->int_val;
		return 1;
	}
	if (n->kind == NdArrow && global_reloc(c, n->a, sym, addend)) {
		*addend += n->int_val;
		return 1;
	}
	return 0;
}

// Recognize static pointer constants accepted in global initializers.
static int
global_reloc(Compiler* c, Node* n, Symbol** sym, int64_t* addend) {
	int64_t delta;
	Type* t;

	if (n == NULL)
		return 0;
	if (n->kind == NdCast)
		return global_reloc(c, n->a, sym, addend);
	if (n->kind == NdAddr)
		return global_lvalue_reloc(c, n->a, sym, addend);
	if (n->kind == NdName && n->symbol &&
	    (n->symbol->kind == SkFunc || is_array(n->type)))
		return global_lvalue_reloc(c, n, sym, addend);
	if (n->kind != NdBin || (n->op != PnPlus && n->op != PnMinus))
		return 0;
	if (global_reloc(c, n->a, sym, addend) && eval_const(c, n->b, &delta)) {
		t = n->a ? n->a->type : NULL;
		if (t && (is_ptr(t) || is_array(t)))
			delta *= type_size(c, t->base);
		*addend += n->op == PnMinus ? -delta : delta;
		return 1;
	}
	if (n->op == PnPlus && global_reloc(c, n->b, sym, addend) &&
	    eval_const(c, n->a, &delta)) {
		t = n->b ? n->b->type : NULL;
		if (t && (is_ptr(t) || is_array(t)))
			delta *= type_size(c, t->base);
		*addend += delta;
		return 1;
	}
	return 0;
}

// Resolve a designated-initializer field path to its type and total byte offset.
static Type*
emit_field_path(Compiler* c, Type* t, Initializer* it, int* totoff) {
	int off, inner, i;
	Field* f;

	off = 0;
	for (i = 0; i < it->fields_len; i++) {
		f = find_field(t, it->fields[i], &inner);
		if (f == NULL) {
			error_at(c, (Span){0}, "no field named %s in initializer", it->fields[i]);
			return NULL;
		}
		off += inner;
		t = f->type;
	}
	*totoff = off;
	return t;
}

static void
flatten_init(Compiler* c, Type* t, Initializer* in, int off);

// Expand a braced initializer list into per-offset global init records.
static void
flatten_init_list(Compiler* c, Type* t, Initializer* in, int off) {
	int i, nextpos, inner, w, j;
	Type* ft;
	Field* f;

	nextpos = 0;
	for (i = 0; i < in->items_len; i++) {
		Initializer* it = &in->items[i];

		if (it->designator == IdIndexEq) {
			if (t->kind != TyArray) {
				error_at(c, (Span){0}, "array designator for non-array type");
				continue;
			}
			w = type_size(c, t->base);
			flatten_init(c, t->base, it, off + (int)(it->index * w));
			nextpos = (int)it->index + 1;
		} else if (it->designator == IdFieldDot) {
			ft = emit_field_path(c, t, it, &inner);
			if (ft)
				flatten_init(c, ft, it, off + inner);
			nextpos = field_designator_cursor(t, it);
		} else if (t->kind == TyArray) {
			w = type_size(c, t->base);
			flatten_init(c, t->base, it, off + nextpos * w);
			nextpos++;
		} else if (is_aggr(t)) {
			for (f = t->fields, j = 0; f && j < nextpos; j++)
				f = f->next;
			if (f)
				flatten_init(c, f->type, it, off + f->offset);
			nextpos++;
		} else
			flatten_init(c, t, it, off);
	}
}

// Flatten one initializer subtree (scalar, array, or nested aggregate).
static void
flatten_init(Compiler* c, Type* t, Initializer* in, int off) {
	int i, w;
	int64_t v, addend;
	double dv;
	float sv;
	uint32_t sbits;
	uint64_t dbits;
	Field* f;
	Symbol* sym;

	if (t == NULL)
		return;
	if (in && in->is_list) {
		flatten_init_list(c, t, in, off);
		return;
	}
	if (t->kind == TyArray) {
		if (in && in->expr && in->expr->kind == NdStr && t->base && t->base->size == 1) {
			int k, n;

			n = in->expr->type ? type_size(c, in->expr->type) : 0;
			w = type_size(c, t);
			for (k = 0; k < w && k < n; k++)
				addgi(off + k, 1, 1,
				      c->strpool[(int)in->expr->int_val + k], NULL, 0);
			return;
		}
		w = t->base ? type_size(c, t->base) : 4;
		if (in == NULL) {
			for (i = 0; i < t->len; i++)
				flatten_init(c, t->base, NULL, off + i * w);
		} else {
			flatten_init(c, t->base, in, off);
			for (i = 1; i < t->len; i++)
				flatten_init(c, t->base, NULL, off + i * w);
		}
		return;
	}
	if (is_aggr(t)) {
		if (in == NULL) {
			for (f = t->fields; f; f = f->next)
				flatten_init(c, f->type, NULL, off + f->offset);
		}
		return;
	}
	w = type_size(c, t);
	if (in && in->expr && (t->kind == TyFloat || t->kind == TyDouble)) {
		if (!eval_float_const(c, in->expr, &dv)) {
			error_at(c, in->expr->span,
				 "global initializer is not a constant expression");
			return;
		}
		if (t->kind == TyFloat) {
			sv = (float)dv;
			memcpy(&sbits, &sv, sizeof(sbits));
			addgi(off, 4, 1, (int64_t)sbits, NULL, 0);
		} else {
			memcpy(&dbits, &dv, sizeof(dbits));
			addgi(off, 8, 1, (int64_t)dbits, NULL, 0);
		}
		return;
	}
	if (in && in->expr && in->expr->kind == NdStr) {
		addgi(off, 8, 2, in->expr->int_val, NULL, (int)in->expr->int_val);
		return;
	}
	if (in && in->expr && w == 8 &&
	    global_reloc(c, in->expr, &sym, &addend)) {
		addgi(off, w, 3, addend, sym, 0);
		return;
	}
	if (in && in->expr && eval_const(c, in->expr, &v))
		addgi(off, w, 1, v, NULL, 0);
	else if (in == NULL)
		;
	else if (in->expr)
		error_at(c, in->expr->span,
			 "global initializer is not a constant expression");
}

// Emit QBE data for one global variable from flattened init fragments.
static void
emitgsym(Compiler* c, Node* d) {
	Symbol* s;
	Type* t;
	int size, pos, i, first, gap, export;
	GInit* gi;

	s = d->symbol;
	if (s == NULL || s->kind == SkFunc)
		return;
	t = s->type ? s->type : d->type;
	if (t == NULL)
		return;
	if (s->storage == StExtern && !s->defined && d->init == NULL)
		return;
	ginits_len = 0;
	if (d->init)
		flatten_init(c, t, d->init, 0);
	size = type_size(c, t);
	export = s->storage != StStatic && s->storage != StLocal;
	if (export)
		fputs("export ", outf);
	fprintf(outf, "data $%s = {", symbol_link_name(s));
	if (ginits_len == 0)
		fprintf(outf, " z %d", size > 0 ? size : 4);
	else {
		if (ginits_len > 1)
			qsort(ginits, ginits_len, sizeof(ginits[0]), ginit_cmp);
		pos = 0;
		first = 1;
		for (i = 0; i < ginits_len; i++) {
			gi = &ginits[i];
			if (i > 0 && i % 64 == 0)
				fputs("\n\t", outf);
			gap = gi->off - pos;
			if (gap > 0) {
				if (!first)
					fputc(',', outf);
				fprintf(outf, " z %d", gap);
				first = 0;
			}
			if (!first)
				fputc(',', outf);
			if (gi->kind == 2)
				fprintf(outf, " l $%s + %d", emit_str_symbol, gi->stroff);
			else if (gi->kind == 3) {
				fprintf(outf, " l $%s", symbol_link_name(gi->symbol));
				if (gi->val > 0)
					fprintf(outf, " + %" PRId64, gi->val);
				else if (gi->val < 0)
					fprintf(outf, " - %" PRId64, -gi->val);
			}
			else {
				switch (gi->w) {
				case 1:
					fprintf(outf, " b %" PRId64, (int64_t)(gi->val & 0xff));
					break;
				case 2:
					fprintf(outf, " h %" PRId64, (int64_t)(gi->val & 0xffff));
					break;
				case 8:
					fprintf(outf, " l %" PRId64, (int64_t)gi->val);
					break;
				default:
					fprintf(outf, " w %" PRId64, (int64_t)(gi->val & 0xffffffffu));
					break;
				}
			}
			first = 0;
			pos = gi->off + gi->w;
		}
		gap = size - pos;
		if (gap > 0) {
			if (!first)
				fputc(',', outf);
			fprintf(outf, " z %d", gap);
		}
	}
	fputs(" }\n\n", outf);
}

// Emit the pooled string literal blob referenced as $emit_str_symbol.
static void
emitstrdata(Compiler* c) {
	int i;

	if (c->strpool_len <= 0)
		return;
	fprintf(outf, "data $%s = { b", emit_str_symbol);
	for (i = 0; i < c->strpool_len; i++) {
		if (i > 0 && i % 64 == 0)
			fputs(",\n\tb", outf);
		fprintf(outf, " %u", (unsigned)c->strpool[i]);
	}
	fputs(" }\n\n", outf);
}

// True when nested aggregates are emitted so this struct/union can be defined.
static int
su_ready(Type* t) {
	Field* f;
	Type* e;

	if (t == NULL || !is_aggr(t) || t->emit_id <= 0)
		return 0;
	for (f = t->fields; f; f = f->next) {
		if (is_aggr(f->type) && f->type->emit_id > 0)
			return 0;
		e = array_elem(f->type);
		if (e != f->type && is_aggr(e) && e->emit_id > 0)
			return 0;
	}
	return 1;
}

// Emit struct/union types in dependency order (inner aggregates first).
static void
emitsuall(Compiler* c) {
	Type* t;
	int progress;

	do {
		progress = 0;
		for (t = c->type_list; t; t = t->next) {
			if (su_ready(t)) {
				emitsutype(c, t);
				t->emit_id = -t->emit_id;
				progress = 1;
			}
		}
	} while (progress);
}

// Restore emit_id after a package/TU emit pass (negated while types were printed).
static void
reset_su_ids(Compiler* c) {
	Type* t;

	for (t = c->type_list; t; t = t->next)
		if (t->emit_id < 0)
			t->emit_id = -t->emit_id;
}

// True if n's defining file belongs to package directory pkg_dir.
static int
node_in_pkg(Node* n, const char* pkg_dir) {
	char root[HOST_PATH_MAX], abs[HOST_PATH_MAX], want[HOST_PATH_MAX];

	if (n == NULL || pkg_dir == NULL || n->span.file == NULL)
		return 0;
	pkg_file_root(n->span.file, root, sizeof(root));
	if (host_abspath(root, abs, sizeof(abs)) == 0)
		snprintf(root, sizeof(root), "%s", abs);
	snprintf(want, sizeof(want), "%s", pkg_dir);
	if (host_abspath(want, abs, sizeof(abs)) == 0)
		snprintf(want, sizeof(want), "%s", abs);
	return strcmp(root, want) == 0;
}

/* ---- entry ---- */

// Full IL for one Compiler: types, string pool, data, then functions.
int emit_qbe(Compiler* c, FILE* out) {
	int i;

	outf = out;
	emit_str_symbol = "__string";
	emit_pkg_filter = NULL;
	for (i = 0; i < c->funcs_len; i++)
		collect(c, c->funcs[i]);
	emitsuall(c);
	emitstrdata(c);
	for (i = 0; i < c->globals_len; i++)
		emitgsym(c, c->globals[i]);
	for (i = 0; i < c->funcs_len; i++)
		emitfunc(c, c->funcs[i]);
	reset_su_ids(c);
	return 0;
}

// Emit one package's funcs/globals; string pool uses str_symbol (unique per .o).
int emit_qbe_pkg(Compiler* c, FILE* out, const char* pkg_dir, const char* str_symbol) {
	int i;

	outf = out;
	emit_str_symbol = (str_symbol && str_symbol[0]) ? str_symbol : "__string";
	emit_pkg_filter = pkg_dir;
	tempno = 0;
	lblno = 0;
	isites_len = 0;
	for (i = 0; i < c->funcs_len; i++)
		if (node_in_pkg(c->funcs[i], pkg_dir))
			collect(c, c->funcs[i]);
	for (i = 0; i < c->globals_len; i++)
		if (node_in_pkg(c->globals[i], pkg_dir))
			collect(c, c->globals[i]);
	emitsuall(c);
	emitstrdata(c);
	for (i = 0; i < c->globals_len; i++)
		if (node_in_pkg(c->globals[i], pkg_dir))
			emitgsym(c, c->globals[i]);
	for (i = 0; i < c->funcs_len; i++)
		if (node_in_pkg(c->funcs[i], pkg_dir))
			emitfunc(c, c->funcs[i]);
	reset_su_ids(c);
	emit_str_symbol = "__string";
	emit_pkg_filter = NULL;
	return 0;
}
