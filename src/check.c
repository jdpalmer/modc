/*
 * Post-parse checks: uninit, fall-off, exhaust, autoconst, unused, …
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
	if (n->kind == NGoto)
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
	if (s == NULL || s->kind != SK_VAR || s->storage != ST_LOCAL)
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

static void
da_set(DAState* st, Symbol* s, int assigned) {
	int i;

	i = da_find(st, s);
	if (i >= 0)
		st->assigned[i] = (unsigned char)assigned;
	else
		da_add(st, s, assigned);
}

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

	if (n == NULL || n->kind != NName || n->symbol == NULL)
		return;
	i = da_find(st, n->symbol);
	if (i < 0 || st->assigned[i])
		return;
	if (user_source(c, n->span))
		error_at(c, n->span, "variable '%s' is used uninitialized", n->symbol->name);
	/* avoid repeat noise on the same name along this path */
	st->assigned[i] = 1;
}

static void
da_mark_lhs(DAState* st, Node* n) {
	if (n == NULL)
		return;
	if (n->kind == NName) {
		da_set(st, n->symbol, 1);
		return;
	}
	if (n->kind == NComma)
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

// Propagate definite-assignment through expressions; models short-circuit and branches.
static void
da_expr(Compiler* c, Node* n, DAState* st, int as_lval) {
	DAState a, b;
	int i;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NLit:
	case NStr:
	case NSizeofT:
	case NSkip:
		return;
	case NSizeof:
		/* operand not evaluated */
		return;
	case NName:
		if (!as_lval)
			da_use(c, n, st);
		return;
	case NAddr:
		da_expr(c, n->a, st, 1);
		/* escaping address: stop requiring prior init */
		if (n->a && n->a->kind == NName)
			da_set(st, n->a->symbol, 1);
		return;
	case NAssign:
		da_expr(c, n->b, st, 0);
		if (n->op != PEq)
			da_expr(c, n->a, st, 0);
		else
			da_expr(c, n->a, st, 1);
		da_mark_lhs(st, n->a);
		return;
	case NUn:
		if (n->op == PPlusPlus || n->op == PMinusMinus) {
			da_expr(c, n->a, st, 0);
			da_mark_lhs(st, n->a);
			return;
		}
		da_expr(c, n->a, st, 0);
		return;
	case NPost:
		da_expr(c, n->a, st, 0);
		da_mark_lhs(st, n->a);
		return;
	case NBin:
		if (n->op == PAmpAmp || n->op == PPipePipe) {
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
	case NCond:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_copy(&b, st);
		da_expr(c, n->b, &a, 0);
		da_expr(c, n->c, &b, 0);
		da_intersect(st, &a, &b);
		return;
	case NComma:
		da_expr(c, n->a, st, 0);
		da_expr(c, n->b, st, as_lval);
		return;
	case NCall:
		for (i = 0; i < n->children_len; i++)
			da_expr(c, n->children[i], st, 0);
		da_expr(c, n->a, st, 0);
		return;
	case NCast:
		/* (void)x; silences unused and is not an uninit use */
		if (n->type && n->type->kind == TY_VOID && n->a && n->a->kind == NName) {
			da_set(st, n->a->symbol, 1);
			return;
		}
		da_expr(c, n->a, st, 0);
		return;
	case NIndex:
	case NSubrange:
	case NDot:
	case NArrow:
	case NDeref:
	case NTupleLit:
		da_expr(c, n->a, st, as_lval && (n->kind == NDot || n->kind == NArrow || n->kind == NIndex || n->kind == NDeref));
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
	if (init && init->kind == NDecl)
		da_stmt(c, init, st);
	else
		da_expr(c, init, st, 0);
}

static void
da_stmt(Compiler* c, Node* n, DAState* st) {
	DAState a, b;
	int i, has_init;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NDecl:
		da_init(c, n->init, st);
		has_init = n->init != NULL && (n->init->expr != NULL || n->init->items_len > 0 || n->init->is_list);
		if (n->symbol && da_trackable(n->symbol))
			da_add(st, n->symbol, has_init);
		return;
	case NBlock:
		for (i = 0; i < n->children_len; i++)
			da_stmt(c, n->children[i], st);
		return;
	case NIf:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_copy(&b, st);
		da_stmt(c, n->b, &a);
		if (n->c)
			da_stmt(c, n->c, &b);
		da_intersect(st, &a, &b);
		return;
	case NWhile:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_stmt(c, n->b, &a);
		/* body may not run — keep pre-loop assignments only */
		return;
	case NDo:
		da_stmt(c, n->a, st);
		da_expr(c, n->b, st, 0);
		return;
	case NFor:
		for_init_da(c, n->a, st);
		da_expr(c, n->b, st, 0);
		da_copy(&a, st);
		if (n->children_len > 0)
			da_stmt(c, n->children[0], &a);
		da_expr(c, n->c, &a, 0);
		return;
	case NSwitch:
		da_expr(c, n->a, st, 0);
		da_copy(&a, st);
		da_stmt(c, n->b, &a);
		return;
	case NCase:
	case NDefault:
		return;
	case NLabel:
		da_stmt(c, n->a, st);
		return;
	case NReturn:
		da_expr(c, n->a, st, 0);
		return;
	case NDefer:
		da_copy(&a, st);
		da_stmt(c, n->a, &a);
		return;
	case NBreak:
	case NContinue:
	case NGoto:
	case NFallthrough:
	case NSkip:
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

	if (fn == NULL || fn->kind != NFunc || fn->a == NULL)
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
	if (n->kind == NBreak)
		return 1;
	/* nested loop/switch break targets inner, not outer — still conservative */
	if (n->kind == NWhile || n->kind == NFor || n->kind == NDo || n->kind == NSwitch)
		return 0;
	if (node_has_break(n->a) || node_has_break(n->b) || node_has_break(n->c))
		return 1;
	for (i = 0; i < n->children_len; i++)
		if (node_has_break(n->children[i]))
			return 1;
	return 0;
}

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
	if (n->kind == NCase) {
		if (swcases_add(sc, n->int_val) < 0)
			return;
		if (n->b)
			(void)swcases_add_range(sc, n->int_val, n->b->int_val);
		return;
	}
	if (n->kind == NDefault) {
		sc->has_default = 1;
		return;
	}
	if (n->kind == NSwitch)
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
	if (t && t->kind == TY_ENUM && t->complete && t->tag)
		return t;
	return NULL;
}

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
		if (s->kind != SK_ENUMCON || s->type == NULL || !type_eq(s->type, et))
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

	if (sw == NULL || sw->kind != NSwitch)
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
		if (s->kind != SK_ENUMCON || s->type == NULL || !type_eq(s->type, et))
			continue;
		if (!swcases_has(&sc, s->int_val))
			error_at(c, sw->span, "switch on %s is not exhaustive; missing case %s",
				 et->tag, s->name ? s->name : "?");
	}
}

