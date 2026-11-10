/*
 * Post-parse checks: uninit, fall-off, exhaust, unused, …
 *
 * Compilation pipeline: lex → pp → parse → [type check] → emit → QBE
 * Entry: type_check_unit. Runs after parse+type_expr on a finished AST.
 */
#include "ast.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- dead initializer (da_) ---- */
/*
 * Prefix da_ — dead/unused initializer on scalar and aggregate locals.
 * Required init at decl already rules out uninit reads in .mc; this pass
 * flags initializers that are always overwritten before any read
 * (placeholder init). Aggregates are whole-object only: `p = q` kills,
 * `p.x = …` / `p[i] = …` do not. Joins union "init still reaching"; loops
 * keep pre-loop reach (body may not run). Skipped for headers and for
 * functions that contain goto. Growable — no silent cap drop.
 */
typedef struct DAState DAState;
struct DAState {
	Symbol** symbols;
	unsigned char* init_reach; /* decl init may still reach here */
	unsigned char* init_used;  /* init value observed on some path (sticky OR) */
	int n;
	int cap;
};

static void
da_release(DAState* st) {
	if (st == NULL)
		return;
	free(st->symbols);
	free(st->init_reach);
	free(st->init_used);
	st->symbols = NULL;
	st->init_reach = NULL;
	st->init_used = NULL;
	st->n = 0;
	st->cap = 0;
}

static void
da_grow(DAState* st, int need) {
	int ncap, i;

	if (need <= st->cap)
		return;
	ncap = st->cap ? st->cap * 2 : 32;
	while (ncap < need)
		ncap *= 2;
	st->symbols = xrealloc(st->symbols, (size_t)ncap * sizeof(Symbol*));
	st->init_reach = xrealloc(st->init_reach, (size_t)ncap);
	st->init_used = xrealloc(st->init_used, (size_t)ncap);
	for (i = st->cap; i < ncap; i++) {
		st->init_reach[i] = 0;
		st->init_used[i] = 0;
	}
	st->cap = ncap;
}

static int
da_has_goto(Node* n) {
	int i;

	if (n == NULL)
		return 0;
	if (n->kind == NdGoto)
		return 1;
	if (da_has_goto(n->a) || da_has_goto(n->b) || da_has_goto(n->c))
		return 1;
	for (i = 0; i < n->children_len; i++) {
		if (da_has_goto(n->children[i]))
			return 1;
	}
	return 0;
}

static int
da_trackable(Symbol* s) {
	if (s == NULL || s->kind != SkVar || s->storage != StLocal)
		return 0;
	if (s->maybe_unused)
		return 0;
	/* Scalars + assignable aggregates (struct/union, including T[..] / tuples).
	 * Fixed arrays are not assignable as wholes, so skip them. */
	return is_scalar(s->type) || is_aggr(s->type);
}

static int
da_find(DAState* st, Symbol* s) {
	int i;

	for (i = 0; i < st->n; i++) {
		if (st->symbols[i] == s)
			return i;
	}
	return -1;
}

/* Begin tracking a scalar local whose decl initializer may reach uses. */
static void
da_track(DAState* st, Symbol* s) {
	int i;

	if (!da_trackable(s))
		return;
	i = da_find(st, s);
	if (i >= 0) {
		st->init_reach[i] = 1;
		return;
	}
	da_grow(st, st->n + 1);
	i = st->n++;
	st->symbols[i] = s;
	st->init_reach[i] = 1;
	st->init_used[i] = 0;
}

static void
da_kill(DAState* st, Symbol* s) {
	int i;

	i = da_find(st, s);
	if (i >= 0)
		st->init_reach[i] = 0;
}

static void
da_copy(DAState* dst, DAState* src) {
	int i;

	da_grow(dst, src->n);
	if (src->n > 0) {
		memcpy(dst->symbols, src->symbols, (size_t)src->n * sizeof(Symbol*));
		memcpy(dst->init_reach, src->init_reach, (size_t)src->n);
	}
	dst->n = src->n;
	/* Branch-local used bits start clear; OR into parent at join. */
	for (i = 0; i < dst->n; i++)
		dst->init_used[i] = 0;
}

/* Absorb a symbol from a branch into dst (inner decl). */
static void
da_absorb(DAState* dst, DAState* src, int si) {
	int i;
	Symbol* s;

	s = src->symbols[si];
	i = da_find(dst, s);
	if (i < 0) {
		da_grow(dst, dst->n + 1);
		i = dst->n++;
		dst->symbols[i] = s;
		dst->init_reach[i] = src->init_reach[si];
		dst->init_used[i] = src->init_used[si];
		return;
	}
	dst->init_reach[i] =
		(unsigned char)(dst->init_reach[i] | src->init_reach[si]);
	dst->init_used[i] =
		(unsigned char)(dst->init_used[i] | src->init_used[si]);
}

/*
 * After a branch whose arms were da_copy'd from dst: union reach; OR uses.
 * Inner decls tracked only on an arm are absorbed into dst.
 */
static void
da_join(DAState* dst, DAState* a, DAState* b) {
	int i, j;

	for (i = 0; i < dst->n; i++) {
		int ja = da_find(a, dst->symbols[i]);
		int jb = da_find(b, dst->symbols[i]);

		dst->init_reach[i] = (unsigned char)((ja >= 0 && a->init_reach[ja]) ||
						     (jb >= 0 && b->init_reach[jb]));
		dst->init_used[i] = (unsigned char)(
			dst->init_used[i] | (ja >= 0 ? a->init_used[ja] : 0) |
			(jb >= 0 ? b->init_used[jb] : 0));
	}
	for (i = 0; i < a->n; i++) {
		if (da_find(dst, a->symbols[i]) < 0)
			da_absorb(dst, a, i);
	}
	for (i = 0; i < b->n; i++) {
		if (da_find(dst, b->symbols[i]) < 0)
			da_absorb(dst, b, i);
	}
}

static void
da_or_used(DAState* dst, DAState* src) {
	int i, j;

	for (i = 0; i < src->n; i++) {
		j = da_find(dst, src->symbols[i]);
		if (j >= 0)
			dst->init_used[j] = (unsigned char)(dst->init_used[j] | src->init_used[i]);
	}
}

static void da_expr(Compiler* c, Node* n, DAState* st, int as_lval);
static void da_stmt(Compiler* c, Node* n, DAState* st);
static void da_init(Compiler* c, Initializer* in, DAState* st);

/* Read of x: if decl init still reaches, it was observed. */
static void
da_use(DAState* st, Node* n) {
	int i;

	if (n == NULL || n->kind != NdName || n->symbol == NULL)
		return;
	i = da_find(st, n->symbol);
	if (i >= 0 && st->init_reach[i])
		st->init_used[i] = 1;
}

static void
da_mark_lhs(DAState* st, Node* n) {
	if (n == NULL)
		return;
	if (n->kind == NdName) {
		da_kill(st, n->symbol);
		return;
	}
	if (n->kind == NdComma)
		da_mark_lhs(st, n->b);
}

static void
da_init(Compiler* c, Initializer* in, DAState* st) {
	int i;

	if (in == NULL)
		return;
	if (in->expr)
		da_expr(c, in->expr, st, 0);
	for (i = 0; i < in->items_len; i++)
		da_init(c, &in->items[i], st);
}

static void
da_expr(Compiler* c, Node* n, DAState* st, int as_lval) {
	DAState a = { 0 }, b = { 0 };
	int i;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NdLit:
	case NdStr:
	case NdSizeofT:
	case NdTypeArg:
	case NdSkip:
		return;
	case NdSizeof:
		return;
	case NdName:
		if (!as_lval)
			da_use(st, n);
		return;
	case NdAddr:
		/* &x / &p.x / &a[i]: conservatively observe the current value. */
		da_expr(c, n->a, st, 0);
		return;
	case NdAssign:
		da_expr(c, n->b, st, 0);
		if (n->op != PnEq)
			da_expr(c, n->a, st, 0);
		else
			da_expr(c, n->a, st, 1);
		da_mark_lhs(st, n->a);
		return;
	case NdUn:
		if (n->op == PnPlusPlus || n->op == PnMinusMinus) {
			da_expr(c, n->a, st, 0);
			da_mark_lhs(st, n->a);
			return;
		}
		da_expr(c, n->a, st, 0);
		return;
	case NdPost:
		da_expr(c, n->a, st, 0);
		da_mark_lhs(st, n->a);
		return;
	case NdBin:
		if (n->op == PnAmpAmp || n->op == PnPipePipe) {
			da_expr(c, n->a, st, 0);
			da_copy(&a, st);
			da_expr(c, n->b, &a, 0);
			da_or_used(st, &a);
			/* RHS may not run — keep pre-op reach */
			da_release(&a);
			return;
		}
		da_expr(c, n->a, st, 0);
		da_expr(c, n->b, st, 0);
		return;
	case NdCond:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_copy(&b, st);
		da_expr(c, n->b, &a, 0);
		da_expr(c, n->c, &b, 0);
		da_join(st, &a, &b);
		da_release(&a);
		da_release(&b);
		return;
	case NdComma:
		da_expr(c, n->a, st, 0);
		da_expr(c, n->b, st, as_lval);
		return;
	case NdCall:
		for (i = 0; i < n->children_len; i++)
			da_expr(c, n->children[i], st, 0);
		da_expr(c, n->a, st, 0);
		return;
	case NdCast:
		/* (void)x observes x (same as a read for dead-init). */
		da_expr(c, n->a, st, 0);
		return;
	case NdDeref:
		/* *p / *p = … reads the pointer value even as a store address. */
		da_expr(c, n->a, st, 0);
		return;
	case NdArrow:
		da_expr(c, n->a, st, 0);
		return;
	case NdIndex:
		da_expr(c, n->a, st,
			as_lval && is_array(n->a ? n->a->type : NULL));
		da_expr(c, n->b, st, 0);
		return;
	case NdSubrange:
	case NdDot:
	case NdTupleLit:
		da_expr(c, n->a, st,
			as_lval && (n->kind == NdDot || n->kind == NdIndex));
		da_expr(c, n->b, st, 0);
		da_expr(c, n->c, st, 0);
		for (i = 0; i < n->children_len; i++)
			da_expr(c, n->children[i], st, 0);
		return;
	default:
		da_expr(c, n->a, st, 0);
		da_expr(c, n->b, st, 0);
		da_expr(c, n->c, st, 0);
		for (i = 0; i < n->children_len; i++)
			da_expr(c, n->children[i], st, 0);
		return;
	}
}

static void
for_init_da(Compiler* c, Node* init, DAState* st) {
	if (init && init->kind == NdDecl)
		da_stmt(c, init, st);
	else
		da_expr(c, init, st, 0);
}

static void
da_stmt(Compiler* c, Node* n, DAState* st) {
	DAState a = { 0 }, b = { 0 };
	int i, has_init;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NdDecl:
		da_init(c, n->init, st);
		has_init = n->init != NULL &&
			   (n->init->expr != NULL || n->init->items_len > 0 || n->init->is_list);
		if (n->symbol && has_init)
			da_track(st, n->symbol);
		return;
	case NdBlock:
		for (i = 0; i < n->children_len; i++)
			da_stmt(c, n->children[i], st);
		return;
	case NdIf:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_copy(&b, st);
		da_stmt(c, n->b, &a);
		if (n->c)
			da_stmt(c, n->c, &b);
		da_join(st, &a, &b);
		da_release(&a);
		da_release(&b);
		return;
	case NdWhile:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_stmt(c, n->b, &a);
		da_or_used(st, &a);
		/* body may not run — keep pre-loop reach */
		da_release(&a);
		return;
	case NdDo:
		da_stmt(c, n->a, st);
		da_expr(c, n->b, st, 0);
		return;
	case NdFor:
		for_init_da(c, n->a, st);
		da_expr(c, n->b, st, 0);
		da_copy(&a, st);
		if (n->children_len > 0)
			da_stmt(c, n->children[0], &a);
		da_expr(c, n->c, &a, 0);
		da_or_used(st, &a);
		da_release(&a);
		return;
	case NdSwitch:
		/* Conservative: case assigns do not kill after the switch. */
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_stmt(c, n->b, &a);
		da_or_used(st, &a);
		da_release(&a);
		return;
	case NdCase:
	case NdDefault:
		return;
	case NdLabel:
		da_stmt(c, n->a, st);
		return;
	case NdReturn:
		da_expr(c, n->a, st, 0);
		return;
	case NdDefer:
		da_copy(&a, st);
		da_stmt(c, n->a, &a);
		da_or_used(st, &a);
		da_release(&a);
		return;
	case NdBreak:
	case NdContinue:
	case NdGoto:
	case NdFallthrough:
	case NdSkip:
		return;
	default:
		da_expr(c, n, st, 0);
		return;
	}
}

