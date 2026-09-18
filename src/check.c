/*
 * Post-parse checks: uninit, fall-off, exhaust, unused, …
 *
 * Compilation pipeline: lex → pp → parse → [type check] → emit → QBE
 * Entry: type_check_unit. Runs after parse+type_expr on a finished AST.
 */
#include "ast.h"
#include <ctype.h>

/* ---- definite assignment (da_) ---- */
/*
 * Prefix da_ — definite assignment (uninit locals).
 * Tracks which locals are assigned on every path; merges with intersect at
 * joins; loops keep pre-loop state (body may not run). Skipped when the
 * function contains goto (non-linear CFG).
 */
enum { DAMax = 128 };

typedef struct DAState DAState;
struct DAState {
	Symbol* symbols[DAMax];
	unsigned char assigned[DAMax];
	int n;
};

// True if the subtree contains a goto; skips uninit analysis when control is non-linear.
static int
da_has_goto(Node* n) {
	int i;

	if (n == NULL)
		return 0;
	if (n->kind == NdGoto)
		return 1;
	if (da_has_goto(n->a) || da_has_goto(n->b) || da_has_goto(n->c))
		return 1;
	for (i = 0; i < n->children_len; i++)
		if (da_has_goto(n->children[i]))
			return 1;
	return 0;
}

// Locals with scalar type can be tracked for definite-assignment.
static int
da_trackable(Symbol* s) {
	if (s == NULL || s->kind != SkVar || s->storage != StLocal)
		return 0;
	return is_scalar(s->type);
}

// Index of s in the definite-assignment set, or -1.
static int
da_find(DAState* st, Symbol* s) {
	int i;

	for (i = 0; i < st->n; i++)
		if (st->symbols[i] == s)
			return i;
	return -1;
}

// Record a trackable local and whether it is definitely assigned on this path.
static void
da_add(DAState* st, Symbol* s, int assigned) {
	int i;

	if (!da_trackable(s))
		return;
	i = da_find(st, s);
	if (i >= 0) {
		st->assigned[i] = (unsigned char)assigned;
		return;
	}
	if (st->n >= DAMax)
		return;
	st->symbols[st->n] = s;
	st->assigned[st->n] = (unsigned char)assigned;
	st->n++;
}

// Set assignedness for a local already in the definite-assignment set (or add it).
static void
da_set(DAState* st, Symbol* s, int assigned) {
	int i;

	i = da_find(st, s);
	if (i >= 0)
		st->assigned[i] = (unsigned char)assigned;
	else
		da_add(st, s, assigned);
}

// Copy definite-assignment state.
static void
da_copy(DAState* dst, DAState* src) {
	*dst = *src;
}

// Merge two paths: a symbol is assigned only if assigned on both.
static void
da_intersect(DAState* dst, DAState* a, DAState* b) {
	int i, j;

	da_copy(dst, a);
	for (i = 0; i < dst->n; i++) {
		j = da_find(b, dst->symbols[i]);
		if (j < 0 || !b->assigned[j])
			dst->assigned[i] = 0;
	}
}

static void da_expr(Compiler* c, Node* n, DAState* st, int as_lval);
static void da_stmt(Compiler* c, Node* n, DAState* st);
static void da_init(Compiler* c, Initializer* in, DAState* st);

// Error on use of a scalar local that is not definitely assigned on this path.
static void
da_use(Compiler* c, Node* n, DAState* st) {
	int i;

	if (n == NULL || n->kind != NdName || n->symbol == NULL)
		return;
	i = da_find(st, n->symbol);
	if (i < 0 || st->assigned[i])
		return;
	if (user_source(c, n->span))
		error_at(c, n->span, "variable '%s' is used uninitialized", n->symbol->name);
	/* avoid repeat noise on the same name along this path */
	st->assigned[i] = 1;
}

// Mark the leftmost name of an assignment LHS as definitely assigned.
static void
da_mark_lhs(DAState* st, Node* n) {
	if (n == NULL)
		return;
	if (n->kind == NdName) {
		da_set(st, n->symbol, 1);
		return;
	}
	if (n->kind == NdComma)
		da_mark_lhs(st, n->b);
}

// Walk initializer expressions for definite-assignment uses.
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

