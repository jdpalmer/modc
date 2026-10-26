# %C / modc — hosted C99 compiler (QBE backend)
#
# stdio/stdlib only. No lib9, no yacc.

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wno-unused
ROOT := $(abspath .)
.PHONY: all clean check check-special install uninstall format format-check asan ubsan

PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  := $(PREFIX)/bin
MODCLIB := $(PREFIX)/lib/modc
HOST := $(ROOT)/src/host/include
BUILD := $(ROOT)/build
MODC := $(ROOT)/modc

SRCS := src/main.c src/cli_build.c src/cli_cmd.c src/cli_selftest.c src/diag.c src/lex.c src/pp.c src/parse.c src/type.c src/format.c src/symbol.c src/check.c src/emit.c src/pkg.c src/fmt.c src/vendor.c src/host_os.c src/cache.c src/export.c
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

CFLAGS += -DMODC_INCLUDE=\"$(HOST)\" -DMODC_PKG=\"$(ROOT)\"
ifdef ASAN
CFLAGS += -O1 -fsanitize=address -fno-omit-frame-pointer
endif
ifdef UBSAN
CFLAGS += -O1 -fsanitize=undefined
endif

ifeq ($(filter check,$(MAKECMDGOALS)),check)
export MODC_NO_SYSTEM_INCLUDES = 1
endif

all: $(MODC)

# Rewrite user .mc sources (skips format golden inputs; skips files the lexer rejects).
format: $(MODC)
	@find . -name '*.mc' \
		! -path './build/*' \
		! -path './test/format/messy.mc' \
		! -path './test/format/str_style.mc' \
		! -path './test/pp_file_line_ok.mc' \
		! -path './test/pp_file_line_test.mc' \
		-print0 | while IFS= read -r -d '' f; do \
		./modc format "$$f" 2>/dev/null || true; \
	done

# Fail if any formattable .mc differs from modc format output.
format-check: $(MODC)
	@mkdir -p $(BUILD)
	@err=0; \
	find . -name '*.mc' \
		! -path './build/*' \
		! -path './test/format/messy.mc' \
		! -path './test/format/str_style.mc' \
		! -path './test/pp_file_line_ok.mc' \
		! -path './test/pp_file_line_test.mc' \
		| while read -r f; do \
		cp "$$f" $(BUILD)/fmtchk.mc; \
		if ! ./modc format $(BUILD)/fmtchk.mc 2>/dev/null; then continue; fi; \
		if ! diff -q "$$f" $(BUILD)/fmtchk.mc >/dev/null; then \
			echo "not formatted: $$f"; err=1; \
		fi; \
	done; \
	exit $$err

$(MODC): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BUILD)/%.o: src/%.c src/ast.h src/cli.h src/host_os.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

check: $(MODC)
	@mkdir -p $(BUILD)
	./modc test test
	./modc test --bounds-check test/bounds_ok_test.mc
	./modc test test/special/cli_args_test.mc -- a b
	./modc test -M test test/testdriver
	./modc selftest
	./modc build --bounds-check -o $(BUILD)/bounds_oob test/bounds_oob.mc
	@$(BUILD)/bounds_oob >$(BUILD)/bounds_oob.out 2>$(BUILD)/bounds_oob.err; test $$? -ne 0
	@grep -q 'bounds: index' $(BUILD)/bounds_oob.err
	./modc build --bounds-check -o $(BUILD)/bounds_param_oob test/bounds_param_oob.mc
	@$(BUILD)/bounds_param_oob >$(BUILD)/bounds_param_oob.out 2>$(BUILD)/bounds_param_oob.err; test $$? -ne 0
	@grep -q 'bounds: index' $(BUILD)/bounds_param_oob.err
	./modc build --bounds-check -o $(BUILD)/bounds_subrange_oob test/bounds_subrange_oob.mc
	@$(BUILD)/bounds_subrange_oob >$(BUILD)/bounds_subrange_oob.out 2>$(BUILD)/bounds_subrange_oob.err; test $$? -ne 0
	@grep -q 'bounds: index' $(BUILD)/bounds_subrange_oob.err
	./modc build --bounds-check -o $(BUILD)/bounds_subrange_order_oob test/bounds_subrange_order_oob.mc
	@$(BUILD)/bounds_subrange_order_oob >$(BUILD)/bounds_subrange_order_oob.out 2>$(BUILD)/bounds_subrange_order_oob.err; test $$? -ne 0
	@grep -q 'bounds: index' $(BUILD)/bounds_subrange_order_oob.err
	./modc build -o $(BUILD)/crash_panic test/crash_panic_die.mc
	@$(BUILD)/crash_panic >/dev/null 2>&1; test $$? -ne 0
	./modc build -o $(BUILD)/crash_cleanup test/crash_cleanup_die.mc
	@$(BUILD)/crash_cleanup >$(BUILD)/crash_cleanup.out 2>$(BUILD)/crash_cleanup.err; test $$? -ne 0
	@grep -q cleaned $(BUILD)/crash_cleanup.err
	@grep -q after-cleanup $(BUILD)/crash_cleanup.err