/* .mc only: scalar/aggregate decl init never read before overwrite. */
static void
check_dead_init_func(Compiler* c, Node* fn) {
	DAState st = { 0 };
	int i;
	Symbol* s;

	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	if (da_has_goto(fn->a))
		return;
	da_stmt(c, fn->a, &st);
	for (i = 0; i < st.n; i++) {
		if (st.init_used[i])
			continue;
		s = st.symbols[i];
		if (s && s->name)
			error_at(c, s->span,
				 "initializer of '%s' is never used; every path assigns "
				 "before reading it (initialize at the first assignment "
				 "instead of a placeholder)",
				 s->name);
	}
	da_release(&st);
}

static int stmt_returns(Compiler* c, Node* n);

// True if a break appears outside an inner loop/switch (conservative for falloff).
static int node_has_break(Node* n) {
	if (n == NULL) {
		return 0;
	}
	if (n->kind == NdBreak) {
		return 1;
	}
	/* nested loop/switch break targets inner, not outer — still conservative */
	if (n->kind == NdWhile || n->kind == NdFor || n->kind == NdDo || n->kind == NdSwitch) {
		return 0;
	}
	if (node_has_break(n->a) || node_has_break(n->b) || node_has_break(n->c)) {
		return 1;
	}
	for (int i = 0; i < n->children_len; i++) {
		if (node_has_break(n->children[i])) {
			return 1;
		}
	}
	return 0;
}

// True if n is a constant expression evaluating to non-zero.
static int is_const_nonzero(Compiler* c, Node* n) {
	int64_t v;
	return n != NULL && eval_const(c, n, &v) && v != 0;
}

enum { SwitchCaseMax = 256,
       SwitchRangeMax = 256 };

typedef struct SwitchCases SwitchCases;
struct SwitchCases {
	int64_t v[SwitchCaseMax];
	int n;
	int has_default;
	int overflow;
};

// True if val is already listed in the collected switch cases.
static int swcases_has(SwitchCases* sc, int64_t val) {
	for (int i = 0; i < sc->n; i++) {
		if (sc->v[i] == val) {
			return 1;
		}
	}
	return 0;
}

// Add one case value; set overflow when the table is full.
static int swcases_add(SwitchCases* sc, int64_t val) {
	if (sc->has_default || sc->overflow) {
		return 0;
	}
	if (swcases_has(sc, val)) {
		return 0;
	}
	if (sc->n >= SwitchCaseMax) {
		sc->overflow = 1;
		return -1;
	}
	sc->v[sc->n++] = val;
	return 0;
}

// Expand an inclusive case range into individual values (bounded work).
static int swcases_add_range(SwitchCases* sc, int64_t lo, int64_t hi) {
	if (hi < lo) {
		return 0;
	}
	if (hi - lo > SwitchRangeMax) {
		sc->overflow = 1;
		return -1;
	}
	for (int64_t v = lo; v <= hi; v++) {
		if (swcases_add(sc, v) < 0) {
			return -1;
		}
	}
	return 0;
}

// Recursively gather case labels from a switch body (not nested switches).
static void swcases_collect(Node* n, SwitchCases* sc) {
	if (n == NULL || sc->overflow) {
		return;
	}
	if (n->kind == NdCase) {
		if (swcases_add(sc, n->int_val) < 0) {
			return;
		}
		if (n->b) {
			(void)swcases_add_range(sc, n->int_val, n->b->int_val);
		}
		return;
	}
	if (n->kind == NdDefault) {
		sc->has_default = 1;
		return;
	}
	if (n->kind == NdSwitch)
		return;
	swcases_collect(n->a, sc);
	swcases_collect(n->b, sc);
	swcases_collect(n->c, sc);
	for (int i = 0; i < n->children_len; i++) {
		swcases_collect(n->children[i], sc);
	}
}

// Switch scrutinee type when it is a complete enum tag type.
static Type* switch_enum_type(Type* t) {
	if (t && t->kind == TyEnum && t->complete && t->tag) {
		return t;
	}
	return NULL;
}

// True if body covers every enumerator of et (or has default / overflow).
static int switch_enum_exhaustive(Compiler* c, Type* et, Node* body) {
	SwitchCases sc;
	Symbol* s;

	et = switch_enum_type(et);
	if (et == NULL) {
		return 0;
	}
	memset(&sc, 0, sizeof(sc));
	swcases_collect(body, &sc);
	if (sc.has_default || sc.overflow) {
		return sc.has_default;
	}
	for (s = c->syms.symbols; s; s = s->next) {
		if (s->hidden || s->dead) {
			continue;
		}
		if (s->kind != SkEnumCon || s->type == NULL || !type_eq(s->type, et)) {
			continue;
		}
		if (!swcases_has(&sc, s->int_val)) {
			return 0;
		}
	}
	return 1;
}

// Diagnose enum switches in user code that omit a constant.
static void
check_switch_enum_exhaust(Compiler* c, Node* sw) {
	Type* et;
	SwitchCases sc;
	Symbol* s;

	if (sw == NULL || sw->kind != NdSwitch) {
		return;
	}
	if (!user_source(c, sw->span))
		return;
	et = switch_enum_type(sw->a ? sw->a->type : NULL);
	if (et == NULL)
		return;
	memset(&sc, 0, sizeof(sc));
	swcases_collect(sw->b, &sc);
	if (sc.has_default || sc.overflow)
		return;
	for (s = c->syms.symbols; s; s = s->next) {
		if (s->hidden || s->dead)
			continue;
		if (s->kind != SkEnumCon || s->type == NULL || !type_eq(s->type, et))
			continue;
		if (!swcases_has(&sc, s->int_val))
			error_at(c, sw->span, "switch on %s is not exhaustive; missing case %s",
				 et->tag, s->name ? s->name : "?");
	}
}

// Recursively find switches and check enum exhaustiveness.
static void
walk_enum_exhaust(Compiler* c, Node* n) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NdSwitch) {
		check_switch_enum_exhaust(c, n);
		walk_enum_exhaust(c, n->b);
		return;
	}
	walk_enum_exhaust(c, n->a);
	walk_enum_exhaust(c, n->b);
	walk_enum_exhaust(c, n->c);
	for (i = 0; i < n->children_len; i++)
		walk_enum_exhaust(c, n->children[i]);
}

// Per-function entry for exhaustive enum switch checking.
static void
check_enum_exhaust_func(Compiler* c, Node* fn) {
	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	walk_enum_exhaust(c, fn->a);
}

// Switch returns on every value if there is a default, an exhaustive enum
// switch, and no path leaves the switch without returning.
static int
switch_returns(Compiler* c, Node* body, Type* swty) {
	int i, has_default, path_open, path_returned, falling, ok;
	Node* s;

	if (body == NULL)
		return 0;
	if (body->kind != NdBlock)
		return stmt_returns(c, body);
	has_default = 0;
	path_open = 0;
	path_returned = 0;
	falling = 0;
	ok = 1;
	for (i = 0; i < body->children_len; i++) {
		s = body->children[i];
		if (s->kind == NdCase || s->kind == NdDefault) {
			if (s->kind == NdDefault)
				has_default = 1;
			if (!falling) {
				if (path_open && !path_returned)
					ok = 0;
				path_open = 0;
				path_returned = 0;
			}
			falling = 0;
			continue;
		}
		if (s->kind == NdDefer || s->kind == NdSkip)
			continue;
		if (s->kind == NdFallthrough) {
			path_open = 1;
			path_returned = 0;
			falling = 1;
			continue;
		}
		if (s->kind == NdBreak) {
			ok = 0;
			path_open = 1;
			path_returned = 1;
			falling = 0;
			continue;
		}
		path_open = 1;
		falling = 0;
		if (stmt_returns(c, s))
			path_returned = 1;
		else
			path_returned = 0;
	}
	if (path_open && !path_returned)
		ok = 0;
	if (!has_default && !switch_enum_exhaustive(c, swty, body))
		ok = 0;
	return ok;
}

// True if every path through n returns (or never falls off).
static int
stmt_returns(Compiler* c, Node* n) {
	int i;

	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NdReturn:
		return 1;
	case NdGoto:
		/* path leaves; labels may still fall off — OK for linear tails */
		return 1;
	case NdBlock:
		for (i = n->children_len - 1; i >= 0; i--) {
			if (n->children[i]->kind == NdDefer)
				continue;
			return stmt_returns(c, n->children[i]);
		}
		return 0;
	case NdIf:
		return stmt_returns(c, n->b) && n->c != NULL && stmt_returns(c, n->c);
	case NdLabel:
		return stmt_returns(c, n->a);
	case NdWhile:
		if (is_const_nonzero(c, n->a) && !node_has_break(n->b))
			return 1;
		return 0;
	case NdFor:
		/* for(;0;) never; for(;;)/for(;1;) without break never falls off */
		if ((n->b == NULL || is_const_nonzero(c, n->b)) && !node_has_break(n->children_len > 0 ? n->children[0] : NULL))
			return 1;
		return 0;
	case NdDo:
		if (stmt_returns(c, n->a))
			return 1;
		if (is_const_nonzero(c, n->b) && !node_has_break(n->a))
			return 1;
		return 0;
	case NdSwitch:
		return switch_returns(c, n->b, n->a ? n->a->type : NULL);
	default:
		return 0;
	}
}

// Error when a non-void function body can fall off the end.
static void
check_falloff_func(Compiler* c, Node* fn) {
	Type* ret;

	if (fn == NULL || fn->kind != NdFunc || fn->type == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	ret = fn->type->base;
	if (ret == NULL || ret->kind == TyVoid)
		return;
	if (stmt_returns(c, fn->a))
		return;
	error_at(c, fn->span, "control reaches end of non-void function");
}

enum { GEDecl = 1,
       GELabel,
       GEGoto,
       GEDefer,
       GEMax = 512 };

typedef struct GEvent GEvent;
struct GEvent {
	int kind;
	Node* n;
};

// Append one GEvent (decl/label/goto) to the linearized timeline.
static void
ge_add(GEvent* ev, int* nev, int kind, Node* n) {
	if (*nev >= GEMax)
		return;
	ev[*nev].kind = kind;
	ev[*nev].n = n;
	(*nev)++;
}

// Linearize decls, labels, and gotos in statement order for goto-over-decl checks.
static void
ge_walk(Node* n, GEvent* ev, int* nev) {
	int i;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NdDecl:
		ge_add(ev, nev, GEDecl, n);
		return;
	case NdLabel:
		ge_add(ev, nev, GELabel, n);
		ge_walk(n->a, ev, nev);
		return;
	case NdGoto:
		ge_add(ev, nev, GEGoto, n);
		return;
	case NdDefer:
		ge_add(ev, nev, GEDefer, n);
		return;
	case NdBlock:
		for (i = 0; i < n->children_len; i++)
			ge_walk(n->children[i], ev, nev);
		return;
	case NdIf:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		ge_walk(n->c, ev, nev);
		return;
	case NdWhile:
	case NdDo:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		return;
	case NdFor:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		ge_walk(n->c, ev, nev);
		for (i = 0; i < n->children_len; i++)
			ge_walk(n->children[i], ev, nev);
		return;
	case NdSwitch:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		return;
	default:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		ge_walk(n->c, ev, nev);
		for (i = 0; i < n->children_len; i++)
			ge_walk(n->children[i], ev, nev);
		return;
	}
}

// True when ancestor is scope itself or one of its lexical parents.
static int
scope_contains(Node* ancestor, Node* scope) {
	for (; scope; scope = scope->scope)
		if (scope == ancestor)
			return 1;
	return 0;
}

