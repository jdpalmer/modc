# %C / modc — hosted C99 compiler (QBE backend)
#
# stdio/stdlib only. No lib9, no yacc.

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wno-unused
ROOT := $(abspath .)
.PHONY: all clean check check-special install uninstall format format-check

PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  := $(PREFIX)/bin
MODCLIB := $(PREFIX)/lib/modc
HOST := $(ROOT)/src/host/include
BUILD := $(ROOT)/build
MODC := $(ROOT)/modc

SRCS := src/main.c src/cli_build.c src/cli_cmd.c src/diag.c src/lex.c src/pp.c src/parse.c src/type.c src/symbol.c src/check.c src/emit.c src/pkg.c src/fmt.c src/vendor.c src/host_os.c src/cache.c
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

CFLAGS += -DMODC_INCLUDE=\"$(HOST)\" -DMODC_PKG=\"$(ROOT)\"

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
		! -path './test/pp_file_line.mc' \
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
		! -path './test/pp_file_line.mc' \
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
	./scripts/check-corpus.sh
	@$(MAKE) --no-print-directory check-special

check-special: $(MODC)
	@mkdir -p $(BUILD)
	./modc build test/pkg_clib_main.mc -o $(BUILD)/pkg_clib-bin
	$(BUILD)/pkg_clib-bin
	./modc build test/pkg_csrc_main.mc -o $(BUILD)/pkg_csrc-bin
	$(BUILD)/pkg_csrc-bin
	./modc build test/pkg_encap_main.mc -o $(BUILD)/pkg_encap-bin
	$(BUILD)/pkg_encap-bin
	./modc build test/pkg_xinline_main.mc -o $(BUILD)/pkg_xinline-bin
	$(BUILD)/pkg_xinline-bin
	./modc build test/pkg_order_main.mc -o $(BUILD)/pkg_order-bin
	$(BUILD)/pkg_order-bin
	./modc build test/pkg_method_order_main.mc -o $(BUILD)/pkg_method_order-bin
	$(BUILD)/pkg_method_order-bin
	@rm -rf $(BUILD)/vrepos $(BUILD)/vendor_app $(BUILD)/vendor_conflict $(BUILD)/vendor_app-bin
	@mkdir -p $(BUILD)/vrepos/log $(BUILD)/vrepos/engine $(BUILD)/vrepos/ui $(BUILD)/vendor_app $(BUILD)/vendor_conflict
	@cp test/vendor_fix/log/mod.mc test/vendor_fix/log/modc.ini $(BUILD)/vrepos/log/
	@cp test/vendor_fix/engine/mod.mc $(BUILD)/vrepos/engine/
	@cp test/vendor_fix/ui/mod.mc $(BUILD)/vrepos/ui/
	@cp test/vendor_fix/app/main.mc $(BUILD)/vendor_app/
	@cd $(BUILD)/vrepos/log && git init -q && git add . && git -c user.email=t@test.com -c user.name=t commit -q -m init && git tag v1
	@printf '[package]\nname = engine\n\n[deps.log]\ngit = file://$(BUILD)/vrepos/log\ntag = v1\n' > $(BUILD)/vrepos/engine/modc.ini
	@printf '[package]\nname = ui\n\n[deps.log]\ngit = file://$(BUILD)/vrepos/log\ntag = v1\n' > $(BUILD)/vrepos/ui/modc.ini
	@cd $(BUILD)/vrepos/engine && git init -q && git add . && git -c user.email=t@test.com -c user.name=t commit -q -m init && git tag v1
	@cd $(BUILD)/vrepos/ui && git init -q && git add . && git -c user.email=t@test.com -c user.name=t commit -q -m init && git tag v1
	@printf '[deps.engine]\ngit = file://$(BUILD)/vrepos/engine\ntag = v1\n\n[deps.ui]\ngit = file://$(BUILD)/vrepos/ui\ntag = v1\n' > $(BUILD)/vendor_app/modc.ini
	./modc vendor -C $(BUILD)/vendor_app
	@test -d $(BUILD)/vendor_app/vendor/log
	@test -d $(BUILD)/vendor_app/vendor/engine
	@test -d $(BUILD)/vendor_app/vendor/ui
	./modc vendor --check -C $(BUILD)/vendor_app
	./modc build $(BUILD)/vendor_app/main.mc -o $(BUILD)/vendor_app-bin
	$(BUILD)/vendor_app-bin
	@printf '\n/* v2 */\n' >> $(BUILD)/vrepos/log/mod.mc
	@cd $(BUILD)/vrepos/log && git add mod.mc && git -c user.email=t@test.com -c user.name=t commit -q -m v2 && git tag v2
	@printf '[package]\nname = ui\n\n[deps.log]\ngit = file://$(BUILD)/vrepos/log\ntag = v2\n' > $(BUILD)/vrepos/ui/modc.ini
	@cd $(BUILD)/vrepos/ui && git add modc.ini && git -c user.email=t@test.com -c user.name=t commit -q -m v2 && git tag v2
	@printf '[deps.engine]\ngit = file://$(BUILD)/vrepos/engine\ntag = v1\n\n[deps.ui]\ngit = file://$(BUILD)/vrepos/ui\ntag = v2\n' > $(BUILD)/vendor_conflict/modc.ini
	@./modc vendor -C $(BUILD)/vendor_conflict >$(BUILD)/vendor_conflict.out 2>&1; test $$? -ne 0
	@grep -Fq 'version conflict for "log"' $(BUILD)/vendor_conflict.out
	@rm -rf $(BUILD)/vendor_inject $(BUILD)/vendor-pwned
	@mkdir -p $(BUILD)/vendor_inject
	@printf '[deps.bad]\ngit = $$(touch $(BUILD)/vendor-pwned)\nrev = 0000000000000000000000000000000000000000\n' > $(BUILD)/vendor_inject/modc.ini
	@./modc vendor -C $(BUILD)/vendor_inject >/dev/null 2>&1; test $$? -ne 0
	@test ! -e $(BUILD)/vendor-pwned
	./modc help vendor > /dev/null
	./modc build test/cli_build.mc -o $(BUILD)/cli_build-bin
	$(BUILD)/cli_build-bin
	@rm -rf test/.modc-cache
	./modc build -v test/cli_build.mc -o $(BUILD)/cache_cli 2>&1 | tee $(BUILD)/cache_cli1.log
	@grep -q 'cache miss graph' $(BUILD)/cache_cli1.log
	./modc build -v test/cli_build.mc -o $(BUILD)/cache_cli 2>&1 | tee $(BUILD)/cache_cli2.log
	@grep -q 'cache hit graph' $(BUILD)/cache_cli2.log
	@grep -q 'cache hit pkg' $(BUILD)/cache_cli2.log
	$(BUILD)/cache_cli
	@rm -rf test/.modc-cache
	./modc build -v test/pkg_csrc_main.mc -o $(BUILD)/cache_csrc 2>&1 | tee $(BUILD)/cache_csrc1.log
	@grep -q 'cache miss foreign' $(BUILD)/cache_csrc1.log
	./modc build -v test/pkg_csrc_main.mc -o $(BUILD)/cache_csrc 2>&1 | tee $(BUILD)/cache_csrc2.log
	@grep -q 'cache hit foreign' $(BUILD)/cache_csrc2.log
	@grep -q 'cache hit graph' $(BUILD)/cache_csrc2.log
	@printf '%s\n' 'int c_add_one(int x); /* cache invalidation */' > test/pkg_csrc/shim/add_one.h
	./modc build -v test/pkg_csrc_main.mc -o $(BUILD)/cache_csrc 2>&1 | tee $(BUILD)/cache_csrc3.log
	@grep -q 'cache miss pkg pkg_csrc' $(BUILD)/cache_csrc3.log
	@grep -q 'cache miss foreign' $(BUILD)/cache_csrc3.log
	@printf '%s\n' 'int c_add_one(int x);' > test/pkg_csrc/shim/add_one.h
	$(BUILD)/cache_csrc
	@rm -rf test/cache_two/.modc-cache
	./modc build -v test/cache_two/main.mc -o $(BUILD)/cache_two 2>&1 | tee $(BUILD)/cache_two1.log
	@grep -q 'cache miss graph' $(BUILD)/cache_two1.log
	./modc build -v test/cache_two/main.mc -o $(BUILD)/cache_two 2>&1 | tee $(BUILD)/cache_two2.log
	@grep -q 'cache hit graph' $(BUILD)/cache_two2.log
	@printf '%s\n' 'import "leaf";' '' 'int main() {' '	return leaf_add(21, 21) == 42 ? 0: 1;' '}' > test/cache_two/main.mc
	./modc build -v test/cache_two/main.mc -o $(BUILD)/cache_two 2>&1 | tee $(BUILD)/cache_two3.log
	@grep -q 'cache miss pkg leaf' $(BUILD)/cache_two3.log
	@grep -q 'cache miss pkg main_mc' $(BUILD)/cache_two3.log
	$(BUILD)/cache_two
	@printf '%s\n' 'import "leaf";' '' 'int main() {' '	return leaf_add(20, 22) == 42 ? 0: 1;' '}' > test/cache_two/main.mc
	./modc build test/cache_two/main.mc -o $(BUILD)/cache_two >/dev/null
	@printf '%s\n' 'int leaf_add(int a, int b) {' '	return a + b;' '}' '' 'static int leaf_priv() {' '	return 2;' '}' > test/cache_two/leaf/mod.mc
	./modc build -v test/cache_two/main.mc -o $(BUILD)/cache_two 2>&1 | tee $(BUILD)/cache_two5.log
	@grep -q 'cache miss pkg leaf' $(BUILD)/cache_two5.log
	@grep -q 'cache miss pkg main_mc' $(BUILD)/cache_two5.log
	$(BUILD)/cache_two
	@printf '%s\n' 'double leaf_add(int a, int b) {' '	return a + b;' '}' '' 'static int leaf_priv() {' '	return 1;' '}' > test/cache_two/leaf/mod.mc
	./modc build -v test/cache_two/main.mc -o $(BUILD)/cache_two 2>&1 | tee $(BUILD)/cache_two4.log
	@grep -q 'cache miss pkg leaf' $(BUILD)/cache_two4.log
	@grep -q 'cache miss pkg main_mc' $(BUILD)/cache_two4.log
	@printf '%s\n' 'int leaf_add(int a, int b) {' '	return a + b;' '}' '' 'static int leaf_priv() {' '	return 1;' '}' > test/cache_two/leaf/mod.mc
	./modc clean -v test/cli_build.mc 2>&1 | tee $(BUILD)/cache_clean.log
	@grep -q 'modc clean: removed' $(BUILD)/cache_clean.log
	@test ! -d test/.modc-cache
	./modc clean test/cli_build.mc
	./modc help clean > /dev/null
	./modc build test/cli_dirbuild -o $(BUILD)/cli_dirbuild-bin
	$(BUILD)/cli_dirbuild-bin
	./modc build test/cli_dirbuild_imp -o $(BUILD)/cli_dirbuild_imp-bin
	$(BUILD)/cli_dirbuild_imp-bin
	./modc build test/project_root/cmd/app -o $(BUILD)/project-root-bin
	$(BUILD)/project-root-bin
	./modc clean test/project_root/cmd/app
	./modc build test/pkg_shell -o '$(BUILD)/shell;literal-bin'
	'$(BUILD)/shell;literal-bin'
	./modc clean test/pkg_shell
	@rm -f $(BUILD)/cli_build $(BUILD)/cli_dirbuild
	./modc build test/cli_build.mc && test -x cli_build && mv cli_build $(BUILD)/cli_build-default
	$(BUILD)/cli_build-default
	(cd test/cli_dirbuild && $(MODC) build -o $(BUILD)/cli_dirbuild-dot)
	$(BUILD)/cli_dirbuild-dot
	./modc run test/cli_build.mc
	./modc run test/stdio_smoke.mc
	./modc run test/cli_dirbuild
	./modc run test/cli_args_test.mc -- a b
	./modc test test/cli_args_test.mc -- a b
	./scripts/check-limits.sh
	./modc build -Ftest/fwk_root test/fwk_include.mc -o $(BUILD)/fwk_include-bin
	$(BUILD)/fwk_include-bin