# Optional host integration (vendor, cache, format, doc, includes).
check-special: $(MODC)
	@mkdir -p $(BUILD)
	# --- vendor: happy path, version conflict, symlink refuse, shell inject ---
	@rm -rf $(BUILD)/vrepos $(BUILD)/vendor_app $(BUILD)/vendor_conflict $(BUILD)/vendor_atomic $(BUILD)/vendor_app-bin $(BUILD)/vendor_inject $(BUILD)/vendor-pwned
	@mkdir -p $(BUILD)/vrepos/log $(BUILD)/vrepos/engine $(BUILD)/vrepos/ui $(BUILD)/vrepos/linkdep $(BUILD)/vendor_app $(BUILD)/vendor_conflict $(BUILD)/vendor_atomic/vendor/old $(BUILD)/vendor_inject
	@cp test/vendor_fix/log/mod.mc test/vendor_fix/log/modc.ini $(BUILD)/vrepos/log/
	@cp test/vendor_fix/engine/mod.mc $(BUILD)/vrepos/engine/
	@cp test/vendor_fix/ui/mod.mc $(BUILD)/vrepos/ui/
	@cp test/vendor_fix/app/main.mc $(BUILD)/vendor_app/
	@cd $(BUILD)/vrepos/log && git init -q && git add . && git -c user.email=t@test.com -c user.name=t commit -q -m init && git tag v1
	@printf '[package]\nname = engine\n\n[deps.log]\ngit = file://$(BUILD)/vrepos/log\ntag = v1\n' > $(BUILD)/vrepos/engine/modc.ini
	@printf '[package]\nname = ui\n\n[deps.log]\ngit = file://$(BUILD)/vrepos/log\ntag = v1\n' > $(BUILD)/vrepos/ui/modc.ini
	@cd $(BUILD)/vrepos/engine && git init -q && git add . && git -c user.email=t@test.com -c user.name=t commit -q -m init && git tag v1
	@cd $(BUILD)/vrepos/ui && git init -q && git add . && git -c user.email=t@test.com -c user.name=t commit -q -m init && git tag v1
	@cp test/vendor_fix/log/mod.mc $(BUILD)/vrepos/linkdep/
	@cd $(BUILD)/vrepos/linkdep && ln -s . loop && git init -q && git add . && git -c user.email=t@test.com -c user.name=t commit -q -m init && git tag v1
	@printf '[deps.engine]\ngit = file://$(BUILD)/vrepos/engine\ntag = v1\n\n[deps.ui]\ngit = file://$(BUILD)/vrepos/ui\ntag = v1\n' > $(BUILD)/vendor_app/modc.ini
	./modc vendor -C $(BUILD)/vendor_app
	./modc build $(BUILD)/vendor_app/main.mc -o $(BUILD)/vendor_app-bin
	$(BUILD)/vendor_app-bin
	# Vendored freeze (lock+rev): touching vendor sources must not miss.
	@printf '%s\n' 'import "log";' 'int engine_val() { log_ping(); return 7; } /* touch */' \
		> $(BUILD)/vendor_app/vendor/engine/mod.mc
	./modc build -v $(BUILD)/vendor_app/main.mc -o $(BUILD)/vendor_app-bin \
		2>&1 | tee $(BUILD)/vendor_freeze.log
	@grep -q 'cache hit pkg engine' $(BUILD)/vendor_freeze.log
	@grep -q 'cache hit graph' $(BUILD)/vendor_freeze.log
	# Updating the lock fingerprint must invalidate the freeze.
	@printf '\n# freeze-bust\n' >> $(BUILD)/vendor_app/modc.lock
	./modc build -v $(BUILD)/vendor_app/main.mc -o $(BUILD)/vendor_app-bin \
		2>&1 | tee $(BUILD)/vendor_freeze2.log
	@grep -q 'cache miss pkg engine' $(BUILD)/vendor_freeze2.log
	$(BUILD)/vendor_app-bin
	# Re-vendor, then missing .modc-vendor-rev refuses freeze (source edit misses).
	./modc vendor -C $(BUILD)/vendor_app
	./modc build $(BUILD)/vendor_app/main.mc -o $(BUILD)/vendor_app-bin
	@rm -f $(BUILD)/vendor_app/vendor/engine/.modc-vendor-rev
	@printf '%s\n' 'import "log";' 'int engine_val() { log_ping(); return 7; } /* rev */' \
		> $(BUILD)/vendor_app/vendor/engine/mod.mc
	./modc build -v $(BUILD)/vendor_app/main.mc -o $(BUILD)/vendor_app-bin \
		2>&1 | tee $(BUILD)/vendor_freeze_rev.log
	@grep -q 'cache miss pkg engine' $(BUILD)/vendor_freeze_rev.log
	$(BUILD)/vendor_app-bin
	@printf '\n/* v2 */\n' >> $(BUILD)/vrepos/log/mod.mc
	@cd $(BUILD)/vrepos/log && git add mod.mc && git -c user.email=t@test.com -c user.name=t commit -q -m v2 && git tag v2
	@printf '[package]\nname = ui\n\n[deps.log]\ngit = file://$(BUILD)/vrepos/log\ntag = v2\n' > $(BUILD)/vrepos/ui/modc.ini
	@cd $(BUILD)/vrepos/ui && git add modc.ini && git -c user.email=t@test.com -c user.name=t commit -q -m v2 && git tag v2
	@printf '[deps.engine]\ngit = file://$(BUILD)/vrepos/engine\ntag = v1\n\n[deps.ui]\ngit = file://$(BUILD)/vrepos/ui\ntag = v2\n' > $(BUILD)/vendor_conflict/modc.ini
	@./modc vendor -C $(BUILD)/vendor_conflict >$(BUILD)/vendor_conflict.out 2>&1; test $$? -ne 0
	@grep -Fq 'version conflict for "log"' $(BUILD)/vendor_conflict.out
	@printf '[deps.linkdep]\ngit = file://$(BUILD)/vrepos/linkdep\ntag = v1\n' > $(BUILD)/vendor_atomic/modc.ini
	@./modc vendor -C $(BUILD)/vendor_atomic >$(BUILD)/vendor_atomic.out 2>&1; test $$? -ne 0
	@grep -Fq 'refusing to follow symlink' $(BUILD)/vendor_atomic.out
	@printf '[deps.bad]\ngit = $$(touch $(BUILD)/vendor-pwned)\nrev = 0000000000000000000000000000000000000000\n' > $(BUILD)/vendor_inject/modc.ini
	@./modc vendor -C $(BUILD)/vendor_inject >/dev/null 2>&1; test $$? -ne 0
	@test ! -e $(BUILD)/vendor-pwned
	@printf '[deps.bad]\ngit = --upload-pack=evil\nrev = 0000000000000000000000000000000000000000\n' > $(BUILD)/vendor_inject/modc.ini
	@./modc vendor -C $(BUILD)/vendor_inject >$(BUILD)/vendor_inject.out 2>&1; test $$? -ne 0
	@grep -Fq 'git URL' $(BUILD)/vendor_inject.out
	@printf '[deps.bad]\ngit = file://$(BUILD)/vrepos/log\nrev = --output=/tmp/modc-vendor-pwned\n' > $(BUILD)/vendor_inject/modc.ini
	@./modc vendor -C $(BUILD)/vendor_inject >$(BUILD)/vendor_inject.out 2>&1; test $$? -ne 0
	@grep -Fq 'invalid rev' $(BUILD)/vendor_inject.out
	@test ! -e /tmp/modc-vendor-pwned
	# --- cache: miss, hit, one foreign invalidation ---
	@rm -rf test/.modc-cache
	./modc build -v test/cli_build_test.mc -o $(BUILD)/cache_cli 2>&1 | tee $(BUILD)/cache_cli1.log
	@grep -q 'cache miss graph' $(BUILD)/cache_cli1.log
	./modc build -v test/cli_build_test.mc -o $(BUILD)/cache_cli 2>&1 | tee $(BUILD)/cache_cli2.log
	@grep -q 'cache hit graph' $(BUILD)/cache_cli2.log
	$(BUILD)/cache_cli
	@rm -rf test/.modc-cache
	./modc build -v test/pkg_csrc_test.mc -o $(BUILD)/cache_csrc 2>&1 | tee $(BUILD)/cache_csrc1.log
	@grep -q 'cache miss foreign' $(BUILD)/cache_csrc1.log
	@printf '%s\n' 'int c_add_one(int x); /* cache invalidation */' > test/pkg_csrc/shim/add_one.h
	./modc build -v test/pkg_csrc_test.mc -o $(BUILD)/cache_csrc 2>&1 | tee $(BUILD)/cache_csrc2.log
	@grep -q 'cache miss foreign' $(BUILD)/cache_csrc2.log
	@printf '%s\n' 'int c_add_one(int x);' > test/pkg_csrc/shim/add_one.h
	$(BUILD)/cache_csrc
	# --- cache: partial rebuild must not clobber hit-pkg .deps / linkmeta ---
	@rm -rf $(BUILD)/cache_stale
	@mkdir -p $(BUILD)/cache_stale/roots/pkg_a $(BUILD)/cache_stale/roots/pkg_b \
		$(BUILD)/cache_stale/roots/pkg_lib $(BUILD)/cache_stale/roots/pkg_other \
		$(BUILD)/cache_stale/deps_app $(BUILD)/cache_stale/link_app
	@printf '%s\n' 'enum { SECRET = 7 };' > $(BUILD)/cache_stale/roots/pkg_a/secret.h
	@printf '%s\n' '#include "secret.h"' 'int a_val() { return SECRET; }' \
		> $(BUILD)/cache_stale/roots/pkg_a/mod.mc
	@printf '%s\n' 'int b_val() { return 1; }' > $(BUILD)/cache_stale/roots/pkg_b/mod.mc
	@printf '%s\n' 'import "pkg_a";' 'import "pkg_b";' \
		'int main() { return a_val() + b_val() == 8 ? 0 : 1; }' \
		> $(BUILD)/cache_stale/deps_app/main.mc
	./modc build -M $(BUILD)/cache_stale/roots $(BUILD)/cache_stale/deps_app/main.mc \
		-o $(BUILD)/cache_stale/deps_out
	@printf '%s\n' 'int b_val() { return 1; } /* touch */' \
		> $(BUILD)/cache_stale/roots/pkg_b/mod.mc
	./modc build -M $(BUILD)/cache_stale/roots $(BUILD)/cache_stale/deps_app/main.mc \
		-o $(BUILD)/cache_stale/deps_out -v 2>&1 | tee $(BUILD)/cache_stale/deps_partial.log
	@grep -q 'cache hit pkg pkg_a' $(BUILD)/cache_stale/deps_partial.log
	@grep -q secret.h $(BUILD)/cache_stale/deps_app/.modc-cache/pkg/pkg_a-*/deps
	@printf '%s\n' 'enum { SECRET = 99 };' > $(BUILD)/cache_stale/roots/pkg_a/secret.h
	./modc build -M $(BUILD)/cache_stale/roots $(BUILD)/cache_stale/deps_app/main.mc \
		-o $(BUILD)/cache_stale/deps_out -v 2>&1 | tee $(BUILD)/cache_stale/deps_hdr.log
	@grep -q 'cache miss pkg pkg_a' $(BUILD)/cache_stale/deps_hdr.log
	@printf '%s\n' '#pragma modc c_libs(m)' 'double lib_add(double x) { return x + 1.0; }' \
		> $(BUILD)/cache_stale/roots/pkg_lib/mod.mc
	@printf '%s\n' 'int other_val() { return 1; }' \
		> $(BUILD)/cache_stale/roots/pkg_other/mod.mc
	@printf '%s\n' 'import "pkg_lib";' 'import "pkg_other";' \
		'int main() { return (lib_add(1.0) == 2.0 && other_val() == 1) ? 0 : 1; }' \
		> $(BUILD)/cache_stale/link_app/main.mc
	./modc build -M $(BUILD)/cache_stale/roots $(BUILD)/cache_stale/link_app/main.mc \
		-o $(BUILD)/cache_stale/link_out
	@printf '%s\n' 'int other_val() { return 1; } /* touch */' \
		> $(BUILD)/cache_stale/roots/pkg_other/mod.mc
	./modc build -M $(BUILD)/cache_stale/roots $(BUILD)/cache_stale/link_app/main.mc \
		-o $(BUILD)/cache_stale/link_out -v 2>&1 | tee $(BUILD)/cache_stale/link_partial.log
	@grep -q 'cache hit pkg pkg_lib' $(BUILD)/cache_stale/link_partial.log
	@grep -q -- '-l m' $(BUILD)/cache_stale/link_partial.log
	@grep -Fq 'L m' $(BUILD)/cache_stale/link_app/.modc-cache/linkmeta
	$(BUILD)/cache_stale/link_out
	# --- cache: body-only dep edit keeps importer hit; API change misses ---
	@rm -rf $(BUILD)/cache_iface
	@mkdir -p $(BUILD)/cache_iface/roots/lib $(BUILD)/cache_iface/app
	@printf '%s\n' 'static int secret() { return 7; }' \
		'int lib_val() { return secret(); }' \
		> $(BUILD)/cache_iface/roots/lib/mod.mc
	@printf '%s\n' 'import "lib";' \
		'int main() { return lib_val() == 7 ? 0 : 1; }' \
		> $(BUILD)/cache_iface/app/main.mc
	./modc build -M $(BUILD)/cache_iface/roots $(BUILD)/cache_iface/app/main.mc \
		-o $(BUILD)/cache_iface/out
	$(BUILD)/cache_iface/out
	@printf '%s\n' 'static int secret() { return 7; } /* body */' \
		'int lib_val() { return secret(); }' \
		> $(BUILD)/cache_iface/roots/lib/mod.mc
	./modc build -M $(BUILD)/cache_iface/roots $(BUILD)/cache_iface/app/main.mc \
		-o $(BUILD)/cache_iface/out -v 2>&1 | tee $(BUILD)/cache_iface/body.log
	@grep -q 'cache miss pkg lib' $(BUILD)/cache_iface/body.log
	@grep -q 'cache hit pkg app' $(BUILD)/cache_iface/body.log
	$(BUILD)/cache_iface/out
	@printf '%s\n' 'static int secret() { return 7; }' \
		'int lib_val() { return secret(); }' \
		'int lib_extra() { return 1; }' \
		> $(BUILD)/cache_iface/roots/lib/mod.mc
	./modc build -M $(BUILD)/cache_iface/roots $(BUILD)/cache_iface/app/main.mc \
		-o $(BUILD)/cache_iface/out -v 2>&1 | tee $(BUILD)/cache_iface/api.log
	@grep -q 'cache miss pkg lib' $(BUILD)/cache_iface/api.log
	@grep -q 'cache miss pkg app' $(BUILD)/cache_iface/api.log
	$(BUILD)/cache_iface/out
	# --- cache: modc check reuses exports; body-only keeps importer hit ---
	./modc check -M $(BUILD)/cache_iface/roots $(BUILD)/cache_iface/app/main.mc \
		-v 2>&1 | tee $(BUILD)/cache_iface/check_hit.log
	@grep -q 'cache hit graph' $(BUILD)/cache_iface/check_hit.log
	@printf '%s\n' 'static int secret() { return 7; } /* check-body */' \
		'int lib_val() { return secret(); }' \
		'int lib_extra() { return 1; }' \
		> $(BUILD)/cache_iface/roots/lib/mod.mc
	./modc check -M $(BUILD)/cache_iface/roots $(BUILD)/cache_iface/app/main.mc \
		-v 2>&1 | tee $(BUILD)/cache_iface/check_body.log
	@grep -q 'cache miss pkg lib' $(BUILD)/cache_iface/check_body.log
	@grep -q 'cache hit pkg app' $(BUILD)/cache_iface/check_body.log
	# --- check is parse_only: body type errors ignored; build catches them ---
	@rm -rf $(BUILD)/check_parse_only
	@mkdir -p $(BUILD)/check_parse_only
	@printf '%s\n' 'int main() { return "nope"; }' \
		> $(BUILD)/check_parse_only/main.mc
	./modc check $(BUILD)/check_parse_only/main.mc
	@./modc build $(BUILD)/check_parse_only/main.mc \
		-o $(BUILD)/check_parse_only/out >/dev/null 2>&1; test $$? -ne 0
	# --- cache: same basename packages get distinct ids (no shared meta) ---
	@rm -rf $(BUILD)/cache_pkgid
	@mkdir -p $(BUILD)/cache_pkgid/left/util $(BUILD)/cache_pkgid/right/util \
		$(BUILD)/cache_pkgid/app
	@printf '%s\n' 'int left_util() { return 1; }' \
		> $(BUILD)/cache_pkgid/left/util/mod.mc
	@printf '%s\n' 'int right_util() { return 2; }' \
		> $(BUILD)/cache_pkgid/right/util/mod.mc
	@printf '%s\n' 'import "left/util";' 'import "right/util";' \
		'int main() { return left_util() + right_util() == 3 ? 0 : 1; }' \
		> $(BUILD)/cache_pkgid/app/main.mc
	./modc build -M $(BUILD)/cache_pkgid $(BUILD)/cache_pkgid/app/main.mc \
		-o $(BUILD)/cache_pkgid/out
	$(BUILD)/cache_pkgid/out
	@test $$(ls -d $(BUILD)/cache_pkgid/app/.modc-cache/pkg/util-*/meta | wc -l | tr -d ' ') -eq 2
	@printf '%s\n' 'int left_util() { return 1; } /* touch */' \
		> $(BUILD)/cache_pkgid/left/util/mod.mc
	./modc build -M $(BUILD)/cache_pkgid $(BUILD)/cache_pkgid/app/main.mc \
		-o $(BUILD)/cache_pkgid/out -v 2>&1 | tee $(BUILD)/cache_pkgid/touch.log
	@grep -q 'cache miss pkg util-' $(BUILD)/cache_pkgid/touch.log
	@grep -q 'cache hit pkg util-' $(BUILD)/cache_pkgid/touch.log
	$(BUILD)/cache_pkgid/out
	# --- cache: export bytes are path-stable (same API → identical digest) ---
	@rm -rf $(BUILD)/cache_expstab $(BUILD)/cache_expstab2
	@mkdir -p $(BUILD)/cache_expstab/lib $(BUILD)/cache_expstab/app
	@printf '%s\n' 'int add(int a, int b) { return a + b; }' \
		> $(BUILD)/cache_expstab/lib/mod.mc
	@printf '%s\n' 'import "lib";' 'int main() { return add(1, 2) == 3 ? 0 : 1; }' \
		> $(BUILD)/cache_expstab/app/main.mc
	./modc build -M $(BUILD)/cache_expstab $(BUILD)/cache_expstab/app/main.mc \
		-o $(BUILD)/cache_expstab/out
	@ex1=$$(ls $(BUILD)/cache_expstab/app/.modc-cache/pkg/lib-*/export | head -1); \
		grep -q '^modc-export 1$$' "$$ex1"; \
		grep -q '^pkg -$$' "$$ex1"; \
		mkdir -p $(BUILD)/cache_expstab2/lib $(BUILD)/cache_expstab2/app; \
		cp $(BUILD)/cache_expstab/lib/mod.mc $(BUILD)/cache_expstab2/lib/mod.mc; \
		cp $(BUILD)/cache_expstab/app/main.mc $(BUILD)/cache_expstab2/app/main.mc; \
		./modc build -M $(BUILD)/cache_expstab2 $(BUILD)/cache_expstab2/app/main.mc \
			-o $(BUILD)/cache_expstab2/out; \
		ex2=$$(ls $(BUILD)/cache_expstab2/app/.modc-cache/pkg/lib-*/export | head -1); \
		cmp -s "$$ex1" "$$ex2"
	# --- cache: MODC_SYSINCLUDE / MODC_INCLUDE change must miss ---
	@rm -rf $(BUILD)/cache_sysinc
	@mkdir -p $(BUILD)/cache_sysinc/inc_a $(BUILD)/cache_sysinc/inc_b \
		$(BUILD)/cache_sysinc/modc_a $(BUILD)/cache_sysinc/modc_b \
		$(BUILD)/cache_sysinc/app
	# MODC_INCLUDE replaces host stubs; seed both trees so crash can rebuild.
	@cp -R src/host/include/. $(BUILD)/cache_sysinc/modc_a/
	@cp -R src/host/include/. $(BUILD)/cache_sysinc/modc_b/
	@printf '%s\n' 'enum { SYSINC_VAL = 1 };' > $(BUILD)/cache_sysinc/inc_a/sysval.h
	@printf '%s\n' 'enum { SYSINC_VAL = 2 };' > $(BUILD)/cache_sysinc/inc_b/sysval.h
	@printf '%s\n' 'enum { MODCINC_VAL = 10 };' > $(BUILD)/cache_sysinc/modc_a/modcval.h
	@printf '%s\n' 'enum { MODCINC_VAL = 20 };' > $(BUILD)/cache_sysinc/modc_b/modcval.h
	@printf '%s\n' '#include <sysval.h>' '#include <modcval.h>' \
		'int main() { return SYSINC_VAL + MODCINC_VAL == 11 ? 0 : 1; }' \
		> $(BUILD)/cache_sysinc/app/main.mc
	MODC_NO_SYSTEM_INCLUDES=1 MODC_SYSINCLUDE=$(BUILD)/cache_sysinc/inc_a \
		MODC_INCLUDE=$(BUILD)/cache_sysinc/modc_a \
		./modc build $(BUILD)/cache_sysinc/app/main.mc -o $(BUILD)/cache_sysinc/out
	$(BUILD)/cache_sysinc/out
	MODC_NO_SYSTEM_INCLUDES=1 MODC_SYSINCLUDE=$(BUILD)/cache_sysinc/inc_a \
		MODC_INCLUDE=$(BUILD)/cache_sysinc/modc_a \
		./modc build $(BUILD)/cache_sysinc/app/main.mc -o $(BUILD)/cache_sysinc/out -v 2>&1 \
		| tee $(BUILD)/cache_sysinc/hit.log
	@grep -q 'cache hit' $(BUILD)/cache_sysinc/hit.log
	MODC_NO_SYSTEM_INCLUDES=1 MODC_SYSINCLUDE=$(BUILD)/cache_sysinc/inc_b \
		MODC_INCLUDE=$(BUILD)/cache_sysinc/modc_a \
		./modc build $(BUILD)/cache_sysinc/app/main.mc -o $(BUILD)/cache_sysinc/out -v 2>&1 \
		| tee $(BUILD)/cache_sysinc/sysmiss.log
	@grep -q 'cache miss' $(BUILD)/cache_sysinc/sysmiss.log
	@printf '%s\n' '#include <sysval.h>' '#include <modcval.h>' \
		'int main() { return SYSINC_VAL + MODCINC_VAL == 12 ? 0 : 1; }' \
		> $(BUILD)/cache_sysinc/app/main.mc
	MODC_NO_SYSTEM_INCLUDES=1 MODC_SYSINCLUDE=$(BUILD)/cache_sysinc/inc_b \
		MODC_INCLUDE=$(BUILD)/cache_sysinc/modc_a \
		./modc build $(BUILD)/cache_sysinc/app/main.mc -o $(BUILD)/cache_sysinc/out
	$(BUILD)/cache_sysinc/out
	MODC_NO_SYSTEM_INCLUDES=1 MODC_SYSINCLUDE=$(BUILD)/cache_sysinc/inc_b \
		MODC_INCLUDE=$(BUILD)/cache_sysinc/modc_b \
		./modc build $(BUILD)/cache_sysinc/app/main.mc -o $(BUILD)/cache_sysinc/out2 -v 2>&1 \
		| tee $(BUILD)/cache_sysinc/modcmiss.log
	@grep -q 'cache miss' $(BUILD)/cache_sysinc/modcmiss.log
	@printf '%s\n' '#include <sysval.h>' '#include <modcval.h>' \
		'int main() { return SYSINC_VAL + MODCINC_VAL == 22 ? 0 : 1; }' \
		> $(BUILD)/cache_sysinc/app/main.mc
	MODC_NO_SYSTEM_INCLUDES=1 MODC_SYSINCLUDE=$(BUILD)/cache_sysinc/inc_b \
		MODC_INCLUDE=$(BUILD)/cache_sysinc/modc_b \
		./modc build $(BUILD)/cache_sysinc/app/main.mc -o $(BUILD)/cache_sysinc/out2
	$(BUILD)/cache_sysinc/out2
	# --- cache: nested package headers participate in impl hash ---
	@rm -rf $(BUILD)/cache_nested
	@mkdir -p $(BUILD)/cache_nested/pkg/sub $(BUILD)/cache_nested/app
	@printf '%s\n' 'enum { NEST = 3 };' > $(BUILD)/cache_nested/pkg/sub/nest.h
	@printf '%s\n' '#include "sub/nest.h"' 'int nest_val() { return NEST; }' \
		> $(BUILD)/cache_nested/pkg/mod.mc
	@printf '%s\n' 'import "pkg";' 'int main() { return nest_val() == 3 ? 0 : 1; }' \
		> $(BUILD)/cache_nested/app/main.mc
	./modc build -M $(BUILD)/cache_nested $(BUILD)/cache_nested/app/main.mc \
		-o $(BUILD)/cache_nested/out
	$(BUILD)/cache_nested/out
	@printf '%s\n' 'enum { NEST = 9 };' > $(BUILD)/cache_nested/pkg/sub/nest.h
	./modc build -M $(BUILD)/cache_nested $(BUILD)/cache_nested/app/main.mc \
		-o $(BUILD)/cache_nested/out -v 2>&1 | tee $(BUILD)/cache_nested/miss.log
	@grep -q 'cache miss pkg pkg' $(BUILD)/cache_nested/miss.log
	@printf '%s\n' 'import "pkg";' 'int main() { return nest_val() == 9 ? 0 : 1; }' \
		> $(BUILD)/cache_nested/app/main.mc
	./modc build -M $(BUILD)/cache_nested $(BUILD)/cache_nested/app/main.mc \
		-o $(BUILD)/cache_nested/out
	$(BUILD)/cache_nested/out
	# --- format goldens ---
	cp test/format/messy.mc $(BUILD)/format_messy.mc
	./modc format $(BUILD)/format_messy.mc
	diff -u test/format/want.mc $(BUILD)/format_messy.mc
	cp test/format/str_style.mc $(BUILD)/format_str_style.mc
	./modc format $(BUILD)/format_str_style.mc
	diff -u test/format/want_str_style.mc $(BUILD)/format_str_style.mc
	# --- doc ---
	./modc doc -M test docpkg | grep -q 'add returns the sum'
	./modc doc -M test docpkg.add | grep -q 'add(int a, int b)'
	# --- includes: -F link + hermetic nosys ---
	./modc build -Ftest/fwk_root test/fwk_include_ok.mc -o $(BUILD)/fwk_include-bin
	$(BUILD)/fwk_include-bin
	@./modc check test/sys_include_run.mc >$(BUILD)/bad_nosys.out 2>&1; test $$? -ne 0
	@grep -Fq 'cannot find include file modc_system_probe.h' $(BUILD)/bad_nosys.out