// Reject gotos that enter scopes or skip declarations/defer registration.
static void
check_goto_over_decl(Compiler* c, Node* fn) {
	GEvent ev[GEMax];
	int nev, i, j, k;
	Node *g, *lab, *d;
	const char* name;

	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	nev = 0;
	ge_walk(fn->a, ev, &nev);
	for (i = 0; i < nev; i++) {
		if (ev[i].kind != GEGoto)
			continue;
		g = ev[i].n;
		name = g->s;
		if (name == NULL)
			continue;
		if (g->symbol == NULL || !g->symbol->defined) {
			error_at(c, g->span, "goto references undefined label '%s'", name);
			continue;
		}
		lab = NULL;
		for (j = 0; j < nev; j++) {
			if (ev[j].kind == GELabel && ev[j].n->s && strcmp(ev[j].n->s, name) == 0) {
				lab = ev[j].n;
				break;
			}
		}
		if (lab == NULL)
			continue;
		if (!scope_contains(lab->scope, g->scope)) {
			error_at(c, g->span, "goto enters a different lexical scope");
			continue;
		}
		if (j <= i)
			continue; /* backward goto */
		for (k = i + 1; k < j; k++) {
			if (ev[k].kind == GEDefer &&
			    scope_contains(ev[k].n->scope, lab->scope)) {
				error_at(c, g->span, "goto jumps over a defer");
				break;
			}
			if (ev[k].kind != GEDecl)
				continue;
			d = ev[k].n;
			if (d->symbol && d->symbol->name)
				error_at(c, g->span,
					 "goto jumps over declaration of '%s'", d->symbol->name);
			else
				error_at(c, g->span, "goto jumps over a declaration");
			break;
		}
	}
}

static void check_unseq_init(Compiler* c, Initializer* in);

// True if evaluating n can modify symbol s (for unsequenced side-effect checks).
static int
expr_modifies_symbol(Node* n, Symbol* s) {
	int i;

	if (n == NULL || s == NULL)
		return 0;
	switch (n->kind) {
	case NdAssign:
		if (n->a && n->a->kind == NdName && n->a->symbol == s)
			return 1;
		return expr_modifies_symbol(n->a, s) || expr_modifies_symbol(n->b, s);
	case NdPost:
	case NdUn:
		if ((n->op == PnPlusPlus || n->op == PnMinusMinus) && n->a && n->a->kind == NdName && n->a->symbol == s)
			return 1;
		return expr_modifies_symbol(n->a, s);
	case NdCond:
		return expr_modifies_symbol(n->a, s) || expr_modifies_symbol(n->b, s) || expr_modifies_symbol(n->c, s);
	case NdCall:
		if (expr_modifies_symbol(n->a, s))
			return 1;
		for (i = 0; i < n->children_len; i++)
			if (expr_modifies_symbol(n->children[i], s))
				return 1;
		return 0;
	case NdComma:
	case NdBin:
	case NdIndex:
	case NdDot:
	case NdArrow:
		return expr_modifies_symbol(n->a, s) || expr_modifies_symbol(n->b, s);
	case NdCast:
	case NdAddr:
	case NdDeref:
	case NdSizeof:
		return expr_modifies_symbol(n->a, s);
	default:
		for (i = 0; i < n->children_len; i++)
			if (expr_modifies_symbol(n->children[i], s))
				return 1;
		return 0;
	}
}

// Diagnose a = f(a, …) style unsequenced updates in one expression.
static void
check_unseq_expr(Compiler* c, Node* n) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NdAssign && n->a && n->a->kind == NdName && n->a->symbol && user_source(c, n->span) && expr_modifies_symbol(n->b, n->a->symbol))
		error_at(c, n->span, "unsequenced modification of '%s'", n->a->symbol->name);
	check_unseq_expr(c, n->a);
	check_unseq_expr(c, n->b);
	check_unseq_expr(c, n->c);
	for (i = 0; i < n->children_len; i++)
		check_unseq_expr(c, n->children[i]);
	if (n->init)
		check_unseq_init(c, n->init);
}

// Walk initializer expressions for unsequenced modification checks.
static void
check_unseq_init(Compiler* c, Initializer* in) {
	int i;

	if (in == NULL)
		return;
	check_unseq_expr(c, in->expr);
	for (i = 0; i < in->items_len; i++)
		check_unseq_init(c, &in->items[i]);
}

// Per-function pass for unsequenced assignment diagnostics.
static void
check_unseq_func(Compiler* c, Node* fn) {
	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	check_unseq_expr(c, fn->a);
}

/* ---- statement walk / unused / discard ---- */
/*
 * Structural statement walk — no CFG joins / state merge.
 * Visits statement children per the Node layout table in ast.h (bodies of
 * if/while/do/for/switch/label/defer; for init+step; block children). Conditions
 * are not walked (they are expressions, not statement positions).
 *
 * visit(c, n, ctx) runs on each reached node. Return 0 to recurse with the
 * default children; non-zero skips default recursion (handled or pruned).
 */
typedef int (*StmtVisitFn)(Compiler* c, Node* n, void* ctx);

// Depth-first walk of statement positions; visit may prune default recursion.
static void
walk_stmt(Compiler* c, Node* n, StmtVisitFn visit, void* ctx) {
	int i;

	if (n == NULL)
		return;
	if (visit && visit(c, n, ctx))
		return;
	switch (n->kind) {
	case NdBlock:
		for (i = 0; i < n->children_len; i++)
			walk_stmt(c, n->children[i], visit, ctx);
		return;
	case NdIf:
		walk_stmt(c, n->b, visit, ctx);
		walk_stmt(c, n->c, visit, ctx);
		return;
	case NdWhile:
		walk_stmt(c, n->b, visit, ctx);
		return;
	case NdDo:
		walk_stmt(c, n->a, visit, ctx);
		return;
	case NdFor:
		walk_stmt(c, n->a, visit, ctx);
		if (n->children_len > 0)
			walk_stmt(c, n->children[0], visit, ctx);
		walk_stmt(c, n->c, visit, ctx);
		return;
	case NdSwitch:
		walk_stmt(c, n->b, visit, ctx);
		return;
	case NdLabel:
	case NdDefer:
		walk_stmt(c, n->a, visit, ctx);
		return;
	default:
		return;
	}
}

// True if n is a cast to void.
static int
is_void_cast(Node* n) {
	return n && n->kind == NdCast && n->type && n->type->kind == TyVoid;
}

// Callee symbol for a typed call (after overload / method lowering).
static Symbol*
call_callee_symbol(Node* n) {
	if (n == NULL || n->kind != NdCall || n->a == NULL || n->a->kind != NdName)
		return NULL;
	return n->a->symbol;
}

// True if n is a discarded [[nodiscard]] call used as an expression statement.
static int
is_discarded_nodiscard_expr(Node* n) {
	Symbol* s;

	if (n == NULL)
		return 0;
	if (is_void_cast(n))
		return 0;
	if (n->kind == NdComma)
		return is_discarded_nodiscard_expr(n->b);
	if (n->kind != NdCall)
		return 0;
	if (n->type && n->type->kind == TyVoid)
		return 0;
	s = call_callee_symbol(n);
	return s && s->nodiscard;
}

// StmtVisitFn: error on discarded [[nodiscard]] returns; prune non-statement forms.
static int
discard_nodiscard_visit(Compiler* c, Node* n, void* ctx) {
	(void)ctx;
	switch (n->kind) {
	case NdBlock:
	case NdIf:
	case NdWhile:
	case NdDo:
	case NdFor:
	case NdSwitch:
	case NdLabel:
	case NdDefer:
		return 0;
	case NdCase:
	case NdDefault:
	case NdDecl:
	case NdReturn:
	case NdBreak:
	case NdContinue:
	case NdGoto:
	case NdFallthrough:
	case NdSkip:
		return 1;
	default:
		if (user_source(c, n->span) && is_discarded_nodiscard_expr(n))
			error_at(c, n->span,
				 "discarded [[nodiscard]] return value; assign it or cast to void");
		return 1;
	}
}

// Walk a statement tree for discarded [[nodiscard]] call results.
static void
check_discard_nodiscard_stmt(Compiler* c, Node* n) {
	walk_stmt(c, n, discard_nodiscard_visit, NULL);
}


// True if name looks like an internal __temp (skip unused warnings).
static int
is_compiler_temp_name(const char* name) {
	return name && name[0] == '_' && name[1] == '_';
}

// After marking uses, report unused locals and parameters in user functions.
static void
check_unused_func(Compiler* c, Node* fn) {
	Symbol *s, *owner;

	if (fn == NULL || fn->kind != NdFunc || fn->symbol == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	owner = fn->symbol;
	mark_symbol_used(fn->a);
	for (s = c->syms.symbols; s; s = s->next) {
		if (s->owner != owner || s->kind != SkVar || s->used || s->maybe_unused)
			continue;
		if (is_compiler_temp_name(s->name))
			continue;
		if (s->storage == StParam)
			error_at(c, s->span.file ? s->span : fn->span,
				 "unused parameter '%s'", s->name);
		else
			error_at(c, s->span.file ? s->span : fn->span,
				 "unused variable '%s'", s->name);
	}
}

// Per-function pass for discarded [[nodiscard]] return diagnostics.
static void
check_discard_nodiscard_func(Compiler* c, Node* fn) {
	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	check_discard_nodiscard_stmt(c, fn->a);
}

/* ---- method receiver: no null tests, no rebind ---- */

static Symbol*
method_receiver_param(Compiler* c, Node* fn) {
	Symbol *fs, *s;
	const char* rname;

	fs = fn->symbol;
	if (fs == NULL || !fs->is_method || fs->type == NULL || fs->type->param_names == NULL ||
	    fs->type->param_names[0] == NULL)
		return NULL;
	rname = fs->type->param_names[0];
	for (s = c->syms.symbols; s; s = s->next) {
		/* Params are marked dead after the function block closes; still valid. */
		if (s->owner != fs || s->kind != SkVar || s->storage != StParam)
			continue;
		if (s->name && strcmp(s->name, rname) == 0)
			return s;
	}
	return NULL;
}

static int
is_receiver_name(Node* n, Symbol* recv) {
	return n && n->kind == NdName && n->symbol == recv;
}

static void check_method_recv_expr(Compiler* c, Node* n, Symbol* recv);
static void check_method_recv_cond(Compiler* c, Node* n, Symbol* recv);
static void check_method_recv_init(Compiler* c, Initializer* in, Symbol* recv);

// Condition / boolean context: bare receiver is a null test.
static void
check_method_recv_cond(Compiler* c, Node* n, Symbol* recv) {
	if (n == NULL || recv == NULL)
		return;
	if (is_receiver_name(n, recv)) {
		error_at(c, n->span,
			 "cannot test method receiver '%s' for null; callers must pass a live object",
			 recv->name);
		return;
	}
	if (n->kind == NdUn && n->op == PnBang && is_receiver_name(n->a, recv)) {
		error_at(c, n->span,
			 "cannot test method receiver '%s' for null; callers must pass a live object",
			 recv->name);
		return;
	}
	if (n->kind == NdBin && (n->op == PnAmpAmp || n->op == PnPipePipe)) {
		check_method_recv_cond(c, n->a, recv);
		check_method_recv_cond(c, n->b, recv);
		return;
	}
	if (n->kind == NdCast) {
		check_method_recv_cond(c, n->a, recv);
		return;
	}
	check_method_recv_expr(c, n, recv);
}

static void
check_method_recv_expr(Compiler* c, Node* n, Symbol* recv) {
	int i;

	if (n == NULL || recv == NULL)
		return;
	if (n->kind == NdAssign) {
		if (is_receiver_name(n->a, recv))
			error_at(c, n->span,
				 "cannot rebind method receiver '%s'; mutate through it instead",
				 recv->name);
		check_method_recv_expr(c, n->a, recv);
		check_method_recv_expr(c, n->b, recv);
		return;
	}
	if (n->kind == NdBin && (n->op == PnEqEq || n->op == PnBangEq)) {
		if ((is_receiver_name(n->a, recv) && is_null_expr(n->b)) ||
		    (is_receiver_name(n->b, recv) && is_null_expr(n->a)))
			error_at(c, n->span,
				 "cannot compare method receiver '%s' to null; callers must pass a live object",
				 recv->name);
		check_method_recv_expr(c, n->a, recv);
		check_method_recv_expr(c, n->b, recv);
		return;
	}
	if (n->kind == NdUn && n->op == PnBang && is_receiver_name(n->a, recv)) {
		error_at(c, n->span,
			 "cannot test method receiver '%s' for null; callers must pass a live object",
			 recv->name);
		return;
	}
	if (n->kind == NdUn && (n->op == PnPlusPlus || n->op == PnMinusMinus) && is_receiver_name(n->a, recv)) {
		error_at(c, n->span,
			 "cannot rebind method receiver '%s'; mutate through it instead",
			 recv->name);
		return;
	}
	if (n->kind == NdPost && is_receiver_name(n->a, recv)) {
		error_at(c, n->span,
			 "cannot rebind method receiver '%s'; mutate through it instead",
			 recv->name);
		return;
	}
	switch (n->kind) {
	case NdIf:
	case NdWhile:
		check_method_recv_cond(c, n->a, recv);
		check_method_recv_expr(c, n->b, recv);
		check_method_recv_expr(c, n->c, recv);
		break;
	case NdDo:
		check_method_recv_expr(c, n->a, recv);
		check_method_recv_cond(c, n->b, recv);
		break;
	case NdFor:
		check_method_recv_expr(c, n->a, recv);
		check_method_recv_cond(c, n->b, recv);
		check_method_recv_expr(c, n->c, recv);
		for (i = 0; i < n->children_len; i++)
			check_method_recv_expr(c, n->children[i], recv);
		break;
	case NdCond:
		check_method_recv_cond(c, n->a, recv);
		check_method_recv_expr(c, n->b, recv);
		check_method_recv_expr(c, n->c, recv);
		break;
	case NdBin:
		if (n->op == PnAmpAmp || n->op == PnPipePipe) {
			check_method_recv_cond(c, n->a, recv);
			check_method_recv_cond(c, n->b, recv);
		} else {
			check_method_recv_expr(c, n->a, recv);
			check_method_recv_expr(c, n->b, recv);
		}
		break;
	default:
		check_method_recv_expr(c, n->a, recv);
		check_method_recv_expr(c, n->b, recv);
		check_method_recv_expr(c, n->c, recv);
		for (i = 0; i < n->children_len; i++)
			check_method_recv_expr(c, n->children[i], recv);
		if (n->init)
			check_method_recv_init(c, n->init, recv);
		break;
	}
}

static void
check_method_recv_init(Compiler* c, Initializer* in, Symbol* recv) {
	int i;

	if (in == NULL)
		return;
	check_method_recv_expr(c, in->expr, recv);
	for (i = 0; i < in->items_len; i++)
		check_method_recv_init(c, &in->items[i], recv);
}

static void
check_method_receiver_func(Compiler* c, Node* fn) {
	Symbol* recv;

	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL || fn->symbol == NULL)
		return;
	if (!fn->symbol->is_method || !user_source(c, fn->span))
		return;
	recv = method_receiver_param(c, fn);
	if (recv == NULL)
		return;
	check_method_recv_expr(c, fn->a, recv);
}