// Propagate definite-assignment through expressions; models short-circuit and branches.
static void
da_expr(Compiler* c, Node* n, DAState* st, int as_lval) {
	DAState a, b;
	int i;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NdLit:
	case NdStr:
	case NdSizeofT:
	case NdSkip:
		return;
	case NdSizeof:
		/* operand not evaluated */
		return;
	case NdName:
		if (!as_lval)
			da_use(c, n, st);
		return;
	case NdAddr:
		da_expr(c, n->a, st, 1);
		/* escaping address: stop requiring prior init */
		if (n->a && n->a->kind == NdName)
			da_set(st, n->a->symbol, 1);
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
			/* RHS may not run — drop its assignments */
			(void)a;
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
		da_intersect(st, &a, &b);
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
		/* (void)x; silences unused and is not an uninit use */
		if (n->type && n->type->kind == TyVoid && n->a && n->a->kind == NdName) {
			da_set(st, n->a->symbol, 1);
			return;
		}
		da_expr(c, n->a, st, 0);
		return;
	case NdIndex:
	case NdSubrange:
	case NdDot:
	case NdArrow:
	case NdDeref:
	case NdTupleLit:
		da_expr(c, n->a, st, as_lval && (n->kind == NdDot || n->kind == NdArrow || n->kind == NdIndex || n->kind == NdDeref));
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

// For-loop init may be a declaration or an expression.
static void
for_init_da(Compiler* c, Node* init, DAState* st) {
	if (init && init->kind == NdDecl)
		da_stmt(c, init, st);
	else
		da_expr(c, init, st, 0);
}

// Propagate definite-assignment through statements; joins intersect at branches.
static void
da_stmt(Compiler* c, Node* n, DAState* st) {
	DAState a, b;
	int i, has_init;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NdDecl:
		da_init(c, n->init, st);
		has_init = n->init != NULL && (n->init->expr != NULL || n->init->items_len > 0 || n->init->is_list);
		if (n->symbol && da_trackable(n->symbol))
			da_add(st, n->symbol, has_init);
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
		da_intersect(st, &a, &b);
		return;
	case NdWhile:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_stmt(c, n->b, &a);
		/* body may not run — keep pre-loop assignments only */
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
		return;
	case NdSwitch:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_stmt(c, n->b, &a);
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
		return;
	case NdBreak:
	case NdContinue:
	case NdGoto:
	case NdFallthrough:
	case NdSkip:
		return;
	default:
		/* expression statement */
		da_expr(c, n, st, 0);
		return;
	}
}

// Per-function pass: diagnose reads of uninitialized scalar locals.
static void
check_uninit_func(Compiler* c, Node* fn) {
	DAState st;

	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	if (da_has_goto(fn->a))
		return;
	memset(&st, 0, sizeof(st));
	da_stmt(c, fn->a, &st);
}

static int stmt_returns(Compiler* c, Node* n);

// True if a break appears outside an inner loop/switch (conservative for falloff).
static int
node_has_break(Node* n) {
	int i;

	if (n == NULL)
		return 0;
	if (n->kind == NdBreak)
		return 1;
	/* nested loop/switch break targets inner, not outer — still conservative */
	if (n->kind == NdWhile || n->kind == NdFor || n->kind == NdDo || n->kind == NdSwitch)
		return 0;
	if (node_has_break(n->a) || node_has_break(n->b) || node_has_break(n->c))
		return 1;
	for (i = 0; i < n->children_len; i++)
		if (node_has_break(n->children[i]))
			return 1;
	return 0;
}

