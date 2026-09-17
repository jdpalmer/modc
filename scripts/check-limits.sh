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