/* ---- stack escape + [[retains]] / [[region]] (asymmetric: prove .mc) ---- */

enum { MaxRegionDeps = 4 };

typedef struct EscapeState EscapeState;
struct EscapeState {
	Symbol** syms;
	unsigned char* stack_taint;
	unsigned char* dead_taint; /* may hold a value a defer will consume */
	Symbol** region_deps; /* n * MaxRegionDeps: views into [[region]] objects */
	unsigned char* region_dep_n;
	uint64_t* param_bits;
	int n;
	int cap;
	Symbol* params[MaxParams];
	int nparams;
	Type* fnty;
	int infer;
	int diag;
	int changed;
	/* Defers in nested blocks: kill[i] invalidated at exit of scope i. */
	Symbol* kill[MaxDeferDepth][32];
	int kill_n[MaxDeferDepth];
	int scope_depth;
};

static int
sym_is_stack_object(Symbol* s) {
	if (s == NULL || s->kind != SkVar)
		return 0;
	return s->storage == StLocal || s->storage == StParam;
}

static int
type_may_carry_ptr(Type* t) {
	Field* f;

	if (t == NULL)
		return 0;
	if (is_ptr(t) || is_ranged(t))
		return 1;
	if (is_array(t))
		return type_may_carry_ptr(t->base);
	if (is_aggr(t) || is_tuple(t)) {
		for (f = t->fields; f; f = f->next) {
			if (type_may_carry_ptr(f->type))
				return 1;
		}
	}
	return 0;
}

static Symbol*
lvalue_stack_object(Node* n) {
	Type* bt;

	if (n == NULL)
		return NULL;
	if (n->kind == NdName && sym_is_stack_object(n->symbol))
		return n->symbol;
	if (n->kind == NdDot || n->kind == NdArrow) {
		bt = n->a ? n->a->type : NULL;
		if (bt && is_ptr(bt))
			return NULL;
		return lvalue_stack_object(n->a);
	}
	if (n->kind == NdIndex) {
		bt = n->a ? n->a->type : NULL;
		if (bt && is_ptr(bt))
			return NULL;
		return lvalue_stack_object(n->a);
	}
	return NULL;
}

/* Syntactic stack root in this frame (no taint). */
static Symbol*
expr_stack_addr_root(Node* n) {
	Symbol* s;
	Node* arg;
	const char* bn;

	if (n == NULL)
		return NULL;
	if (n->kind == NdAddr)
		return lvalue_stack_object(n->a);
	if (n->kind == NdCast)
		return expr_stack_addr_root(n->a);
	if (n->kind == NdComma)
		return expr_stack_addr_root(n->b);
	if (n->kind == NdCond) {
		s = expr_stack_addr_root(n->b);
		if (s)
			return s;
		return expr_stack_addr_root(n->c);
	}
	if (n->kind == NdName && sym_is_stack_object(n->symbol) && is_array(n->type))
		return n->symbol;
	if (n->kind == NdSubrange) {
		if (n->a && is_array(n->a->type))
			return lvalue_stack_object(n->a);
		return NULL;
	}
	if (n->kind == NdCall && n->a && n->a->kind == NdName && n->a->s && !n->a->symbol) {
		bn = n->a->s;
		if ((strcmp(bn, "ranged") == 0 || strcmp(bn, "ptr") == 0) &&
		    n->children_len >= 1) {
			arg = n->children[0];
			if (arg && arg->kind == NdName && sym_is_stack_object(arg->symbol) &&
			    is_array(arg->type))
				return arg->symbol;
			if (arg && is_array(arg->type)) {
				s = lvalue_stack_object(arg);
				if (s)
					return s;
			}
			return expr_stack_addr_root(arg);
		}
	}
	if (n->kind == NdDot && n->is_synth && n->s && strcmp(n->s, "ptr") == 0)
		return expr_stack_addr_root(n->a);
	if (n->kind == NdBin && (n->op == PnPlus || n->op == PnMinus)) {
		s = expr_stack_addr_root(n->a);
		if (s)
			return s;
		if (n->op == PnPlus)
			return expr_stack_addr_root(n->b);
	}
	return NULL;
}

static void
esc_release(EscapeState* st) {
	free(st->syms);
	free(st->stack_taint);
	free(st->dead_taint);
	free(st->region_deps);
	free(st->region_dep_n);
	free(st->param_bits);
	st->syms = NULL;
	st->stack_taint = NULL;
	st->dead_taint = NULL;
	st->region_deps = NULL;
	st->region_dep_n = NULL;
	st->param_bits = NULL;
	st->n = 0;
	st->cap = 0;
}

static void
esc_grow(EscapeState* st, int need) {
	int ncap, i, j;

	if (need <= st->cap)
		return;
	ncap = st->cap ? st->cap * 2 : 32;
	while (ncap < need)
		ncap *= 2;
	st->syms = xrealloc(st->syms, (size_t)ncap * sizeof(Symbol*));
	st->stack_taint = xrealloc(st->stack_taint, (size_t)ncap);
	st->dead_taint = xrealloc(st->dead_taint, (size_t)ncap);
	st->region_deps =
		xrealloc(st->region_deps, (size_t)ncap * MaxRegionDeps * sizeof(Symbol*));
	st->region_dep_n = xrealloc(st->region_dep_n, (size_t)ncap);
	st->param_bits = xrealloc(st->param_bits, (size_t)ncap * sizeof(uint64_t));
	for (i = st->cap; i < ncap; i++) {
		st->dead_taint[i] = 0;
		st->region_dep_n[i] = 0;
		for (j = 0; j < MaxRegionDeps; j++)
			st->region_deps[i * MaxRegionDeps + j] = NULL;
	}
	st->cap = ncap;
}

static int
esc_find(EscapeState* st, Symbol* s) {
	int i;

	for (i = 0; i < st->n; i++) {
		if (st->syms[i] == s)
			return i;
	}
	return -1;
}

static int
esc_index(EscapeState* st, Symbol* s) {
	int i;

	if (s == NULL || s->kind != SkVar)
		return -1;
	if (s->storage != StLocal && s->storage != StParam)
		return -1;
	i = esc_find(st, s);
	if (i >= 0)
		return i;
	esc_grow(st, st->n + 1);
	i = st->n++;
	st->syms[i] = s;
	st->stack_taint[i] = 0;
	st->dead_taint[i] = 0;
	st->region_dep_n[i] = 0;
	st->param_bits[i] = 0;
	return i;
}

static void
esc_clear_region_deps(EscapeState* st, int i) {
	int j;

	if (i < 0)
		return;
	st->region_dep_n[i] = 0;
	for (j = 0; j < MaxRegionDeps; j++)
		st->region_deps[i * MaxRegionDeps + j] = NULL;
}

static void
esc_add_region_dep(EscapeState* st, Symbol* dst, Symbol* region) {
	int i, j, n;

	if (dst == NULL || region == NULL)
		return;
	i = esc_index(st, dst);
	if (i < 0)
		return;
	n = st->region_dep_n[i];
	for (j = 0; j < n; j++) {
		if (st->region_deps[i * MaxRegionDeps + j] == region)
			return;
	}
	if (n >= MaxRegionDeps)
		return;
	st->region_deps[i * MaxRegionDeps + n] = region;
	st->region_dep_n[i] = (unsigned char)(n + 1);
}

static void
esc_mark_dead(EscapeState* st, Symbol* s) {
	int i;

	i = esc_index(st, s);
	if (i >= 0)
		st->dead_taint[i] = 1;
}

/* Kill views minted from region R (pool/arena). Does not mark R itself dead. */
static void
esc_invalidate_region_views(EscapeState* st, Symbol* region) {
	int i, j, n;

	if (region == NULL)
		return;
	for (i = 0; i < st->n; i++) {
		n = st->region_dep_n[i];
		for (j = 0; j < n; j++) {
			if (st->region_deps[i * MaxRegionDeps + j] == region) {
				st->dead_taint[i] = 1;
				break;
			}
		}
	}
}

/* After free/consumes of region R: R and views minted from R are dead. */
static void
esc_invalidate_region(EscapeState* st, Symbol* region) {
	if (region == NULL)
		return;
	esc_mark_dead(st, region);
	esc_invalidate_region_views(st, region);
}

static void
esc_add_param_bits(EscapeState* st, Symbol* s, uint64_t bits) {
	int i;

	if (bits == 0)
		return;
	i = esc_index(st, s);
	if (i < 0)
		return;
	st->param_bits[i] |= bits;
}

static int
esc_stack_tainted(EscapeState* st, Symbol* s) {
	int i;

	i = esc_find(st, s);
	return i >= 0 && st->stack_taint[i];
}

static int
esc_dead_tainted(EscapeState* st, Symbol* s) {
	int i;

	i = esc_find(st, s);
	return i >= 0 && st->dead_taint[i];
}

static uint64_t
esc_param_bits(EscapeState* st, Symbol* s) {
	int i;

	i = esc_find(st, s);
	return i >= 0 ? st->param_bits[i] : 0;
}

static int
sym_in_defer_kill(EscapeState* st, Symbol* s) {
	int d, j;

	if (s == NULL)
		return 0;
	for (d = 0; d < st->scope_depth; d++) {
		for (j = 0; j < st->kill_n[d]; j++) {
			if (st->kill[d][j] == s)
				return 1;
		}
	}
	return 0;
}

