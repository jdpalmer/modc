#!/bin/sh
# Corpus runner: emit+link+run, check-ok, and expect-fail tests.
# Special multi-step scenarios live in check-special.sh.
set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

MODC=${MODC:-"$ROOT/modc"}
CC=${CC:-cc}
BUILD=${BUILD:-"$ROOT/build"}
QBE=${QBE:-qbe}

mkdir -p "$BUILD"
export MODC_NO_SYSTEM_INCLUDES="${MODC_NO_SYSTEM_INCLUDES:-1}"

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

expand_vars() {
	# Replace $ROOT in a string.
	printf '%s\n' "$1" | sed "s|\$ROOT|$ROOT|g"
}

run_fail_expect() {
	# $1 = .mc path, $2 = .expect path
	mc=$1
	exp=$2
	stem=$(basename "$mc" .mc)
	out=$BUILD/${stem}.out
	flags=
	while IFS= read -r line || [ -n "$line" ]; do
		case "$line" in
		''|'#'*) ;;
		'# flags:'*)
			flags=$(expand_vars "${line#\# flags: }")
			;;
		esac
	done < "$exp"

	set -- $flags
	if "$MODC" check "$@" "$mc" >"$out" 2>&1; then
		fail "$mc: expected check to fail"
	fi
	while IFS= read -r line || [ -n "$line" ]; do
		case "$line" in
		''|'#'*) continue ;;
		esac
		if [ "${line#F:}" != "$line" ]; then
			needle=$(expand_vars "${line#F:}")
			grep -Fq -- "$needle" "$out" || fail "$mc: missing fixed needle: $needle"
		else
			needle=$(expand_vars "$line")
			grep -q -- "$needle" "$out" || fail "$mc: missing needle: $needle"
		fi
	done < "$exp"
	echo "  fail  $mc"
}

run_check_ok() {
	# remaining args are check argv (flags + paths)
	if ! "$MODC" check "$@"; then
		fail "check $*: expected success"
	fi
	echo "  check $*"
}

apply_qbe_expect() {
	# $1 = stem, $2 = .qbe path
	stem=$1
	qbe=$2
	exp=test/${stem}.qbe.expect
	[ -f "$exp" ] || return 0
	while IFS= read -r line || [ -n "$line" ]; do
		case "$line" in
		''|'#'*) continue ;;
		+*)
			needle=${line#+}
			grep -q -- "$needle" "$qbe" || fail "$stem: QBE missing: $needle"
			;;
		-*)
			needle=${line#-}
			if grep -q -- "$needle" "$qbe"; then
				fail "$stem: QBE should not contain: $needle"
			fi
			;;
		*)
			fail "$stem: bad qbe.expect line: $line"
			;;
		esac
	done < "$exp"
}

load_env_file() {
	# $1 = .env path; exports vars for the duration of the caller subshell
	envf=$1
	[ -f "$envf" ] || return 0
	while IFS= read -r envline || [ -n "$envline" ]; do
		case "$envline" in
		''|'#'*) continue ;;
		*=*)
			key=${envline%%=*}
			val=$(expand_vars "${envline#*=}")
			if [ -z "$val" ]; then
				unset "$key"
			else
				export "$key=$val"
			fi
			;;
		esac
	done < "$envf"
}

run_emit_link() {
	# $1 = .mc path (may be NAME_modc.mc)
	mc=$1
	stem=$(basename "$mc" .mc)
	base=$stem
	case "$stem" in
	*_modc) base=${stem%_modc} ;;
	esac

	emitflags=
	[ -f "test/${stem}.emitflags" ] && emitflags=$(cat "test/${stem}.emitflags")

	(
		load_env_file "test/${stem}.env"
		# shellcheck disable=SC2086
		set -- $emitflags
		"$MODC" emit "$@" "$mc" >"$BUILD/${stem}.qbe"
		apply_qbe_expect "$stem" "$BUILD/${stem}.qbe"
		"$QBE" -o "$BUILD/${stem}.s" "$BUILD/${stem}.qbe"

		if [ -f "test/${base}_host.c" ]; then
			"$CC" -c -o "$BUILD/${base}_host.o" "test/${base}_host.c"
			"$CC" -o "$BUILD/${base}-test" "test/${base}_main.c" "$BUILD/${stem}.s" "$BUILD/${base}_host.o"
		else
			"$CC" -o "$BUILD/${base}-test" "test/${base}_main.c" "$BUILD/${stem}.s"
		fi
		"$BUILD/${base}-test"
	) || fail "run $mc"
	echo "  run   $mc"
}

echo "== corpus: expect-fail =="
nfail=0
for exp in test/*.expect; do
	[ -f "$exp" ] || continue
	stem=$(basename "$exp" .expect)
	case "$stem" in
	*.nosys|*.qbe) continue ;; # nosys in check-special; *.qbe.expect is IR asserts
	esac
	# Skip NAME.qbe.expect (basename.stem leaves "NAME.qbe")
	case "$exp" in
	*.qbe.expect) continue ;;
	esac
	mc=test/${stem}.mc
	[ -f "$mc" ] || fail "missing $mc for $exp"
	run_fail_expect "$mc" "$exp"
	nfail=$((nfail + 1))
done

echo "== corpus: check-ok =="
ncheck=0
while IFS= read -r line || [ -n "$line" ]; do
	case "$line" in
	''|'#'*) continue ;;
	esac
	# Optional env from first .mc on the line
	set -- $line
	mcpath=
	for a in "$@"; do
		case "$a" in
		*.mc) mcpath=$a; break ;;
		esac
	done
	stem=
	[ -n "$mcpath" ] && stem=$(basename "$mcpath" .mc)
	(
		[ -n "$stem" ] && load_env_file "test/${stem}.env"
		# shellcheck disable=SC2086
		run_check_ok $line
	) || exit 1
	ncheck=$((ncheck + 1))
done < test/check-ok.list

echo "== corpus: emit+link+run =="
nrun=0
while IFS= read -r mc || [ -n "$mc" ]; do
	case "$mc" in
	''|'#'*) continue ;;
	esac
	run_emit_link "$mc"
	nrun=$((nrun + 1))
done < test/run.list

echo "== corpus: $nfail fail, $ncheck check-ok, $nrun run =="