// True if n is a constant expression evaluating to non-zero.
static int
is_const_nonzero(Compiler* c, Node* n) {
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
static int
swcases_has(SwitchCases* sc, int64_t val) {
	int i;

	for (i = 0; i < sc->n; i++)
		if (sc->v[i] == val)
			return 1;
	return 0;
}

// Add one case value; set overflow when the table is full.
static int
swcases_add(SwitchCases* sc, int64_t val) {
	if (sc->has_default || sc->overflow)
		return 0;
	if (swcases_has(sc, val))
		return 0;
	if (sc->n >= SwitchCaseMax) {
		sc->overflow = 1;
		return -1;
	}
	sc->v[sc->n++] = val;
	return 0;
}

// Expand an inclusive case range into individual values (bounded work).
static int
swcases_add_range(SwitchCases* sc, int64_t lo, int64_t hi) {
	int64_t v;

	if (hi < lo)
		return 0;
	if (hi - lo > SwitchRangeMax) {
		sc->overflow = 1;
		return -1;
	}
	for (v = lo; v <= hi; v++)
		if (swcases_add(sc, v) < 0)
			return -1;
	return 0;
}

// Recursively gather case labels from a switch body (not nested switches).
static void
swcases_collect(Node* n, SwitchCases* sc) {
	int i;

	if (n == NULL || sc->overflow)
		return;
	if (n->kind == NdCase) {
		if (swcases_add(sc, n->int_val) < 0)
			return;
		if (n->b)
			(void)swcases_add_range(sc, n->int_val, n->b->int_val);
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
	for (i = 0; i < n->children_len; i++)
		swcases_collect(n->children[i], sc);
}

// Switch scrutinee type when it is a complete enum tag type.
static Type*
switch_enum_type(Type* t) {
	if (t && t->kind == TyEnum && t->complete && t->tag)
		return t;
	return NULL;
}

// True if body covers every enumerator of et (or has default / overflow).
static int
switch_enum_exhaustive(Compiler* c, Type* et, Node* body) {
	SwitchCases sc;
	Symbol* s;

	et = switch_enum_type(et);
	if (et == NULL)
		return 0;
	memset(&sc, 0, sizeof(sc));
	swcases_collect(body, &sc);
	if (sc.has_default || sc.overflow)
		return sc.has_default;
	for (s = c->symbols; s; s = s->next) {
		if (s->hidden || s->dead)
			continue;
		if (s->kind != SkEnumCon || s->type == NULL || !type_eq(s->type, et))
			continue;
		if (!swcases_has(&sc, s->int_val))
			return 0;
	}
	return 1;
}

// Diagnose enum switches in user code that omit a constant.
static void
check_switch_enum_exhaust(Compiler* c, Node* sw) {
	Type* et;
	SwitchCases sc;
	Symbol* s;

	if (sw == NULL || sw->kind != NdSwitch)
		return;
	if (!user_source(c, sw->span))
		return;
	et = switch_enum_type(sw->a ? sw->a->type : NULL);
	if (et == NULL)
		return;
	memset(&sc, 0, sizeof(sc));
	swcases_collect(sw->b, &sc);
	if (sc.has_default || sc.overflow)
		return;
	for (s = c->symbols; s; s = s->next) {
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

// True if n is a multi-return call used as a discarded expression statement.
static int
is_discarded_tuple_expr(Node* n) {
	if (n == NULL)
		return 0;
	if (is_void_cast(n))
		return 0;
	if (n->kind == NdComma)
		return is_discarded_tuple_expr(n->b);
	if (n->kind == NdCall && is_tuple(n->type))
		return 1;
	return 0;
}

// StmtVisitFn: error on discarded tuple returns; prune non-statement forms.
static int
discard_tuple_visit(Compiler* c, Node* n, void* ctx) {
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
		if (user_source(c, n->span) && is_discarded_tuple_expr(n))
			error_at(c, n->span,
				 "discarded multi-return value; assign it or cast to void");
		return 1;
	}
}

// Walk a statement tree for discarded multi-return values.
static void
check_discard_tuple_stmt(Compiler* c, Node* n) {
	walk_stmt(c, n, discard_tuple_visit, NULL);
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
	for (s = c->symbols; s; s = s->next) {
		if (s->owner != owner || s->kind != SkVar || s->used)
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

// Per-function pass for discarded tuple return diagnostics.
static void
check_discard_tuple_func(Compiler* c, Node* fn) {
	if (fn == NULL || fn->kind != NdFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	check_discard_tuple_stmt(c, fn->a);
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

	for (i = 0; i < c->globals_len; i++) {
		d = c->globals[i];
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

	for (i = 0; i < c->funcs_len; i++)
		check_emitter_limits_node(c, c->funcs[i], 0, 0);
}

/* ---- type_check_unit ---- */

// True if t (or any nested component) is a package-private type.
static int
type_mentions_pkg_private(Type* t) {
	int i;

	if (t == NULL)
		return 0;
	if (t->pkg_private)
		return 1;
	if (t->base && type_mentions_pkg_private(t->base))
		return 1;
	if (t->kind == TyFunc) {
		if (type_mentions_pkg_private(t->base))
			return 1;
		for (i = 0; i < t->params_len; i++) {
			if (type_mentions_pkg_private(t->params[i]))
				return 1;
		}
	}
	if ((t->kind == TyStruct || t->kind == TyUnion) && t->fields) {
		Field* f;

		for (f = t->fields; f; f = f->next) {
			if (type_mentions_pkg_private(f->type))
				return 1;
		}
	}
	return 0;
}

// Public API must not mention package-private types.
static void
check_pkg_private_leaks(Compiler* c) {
	Symbol* s;

	for (s = c->symbols; s; s = s->next) {
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
	for (i = 0; i < c->funcs_len; i++) {
		check_uninit_func(c, c->funcs[i]);
		check_falloff_func(c, c->funcs[i]);
		check_enum_exhaust_func(c, c->funcs[i]);
		check_goto_over_decl(c, c->funcs[i]);
		check_unseq_func(c, c->funcs[i]);
		check_discard_tuple_func(c, c->funcs[i]);
		check_unused_func(c, c->funcs[i]);
	}
}