static void
esc_add_kill(EscapeState* st, Symbol* s) {
	int d, j;

	if (s == NULL || st->scope_depth <= 0)
		return;
	d = st->scope_depth - 1;
	if (st->kill_n[d] >= 32)
		return;
	for (j = 0; j < st->kill_n[d]; j++) {
		if (st->kill[d][j] == s)
			return;
	}
	st->kill[d][st->kill_n[d]++] = s;
}

/* Region R is scheduled for free/reset by a defer — views must not escape. */
static int
sym_region_killed(EscapeState* st, Symbol* s) {
	int i, j, n;
	Symbol* r;

	i = esc_find(st, s);
	if (i < 0)
		return 0;
	n = st->region_dep_n[i];
	for (j = 0; j < n; j++) {
		r = st->region_deps[i * MaxRegionDeps + j];
		if (r && (sym_in_defer_kill(st, r) || esc_dead_tainted(st, r)))
			return 1;
	}
	return 0;
}

/* Region already freed/reset (not merely deferred) — local uses are invalid. */
static int
sym_region_already_dead(EscapeState* st, Symbol* s) {
	int i, j, n;
	Symbol* r;

	i = esc_find(st, s);
	if (i < 0)
		return 0;
	n = st->region_dep_n[i];
	for (j = 0; j < n; j++) {
		r = st->region_deps[i * MaxRegionDeps + j];
		if (r && esc_dead_tainted(st, r))
			return 1;
	}
	return 0;
}

static Type* call_func_type(Node* call);
static int param_is_region(Type* ft, int i);

static int
param_is_consumes(Type* ft, int i) {
	if (ft == NULL || ft->param_consumes == NULL || i < 0 || i >= ft->params_len)
		return 0;
	return ft->param_consumes[i] != 0;
}

static int
param_is_region(Type* ft, int i) {
	if (ft == NULL || ft->param_region == NULL || i < 0 || i >= ft->params_len)
		return 0;
	return ft->param_region[i] != 0;
}

/* libc-shaped consumptions when the callee is not a %C [[consumes]] API. */
static int
builtin_arg_consumes(Symbol* callee, int i) {
	const char* n;

	if (i != 0)
		return 0;
	n = callee && callee->name ? callee->name : NULL;
	if (n && (strcmp(n, "free") == 0 || strcmp(n, "fclose") == 0))
		return 1;
	return 0;
}

static int
call_name_is(Node* call, const char* want) {
	if (call == NULL || call->a == NULL || call->a->kind != NdName || call->a->s == NULL)
		return 0;
	return strcmp(call->a->s, want) == 0;
}

static int
call_is_pool_free(Node* call) {
	return call_name_is(call, "__modc_pool_free");
}

static int
call_is_pool_reset(Node* call) {
	return call_name_is(call, "__modc_pool_reset");
}

/* free ends the pool; reset only kills minted views (slab reused). */
static int
call_is_pool_consume(Node* call) {
	return call_is_pool_free(call) || call_is_pool_reset(call);
}

static int
call_is_pool_region_mint(Node* call) {
	const char* n;

	if (call == NULL || call->a == NULL || call->a->kind != NdName || call->a->s == NULL)
		return 0;
	n = call->a->s;
	return strcmp(n, "__modc_pool_copy") == 0 || strcmp(n, "__modc_pool_append") == 0 ||
	       strcmp(n, "__modc_pool_push") == 0 || strcmp(n, "__modc_pool_join") == 0 ||
	       strcmp(n, "__modc_pool_replace") == 0 || strcmp(n, "__modc_pool_cstr") == 0 ||
	       strcmp(n, "__modc_pool_alloc_ptr") == 0 ||
	       strcmp(n, "__modc_pool_alloc_slice") == 0;
}

/* Local/param object whose lifetime a consuming call ends. */
static Symbol*
consume_target_sym(Node* arg) {
	Symbol* s;
	const char* bn;

	if (arg == NULL)
		return NULL;
	if (arg->kind == NdCast)
		return consume_target_sym(arg->a);
	if (arg->kind == NdAddr)
		return lvalue_stack_object(arg->a);
	if (arg->kind == NdCall && arg->a && arg->a->kind == NdName && arg->a->s &&
	    !arg->a->symbol && arg->children_len >= 1) {
		bn = arg->a->s;
		if (strcmp(bn, "ptr") == 0 || strcmp(bn, "ranged") == 0)
			return consume_target_sym(arg->children[0]);
	}
	if (arg->kind == NdName && arg->symbol &&
	    (arg->symbol->storage == StLocal || arg->symbol->storage == StParam))
		return arg->symbol;
	s = lvalue_stack_object(arg);
	return s;
}

static int
call_arg_consumes(Node* call, int i) {
	Type* ft;
	Symbol* callee;

	ft = call_func_type(call);
	callee = (call->a && call->a->kind == NdName) ? call->a->symbol : NULL;
	if (ft && param_is_consumes(ft, i))
		return 1;
	if (builtin_arg_consumes(callee, i))
		return 1;
	if (i == 0 && call_is_pool_consume(call))
		return 1;
	return 0;
}

static void
defer_register_consumes(EscapeState* st, Node* n) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NdBlock) {
		for (i = 0; i < n->children_len; i++)
			defer_register_consumes(st, n->children[i]);
		return;
	}
	if (n->kind == NdCall) {
		for (i = 0; i < n->children_len; i++) {
			if (!call_arg_consumes(n, i))
				continue;
			esc_add_kill(st, consume_target_sym(n->children[i]));
		}
		return;
	}
	/* Comma / cast wrappers around a call. */
	if (n->kind == NdCast || n->kind == NdComma)
		defer_register_consumes(st, n->kind == NdComma ? n->b : n->a);
}

static uint64_t
expr_param_bits(EscapeState* st, Node* n);

static int
sym_is_consumed(EscapeState* st, Symbol* s) {
	if (s == NULL)
		return 0;
	return sym_in_defer_kill(st, s) || esc_dead_tainted(st, s) ||
	       sym_region_killed(st, s);
}

static int
expr_may_dead(EscapeState* st, Node* n) {
	Symbol* s;
	Type* bt;
	int i, j, lim;
	Type* ft;

	if (n == NULL)
		return 0;
	if (n->kind == NdTupleLit) {
		for (i = 0; i < n->children_len; i++) {
			if (expr_may_dead(st, n->children[i]))
				return 1;
		}
		return 0;
	}
	if (n->kind == NdCast || n->kind == NdComma)
		return expr_may_dead(st, n->kind == NdComma ? n->b : n->a);
	if (n->kind == NdAddr)
		return expr_may_dead(st, n->a);
	if (n->kind == NdCond)
		return expr_may_dead(st, n->b) || expr_may_dead(st, n->c);
	if (n->kind == NdName && n->symbol)
		return sym_is_consumed(st, n->symbol);
	if (n->kind == NdDeref)
		return expr_may_dead(st, n->a);
	if ((n->kind == NdDot || n->kind == NdArrow || n->kind == NdIndex) && n->a) {
		/* Any access through a consumed pointer is a use (including p[0] / p.x). */
		bt = n->a->type;
		if (bt && is_ptr(bt) && expr_may_dead(st, n->a))
			return 1;
		if (!type_may_carry_ptr(n->type) && !is_array(n->type))
			return 0;
		s = lvalue_stack_object(n->a);
		if (sym_is_consumed(st, s))
			return 1;
		if (n->a->kind == NdName && sym_is_consumed(st, n->a->symbol))
			return 1;
	}
	if (n->kind == NdBin && (n->op == PnPlus || n->op == PnMinus)) {
		if (expr_may_dead(st, n->a))
			return 1;
		if (n->op == PnPlus && expr_may_dead(st, n->b))
			return 1;
		return 0;
	}
	if (n->kind == NdCall) {
		ft = call_func_type(n);
		if (ft) {
			lim = n->children_len;
			if (lim > ft->params_len)
				lim = ft->params_len;
			for (j = 0; j < lim; j++) {
				if (param_is_region(ft, j)) {
					s = consume_target_sym(n->children[j]);
					if (sym_is_consumed(st, s))
						return 1;
				}
				if (ft->param_returned && ft->param_returned[j] &&
				    expr_may_dead(st, n->children[j]))
					return 1;
			}
		}
	}
	if (n->kind == NdDot && n->is_synth && n->s && strcmp(n->s, "ptr") == 0)
		return expr_may_dead(st, n->a);
	if (n->kind == NdCall && n->a && n->a->kind == NdName && n->a->s && !n->a->symbol &&
	    n->children_len >= 1 &&
	    (strcmp(n->a->s, "ptr") == 0 || strcmp(n->a->s, "ranged") == 0))
		return expr_may_dead(st, n->children[0]);
	return 0;
}

static Symbol*
expr_consumed_sym(EscapeState* st, Node* n) {
	Symbol* s;

	if (n == NULL)
		return NULL;
	if (n->kind == NdCast || n->kind == NdComma)
		return expr_consumed_sym(st, n->kind == NdComma ? n->b : n->a);
	if (n->kind == NdAddr)
		return expr_consumed_sym(st, n->a);
	if (n->kind == NdName && n->symbol &&
	    (sym_is_consumed(st, n->symbol) || esc_dead_tainted(st, n->symbol) ||
	     sym_region_killed(st, n->symbol)))
		return n->symbol;
	if (n->kind == NdDeref || n->kind == NdUn || n->kind == NdPost)
		return expr_consumed_sym(st, n->a);
	if ((n->kind == NdDot || n->kind == NdArrow || n->kind == NdIndex) && n->a) {
		s = expr_consumed_sym(st, n->a);
		if (s)
			return s;
	}
	if (n->kind == NdBin) {
		s = expr_consumed_sym(st, n->a);
		if (s)
			return s;
		return expr_consumed_sym(st, n->b);
	}
	return NULL;
}

static void
report_consumed_use(Compiler* c, EscapeState* st, Node* n) {
	Symbol* s;

	if (n == NULL)
		return;
	s = expr_consumed_sym(st, n);
	if (s == NULL && n->kind == NdName)
		s = n->symbol;
	if (s && s->name)
		error_at(c, n->span, "use of consumed value '%s'", s->name);
	else
		error_at(c, n->span, "use of consumed value");
}

static void
report_defer_escape(Compiler* c, EscapeState* st, Node* n) {
	Symbol* s;

	if (n == NULL)
		return;
	s = expr_consumed_sym(st, n);
	if (s == NULL && n->kind == NdName)
		s = n->symbol;
	if (s && s->name && sym_in_defer_kill(st, s))
		error_at(c, n->span,
			 "'%s' is invalidated by a defer in this block and cannot escape",
			 s->name);
	else if (s && s->name)
		error_at(c, n->span, "consumed value '%s' cannot escape", s->name);
	else if (s && sym_in_defer_kill(st, s))
		error_at(c, n->span,
			 "value invalidated by a defer in this block cannot escape");
	else
		error_at(c, n->span, "consumed value cannot escape");
}

/* Already consumed (free/[[consumes]]), not merely scheduled by defer. */
static int
expr_may_already_consumed(EscapeState* st, Node* n) {
	Symbol* s;
	Type* bt;
	int i;

	if (n == NULL)
		return 0;
	if (n->kind == NdTupleLit) {
		for (i = 0; i < n->children_len; i++) {
			if (expr_may_already_consumed(st, n->children[i]))
				return 1;
		}
		return 0;
	}
	if (n->kind == NdCast || n->kind == NdComma)
		return expr_may_already_consumed(st, n->kind == NdComma ? n->b : n->a);
	if (n->kind == NdAddr)
		return expr_may_already_consumed(st, n->a);
	if (n->kind == NdCond)
		return expr_may_already_consumed(st, n->b) ||
		       expr_may_already_consumed(st, n->c);
	if (n->kind == NdName && n->symbol)
		return esc_dead_tainted(st, n->symbol) ||
		       sym_region_already_dead(st, n->symbol);
	if (n->kind == NdDeref)
		return expr_may_already_consumed(st, n->a);
	if ((n->kind == NdDot || n->kind == NdArrow || n->kind == NdIndex) && n->a) {
		bt = n->a->type;
		if (bt && is_ptr(bt) && expr_may_already_consumed(st, n->a))
			return 1;
		s = lvalue_stack_object(n->a);
		if (s && (esc_dead_tainted(st, s) || sym_region_already_dead(st, s)))
			return 1;
		if (n->a->kind == NdName && n->a->symbol &&
		    (esc_dead_tainted(st, n->a->symbol) ||
		     sym_region_already_dead(st, n->a->symbol)))
			return 1;
	}
	if (n->kind == NdBin)
		return expr_may_already_consumed(st, n->a) ||
		       expr_may_already_consumed(st, n->b);
	if (n->kind == NdUn || n->kind == NdPost)
		return expr_may_already_consumed(st, n->a);
	return 0;
}

