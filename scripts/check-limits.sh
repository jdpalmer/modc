#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
MODC=${MODC:-"$ROOT/modc"}
TMP="$ROOT/build/limit_check"

rm -rf "$TMP"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

awk 'BEGIN {
	printf "char big[5001] = \""
	for (i = 0; i < 5000; i++) printf "x"
	print "\";"
	print "int main() { return sizeof(big) == 5001 && big[4999] == '\''x'\'' ? 0: 1; }"
}' > "$TMP/large.mc"
"$MODC" build "$TMP/large.mc" -o "$TMP/large-bin"
"$TMP/large-bin"

awk 'BEGIN {
	printf "int last("
	for (i = 0; i < 64; i++) printf "%sint p%d", i ? ", " : "", i
	printf ") { return "
	for (i = 0; i < 64; i++) printf "%sp%d", i ? " + " : "", i
	print "; }"
	printf "int main() { return last("
	for (i = 0; i < 64; i++) printf "%s%d", i ? ", " : "", i
	print ") == 2016 ? 0: 1; }"
}' > "$TMP/params64.mc"
"$MODC" build "$TMP/params64.mc" -o "$TMP/params64-bin"
"$TMP/params64-bin"

awk 'BEGIN {
	printf "int too_many("
	for (i = 0; i < 65; i++) printf "%sint p%d", i ? ", " : "", i
	print ") { return 0; }"
}' > "$TMP/params65.mc"
if "$MODC" check "$TMP/params65.mc" >"$TMP/params65.out" 2>&1; then
	echo "expected 65-parameter declaration to fail" >&2
	exit 1
fi
grep -q "limited to 64 parameters" "$TMP/params65.out"

awk 'BEGIN {
	print "int many(int x, ...) { return x; }"
	printf "int main() { return many("
	for (i = 0; i < 65; i++) printf "%s%d", i ? ", " : "", i
	print "); }"
}' > "$TMP/args65.mc"
if "$MODC" check "$TMP/args65.mc" >"$TMP/args65.out" 2>&1; then
	echo "expected 65-argument call to fail" >&2
	exit 1
fi
grep -q "limited to 64 arguments" "$TMP/args65.out"

awk 'BEGIN {
	print "int too_deep() {"
	for (i = 0; i < 64; i++) print "{"
	print "return 0;"
	for (i = 0; i < 64; i++) print "}"
	print "}"
}' > "$TMP/blocks65.mc"
if "$MODC" check "$TMP/blocks65.mc" >"$TMP/blocks65.out" 2>&1; then
	echo "expected 65 nested blocks to fail" >&2
	exit 1
fi
grep -q "block nesting exceeds implementation limit of 64" "$TMP/blocks65.out"

awk 'BEGIN {
	print "int too_many_defers() {"
	for (i = 0; i < 65; i++) print "defer 0;"
	print "return 0;"
	print "}"
}' > "$TMP/defers65.mc"
if "$MODC" check "$TMP/defers65.mc" >"$TMP/defers65.out" 2>&1; then
	echo "expected 65 defers in one scope to fail" >&2
	exit 1
fi
grep -q "scope has 65 defers; implementation limit is 64" "$TMP/defers65.out"

awk 'BEGIN {
	print "int too_many_cases(int x) {"
	print "switch (x) {"
	for (i = 0; i < 129; i++) printf "case %d: return %d;\n", i, i
	print "default: return -1;"
	print "}"
	print "}"
}' > "$TMP/cases129.mc"
if "$MODC" check "$TMP/cases129.mc" >"$TMP/cases129.out" 2>&1; then
	echo "expected 129 switch cases to fail" >&2
	exit 1
fi
grep -q "switch has 129 cases; implementation limit is 128" "$TMP/cases129.out"

awk 'BEGIN {
	print "int too_many_loops() {"
	for (i = 0; i < 33; i++) print "while (1) {"
	print "return 0;"
	for (i = 0; i < 33; i++) print "}"
	print "}"
}' > "$TMP/loops33.mc"
if "$MODC" check "$TMP/loops33.mc" >"$TMP/loops33.out" 2>&1; then
	echo "expected 33 nested loops to fail" >&2
	exit 1
fi
grep -q "control-flow nesting exceeds implementation limit of 32" "$TMP/loops33.out"

awk 'BEGIN {
	printf "#pragma modc c_sources("
	for (i = 0; i < 1024; i++) printf "a"
	print ".c)"
	print "int main() { return 0; }"
}' > "$TMP/csource_long.mc"
if "$MODC" check "$TMP/csource_long.mc" >"$TMP/csource_long.out" 2>&1; then
	echo "expected oversized c_sources path to fail" >&2
	exit 1
fi
grep -q "c_sources path exceeds implementation limit of 1023 bytes" "$TMP/csource_long.out"