static void
walk_enum_exhaust(Compiler* c, Node* n) {
	int i;

	if (n == NULL)
		return;
	if (n->kind == NSwitch) {
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
	if (fn == NULL || fn->kind != NFunc || fn->a == NULL)
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
	if (body->kind != NBlock)
		return stmt_returns(c, body);
	has_default = 0;
	path_open = 0;
	path_returned = 0;
	falling = 0;
	ok = 1;
	for (i = 0; i < body->children_len; i++) {
		s = body->children[i];
		if (s->kind == NCase || s->kind == NDefault) {
			if (s->kind == NDefault)
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
		if (s->kind == NDefer || s->kind == NSkip)
			continue;
		if (s->kind == NFallthrough) {
			path_open = 1;
			path_returned = 0;
			falling = 1;
			continue;
		}
		if (s->kind == NBreak) {
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

static int
stmt_returns(Compiler* c, Node* n) {
	int i;

	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NReturn:
		return 1;
	case NGoto:
		/* path leaves; labels may still fall off — OK for linear tails */
		return 1;
	case NBlock:
		for (i = n->children_len - 1; i >= 0; i--) {
			if (n->children[i]->kind == NDefer)
				continue;
			return stmt_returns(c, n->children[i]);
		}
		return 0;
	case NIf:
		return stmt_returns(c, n->b) && n->c != NULL && stmt_returns(c, n->c);
	case NLabel:
		return stmt_returns(c, n->a);
	case NWhile:
		if (is_const_nonzero(c, n->a) && !node_has_break(n->b))
			return 1;
		return 0;
	case NFor:
		/* for(;0;) never; for(;;)/for(;1;) without break never falls off */
		if ((n->b == NULL || is_const_nonzero(c, n->b)) && !node_has_break(n->children_len > 0 ? n->children[0] : NULL))
			return 1;
		return 0;
	case NDo:
		if (stmt_returns(c, n->a))
			return 1;
		if (is_const_nonzero(c, n->b) && !node_has_break(n->a))
			return 1;
		return 0;
	case NSwitch:
		return switch_returns(c, n->b, n->a ? n->a->type : NULL);
	default:
		return 0;
	}
}

// Error when a non-void function body can fall off the end.
static void
check_falloff_func(Compiler* c, Node* fn) {
	Type* ret;

	if (fn == NULL || fn->kind != NFunc || fn->type == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	ret = fn->type->base;
	if (ret == NULL || ret->kind == TY_VOID)
		return;
	if (stmt_returns(c, fn->a))
		return;
	error_at(c, fn->span, "control reaches end of non-void function");
}

enum { GEDecl = 1,
       GELabel,
       GEGoto,
       GEMax = 512 };

typedef struct GEvent GEvent;
struct GEvent {
	int kind;
	Node* n;
};

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
	case NDecl:
		ge_add(ev, nev, GEDecl, n);
		return;
	case NLabel:
		ge_add(ev, nev, GELabel, n);
		ge_walk(n->a, ev, nev);
		return;
	case NGoto:
		ge_add(ev, nev, GEGoto, n);
		return;
	case NBlock:
		for (i = 0; i < n->children_len; i++)
			ge_walk(n->children[i], ev, nev);
		return;
	case NIf:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		ge_walk(n->c, ev, nev);
		return;
	case NWhile:
	case NDo:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		return;
	case NFor:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		ge_walk(n->c, ev, nev);
		for (i = 0; i < n->children_len; i++)
			ge_walk(n->children[i], ev, nev);
		return;
	case NSwitch:
		ge_walk(n->a, ev, nev);
		ge_walk(n->b, ev, nev);
		return;
	case NDefer:
		ge_walk(n->a, ev, nev);
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

// Reject forward gotos that skip over local declarations (C restriction).
static void
check_goto_over_decl(Compiler* c, Node* fn) {
	GEvent ev[GEMax];
	int nev, i, j, k;
	Node *g, *lab, *d;
	const char* name;

	if (fn == NULL || fn->kind != NFunc || fn->a == NULL)
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
		lab = NULL;
		for (j = 0; j < nev; j++) {
			if (ev[j].kind == GELabel && ev[j].n->s && strcmp(ev[j].n->s, name) == 0) {
				lab = ev[j].n;
				break;
			}
		}
		if (lab == NULL || j <= i)
			continue; /* unknown or backward goto */
		for (k = i + 1; k < j; k++) {
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
	case NAssign:
		if (n->a && n->a->kind == NName && n->a->symbol == s)
			return 1;
		return expr_modifies_symbol(n->a, s) || expr_modifies_symbol(n->b, s);
	case NPost:
	case NUn:
		if ((n->op == PPlusPlus || n->op == PMinusMinus) && n->a && n->a->kind == NName && n->a->symbol == s)
			return 1;
		return expr_modifies_symbol(n->a, s);
	case NCond:
		return expr_modifies_symbol(n->a, s) || expr_modifies_symbol(n->b, s) || expr_modifies_symbol(n->c, s);
	case NCall:
		if (expr_modifies_symbol(n->a, s))
			return 1;
		for (i = 0; i < n->children_len; i++)
			if (expr_modifies_symbol(n->children[i], s))
				return 1;
		return 0;
	case NComma:
	case NBin:
	case NIndex:
	case NDot:
	case NArrow:
		return expr_modifies_symbol(n->a, s) || expr_modifies_symbol(n->b, s);
	case NCast:
	case NAddr:
	case NDeref:
	case NSizeof:
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
	if (n->kind == NAssign && n->a && n->a->kind == NName && n->a->symbol && user_source(c, n->span) && expr_modifies_symbol(n->b, n->a->symbol))
		error_at(c, n->span, "unsequenced modification of '%s'", n->a->symbol->name);
	check_unseq_expr(c, n->a);
	check_unseq_expr(c, n->b);
	check_unseq_expr(c, n->c);
	for (i = 0; i < n->children_len; i++)
		check_unseq_expr(c, n->children[i]);
	if (n->init)
		check_unseq_init(c, n->init);
}

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
	if (fn == NULL || fn->kind != NFunc || fn->a == NULL)
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

static void
walk_stmt(Compiler* c, Node* n, StmtVisitFn visit, void* ctx) {
	int i;

	if (n == NULL)
		return;
	if (visit && visit(c, n, ctx))
		return;
	switch (n->kind) {
	case NBlock:
		for (i = 0; i < n->children_len; i++)
			walk_stmt(c, n->children[i], visit, ctx);
		return;
	case NIf:
		walk_stmt(c, n->b, visit, ctx);
		walk_stmt(c, n->c, visit, ctx);
		return;
	case NWhile:
		walk_stmt(c, n->b, visit, ctx);
		return;
	case NDo:
		walk_stmt(c, n->a, visit, ctx);
		return;
	case NFor:
		walk_stmt(c, n->a, visit, ctx);
		if (n->children_len > 0)
			walk_stmt(c, n->children[0], visit, ctx);
		walk_stmt(c, n->c, visit, ctx);
		return;
	case NSwitch:
		walk_stmt(c, n->b, visit, ctx);
		return;
	case NLabel:
	case NDefer:
		walk_stmt(c, n->a, visit, ctx);
		return;
	default:
		return;
	}
}

static int
is_void_cast(Node* n) {
	return n && n->kind == NCast && n->type && n->type->kind == TY_VOID;
}

static int
is_discarded_tuple_expr(Node* n) {
	if (n == NULL)
		return 0;
	if (is_void_cast(n))
		return 0;
	if (n->kind == NComma)
		return is_discarded_tuple_expr(n->b);
	if (n->kind == NCall && is_tuple(n->type))
		return 1;
	return 0;
}

static int
discard_tuple_visit(Compiler* c, Node* n, void* ctx) {
	(void)ctx;
	switch (n->kind) {
	case NBlock:
	case NIf:
	case NWhile:
	case NDo:
	case NFor:
	case NSwitch:
	case NLabel:
	case NDefer:
		return 0;
	case NCase:
	case NDefault:
	case NDecl:
	case NReturn:
	case NBreak:
	case NContinue:
	case NGoto:
	case NFallthrough:
	case NSkip:
		return 1;
	default:
		if (user_source(c, n->span) && is_discarded_tuple_expr(n))
			error_at(c, n->span,
				 "discarded multi-return value; assign it or cast to void");
		return 1;
	}
}

static void
check_discard_tuple_stmt(Compiler* c, Node* n) {
	walk_stmt(c, n, discard_tuple_visit, NULL);
}


static int
is_compiler_temp_name(const char* name) {
	return name && name[0] == '_' && name[1] == '_';
}

// After marking uses, report unused locals and parameters in user functions.
static void
check_unused_func(Compiler* c, Node* fn) {
	Symbol *s, *owner;

	if (fn == NULL || fn->kind != NFunc || fn->symbol == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	owner = fn->symbol;
	mark_symbol_used(fn->a);
	for (s = c->symbols; s; s = s->next) {
		if (s->owner != owner || s->kind != SK_VAR || s->used)
			continue;
		if (is_compiler_temp_name(s->name))
			continue;
		if (s->storage == ST_PARAM)
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
	if (fn == NULL || fn->kind != NFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	check_discard_tuple_stmt(c, fn->a);
}

/* ---- autoconst / readonly infer (ac_ / ir_) ---- */
/*
 * Prefix ac_ — auto-const (immutable string / readonly pointer tracking).
 * States: ACPlain / ACImmut / ACMutated. Walks like da_ but tracks provenance
 * of string/pointer locals instead of assignedness.
 *
 * Prefix ir_ — infer READONLY (same-compile summaries on pointer params/returns).
 * IRAlias records locals that alias a readonly parameter; used by ac_ and by
 * return-safety checks. “v2” = inferred from visible bodies in this Compiler.
 */
enum { ACPlain = 0,
       ACImmut = 1,
       ACMutated = 2 };
enum { ACMax = 64 };

typedef struct {
	Symbol* symbols[ACMax];
	unsigned char st[ACMax];
	int n;
} ACState;

static Type* ac_fnret; /* current function return type during body walk */

static int ac_tracked_local(Symbol* s);
static int ac_char_ranged(Type* t);
static int ac_mutable_ptr_type(Type* t);

/* ir_: infer whether a pointer parameter is tainted (stored/escaped) in the body. */
enum { IRAliasMax = 24 };

typedef struct {
	Symbol* alias[IRAliasMax];
	int n;
} IRAlias;

static int
ir_is_alias(IRAlias* a, Symbol* param, Symbol* s) {
	int i;

	if (s == NULL)
		return 0;
	if (s == param)
		return 1;
	for (i = 0; i < a->n; i++)
		if (a->alias[i] == s)
			return 1;
	return 0;
}

// Record a local that aliases a pointer parameter for taint propagation.
static void
ir_add_alias(IRAlias* a, Symbol* s) {
	int i;

	if (s == NULL || !ac_tracked_local(s))
		return;
	for (i = 0; i < a->n; i++)
		if (a->alias[i] == s)
			return;
	if (a->n >= IRAliasMax)
		return;
	a->alias[a->n++] = s;
}

static int
ir_node_is_symbol(Node* n, Symbol* s) {
	if (n == NULL || s == NULL)
		return 0;
	if (n->kind == NComma)
		return ir_node_is_symbol(n->b, s);
	return n->kind == NName && n->symbol == s;
}

// True if n is the param, a local alias of it, or a pointer derived from it
// (p+i, &p[i], cast, ?: , etc.). Used so stores / mutable calls through
// rewritten pointers still taint the param.
static int
ir_derived(Node* n, Symbol* param, IRAlias* a) {
	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NComma:
		return ir_derived(n->b, param, a);
	case NName:
		return ir_is_alias(a, param, n->symbol);
	case NCast:
		return ir_derived(n->a, param, a);
	case NBin:
		if (n->op == PPlus || n->op == PMinus)
			return ir_derived(n->a, param, a) || ir_derived(n->b, param, a);
		return 0;
	case NIndex:
	case NSubrange:
		return ir_derived(n->a, param, a);
	case NAddr:
		if (n->a == NULL)
			return 0;
		if (n->a->kind == NIndex)
			return ir_derived(n->a->a, param, a);
		if (n->a->kind == NDeref)
			return ir_derived(n->a->a, param, a);
		return ir_node_is_symbol(n->a, param);
	case NCond:
		return ir_derived(n->b, param, a) || ir_derived(n->c, param, a);
	case NUn:
		if (n->op == PPlus || n->op == PMinus)
			return ir_derived(n->a, param, a);
		return 0;
	default:
		return 0;
	}
}

// Resolve the function type of a call expression for readonly inference.
static Type*
ir_call_fn_type(Node* n) {
	Type* ft;

	if (n == NULL || n->a == NULL)
		return NULL;
	ft = n->a->type;
	if (ft && is_ptr(ft) && ft->base && is_func(ft->base))
		ft = ft->base;
	if (ft && is_func(ft))
		return ft;
	if (n->a->symbol && n->a->symbol->type && is_func(n->a->symbol->type))
		return n->a->symbol->type;
	return NULL;
}

static int ir_stmt_taints(Compiler* c, Node* n, Symbol* param, IRAlias* a);
static int ir_expr_taints(Compiler* c, Node* n, Symbol* param, IRAlias* a);

static int
ir_store_taints(Node* lval, Symbol* param, IRAlias* a) {
	if (lval == NULL)
		return 0;
	if (lval->kind == NComma)
		return ir_store_taints(lval->b, param, a);
	if (lval->kind == NDeref)
		return ir_derived(lval->a, param, a);
	if (lval->kind == NIndex)
		return ir_derived(lval->a, param, a);
	return 0;
}

static int
ir_escape_lhs(Node* lval) {
	if (lval == NULL)
		return 0;
	if (lval->kind == NComma)
		return ir_escape_lhs(lval->b);
	return lval->kind == NDot || lval->kind == NArrow || lval->kind == NDeref || lval->kind == NIndex;
}

// True if the expression may mutate or expose the tracked pointer parameter.
static int
ir_expr_taints(Compiler* c, Node* n, Symbol* param, IRAlias* a) {
	Type *ft, *pt;
	int i, taint;

	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NAssign:
		if (ir_expr_taints(c, n->b, param, a) || ir_expr_taints(c, n->a, param, a))
			return 1;
		if (ir_store_taints(n->a, param, a))
			return 1;
		if (n->op == PEq && ir_derived(n->b, param, a)) {
			if (n->a && n->a->kind == NName && n->a->symbol)
				ir_add_alias(a, n->a->symbol);
			else if (ir_escape_lhs(n->a))
				return 1; /* into memory we do not track */
		}
		return 0;
	case NUn:
		if (ir_expr_taints(c, n->a, param, a))
			return 1;
		if ((n->op == PPlusPlus || n->op == PMinusMinus) && ir_store_taints(n->a, param, a))
			return 1;
		return 0;
	case NPost:
		if (ir_expr_taints(c, n->a, param, a))
			return 1;
		return ir_store_taints(n->a, param, a);
	case NCall:
		if (ir_expr_taints(c, n->a, param, a))
			return 1;
		ft = ir_call_fn_type(n);
		for (i = 0; i < n->children_len; i++) {
			if (ir_expr_taints(c, n->children[i], param, a))
				return 1;
			if (!ir_derived(n->children[i], param, a))
				continue;
			if (ft == NULL || !is_func(ft) || i >= ft->params_len) {
				/* varargs / unknown — may mutate */
				return 1;
			}
			pt = ft->params[i];
			if (pt == NULL || ac_mutable_ptr_type(pt))
				return 1;
		}
		return 0;
	case NCast:
		if (ir_expr_taints(c, n->a, param, a))
			return 1;
		/* Cast of derived pointer to mutable pointer escapes READONLY. */
		if (ir_derived(n->a, param, a) && n->type && is_ptr(n->type) && !n->type->is_readonly)
			return 1;
		return 0;
	case NAddr:
		if (ir_node_is_symbol(n->a, param))
			return 1;
		return ir_expr_taints(c, n->a, param, a);
	default:
		taint = 0;
		taint |= ir_expr_taints(c, n->a, param, a);
		taint |= ir_expr_taints(c, n->b, param, a);
		taint |= ir_expr_taints(c, n->c, param, a);
		for (i = 0; i < n->children_len; i++)
			taint |= ir_expr_taints(c, n->children[i], param, a);
		return taint;
	}
}

// Propagate parameter taint through initializer expressions.
static int
ir_init_taints(Compiler* c, Initializer* in, Symbol* param, IRAlias* a, Symbol* dst) {
	int i, t;

	if (in == NULL)
		return 0;
	t = 0;
	if (in->expr) {
		t |= ir_expr_taints(c, in->expr, param, a);
		if (dst && ir_derived(in->expr, param, a))
			ir_add_alias(a, dst);
	}
	for (i = 0; i < in->items_len; i++)
		t |= ir_init_taints(c, &in->items[i], param, a, NULL);
	return t;
}

static int
ir_stmt_taints(Compiler* c, Node* n, Symbol* param, IRAlias* a) {
	int i, t;

	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NBlock:
		t = 0;
		for (i = 0; i < n->children_len; i++)
			t |= ir_stmt_taints(c, n->children[i], param, a);
		return t;
	case NDecl:
		return ir_init_taints(c, n->init, param, a, n->symbol);
	case NIf:
		return ir_expr_taints(c, n->a, param, a) || ir_stmt_taints(c, n->b, param, a) || ir_stmt_taints(c, n->c, param, a);
	case NWhile:
		return ir_expr_taints(c, n->a, param, a) || ir_stmt_taints(c, n->b, param, a);
	case NDo:
		return ir_stmt_taints(c, n->a, param, a) || ir_expr_taints(c, n->b, param, a);
	case NFor:
		if (n->a && n->a->kind == NDecl)
			t = ir_stmt_taints(c, n->a, param, a);
		else
			t = ir_expr_taints(c, n->a, param, a);
		t |= ir_expr_taints(c, n->b, param, a) || ir_expr_taints(c, n->c, param, a);
		if (n->children_len > 0)
			t |= ir_stmt_taints(c, n->children[0], param, a);
		return t;
	case NSwitch:
		return ir_expr_taints(c, n->a, param, a) || ir_stmt_taints(c, n->b, param, a);
	case NReturn:
	case NDefer:
		return n->kind == NReturn ? ir_expr_taints(c, n->a, param, a)
					  : ir_stmt_taints(c, n->a, param, a);
	case NLabel:
		return ir_stmt_taints(c, n->a, param, a);
	case NCase:
	case NDefault:
	case NBreak:
	case NContinue:
	case NGoto:
	case NFallthrough:
	case NSkip:
		return 0;
	default:
		return ir_expr_taints(c, n, param, a);
	}
}

static Symbol*
ir_param_symbol(Compiler* c, Symbol* fn, int idx) {
	Symbol* s;
	Type* t;
	const char* want;

	if (fn == NULL || fn->type == NULL || !is_func(fn->type))
		return NULL;
	t = fn->type;
	if (idx < 0 || idx >= t->params_len)
		return NULL;
	want = t->param_names ? t->param_names[idx] : NULL;
	if (want == NULL)
		return NULL;
	for (s = c->symbols; s; s = s->next)
		if (s->owner == fn && s->storage == ST_PARAM && s->name && strcmp(s->name, want) == 0)
			return s;
	return NULL;
}

static int
ir_param_tainted(Compiler* c, Node* fn, int idx) {
	Symbol* ps;
	IRAlias a;

	if (fn == NULL || fn->symbol == NULL || fn->a == NULL)
		return 1;
	ps = ir_param_symbol(c, fn->symbol, idx);
	if (ps == NULL)
		return 1;
	memset(&a, 0, sizeof(a));
	return ir_stmt_taints(c, fn->a, ps, &a);
}

/* ir_: infer READONLY on pointer returns from visible function bodies. */

static int ac_immut_source(Node* n);

static int
ir_sym_readonly_param(Compiler* c, Node* fn, Symbol* s) {
	Type* ty;
	int i;
	Symbol* ps;

	if (s == NULL || fn == NULL || fn->symbol == NULL || fn->type == NULL)
		return 0;
	ty = fn->type;
	if (!is_func(ty))
		return 0;
	for (i = 0; i < ty->params_len; i++) {
		ps = ir_param_symbol(c, fn->symbol, i);
		if (ps == s && ty->params[i] && is_ptr(ty->params[i]) && ty->params[i]->is_readonly)
			return 1;
	}
	return 0;
}

static int
ir_is_readonly_symbol(Compiler* c, Node* fn, IRAlias* ro, Symbol* s) {
	int i;

	if (s == NULL)
		return 0;
	if (ir_sym_readonly_param(c, fn, s))
		return 1;
	if (ro == NULL)
		return 0;
	for (i = 0; i < ro->n; i++)
		if (ro->alias[i] == s)
			return 1;
	return 0;
}

// Drop s from the readonly-local alias set after a non-readonly bind.
static void
ir_ro_remove(IRAlias* ro, Symbol* s) {
	int i, j;

	if (s == NULL || ro == NULL)
		return;
	for (i = 0; i < ro->n; i++) {
		if (ro->alias[i] != s)
			continue;
		for (j = i + 1; j < ro->n; j++)
			ro->alias[j - 1] = ro->alias[j];
		ro->n--;
		return;
	}
}

static int
ir_derived_readonly(Node* n, Compiler* c, Node* fn, IRAlias* ro) {
	if (n == NULL)
		return 0;
	switch (n->kind) {
	case NComma:
		return ir_derived_readonly(n->b, c, fn, ro);
	case NName:
		return ir_is_readonly_symbol(c, fn, ro, n->symbol);
	case NCast:
		if (n->type && is_ptr(n->type) && !n->type->is_readonly)
			return 0;
		return ir_derived_readonly(n->a, c, fn, ro);
	case NBin:
		if (n->op == PPlus || n->op == PMinus)
			return ir_derived_readonly(n->a, c, fn, ro) || ir_derived_readonly(n->b, c, fn, ro);
		return 0;
	case NIndex:
	case NSubrange:
		return ir_derived_readonly(n->a, c, fn, ro);
	case NAddr:
		if (n->a == NULL)
			return 0;
		if (n->a->kind == NIndex)
			return ir_derived_readonly(n->a->a, c, fn, ro);
		if (n->a->kind == NDeref)
			return ir_derived_readonly(n->a->a, c, fn, ro);
		return ir_is_readonly_symbol(c, fn, ro, n->a->symbol);
	case NCond:
		return ir_derived_readonly(n->b, c, fn, ro) || ir_derived_readonly(n->c, c, fn, ro);
	case NUn:
		if (n->op == PPlus || n->op == PMinus)
			return ir_derived_readonly(n->a, c, fn, ro);
		return 0;
	default:
		return 0;
	}
}

static int
ir_expr_readonly_safe(Compiler* c, Node* fn, Node* n, IRAlias* ro) {
	Type* ft;

	if (n == NULL)
		return 0;
	if (ac_immut_source(n))
		return 1;
	switch (n->kind) {
	case NComma:
		return ir_expr_readonly_safe(c, fn, n->b, ro);
	case NCall:
		ft = ir_call_fn_type(n);
		if (ft && is_ptr(ft->base) && ft->base->is_readonly && !is_void_ptr(ft->base))
			return 1;
		return 0;
	case NName:
		return ir_is_readonly_symbol(c, fn, ro, n->symbol);
	case NCast:
		if (n->type && is_ptr(n->type) && !n->type->is_readonly)
			return 0;
		return ir_expr_readonly_safe(c, fn, n->a, ro);
	case NBin:
		if (n->op == PPlus || n->op == PMinus)
			return ir_derived_readonly(n, c, fn, ro);
		return 0;
	case NIndex:
	case NSubrange:
	case NAddr:
	case NCond:
	case NUn:
		return ir_derived_readonly(n, c, fn, ro);
	default:
		return 0;
	}
}

static void
ir_readonly_local_bind(Compiler* c, Node* fn, Symbol* dst, Node* rhs, IRAlias* ro) {
	if (dst == NULL || !ac_tracked_local(dst))
		return;
	if (rhs && ir_expr_readonly_safe(c, fn, rhs, ro))
		ir_add_alias(ro, dst);
	else
		ir_ro_remove(ro, dst);
}

typedef struct {
	Node* fn;
	IRAlias* ro;
	int bad;
} IRRetCtx;

static int
ir_return_visit(Compiler* c, Node* n, void* ctx) {
	IRRetCtx* x = ctx;
	Type* ret;

	switch (n->kind) {
	case NBlock:
	case NIf:
	case NWhile:
	case NDo:
	case NFor:
	case NSwitch:
	case NLabel:
		return 0;
	case NDecl:
		if (n->init && n->init->expr)
			ir_readonly_local_bind(c, x->fn, n->symbol, n->init->expr, x->ro);
		return 1;
	case NAssign:
		if (n->op == PEq && n->a && n->a->kind == NName && n->a->symbol)
			ir_readonly_local_bind(c, x->fn, n->a->symbol, n->b, x->ro);
		return 1;
	case NReturn:
		ret = x->fn->type && is_func(x->fn->type) ? x->fn->type->base : NULL;
		if (ret && is_ptr(ret) && !is_void_ptr(ret) && n->a &&
		    !ir_expr_readonly_safe(c, x->fn, n->a, x->ro))
			x->bad = 1;
		return 1;
	default:
		return 1;
	}
}

static int
ir_return_stmt_check(Compiler* c, Node* fn, Node* n, IRAlias* ro) {
	IRRetCtx x;

	x.fn = fn;
	x.ro = ro;
	x.bad = 0;
	walk_stmt(c, n, ir_return_visit, &x);
	return x.bad;
}

static int
ir_return_tainted(Compiler* c, Node* fn) {
	IRAlias ro;

	if (fn == NULL || fn->symbol == NULL || fn->a == NULL || fn->type == NULL)
		return 1;
	if (!is_func(fn->type))
		return 0;
	if (fn->type->base == NULL || !is_ptr(fn->type->base) || is_void_ptr(fn->type->base))
		return 0;
	memset(&ro, 0, sizeof(ro));
	return ir_return_stmt_check(c, fn, fn->a, &ro);
}

// Optimistic fixpoint: mark pointer params and returns of functions with bodies
// READONLY, then clear any that are stored through, passed to a still-mutable
// slot, or return a non-readonly-safe expression.
// Header const (already is_readonly) and functions without bodies are untouched
// except that calls see the evolving summaries.
static void
infer_readonly_summaries(Compiler* c) {
	int i, j, changed, pass, taint;
	Node* fn;
	Type* ty;
	Symbol* ps;

	/* Seed: every pointer param / return of a defined user function. */
	for (i = 0; i < c->funcs_len; i++) {
		fn = c->funcs[i];
		if (fn == NULL || fn->symbol == NULL || fn->a == NULL || fn->type == NULL)
			continue;
		if (!user_source(c, fn->span))
			continue;
		ty = fn->type;
		if (!is_func(ty))
			continue;
		for (j = 0; j < ty->params_len; j++) {
			if (ty->params[j] && is_ptr(ty->params[j]) && !is_void_ptr(ty->params[j]))
				ty->params[j]->is_readonly = 1;
		}
		if (ty->base && is_ptr(ty->base) && !is_void_ptr(ty->base))
			ty->base->is_readonly = 1;
	}

	for (pass = 0; pass < 64; pass++) {
		changed = 0;
		for (i = 0; i < c->funcs_len; i++) {
			fn = c->funcs[i];
			if (fn == NULL || fn->symbol == NULL || fn->a == NULL || fn->type == NULL)
				continue;
			if (!user_source(c, fn->span))
				continue;
			ty = fn->type;
			if (!is_func(ty))
				continue;
			for (j = 0; j < ty->params_len; j++) {
				if (ty->params[j] == NULL || !is_ptr(ty->params[j]))
					continue;
				if (!ty->params[j]->is_readonly)
					continue;
				taint = ir_param_tainted(c, fn, j);
				if (taint) {
					ty->params[j]->is_readonly = 0;
					ps = ir_param_symbol(c, fn->symbol, j);
					if (ps && ps->type)
						ps->type->is_readonly = 0;
					changed = 1;
				}
			}
			if (ty->base && is_ptr(ty->base) && !is_void_ptr(ty->base) && ty->base->is_readonly) {
				taint = ir_return_tainted(c, fn);
				if (taint) {
					ty->base->is_readonly = 0;
					changed = 1;
				}
			}
		}
		if (!changed)
			break;
	}
}

// Index of s in auto-const state, or -1.
static int
ac_find(ACState* st, Symbol* s) {
	int i;

	for (i = 0; i < st->n; i++)
		if (st->symbols[i] == s)
			return i;
	return -1;
}

static int
ac_tracked_local(Symbol* s) {
	Type* t;

	if (s == NULL || s->kind != SK_VAR)
		return 0;
	if (s->storage != ST_LOCAL && s->storage != ST_PARAM)
		return 0;
	t = s->type;
	return t && (is_ptr(t) || is_array(t) || is_ranged(t));
}

static int
ac_char_ranged(Type* t) {
	return t && is_ranged(t) && t->base && (t->base->kind == TY_CHAR || t->base->kind == TY_UCHAR);
}

static void
ac_set(ACState* st, Symbol* s, int state) {
	int i;

	if (!ac_tracked_local(s))
		return;
	i = ac_find(st, s);
	if (i < 0) {
		if (st->n >= ACMax)
			return;
		i = st->n++;
		st->symbols[i] = s;
	}
	st->st[i] = (unsigned char)state;
}

static int
ac_get(ACState* st, Symbol* s) {
	int i;

	i = ac_find(st, s);
	return i >= 0 ? st->st[i] : ACPlain;
}

static void
ac_copy(ACState* dst, ACState* src) {
	*dst = *src;
}

// Merge two paths: immut only if immut on both; any mutation wins.
static void
ac_join(ACState* dst, ACState* a, ACState* b) {
	int i, j, sa, sb;

	ac_copy(dst, a);
	for (i = 0; i < dst->n; i++) {
		j = ac_find(b, dst->symbols[i]);
		sa = dst->st[i];
		sb = j >= 0 ? b->st[j] : ACPlain;
		if (sa == ACMutated || sb == ACMutated)
			dst->st[i] = ACMutated;
		else if (sa == ACImmut && sb == ACImmut)
			dst->st[i] = ACImmut;
		else
			dst->st[i] = ACPlain;
	}
	for (j = 0; j < b->n; j++) {
		if (ac_find(dst, b->symbols[j]) >= 0)
			continue;
		if (b->st[j] == ACPlain)
			continue;
		ac_set(dst, b->symbols[j], b->st[j] == ACMutated ? ACMutated : ACPlain);
	}
}

static int
ac_immut_source(Node* n) {
	if (n == NULL)
		return 0;
	if (n->kind == NComma)
		return ac_immut_source(n->b);
	if (n->kind == NCall && n->a && n->a->kind == NName && n->a->s && strcmp(n->a->s, "ranged") == 0 && n->children_len >= 1 && ac_immut_source(n->children[0]))
		return 1;
	if (expr_is_immutable(n))
		return 1;
	if (n->type && is_ptr(n->type) && n->type->is_readonly && !is_void_ptr(n->type))
		return 1;
	return 0;
}

static int
ac_expr_immut(ACState* st, Node* n) {
	if (n == NULL)
		return 0;
	if (n->kind == NComma)
		return ac_expr_immut(st, n->b);
	if (ac_immut_source(n))
		return 1;
	if (n->kind == NDot && n->a && n->a->kind == NName && n->a->symbol && ac_char_ranged(n->a->symbol->type) && n->s && strcmp(n->s, "ptr") == 0 && ac_get(st, n->a->symbol) == ACImmut)
		return 1;
	if (n->kind == NName && n->symbol && ac_get(st, n->symbol) == ACImmut)
		return 1;
	return 0;
}

static int
ac_mutable_ptr_type(Type* t) {
	if (t == NULL)
		return 0;
	if (is_array(t))
		return 1;
	if (!is_ptr(t))
		return 0;
	return !t->is_readonly;
}

static void ac_expr(Compiler* c, Node* n, ACState* st);
static void ac_stmt(Compiler* c, Node* n, ACState* st);
static void ac_init(Compiler* c, Initializer* in, ACState* st, Symbol* dst);

// Error when an immutable string is passed to a mutable pointer parameter.
static void
ac_check_arg(Compiler* c, Node* arg, Type* param, ACState* st) {
	if (arg == NULL || !user_source(c, arg->span))
		return;
	if (!ac_expr_immut(st, arg))
		return;
	if (param == NULL || ac_mutable_ptr_type(param))
		error_at(c, arg->span, "immutable string passed to mutable pointer parameter");
}

// Diagnose and record stores through immutable pointers.
static void
ac_store_through(Compiler* c, Node* lval, ACState* st) {
	Node* p;

	if (lval == NULL)
		return;
	if (lval->kind == NDeref) {
		p = lval->a;
		if (p && p->kind == NName && p->symbol) {
			if (ac_get(st, p->symbol) == ACImmut)
				error_at(c, lval->span, "store through immutable pointer");
			ac_set(st, p->symbol, ACMutated);
		}
		return;
	}
	if (lval->kind == NIndex) {
		p = lval->a;
		if (p && p->kind == NName && p->symbol) {
			if (is_ptr(p->symbol->type)) {
				if (ac_get(st, p->symbol) == ACImmut)
					error_at(c, lval->span, "store through immutable pointer");
				ac_set(st, p->symbol, ACMutated);
			} else if (ac_char_ranged(p->symbol->type) && ac_get(st, p->symbol) == ACImmut)
				error_at(c, lval->span, "store through immutable pointer");
		}
		/* char buf[] = "hi" is a mutable array copy — indexing is fine */
		return;
	}
	if (lval->kind == NComma)
		ac_store_through(c, lval->b, st);
}

static void
ac_assign_ptr(Compiler* c, Symbol* dst, Node* rhs, ACState* st, Span sp) {
	int im;

	if (!ac_tracked_local(dst))
		return;
	im = ac_expr_immut(st, rhs);
	if (!im)
		im = ac_immut_source(rhs);
	if (im) {
		if (ac_get(st, dst) == ACMutated)
			error_at(c, sp, "immutable string assigned to mutated pointer '%s'",
				 dst->name);
		else if (ac_char_ranged(dst->type))
			ac_set(st, dst, ACImmut);
		else if (dst->type && is_ptr(dst->type) && !dst->type->is_readonly)
			ac_set(st, dst, ACImmut);
		else if (dst->type && is_ptr(dst->type) && dst->type->is_readonly)
			ac_set(st, dst, ACImmut);
		else
			ac_set(st, dst, ACPlain); /* array local: copy, not alias */
	} else
		ac_set(st, dst, ACPlain);
}

static void
ac_init(Compiler* c, Initializer* in, ACState* st, Symbol* dst) {
	int i;

	if (in == NULL)
		return;
	if (in->expr) {
		ac_expr(c, in->expr, st);
		if (dst)
			ac_assign_ptr(c, dst, in->expr, st, in->expr->span);
	}
	for (i = 0; i < in->items_len; i++)
		ac_init(c, &in->items[i], st, NULL);
}

// Propagate auto-const state through expressions and call arguments.
static void
ac_expr(Compiler* c, Node* n, ACState* st) {
	ACState a, b;
	int i;
	Type *ft, *pt;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NLit:
	case NStr:
	case NSizeofT:
	case NSkip:
		return;
	case NSizeof:
		return;
	case NName:
		return;
	case NAddr:
		ac_expr(c, n->a, st);
		if (n->a && n->a->kind == NName && n->a->symbol)
			ac_set(st, n->a->symbol, ACPlain); /* escape */
		return;
	case NAssign:
		ac_expr(c, n->b, st);
		ac_expr(c, n->a, st);
		if (n->op == PEq && n->a && n->a->kind == NName && n->a->symbol)
			ac_assign_ptr(c, n->a->symbol, n->b, st, n->span);
		else
			ac_store_through(c, n->a, st);
		return;
	case NUn:
		ac_expr(c, n->a, st);
		if (n->op == PPlusPlus || n->op == PMinusMinus)
			ac_store_through(c, n->a, st);
		return;
	case NPost:
		ac_expr(c, n->a, st);
		ac_store_through(c, n->a, st);
		return;
	case NBin:
		ac_expr(c, n->a, st);
		ac_expr(c, n->b, st);
		return;
	case NCond:
		ac_expr(c, n->a, st);
		ac_copy(&a, st);
		ac_copy(&b, st);
		ac_expr(c, n->b, &a);
		ac_expr(c, n->c, &b);
		ac_join(st, &a, &b);
		return;
	case NComma:
		ac_expr(c, n->a, st);
		ac_expr(c, n->b, st);
		return;
	case NCall:
		ac_expr(c, n->a, st);
		ft = n->a ? n->a->type : NULL;
		if (ft && is_ptr(ft) && ft->base && is_func(ft->base))
			ft = ft->base;
		if ((ft == NULL || !is_func(ft)) && n->a && n->a->symbol && n->a->symbol->type && is_func(n->a->symbol->type))
			ft = n->a->symbol->type;
		/* Builtin ranged("…") / len(): not a mutable C pointer sink. */
		if (n->a && n->a->kind == NName && n->a->s && (strcmp(n->a->s, "ranged") == 0 || strcmp(n->a->s, "len") == 0)) {
			for (i = 0; i < n->children_len; i++)
				ac_expr(c, n->children[i], st);
			return;
		}
		for (i = 0; i < n->children_len; i++) {
			ac_expr(c, n->children[i], st);
			pt = (ft && is_func(ft) && i < ft->params_len) ? ft->params[i] : NULL;
			ac_check_arg(c, n->children[i], pt, st);
			/* may-mutate: drop IMMUTABLE on pointer args passed to mutable params */
			if (n->children[i] && n->children[i]->kind == NName && n->children[i]->symbol && (pt == NULL || ac_mutable_ptr_type(pt)))
				ac_set(st, n->children[i]->symbol, ACMutated);
		}
		return;
	case NIndex:
	case NDeref:
	case NDot:
	case NArrow:
	case NCast:
		ac_expr(c, n->a, st);
		ac_expr(c, n->b, st);
		ac_expr(c, n->c, st);
		return;
	default:
		ac_expr(c, n->a, st);
		ac_expr(c, n->b, st);
		ac_expr(c, n->c, st);
		for (i = 0; i < n->children_len; i++)
			ac_expr(c, n->children[i], st);
		return;
	}
}

// For-loop init may be a declaration or an expression (auto-const variant).
static void
for_init_ac(Compiler* c, Node* init, ACState* st) {
	if (init && init->kind == NDecl)
		ac_stmt(c, init, st);
	else
		ac_expr(c, init, st);
}

static void
ac_stmt(Compiler* c, Node* n, ACState* st) {
	ACState a, b;
	int i;

	if (n == NULL)
		return;
	switch (n->kind) {
	case NBlock:
		for (i = 0; i < n->children_len; i++)
			ac_stmt(c, n->children[i], st);
		return;
	case NDecl:
		if (n->init)
			ac_init(c, n->init, st, n->symbol);
		return;
	case NIf:
		ac_expr(c, n->a, st);
		ac_copy(&a, st);
		ac_copy(&b, st);
		ac_stmt(c, n->b, &a);
		ac_stmt(c, n->c, &b);
		ac_join(st, &a, &b);
		return;
	case NWhile:
		ac_expr(c, n->a, st);
		ac_copy(&a, st);
		ac_stmt(c, n->b, &a);
		ac_join(st, st, &a);
		return;
	case NDo:
		ac_copy(&a, st);
		ac_stmt(c, n->a, &a);
		ac_expr(c, n->b, &a);
		ac_join(st, st, &a);
		return;
	case NFor:
		for_init_ac(c, n->a, st);
		ac_expr(c, n->b, st);
		ac_copy(&a, st);
		if (n->children_len > 0)
			ac_stmt(c, n->children[0], &a);
		ac_expr(c, n->c, &a);
		ac_join(st, st, &a);
		return;
	case NSwitch:
		ac_expr(c, n->a, st);
		ac_stmt(c, n->b, st);
		return;
	case NReturn:
		ac_expr(c, n->a, st);
		if (ac_fnret && user_source(c, n->span) && n->a && ac_expr_immut(st, n->a) && is_ptr(ac_fnret) && !ac_fnret->is_readonly)
			error_at(c, n->a->span,
				 "returning immutable string through mutable pointer return type");
		return;
	case NDefer:
		ac_stmt(c, n->a, st);
		return;
	case NLabel:
		ac_stmt(c, n->a, st);
		return;
	case NCase:
	case NDefault:
	case NBreak:
	case NContinue:
	case NGoto:
	case NFallthrough:
	case NSkip:
		return;
	default:
		ac_expr(c, n, st);
		return;
	}
}

// Per-function auto-const analysis for immutable string provenance.
static void
check_autoconst_func(Compiler* c, Node* fn) {
	ACState st;

	if (fn == NULL || fn->kind != NFunc || fn->a == NULL)
		return;
	if (!user_source(c, fn->span))
		return;
	ac_fnret = NULL;
	if (fn->type && is_func(fn->type))
		ac_fnret = fn->type->base;
	memset(&st, 0, sizeof(st));
	ac_stmt(c, fn->a, &st);
	ac_fnret = NULL;
}

// Reject immutable string literals assigned to mutable global pointers.
static void
check_autoconst_globals(Compiler* c) {
	int i;
	Node* d;
	Initializer* in;

	for (i = 0; i < c->globals_len; i++) {
		d = c->globals[i];
		if (d == NULL || d->kind != NDecl || d->symbol == NULL || d->init == NULL)
			continue;
		if (!user_source(c, d->span))
			continue;
		in = d->init;
		if (in->expr == NULL || !ac_immut_source(in->expr))
			continue;
		if (d->symbol->type && is_ptr(d->symbol->type) && !d->symbol->type->is_readonly)
			error_at(c, in->expr->span,
				 "immutable string assigned to mutable pointer '%s'",
				 d->symbol->name);
	}
}

/* ---- type_check_unit ---- */

// Whole-unit diagnostics after parse+type_expr. Each check walks function
// bodies independently; order is “safety first, then hygiene.”
void type_check_unit(Compiler* c) {
	int i;

	infer_readonly_summaries(c);
	check_autoconst_globals(c);
	for (i = 0; i < c->funcs_len; i++) {
		check_uninit_func(c, c->funcs[i]);
		check_falloff_func(c, c->funcs[i]);
		check_enum_exhaust_func(c, c->funcs[i]);
		check_goto_over_decl(c, c->funcs[i]);
		check_unseq_func(c, c->funcs[i]);
		check_discard_tuple_func(c, c->funcs[i]);
		check_autoconst_func(c, c->funcs[i]);
		check_unused_func(c, c->funcs[i]);
	}
}