install: $(MODC)
	install -d -m 755 $(DESTDIR)$(BINDIR) $(DESTDIR)$(MODCLIB)/include $(DESTDIR)$(MODCLIB)/pkg
	install -m 755 $(MODC) $(DESTDIR)$(BINDIR)/modc
	cp -R $(HOST)/. $(DESTDIR)$(MODCLIB)/include/
	rm -rf $(DESTDIR)$(MODCLIB)/pkg/str $(DESTDIR)$(MODCLIB)/pkg/arena \
		$(DESTDIR)$(MODCLIB)/pkg/path $(DESTDIR)$(MODCLIB)/pkg/fs \
		$(DESTDIR)$(MODCLIB)/pkg/os $(DESTDIR)$(MODCLIB)/pkg/tty
	cp -R $(ROOT)/str $(DESTDIR)$(MODCLIB)/pkg/str
	cp -R $(ROOT)/arena $(DESTDIR)$(MODCLIB)/pkg/arena
	cp -R $(ROOT)/path $(DESTDIR)$(MODCLIB)/pkg/path
	cp -R $(ROOT)/fs $(DESTDIR)$(MODCLIB)/pkg/fs
	cp -R $(ROOT)/os $(DESTDIR)$(MODCLIB)/pkg/os
	cp -R $(ROOT)/tty $(DESTDIR)$(MODCLIB)/pkg/tty
	# cp -R keeps the source tree's modes (often 600/700 under Dropbox); normalize.
	find $(DESTDIR)$(MODCLIB)/include $(DESTDIR)$(MODCLIB)/pkg -type d -exec chmod 755 {} +
	find $(DESTDIR)$(MODCLIB)/include $(DESTDIR)$(MODCLIB)/pkg -type f -exec chmod 644 {} +

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/modc
	rm -rf $(DESTDIR)$(MODCLIB)

# Rebuild with AddressSanitizer / UBSan (clears prior objs first).
asan:
	$(MAKE) clean
	$(MAKE) all ASAN=1

ubsan:
	$(MAKE) clean
	$(MAKE) all UBSAN=1

clean:
	rm -rf $(BUILD) $(MODC)
