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

SRCS := src/main.c src/cli_build.c src/cli_cmd.c src/cli_selftest.c src/diag.c src/lex.c src/pp.c src/parse.c src/type.c src/format.c src/symbol.c src/check.c src/emit.c src/pkg.c src/fmt.c src/vendor.c src/host_os.c src/cache.c src/export.c
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
	@$(BUILD)/bounds_oob >/dev/null 2>&1; test $$? -ne 0

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