ifeq ($(shell uname -s),Darwin)
	./modc build -v test/fwk_pragma.mc -o $(BUILD)/fwk_pragma-bin 2>&1 | grep -q -- '-framework Cocoa'
	$(BUILD)/fwk_pragma-bin
	@env -u MODC_NO_SYSTEM_INCLUDES -u MODC_SYSINCLUDE ./modc check -v test/add.mc 2>&1 | grep -q 'framework path:'
endif
	@env -u MODC_NO_SYSTEM_INCLUDES -u MODC_SYSINCLUDE ./modc check -v test/add.mc 2>&1 | grep -q 'system include:'
	@./modc check test/sys_include.mc >$(BUILD)/bad_nosys.out 2>&1; test $$? -ne 0
	@grep -Fq 'cannot find include file modc_system_probe.h' $(BUILD)/bad_nosys.out
	./modc help build > /dev/null
	./modc --version > /dev/null
	./modc check test/testdriver
	./modc test -M test test/testdriver
	./modc doc -M test docpkg | grep -q 'add returns the sum'
	./modc doc -M test docpkg.add | grep -q 'add(int a, int b)'
	@./modc doc -M test docpkg.nosuch >/dev/null 2>&1; test $$? -ne 0
	@./modc doc -M test docpkg 2>/dev/null | grep -q hide; test $$? -ne 0
	cp test/format/messy.mc $(BUILD)/format_messy.mc
	./modc format $(BUILD)/format_messy.mc
	diff -u test/format/want.mc $(BUILD)/format_messy.mc
	./modc format $(BUILD)/format_messy.mc
	diff -u test/format/want.mc $(BUILD)/format_messy.mc
	cp test/format/str_style.mc $(BUILD)/format_str_style.mc
	./modc format $(BUILD)/format_str_style.mc
	diff -u test/format/want_str_style.mc $(BUILD)/format_str_style.mc
	@rm -rf $(BUILD)/destdir
	$(MAKE) install DESTDIR=$(BUILD)/destdir PREFIX=/usr/local
	@test -f $(BUILD)/destdir/usr/local/lib/modc/pkg/str/mod.mc
	@test -f $(BUILD)/destdir/usr/local/lib/modc/pkg/arena/mod.mc
	@test -f $(BUILD)/destdir/usr/local/lib/modc/pkg/path/mod.mc
	@test -f $(BUILD)/destdir/usr/local/lib/modc/pkg/fs/mod.mc
	@test -f $(BUILD)/destdir/usr/local/lib/modc/pkg/os/mod.mc
	@test -f $(BUILD)/destdir/usr/local/lib/modc/pkg/tty/mod.mc
	$(BUILD)/destdir/usr/local/bin/modc check test/str_pkg.mc
	$(BUILD)/destdir/usr/local/bin/modc check test/arena_pkg.mc
	$(BUILD)/destdir/usr/local/bin/modc check test/path_pkg.mc
	$(BUILD)/destdir/usr/local/bin/modc check test/fs_pkg.mc
	$(BUILD)/destdir/usr/local/bin/modc check test/os_pkg.mc
	$(BUILD)/destdir/usr/local/bin/modc check test/tty_pkg.mc
	@env -u MODC_NO_SYSTEM_INCLUDES $(BUILD)/destdir/usr/local/bin/modc check -v test/str_pkg.mc 2>&1 | grep -q 'modc pkg:'

install: $(MODC)
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MODCLIB)/include $(DESTDIR)$(MODCLIB)/pkg
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

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/modc
	rm -rf $(DESTDIR)$(MODCLIB)

clean:
	rm -rf $(BUILD) $(MODC)