static void
esc_check_expr_use(Compiler* c, EscapeState* st, Node* n) {
	int i;

	if (n == NULL || !st->diag)
		return;
	if (n->kind == NdTupleLit) {
		for (i = 0; i < n->children_len; i++)
			esc_check_expr_use(c, st, n->children[i]);
		return;
	}
	if (expr_may_already_consumed(st, n))
		report_consumed_use(c, st, n);
}

static int
expr_may_stack(EscapeState* st, Node* n) {
	Symbol* s;
	int i;

	if (n == NULL)
		return 0;
	if (expr_stack_addr_root(n))
		return 1;
	if (n->kind == NdTupleLit) {
		for (i = 0; i < n->children_len; i++) {
			if (expr_may_stack(st, n->children[i]))
				return 1;
		}
		return 0;
	}
	if (n->kind == NdCast || n->kind == NdComma)
		return expr_may_stack(st, n->kind == NdComma ? n->b : n->a);
	if (n->kind == NdCond)
		return expr_may_stack(st, n->b) || expr_may_stack(st, n->c);
	if (n->kind == NdName && n->symbol && esc_stack_tainted(st, n->symbol))
		return 1;
	if ((n->kind == NdDot || n->kind == NdArrow || n->kind == NdIndex) && n->a) {
		/* Only pointer-carrying field/element reads propagate stack taint. */
		if (!type_may_carry_ptr(n->type) && !is_array(n->type))
			return 0;
		s = lvalue_stack_object(n->a);
		if (s && esc_stack_tainted(st, s))
			return 1;
		if (n->a->kind == NdName && esc_stack_tainted(st, n->a->symbol))
			return 1;
	}
	if (n->kind == NdCall) {
		Type* ft;
		int j, lim;

		ft = call_func_type(n);
		if (ft && ft->param_returned) {
			lim = n->children_len;
			if (lim > ft->params_len)
				lim = ft->params_len;
			for (j = 0; j < lim; j++) {
				if (ft->param_returned[j] &&
				    expr_may_stack(st, n->children[j]))
					return 1;
			}
		}
	}
	if (n->kind == NdBin && (n->op == PnPlus || n->op == PnMinus))
		return expr_may_stack(st, n->a) ||
		       (n->op == PnPlus && expr_may_stack(st, n->b));
	if (n->kind == NdDot && n->is_synth && n->s && strcmp(n->s, "ptr") == 0)
		return expr_may_stack(st, n->a);
	return 0;
}

static uint64_t
expr_param_bits(EscapeState* st, Node* n) {
	uint64_t bits;
	int i;
	Symbol* s;

	if (n == NULL)
		return 0;
	if (n->kind == NdName && n->symbol)
		return esc_param_bits(st, n->symbol);
	if (n->kind == NdCast)
		return expr_param_bits(st, n->a);
	if (n->kind == NdComma)
		return expr_param_bits(st, n->b);
	if (n->kind == NdCond)
		return expr_param_bits(st, n->b) | expr_param_bits(st, n->c);
	if (n->kind == NdTupleLit) {
		bits = 0;
		for (i = 0; i < n->children_len; i++)
			bits |= expr_param_bits(st, n->children[i]);
		return bits;
	}
	if (n->kind == NdAddr) {
		s = lvalue_stack_object(n->a);
		if (s && s->storage == StParam) {
			for (i = 0; i < st->nparams; i++) {
				if (st->params[i] == s)
					return (uint64_t)1 << i;
			}
		}
		return expr_param_bits(st, n->a);
	}
	if (n->kind == NdDot || n->kind == NdArrow || n->kind == NdIndex) {
		if (!type_may_carry_ptr(n->type) && !is_array(n->type))
			return 0;
		return expr_param_bits(st, n->a);
	}
	if (n->kind == NdBin && (n->op == PnPlus || n->op == PnMinus)) {
		bits = expr_param_bits(st, n->a);
		if (n->op == PnPlus)
			bits |= expr_param_bits(st, n->b);
		return bits;
	}
	if (n->kind == NdDot && n->is_synth && n->s && strcmp(n->s, "ptr") == 0)
		return expr_param_bits(st, n->a);
	if (n->kind == NdCall && n->a && n->a->kind == NdName && n->a->s && !n->a->symbol &&
	    n->children_len >= 1 &&
	    (strcmp(n->a->s, "ranged") == 0 || strcmp(n->a->s, "ptr") == 0))
		return expr_param_bits(st, n->children[0]);
	if (n->kind == NdSubrange)
		return expr_param_bits(st, n->a);
	return 0;
}

static void
report_stack_escape(Compiler* c, Span sp, Symbol* s) {
	if (s == NULL) {
		error_at(c, sp, "stack address escapes this function");
		return;
	}
	if (s->storage == StParam)
		error_at(c, sp, "address of parameter '%s' escapes this function",
			 s->name ? s->name : "");
	else if (s->type && is_array(s->type))
		error_at(c, sp, "address of local array '%s' escapes this function",
			 s->name ? s->name : "");
	else
		error_at(c, sp, "address of local '%s' escapes this function",
			 s->name ? s->name : "");
}

static void
report_may_stack_escape(Compiler* c, EscapeState* st, Node* n) {
	Symbol* s;

	if (n == NULL || !expr_may_stack(st, n))
		return;
	s = expr_stack_addr_root(n);
	if (s) {
		report_stack_escape(c, n->span, s);
		return;
	}
	if (n->kind == NdName && n->symbol)
		error_at(c, n->span,
			 "stack address held in '%s' escapes this function",
			 n->symbol->name ? n->symbol->name : "");
	else
		error_at(c, n->span, "stack address escapes this function");
}

static int
lhs_is_escape_dest(Node* lhs) {
	Type* bt;

	if (lhs == NULL)
		return 0;
	if (lhs->kind == NdName && lhs->symbol && lhs->symbol->kind == SkVar) {
		if (lhs->symbol->storage == StLocal || lhs->symbol->storage == StParam)
			return 0;
		return 1;
	}
	if (lhs->kind == NdDeref)
		return 1;
	if (lhs->kind == NdIndex) {
		bt = lhs->a ? lhs->a->type : NULL;
		if (bt && is_ptr(bt))
			return 1;
		return lhs_is_escape_dest(lhs->a);
	}
	if (lhs->kind == NdDot || lhs->kind == NdArrow) {
		bt = lhs->a ? lhs->a->type : NULL;
		if (bt && is_ptr(bt))
			return 1;
		return lhs_is_escape_dest(lhs->a);
	}
	return 0;
}

/* Pointer expression a store writes through (*p, p.field, p[i]), or NULL. */
static Node*
lhs_store_through(Node* lhs) {
	Type* bt;

	if (lhs == NULL)
		return NULL;
	if (lhs->kind == NdDeref)
		return lhs->a;
	if (lhs->kind == NdIndex) {
		bt = lhs->a ? lhs->a->type : NULL;
		if (bt && is_ptr(bt))
			return lhs->a;
		return lhs_store_through(lhs->a);
	}
	if (lhs->kind == NdDot || lhs->kind == NdArrow) {
		bt = lhs->a ? lhs->a->type : NULL;
		if (bt && is_ptr(bt))
			return lhs->a;
		return lhs_store_through(lhs->a);
	}
	return NULL;
}

/* Local/param aggregate or scalar that can be stack-tainted by an assignment. */
static Symbol*
lhs_taint_target(Node* lhs) {
	Type* bt;

	if (lhs == NULL)
		return NULL;
	if (lhs->kind == NdName && lhs->symbol &&
	    (lhs->symbol->storage == StLocal || lhs->symbol->storage == StParam))
		return lhs->symbol;
	if (lhs->kind == NdDot || lhs->kind == NdArrow) {
		bt = lhs->a ? lhs->a->type : NULL;
		if (bt && is_ptr(bt))
			return NULL;
		return lhs_taint_target(lhs->a);
	}
	if (lhs->kind == NdIndex) {
		bt = lhs->a ? lhs->a->type : NULL;
		if (bt && is_ptr(bt))
			return NULL;
		return lhs_taint_target(lhs->a);
	}
	return NULL;
}

static void
mark_param_flag(EscapeState* st, char* flags, int i) {
	if (st->fnty == NULL || flags == NULL)
		return;
	if (i < 0 || i >= st->fnty->params_len)
		return;
	if (flags[i])
		return;
	flags[i] = 1;
	st->changed = 1;
}

/* Stash past the call (global / *out / [[retains]] callee) — not mere return. */
static void
note_retains_param_bits(EscapeState* st, uint64_t bits) {
	int i;

	if (!st->infer || bits == 0 || st->fnty == NULL)
		return;
	for (i = 0; i < st->nparams && i < MaxParams; i++) {
		if (bits & ((uint64_t)1 << i))
			mark_param_flag(st, st->fnty->param_retains, i);
	}
}

static void
note_returned_param_bits(EscapeState* st, uint64_t bits) {
	int i;

	if (!st->infer || bits == 0 || st->fnty == NULL)
		return;
	for (i = 0; i < st->nparams && i < MaxParams; i++) {
		if (bits & ((uint64_t)1 << i))
			mark_param_flag(st, st->fnty->param_returned, i);
	}
}

static int
param_is_retains(Type* ft, int i) {
	if (ft == NULL || ft->param_retains == NULL || i < 0 || i >= ft->params_len)
		return 0;
	return ft->param_retains[i] != 0;
}

static Type*
call_func_type(Node* call) {
	Type* t;

	if (call == NULL || call->kind != NdCall || call->a == NULL)
		return NULL;
	if (call->a->kind == NdName && call->a->symbol && call->a->symbol->type &&
	    is_func(call->a->symbol->type))
		return call->a->symbol->type;
	t = call->a->type;
	if (t && is_ptr(t))
		t = t->base;
	if (t && is_func(t))
		return t;
	return NULL;
}

/*
 * %C function that may carry [[retains]] / return-flow (local body or
 * imported export stub). C headers and fn-ptrs are excluded.
 */
static int
is_mc_retains_callee(Symbol* s) {
	if (s == NULL || s->kind != SkFunc || s->type == NULL || !is_func(s->type))
		return 0;
	if (s->header)
		return 0;
	return 1;
}

static void
esc_add_region_deps_from_expr(EscapeState* st, Symbol* dst, Node* src) {
	int i, j, lim, si, n;
	Type* ft;
	Symbol* r;

	if (dst == NULL || src == NULL)
		return;
	if (src->kind == NdCast || src->kind == NdComma) {
		esc_add_region_deps_from_expr(st, dst,
					      src->kind == NdComma ? src->b : src->a);
		return;
	}
	if (src->kind == NdCond) {
		esc_add_region_deps_from_expr(st, dst, src->b);
		esc_add_region_deps_from_expr(st, dst, src->c);
		return;
	}
	if (src->kind == NdTupleLit) {
		for (i = 0; i < src->children_len; i++)
			esc_add_region_deps_from_expr(st, dst, src->children[i]);
		return;
	}
	if (src->kind == NdName && src->symbol) {
		si = esc_find(st, src->symbol);
		if (si < 0)
			return;
		n = st->region_dep_n[si];
		for (j = 0; j < n; j++)
			esc_add_region_dep(st, dst, st->region_deps[si * MaxRegionDeps + j]);
		return;
	}
	if ((src->kind == NdDot || src->kind == NdArrow || src->kind == NdIndex) &&
	    src->a) {
		if (!type_may_carry_ptr(src->type) && !is_array(src->type))
			return;
		esc_add_region_deps_from_expr(st, dst, src->a);
		return;
	}
	if (src->kind == NdDot && src->is_synth && src->s && strcmp(src->s, "ptr") == 0) {
		esc_add_region_deps_from_expr(st, dst, src->a);
		return;
	}
	if (src->kind == NdSubrange) {
		esc_add_region_deps_from_expr(st, dst, src->a);
		return;
	}
	if (src->kind == NdCall) {
		if (src->a && src->a->kind == NdName && src->a->s && !src->a->symbol &&
		    src->children_len >= 1 &&
		    (strcmp(src->a->s, "ptr") == 0 || strcmp(src->a->s, "ranged") == 0)) {
			esc_add_region_deps_from_expr(st, dst, src->children[0]);
			return;
		}
		if ((call_name_is(src, "__modc_pool_cstr") ||
		     call_name_is(src, "__modc_pool_alloc_ptr") ||
		     call_name_is(src, "__modc_pool_alloc_slice")) &&
		    src->children_len >= 1) {
			esc_add_region_dep(st, dst, consume_target_sym(src->children[0]));
			return;
		}
		ft = call_func_type(src);
		if (ft == NULL)
			return;
		lim = src->children_len;
		if (lim > ft->params_len)
			lim = ft->params_len;
		for (j = 0; j < lim; j++) {
			if (param_is_region(ft, j)) {
				r = consume_target_sym(src->children[j]);
				esc_add_region_dep(st, dst, r);
			}
			if (ft->param_returned && ft->param_returned[j])
				esc_add_region_deps_from_expr(st, dst, src->children[j]);
		}
	}
}

static void
esc_propagate_to(EscapeState* st, Symbol* dst, Node* src) {
	int i;

	if (dst == NULL || src == NULL)
		return;
	if (!type_may_carry_ptr(dst->type) && !is_array(dst->type))
		return;
	i = esc_index(st, dst);
	if (i < 0)
		return;
	if (expr_may_stack(st, src))
		st->stack_taint[i] = 1;
	else if (src->kind == NdName || expr_stack_addr_root(src) == NULL)
		st->stack_taint[i] = 0; /* strong update on clear */
	if (expr_may_dead(st, src))
		st->dead_taint[i] = 1;
	else
		st->dead_taint[i] = 0;
	esc_clear_region_deps(st, i);
	esc_add_region_deps_from_expr(st, dst, src);
	esc_add_param_bits(st, dst, expr_param_bits(st, src));
}

static void
esc_apply_consumes(EscapeState* st, Node* call) {
	int i;
	Symbol *pool, *slice;

	if (call == NULL || call->kind != NdCall)
		return;
	for (i = 0; i < call->children_len; i++) {
		if (!call_arg_consumes(call, i))
			continue;
		/* reset: invalidate views only; pool stays reusable. */
		if (i == 0 && call_is_pool_reset(call))
			esc_invalidate_region_views(st, consume_target_sym(call->children[i]));
		else
			esc_invalidate_region(st, consume_target_sym(call->children[i]));
	}
	/* Mutating pool ops: destination slice header is tied to the pool.
	 * Returning mints (alloc_ptr / alloc_slice / z) attach deps via assign. */
	if (call_is_pool_region_mint(call) && call->children_len >= 2 &&
	    !call_name_is(call, "__modc_pool_cstr") &&
	    !call_name_is(call, "__modc_pool_alloc_ptr") &&
	    !call_name_is(call, "__modc_pool_alloc_slice")) {
		pool = consume_target_sym(call->children[0]);
		slice = consume_target_sym(call->children[1]);
		esc_add_region_dep(st, slice, pool);
		/* In-place rewrite of *dst: header is live again. */
		if (slice) {
			i = esc_find(st, slice);
			if (i >= 0)
				st->dead_taint[i] = 0;
		}
	}
}

static void
esc_check_call(Compiler* c, EscapeState* st, Node* call) {
	Type* ft;
	Symbol* callee;
	Node* arg;
	int i, n;
	const char* pname;
	const char* fname;

	ft = call_func_type(call);
	if (ft == NULL)
		return;
	callee = (call->a && call->a->kind == NdName) ? call->a->symbol : NULL;
	/* C / headers / fn-ptrs: do not assume retain. Import stubs OK. */
	if (callee == NULL || !is_mc_retains_callee(callee))
		return;
	n = call->children_len;
	if (n > ft->params_len)
		n = ft->params_len;
	for (i = 0; i < n; i++) {
		arg = call->children[i];
		if (!param_is_retains(ft, i))
			continue;
		note_retains_param_bits(st, expr_param_bits(st, arg));
		if (st->diag && expr_may_stack(st, arg)) {
			pname = (ft->param_names && ft->param_names[i]) ? ft->param_names[i]
								       : "";
			fname = callee->name ? callee->name : "";
			if (pname[0])
				error_at(c, arg->span,
					 "passing stack address to [[retains]] parameter '%s' of '%s'",
					 pname, fname);
			else
				error_at(c, arg->span,
					 "passing stack address to [[retains]] parameter of '%s'",
					 fname);
		}
	}
}

static void esc_walk_stmt(Compiler* c, EscapeState* st, Node* n);

/* True if every path through n leaves the enclosing function (return/goto). */
static int
stmt_always_exits(Node* n) {
	int i;

	if (n == NULL)
		return 0;
	if (n->kind == NdReturn || n->kind == NdGoto)
		return 1;
	if (n->kind == NdBlock) {
		for (i = 0; i < n->children_len; i++) {
			if (stmt_always_exits(n->children[i]))
				return 1;
		}
		return 0;
	}
	if (n->kind == NdIf) {
		if (n->c == NULL)
			return 0;
		return stmt_always_exits(n->b) && stmt_always_exits(n->c);
	}
	return 0;
}

static unsigned char*
esc_snapshot_dead(EscapeState* st, int* out_n) {
	unsigned char* copy;
	int n;

	n = st->n;
	*out_n = n;
	if (n <= 0)
		return NULL;
	copy = xmalloc((size_t)n);
	memcpy(copy, st->dead_taint, (size_t)n);
	return copy;
}

static void
esc_restore_dead(EscapeState* st, unsigned char* snap, int snap_n) {
	int i;

	if (snap_n > 0 && snap != NULL)
		memcpy(st->dead_taint, snap, (size_t)snap_n);
	for (i = snap_n; i < st->n; i++)
		st->dead_taint[i] = 0;
}

static void
esc_union_dead(EscapeState* st, unsigned char* a, int an, unsigned char* b, int bn) {
	int i, n;

	n = st->n;
	for (i = 0; i < n; i++) {
		unsigned char av = (i < an && a) ? a[i] : 0;
		unsigned char bv = (i < bn && b) ? b[i] : 0;
		st->dead_taint[i] = (unsigned char)(av | bv);
	}
}

static void
esc_on_escape_value(Compiler* c, EscapeState* st, Node* n, int via_return) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NdTupleLit) {
		for (i = 0; i < n->children_len; i++)
			esc_on_escape_value(c, st, n->children[i], via_return);
		return;
	}
	if (via_return)
		note_returned_param_bits(st, expr_param_bits(st, n));
	else
		note_retains_param_bits(st, expr_param_bits(st, n));
	if (st->diag) {
		report_may_stack_escape(c, st, n);
		if (expr_may_dead(st, n))
			report_defer_escape(c, st, n);
	}
}

static void
esc_walk_init(Compiler* c, EscapeState* st, Initializer* in, Symbol* dest) {
	int i;

	if (in == NULL)
		return;
	if (in->expr) {
		if (dest && (dest->storage == StLocal || dest->storage == StParam))
			esc_propagate_to(st, dest, in->expr);
		else if (dest && dest->storage != StLocal && dest->storage != StParam)
			esc_on_escape_value(c, st, in->expr, 0);
	}
	for (i = 0; i < in->items_len; i++)
		esc_walk_init(c, st, &in->items[i], dest);
}

static void
esc_walk_stmt(Compiler* c, EscapeState* st, Node* n) {
	Symbol* dst;
	int i;

	if (n == NULL)
		return;
	if (n->kind == NdReturn) {
		esc_on_escape_value(c, st, n->a, 1);
		return;
	}
	if (n->kind == NdAssign) {
		/* Reassignment to a plain local/param is not a use of its old value. */
		if (!(n->a->kind == NdName && lhs_taint_target(n->a)))
			esc_check_expr_use(c, st, n->a);
		esc_check_expr_use(c, st, n->b);
		if (lhs_is_escape_dest(n->a)) {
			/* Write-back into the object reached through param P is not
			 * retaining P; only bits that escape into foreign storage count. */
			uint64_t rhs_bits, through_bits;

			rhs_bits = expr_param_bits(st, n->b);
			through_bits = expr_param_bits(st, lhs_store_through(n->a));
			note_retains_param_bits(st, rhs_bits & ~through_bits);
			if (st->diag) {
				report_may_stack_escape(c, st, n->b);
				if (expr_may_dead(st, n->b))
					report_defer_escape(c, st, n->b);
			}
		}
		dst = lhs_taint_target(n->a);
		if (dst)
			esc_propagate_to(st, dst, n->b);
		esc_walk_stmt(c, st, n->a);
		esc_walk_stmt(c, st, n->b);
		return;
	}
	if (n->kind == NdDecl) {
		if (n->init && n->init->expr) {
			esc_check_expr_use(c, st, n->init->expr);
			if (n->symbol)
				esc_propagate_to(st, n->symbol, n->init->expr);
		} else if (n->init)
			esc_walk_init(c, st, n->init, n->symbol);
		esc_walk_stmt(c, st, n->a);
		return;
	}
	if (n->kind == NdDefer) {
		defer_register_consumes(st, n->a);
		return;
	}
	if (n->kind == NdCall) {
		esc_check_call(c, st, n);
		for (i = 0; i < n->children_len; i++)
			esc_check_expr_use(c, st, n->children[i]);
		esc_walk_stmt(c, st, n->a);
		for (i = 0; i < n->children_len; i++)
			esc_walk_stmt(c, st, n->children[i]);
		esc_apply_consumes(st, n);
		return;
	}
	switch (n->kind) {
	case NdBlock:
		if (st->scope_depth < MaxDeferDepth) {
			st->kill_n[st->scope_depth] = 0;
			st->scope_depth++;
			for (i = 0; i < n->children_len; i++)
				esc_walk_stmt(c, st, n->children[i]);
			st->scope_depth--;
			st->kill_n[st->scope_depth] = 0;
		} else {
			for (i = 0; i < n->children_len; i++)
				esc_walk_stmt(c, st, n->children[i]);
		}
		break;
	case NdIf: {
		/* Path-sensitive dead_taint: free/consumes in a returning branch
		 * must not poison the fall-through path (common fail/return ladders). */
		unsigned char *save = NULL, *then_dead = NULL, *else_dead = NULL;
		int save_n = 0, then_n = 0, else_n = 0;
		int then_exits, else_exits;

		esc_check_expr_use(c, st, n->a);
		esc_walk_stmt(c, st, n->a);
		save = esc_snapshot_dead(st, &save_n);
		esc_walk_stmt(c, st, n->b);
		then_exits = stmt_always_exits(n->b);
		then_dead = esc_snapshot_dead(st, &then_n);
		esc_restore_dead(st, save, save_n);
		esc_walk_stmt(c, st, n->c);
		else_exits = stmt_always_exits(n->c);
		else_dead = esc_snapshot_dead(st, &else_n);
		if (then_exits && (else_exits || n->c == NULL)) {
			/* then exits; else exits or absent → fall-through is pre-if */
			esc_restore_dead(st, save, save_n);
		} else if (then_exits) {
			/* only else falls through */
			esc_restore_dead(st, else_dead, else_n);
		} else if (else_exits) {
			/* only then falls through */
			esc_restore_dead(st, then_dead, then_n);
		} else if (n->c == NULL) {
			/* no else: join pre-if with then */
			esc_union_dead(st, save, save_n, then_dead, then_n);
		} else {
			esc_union_dead(st, then_dead, then_n, else_dead, else_n);
		}
		free(save);
		free(then_dead);
		free(else_dead);
		break;
	}
	case NdWhile:
	case NdDo:
	case NdFor:
		/* Conditions / for-iter expressions may read consumed pointers. */
		esc_check_expr_use(c, st, n->a);
		esc_check_expr_use(c, st, n->b);
		esc_check_expr_use(c, st, n->c);
		esc_walk_stmt(c, st, n->a);
		esc_walk_stmt(c, st, n->b);
		esc_walk_stmt(c, st, n->c);
		for (i = 0; i < n->children_len; i++)
			esc_walk_stmt(c, st, n->children[i]);
		break;
	case NdSwitch:
	case NdCase:
	case NdDefault:
	case NdLabel:
		esc_check_expr_use(c, st, n->a);
		esc_walk_stmt(c, st, n->a);
		esc_walk_stmt(c, st, n->b);
		esc_walk_stmt(c, st, n->c);
		for (i = 0; i < n->children_len; i++)
			esc_walk_stmt(c, st, n->children[i]);
		break;
	default:
		esc_walk_stmt(c, st, n->a);
		esc_walk_stmt(c, st, n->b);
		esc_walk_stmt(c, st, n->c);
		for (i = 0; i < n->children_len; i++)
			esc_walk_stmt(c, st, n->children[i]);
		if (n->init)
			esc_walk_init(c, st, n->init, NULL);
		break;
	}
}

static int
escape_analyze_func(Compiler* c, Node* fn, int infer, int diag) {
	EscapeState st = { 0 };
	Symbol *owner, *s;
	Type* ty;
	int i;

	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL || fn->symbol == NULL)
		return 0;
	if (!user_source(c, fn->span))
		return 0;
	owner = fn->symbol;
	ty = owner->type;
	st.fnty = ty;
	st.infer = infer;
	st.diag = diag;
	st.nparams = 0;
	if (ty && is_func(ty)) {
		for (s = c->syms.symbols; s; s = s->next) {
			if (s->owner != owner || s->kind != SkVar || s->storage != StParam)
				continue;
			for (i = 0; i < ty->params_len && i < MaxParams; i++) {
				if (ty->param_names && ty->param_names[i] && s->name &&
				    strcmp(ty->param_names[i], s->name) == 0) {
					st.params[i] = s;
					if (i + 1 > st.nparams)
						st.nparams = i + 1;
					if (type_may_carry_ptr(s->type))
						esc_add_param_bits(&st, s, (uint64_t)1 << i);
					break;
				}
			}
		}
	}
	esc_walk_stmt(c, &st, fn->a);
	i = st.changed;
	esc_release(&st);
	return i;
}

static void
check_stack_escape_unit(Compiler* c) {
	int i, iter, changed;

	/* Fixpoint: infer [[retains]] from .mc bodies / calls. */
	for (iter = 0; iter < 64; iter++) {
		changed = 0;
		for (i = 0; i < c->unit.funcs_len; i++)
			changed |= escape_analyze_func(c, c->unit.funcs[i], 1, 0);
		if (!changed)
			break;
	}
	for (i = 0; i < c->unit.funcs_len; i++)
		escape_analyze_func(c, c->unit.funcs[i], 0, 1);
}

static int global_pointer_constant(Compiler* c, Node* n);

// Recognize the lvalue portion of an address constant.
static int
global_const_lvalue(Compiler* c, Node* n) {
	int64_t index;

	if (n == NULL)
		return 0;
	if (n->kind == NdName && n->symbol &&
	    (n->symbol->kind == SkFunc ||
	     (n->symbol->kind == SkVar && n->symbol->storage != StLocal &&
	      n->symbol->storage != StParam)))
		return 1;
	if (n->kind == NdDeref)
		return global_pointer_constant(c, n->a);
	if (n->kind == NdDot)
		return global_const_lvalue(c, n->a);
	if (n->kind == NdArrow)
		return global_pointer_constant(c, n->a);
	if (n->kind == NdIndex)
		return global_pointer_constant(c, n->a) &&
		       eval_const(c, n->b, &index);
	return 0;
}

// Recognize null, string, function, array, and address-plus-constant pointers.
static int
global_pointer_constant(Compiler* c, Node* n) {
	int64_t v;

	if (n == NULL)
		return 0;
	if (n->kind == NdCast)
		return global_pointer_constant(c, n->a);
	if (n->kind == NdStr)
		return 1;
	if (eval_const(c, n, &v))
		return v == 0;
	if (n->kind == NdAddr)
		return global_const_lvalue(c, n->a);
	if (n->kind == NdName && n->symbol &&
	    (n->symbol->kind == SkFunc || is_array(n->type)))
		return 1;
	if (n->kind == NdBin && (n->op == PnPlus || n->op == PnMinus)) {
		if (global_pointer_constant(c, n->a) && eval_const(c, n->b, &v))
			return 1;
		if (n->op == PnPlus && eval_const(c, n->a, &v) &&
		    global_pointer_constant(c, n->b))
			return 1;
	}
	return 0;
}

/* ranged("…") from string→const char[..] conversion; arg is the NdStr. */
static Node*
ranged_string_lit_arg(Node* n) {
	if (n == NULL || n->kind != NdCall || n->a == NULL || n->a->kind != NdName)
		return NULL;
	if (n->a->s == NULL || strcmp(n->a->s, "ranged") != 0 || n->a->symbol)
		return NULL;
	if (n->children_len != 1 || n->children[0] == NULL ||
	    n->children[0]->kind != NdStr)
		return NULL;
	return n->children[0];
}

static void
check_global_init(Compiler* c, Initializer* in) {
	double fv;
	int64_t iv;
	int i, ok;

	if (in == NULL)
		return;
	if (in->expr) {
		if (in->expr->kind == NdStr)
			ok = 1;
		else if (ranged_string_lit_arg(in->expr))
			ok = 1;
		else if ((in->expr->type && is_ptr(in->expr->type)) ||
			 (in->expr->kind == NdName && in->expr->symbol &&
			  (in->expr->symbol->kind == SkFunc ||
			   is_array(in->expr->type))))
			ok = global_pointer_constant(c, in->expr);
		else if (in->expr->type &&
			 (in->expr->type->kind == TyFloat ||
			  in->expr->type->kind == TyDouble))
			ok = eval_float_const(c, in->expr, &fv);
		else
			ok = eval_const(c, in->expr, &iv);
		if (!ok)
			error_at(c, in->expr->span,
				 "global initializer is not a constant expression");
	}
	for (i = 0; i < in->items_len; i++)
		check_global_init(c, &in->items[i]);
}

static void
check_global_initializers(Compiler* c) {
	int i;
	Node* d;

	for (i = 0; i < c->unit.globals_len; i++) {
		d = c->unit.globals[i];
		if (d && d->kind == NdDecl && d->init)
			check_global_init(c, d->init);
	}
}

static int
switch_case_count(Node* n) {
	int i, total;

	if (n == NULL || n->kind == NdSwitch)
		return 0;
	if (n->kind == NdCase)
		return 1;
	total = switch_case_count(n->a) + switch_case_count(n->b) +
		switch_case_count(n->c);
	for (i = 0; i < n->children_len; i++)
		total += switch_case_count(n->children[i]);
	return total;
}

// Enforce fixed emitter capacities before any QBE is written.
static void
check_emitter_limits_node(Compiler* c, Node* n, int control_depth,
			  int defer_depth) {
	int i, ndefers, ncases;

	if (n == NULL)
		return;
	if (n->kind == NdWhile || n->kind == NdDo || n->kind == NdFor ||
	    n->kind == NdSwitch) {
		control_depth++;
		if (control_depth == MaxControlDepth + 1)
			error_at(c, n->span,
				 "control-flow nesting exceeds implementation limit of %d",
				 MaxControlDepth);
	}
	if (n->kind == NdBlock) {
		defer_depth++;
		if (defer_depth == MaxDeferDepth + 1)
			error_at(c, n->span,
				 "block nesting exceeds implementation limit of %d",
				 MaxDeferDepth);
		ndefers = 0;
		for (i = 0; i < n->children_len; i++)
			if (n->children[i] && n->children[i]->kind == NdDefer)
				ndefers++;
		if (ndefers > MaxDefersPerScope)
			error_at(c, n->span,
				 "scope has %d defers; implementation limit is %d",
				 ndefers, MaxDefersPerScope);
	}
	if (n->kind == NdSwitch) {
		ncases = switch_case_count(n->b);
		if (ncases > MaxSwitchCases)
			error_at(c, n->span,
				 "switch has %d cases; implementation limit is %d",
				 ncases, MaxSwitchCases);
	}
	check_emitter_limits_node(c, n->a, control_depth, defer_depth);
	check_emitter_limits_node(c, n->b, control_depth, defer_depth);
	check_emitter_limits_node(c, n->c, control_depth, defer_depth);
	for (i = 0; i < n->children_len; i++)
		check_emitter_limits_node(c, n->children[i], control_depth,
					  defer_depth);
}

static void
check_emitter_limits(Compiler* c) {
	int i;

	for (i = 0; i < c->unit.funcs_len; i++)
		check_emitter_limits_node(c, c->unit.funcs[i], 0, 0);
}

/* ---- type_check_unit ---- */

// True if t (or any nested component) is a package-private type.
// Self-referential pointer layouts (T* / T** fields) must not recurse forever.
static int
type_mentions_pkg_private_rec(Type* t, Type** stack, int depth) {
	int i;
	Field* f;

	if (t == NULL)
		return 0;
	for (i = 0; i < depth; i++) {
		if (stack[i] == t)
			return 0;
	}
	if (t->pkg_private)
		return 1;
	if (depth >= 64)
		return 0;
	stack[depth] = t;
	if (t->base && type_mentions_pkg_private_rec(t->base, stack, depth + 1))
		return 1;
	if (t->kind == TyFunc) {
		for (i = 0; i < t->params_len; i++) {
			if (type_mentions_pkg_private_rec(t->params[i], stack, depth + 1))
				return 1;
		}
	}
	if ((t->kind == TyStruct || t->kind == TyUnion) && t->fields) {
		for (f = t->fields; f; f = f->next) {
			if (type_mentions_pkg_private_rec(f->type, stack, depth + 1))
				return 1;
		}
	}
	return 0;
}

static int
type_mentions_pkg_private(Type* t) {
	Type* stack[64];

	return type_mentions_pkg_private_rec(t, stack, 0);
}

// Public API must not mention package-private types.
static void
check_pkg_private_leaks(Compiler* c) {
	Symbol* s;

	for (s = c->syms.symbols; s; s = s->next) {
		if (s->dead || s->hidden || s->header || s->block != 0)
			continue;
		if (s->storage == StStatic || s->storage == StLocal || s->storage == StParam)
			continue;
		if (!user_source(c, s->span))
			continue;
		if (s->kind != SkFunc && s->kind != SkVar && s->kind != SkTypedef && s->kind != SkTag)
			continue;
		if (!type_mentions_pkg_private(s->type))
			continue;
		error_at(c, s->span,
			 "public %s '%s' must not use a static (package-private) type",
			 s->kind == SkFunc	 ? "function"
			 : s->kind == SkVar	 ? "variable"
			 : s->kind == SkTypedef ? "typedef"
						 : "type",
			 s->name ? s->name : "");
	}
}

// Whole-unit diagnostics after parse+type_expr. Each check walks function
// bodies independently; order is “safety first, then hygiene.”
void type_check_unit(Compiler* c) {
	int i;

	check_global_initializers(c);
	check_emitter_limits(c);
	check_pkg_private_leaks(c);
	/* parse_only: no bodies — skip CFG / body walks. */
	if (c->opt.parse_only)
		return;
	for (i = 0; i < c->unit.funcs_len; i++) {
		check_dead_init_func(c, c->unit.funcs[i]);
		check_falloff_func(c, c->unit.funcs[i]);
		check_enum_exhaust_func(c, c->unit.funcs[i]);
		check_goto_over_decl(c, c->unit.funcs[i]);
		check_unseq_func(c, c->unit.funcs[i]);
		check_discard_nodiscard_func(c, c->unit.funcs[i]);
		check_method_receiver_func(c, c->unit.funcs[i]);
		check_unused_func(c, c->unit.funcs[i]);
	}
	check_stack_escape_unit(c);
}
