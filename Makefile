CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g

# Install layout. Override PREFIX to install elsewhere, e.g. `make install PREFIX=$HOME/.local`.
PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
DATADIR := $(PREFIX)/share/gbasic
STDLIBDIR := $(DATADIR)/stdlib
# Apache-2.0 requires the licence to travel with the work, so an install that
# drops it is not a compliant distribution.
DOCDIR := $(PREFIX)/share/doc/gbasic
# Baked-in fallback the loader searches when GBASIC_PATH does not resolve a library,
# so an installed `gbasic` finds its stdlib without any environment setup.
CFLAGS += -DGBASIC_DEFAULT_STDLIB='"$(STDLIBDIR)"'

# Because that path is COMPILED IN, changing PREFIX has to invalidate the objects
# that carry it -- and make cannot see a changed -D on its own. Without this, the
# sequence the comment above actually recommends is silently broken:
#
#     make                              # bakes /usr/local/share/gbasic/stdlib
#     make install PREFIX=$HOME/.local  # installs there, binary still looks in /usr/local
#
# make sees `gbasic` already built and up to date, so it installs the OLD binary
# under the new prefix. Nothing errors; `load frame` just fails later, or worse
# silently resolves against a different gBASIC's stdlib in /usr/local. The stamp
# records the STDLIBDIR each object was built with; only main.o and eval.o read
# the macro, so only they rebuild when it changes. The rule itself lives further
# down, below `all` -- a target defined up here would become the DEFAULT GOAL and
# a bare `make` would build nothing but the stamp.
GTK_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists gtk+-3.0 && printf 1 || printf 0)
GTK_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs gtk+-3.0 2>/dev/null)
LIBPQ_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libpq && printf 1 || printf 0)
LIBPQ_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags libpq 2>/dev/null)
LIBPQ_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs libpq 2>/dev/null)
SQLITE3_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sqlite3 && printf 1 || printf 0)
SQLITE3_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags sqlite3 2>/dev/null)
SQLITE3_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs sqlite3 2>/dev/null)
ODBC_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists odbc && printf 1 || printf 0)
ODBC_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags odbc 2>/dev/null)
ODBC_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs odbc 2>/dev/null)
LDAP_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists ldap && printf 1 || printf 0)
LDAP_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags ldap 2>/dev/null)
LDAP_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs ldap lber 2>/dev/null)
LIBCURL_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcurl && printf 1 || printf 0)
LIBCURL_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags libcurl 2>/dev/null)
LIBCURL_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs libcurl 2>/dev/null)
LIBXCRYPT_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libxcrypt && printf 1 || printf 0)
LIBXCRYPT_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags libxcrypt 2>/dev/null)
LIBXCRYPT_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs libxcrypt 2>/dev/null)

LIBCRYPTO_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcrypto && printf 1 || printf 0)
LIBCRYPTO_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags libcrypto 2>/dev/null)
LIBCRYPTO_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs libcrypto 2>/dev/null)
# libssl (TLS for the webserver, PLAT-WEB-3) is detected separately from
# libcrypto: the crypto builtins need only -lcrypto, and a machine can carry
# one without the other. Compiled out, webserver.listen with a tls: option
# raises the usual clean "not available in this build" error.
LIBSSL_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libssl && printf 1 || printf 0)
LIBSSL_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags libssl 2>/dev/null)
LIBSSL_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs libssl 2>/dev/null)

LIBXML2_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libxml-2.0 && printf 1 || printf 0)
LIBXML2_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags libxml-2.0 2>/dev/null)
LIBXML2_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs libxml-2.0 2>/dev/null)

# zlib, for the xlsx module's ZIP container (docs/xlsx_design.md §13.B). An
# .xlsx is a ZIP of XML parts; zlib supplies inflate/deflate and the container
# itself -- central directory, local headers, CRCs -- is ours. Chosen over
# libzip/minizip because zlib is effectively universal and those are not
# installed on the development or RISC-V targets, so requiring them would gate
# every build and test behind a dependency install.
ZLIB_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists zlib && printf 1 || printf 0)
ZLIB_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags zlib 2>/dev/null)
ZLIB_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs zlib 2>/dev/null)

# GObject-Introspection bridge (gi.* module). Targets the modern GLib-merged
# libgirepository (girepository-2.0, gi_repository_* API, GLib >= 2.80); the
# legacy 1.x gobject-introspection-1.0 API is intentionally NOT supported.
#
# `--exists` IS the version floor here, and deliberately so: girepository-2.0
# did not exist before GLib 2.80 (that is the release the library was merged
# into GLib), so a system that has the module has at least 2.80.
#
# The floor that bites is therefore not this one but the API's own. Symbols kept
# arriving after 2.80, and using one adds a floor NOTHING HERE CHECKS -- the
# build simply fails to LINK, and it takes the whole gbasic binary with it, not
# just the gi module. That happened: gi_repository_dup_default() is absent in
# 2.80.0 (Ubuntu 24.04 LTS) and 2.84.1 (25.04) and present in 2.88.0, so the
# current LTS could not build gBASIC at all while every dev box here was fine.
# Fixed by using gi_repository_new(), which exists across all three.
#
# So: when adding a gi_* call, check it exists in 2.80, or raise this floor to
# a `--atleast-version` and say why. Verified by BUILDING in an ubuntu:24.04
# container, which is the only thing that actually proves it.
GIR_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists girepository-2.0 && printf 1 || printf 0)
GIR_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags girepository-2.0 2>/dev/null)
GIR_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs girepository-2.0 2>/dev/null)

# GIO (NAP-12 row-model adapter). `GListModel` is a GIO interface, and
# girepository-2.0 links only gobject/glib — so the DataGrid's native
# adapter needs gio-2.0 explicitly. Independent of GTK: the adapter
# implements a GIO interface and is never linked against GTK itself.
GIO_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists gio-2.0 && printf 1 || printf 0)
GIO_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags gio-2.0 2>/dev/null)
GIO_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs gio-2.0 2>/dev/null)

# WINDOWS: NO OPTIONAL MODULE IS AUTO-DETECTED. An MSYS2 install carries
# libcurl, OpenSSL, libxml2, sqlite3 and zlib as other packages' dependencies,
# so detection finds them -- and the build then fails to LINK, because the
# Windows binary is static (gbasic.exe must need nothing beside it) and what
# pkg-config describes is the DLL form. Each module arrives deliberately, as its
# milestone does (docs/windows_port_plan.md §6: M2 ODBC, M3 xlsx, M4 sqlite,
# M5 libcurl), with its static link worked out and measured. A command-line
# override (make LIBXML2_AVAILABLE=1) still wins over these, as it does on Linux.
ifeq ($(OS),Windows_NT)
GTK_AVAILABLE := 0
LIBPQ_AVAILABLE := 0
# M4: sqlite, STATICALLY. One library and zlib (its pkg-config Libs.private),
# and its header declares no dllimport, so no _STATIC define is needed the way
# libxml2's is. Unlocks dbframe, edgar and screener.
SQLITE3_AVAILABLE := 1
SQLITE3_CFLAGS :=
SQLITE3_LIBS := -lsqlite3 -lz
# ODBC IS ON, and is the one exception: its driver manager is odbc32.dll,
# which ships WITH Windows, so linking it costs the download nothing and needs
# nothing installed (milestone M2, docs/windows_port_plan.md §4). The drivers
# themselves are the operator's, exactly as on Linux.
ODBC_AVAILABLE := 1
ODBC_CFLAGS :=
ODBC_LIBS := -lodbc32
LDAP_AVAILABLE := 0
# M5: libcurl, STATICALLY, over SCHANNEL -- Windows' own TLS, which trusts the
# Windows certificate store and needs no OpenSSL. Built by
# tools/build-curl-windows.sh (pinned version and SHA-256) into
# ~/gbasic-deps/curl-schannel, and switched on exactly when that library
# exists, so a machine that has not built it still builds a gbasic.exe -- with
# webclient/http/smtp refusing cleanly, the HAVE_* contract. Its dependencies
# are zlib and Windows' own libraries: no LGPL (§19 of the status doc).
CURL_WIN_PREFIX ?= $(shell cygpath -m "$$HOME/gbasic-deps/curl-schannel" 2>/dev/null)
ifneq ($(wildcard $(CURL_WIN_PREFIX)/lib/libcurl.a),)
LIBCURL_AVAILABLE := 1
LIBCURL_CFLAGS := -I$(CURL_WIN_PREFIX)/include -DCURL_STATICLIB
LIBCURL_LIBS := $(CURL_WIN_PREFIX)/lib/libcurl.a -lsecur32 -lbcrypt -ladvapi32 -lcrypt32 -lz -lws2_32 -liphlpapi
else
LIBCURL_AVAILABLE := 0
endif
LIBXCRYPT_AVAILABLE := 0
YESCRYPT_VENDORED := 1
# Crypto builtins over OpenSSL's libcrypto, STATICALLY -- the same code the
# Linux build runs, Apache-2.0. MSYS2's openssl package carries the archive, so
# nothing is downloaded. It is the largest single cost in the binary (measured
# stripped 4.5 -> 9.3 MB, OpenSSL 3 linking its provider layer for the thirteen
# builtins used); Windows' own CNG was the alternative and lacks Ed25519 and
# scrypt outright (status doc §21). libssl stays off: it serves only the
# webserver's TLS, which needs a listener this port does not have yet.
LIBCRYPTO_AVAILABLE := 1
LIBCRYPTO_CFLAGS :=
LIBCRYPTO_LIBS := -lcrypto -lws2_32 -lcrypt32 -luser32 -ladvapi32 -lbcrypt
LIBSSL_AVAILABLE := 0
# M3: zlib + libxml2, STATICALLY, for xml and xlsx -- the headline Windows
# feature (docs/windows_port_plan.md §4: the Linux tarballs omit xlsx because
# libxml2's soname varies across distributions, a problem a static Windows
# build does not have). LIBXML_STATIC is not optional: without it libxml2's
# headers declare every function __declspec(dllimport) and the static archive
# cannot satisfy them. The dependency list is pkg-config --static's LESS
# -liconv: libiconv is LGPL, and the three functions libxml2 takes from it are
# supplied by src/platform_win32.c over the Windows code pages instead, so no
# LGPL code is linked into gbasic.exe (tests/windows/xml_encodings.bas).
LIBXML2_AVAILABLE := 1
LIBXML2_CFLAGS := $(shell pkg-config --cflags libxml-2.0 2>/dev/null) -DLIBXML_STATIC
LIBXML2_LIBS := -lxml2 -lbcrypt -lz
ZLIB_AVAILABLE := 1
ZLIB_CFLAGS :=
ZLIB_LIBS := -lz
GIR_AVAILABLE := 0
GIO_AVAILABLE := 0
endif

ifeq ($(GTK_AVAILABLE),1)
CFLAGS += -DHAVE_GTK=1 $(GTK_CFLAGS)
LDLIBS += $(GTK_LIBS) -lm
else
CFLAGS += -DHAVE_GTK=0
LDLIBS += -lm
endif

ifeq ($(LIBPQ_AVAILABLE),1)
CFLAGS += -DHAVE_LIBPQ=1 $(LIBPQ_CFLAGS)
LDLIBS += $(LIBPQ_LIBS)
else
CFLAGS += -DHAVE_LIBPQ=0
endif

ifeq ($(SQLITE3_AVAILABLE),1)
CFLAGS += -DHAVE_SQLITE3=1 $(SQLITE3_CFLAGS)
LDLIBS += $(SQLITE3_LIBS)
else
CFLAGS += -DHAVE_SQLITE3=0
endif

# ODBC reaches the databases that have no free, packaged C client -- SQL
# Server, Oracle, DB2 -- through the driver the OPERATOR installs, so gBASIC
# never has to ship a proprietary library. One module instead of four.
ifeq ($(ODBC_AVAILABLE),1)
CFLAGS += -DHAVE_ODBC=1 $(ODBC_CFLAGS)
LDLIBS += $(ODBC_LIBS)
else
CFLAGS += -DHAVE_ODBC=0
endif

ifeq ($(LDAP_AVAILABLE),1)
CFLAGS += -DHAVE_LDAP=1 $(LDAP_CFLAGS)
LDLIBS += $(LDAP_LIBS)
else
CFLAGS += -DHAVE_LDAP=0
endif

ifeq ($(LIBCURL_AVAILABLE),1)
CFLAGS += -DHAVE_LIBCURL=1 $(LIBCURL_CFLAGS)
LDLIBS += $(LIBCURL_LIBS)
else
CFLAGS += -DHAVE_LIBCURL=0
endif

ifeq ($(LIBXCRYPT_AVAILABLE),1)
CFLAGS += -DHAVE_LIBXCRYPT=1 $(LIBXCRYPT_CFLAGS)
LDLIBS += $(LIBXCRYPT_LIBS)
else
CFLAGS += -DHAVE_LIBXCRYPT=0
endif

# password_hash WITHOUT libxcrypt: the vendored yescrypt (third_party/yescrypt,
# BSD-2), which produces and verifies the same `$y$` hashes libxcrypt does.
# Only where libxcrypt is absent -- the Windows block sets it, and
# `make YESCRYPT_VENDORED=1 LIBXCRYPT_AVAILABLE=0` builds it anywhere, which is
# how the two are compared.
YESCRYPT_VENDORED ?= 0
YESCRYPT_OBJS :=
ifeq ($(LIBXCRYPT_AVAILABLE)$(YESCRYPT_VENDORED),01)
CFLAGS += -DHAVE_YESCRYPT=1
YESCRYPT_OBJS := third_party/yescrypt/yescrypt-opt.o third_party/yescrypt/yescrypt-common.o \
                 third_party/yescrypt/sha256.o third_party/yescrypt/insecure_memzero.o
else
CFLAGS += -DHAVE_YESCRYPT=0
endif

ifeq ($(LIBSSL_AVAILABLE),1)
CFLAGS += -DHAVE_LIBSSL=1 $(LIBSSL_CFLAGS)
LDLIBS += $(LIBSSL_LIBS)
else
CFLAGS += -DHAVE_LIBSSL=0
endif
ifeq ($(LIBCRYPTO_AVAILABLE),1)
CFLAGS += -DHAVE_LIBCRYPTO=1 $(LIBCRYPTO_CFLAGS)
LDLIBS += $(LIBCRYPTO_LIBS)
else
CFLAGS += -DHAVE_LIBCRYPTO=0
endif

ifeq ($(LIBXML2_AVAILABLE),1)
CFLAGS += -DHAVE_LIBXML2=1 $(LIBXML2_CFLAGS)
LDLIBS += $(LIBXML2_LIBS)
else
CFLAGS += -DHAVE_LIBXML2=0
endif

ifeq ($(ZLIB_AVAILABLE),1)
CFLAGS += -DHAVE_ZLIB=1 $(ZLIB_CFLAGS)
LDLIBS += $(ZLIB_LIBS)
else
CFLAGS += -DHAVE_ZLIB=0
endif

ifeq ($(GIR_AVAILABLE),1)
CFLAGS += -DHAVE_GIR=1 $(GIR_CFLAGS)
LDLIBS += $(GIR_LIBS)
else
CFLAGS += -DHAVE_GIR=0
endif

ifeq ($(GIO_AVAILABLE),1)
CFLAGS += -DHAVE_GIO=1 $(GIO_CFLAGS)
LDLIBS += $(GIO_LIBS)
else
CFLAGS += -DHAVE_GIO=0
endif

# libgbasic is every object except the CLI entry points (src/main.o, src/repl.o,
# src/lineedit.o). The CLI is
# its first consumer; the archive is the seam a future embedder links against.
# WHICH PLATFORM FILE. Overridable, because tools/cross-build-windows.sh must
# build the Windows one or it measures the POSIX file failing to be Windows,
# which is true and says nothing.
#
# NATIVE WINDOWS (an "MSYS2 UCRT64" shell, docs/windows_port_status.md §9):
# make's own OS variable is Windows_NT there. Winsock is a library rather than
# part of libc, and -static links libgcc/winpthread into the binary so
# gbasic.exe needs nothing beside it -- the UCRT itself ships with Windows.
ifeq ($(OS),Windows_NT)
PLATFORM_OBJ ?= src/platform_win32.o
# libsystre: the POSIX regex.h API over TRE (BSD-2), since Windows has none.
# TRE takes its messages through gettext; that one function is supplied by
# src/platform_win32.c, so the LGPL libintl (and the libiconv it needs) is
# NOT linked -- gbasic.exe carries permissive-licensed code only.
LDLIBS += -lsystre -ltre -lws2_32 -lbcrypt -static
# AN 8 MB MAIN-THREAD STACK, the Linux default. Windows reserves 1 MB, and the
# tree-walking evaluator and the JSON parser recurse: the parser's 10,000-level
# nesting cap was sized for 8 MB, so on 1 MB a deep document CRASHED the
# interpreter (STATUS_STACK_OVERFLOW, measured by run_try_decode.sh) before the
# cap could refuse it -- the very crash the cap exists to prevent.
LDLIBS += -Wl,--stack,8388608
# The application manifest (UTF-8 code page): src/gbasic.manifest.
WIN_RES := src/gbasic_win32.res.o
endif
WIN_RES ?=
PLATFORM_OBJ ?= src/platform_posix.o

LIB_OBJS := src/lexer.o src/parser.tab.o src/ast.o src/eval.o src/builtins.o src/actor.o src/diagnostics.o src/frontend.o $(PLATFORM_OBJ) $(YESCRYPT_OBJS)
OBJS := src/main.o src/repl.o src/lineedit.o $(LIB_OBJS)

# gbasic-lsp: the Language Server, first external consumer of libgbasic. Kept out
# of the default `all` target so a plain `make` stays lean; build it with
# `make gbasic-lsp`. It links the vendored cJSON (third_party/cjson) plus the
# archive. cJSON is built with relaxed warnings since it is third-party code.
LSP_CFLAGS := $(CFLAGS) -Isrc/lsp -Ithird_party/cjson
LSP_OBJS := src/lsp/main.o src/lsp/rpc.o src/lsp/handlers.o src/lsp/lsp_position.o third_party/cjson/cJSON.o

.PHONY: all dev clean install uninstall

all: libgbasic.a gbasic

# Build every binary in the tree, including the ones kept out of `all` (the LSP
# server). This is the routine developer/CI entry point: it guarantees gbasic-lsp
# still compiles so it cannot silently rot while default builds stay lean. It also
# runs the executable-docs gate so docs/ai/COOKBOOK.md references cannot rot.
dev: all gbasic-lsp
	@bash tests/run_docs_gate.sh

libgbasic.a: $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

gbasic: src/main.o src/repl.o src/lineedit.o libgbasic.a $(WIN_RES)
	$(CC) $(CFLAGS) -o $@ src/main.o src/repl.o src/lineedit.o $(WIN_RES) libgbasic.a $(LDLIBS)

src/gbasic_win32.res.o: src/gbasic_win32.rc src/gbasic.manifest
	windres -i src/gbasic_win32.rc -o $@

gbasic-lsp: $(LSP_OBJS) libgbasic.a
	$(CC) $(LSP_CFLAGS) -o $@ $(LSP_OBJS) libgbasic.a $(LDLIBS)

src/lsp/main.o: src/lsp/main.c src/lsp/rpc.h src/lsp/handlers.h third_party/cjson/cJSON.h
	$(CC) $(LSP_CFLAGS) -c $< -o $@

src/lsp/rpc.o: src/lsp/rpc.c src/lsp/rpc.h third_party/cjson/cJSON.h
	$(CC) $(LSP_CFLAGS) -c $< -o $@

src/lsp/handlers.o: src/lsp/handlers.c src/lsp/handlers.h src/lsp/rpc.h src/lsp/lsp_position.h include/gbasic.h include/diagnostics.h include/ast.h third_party/cjson/cJSON.h
	$(CC) $(LSP_CFLAGS) -c $< -o $@

src/lsp/lsp_position.o: src/lsp/lsp_position.c src/lsp/lsp_position.h
	$(CC) $(LSP_CFLAGS) -c $< -o $@

third_party/cjson/cJSON.o: third_party/cjson/cJSON.c third_party/cjson/cJSON.h
	$(CC) -std=c11 -O2 -Ithird_party/cjson -c $< -o $@

# Upstream's own flags (its Makefile builds -O2 -fomit-frame-pointer); kept off
# our -Wpedantic because the code is vendored unmodified.
third_party/yescrypt/%.o: third_party/yescrypt/%.c third_party/yescrypt/yescrypt.h third_party/yescrypt/sha256.h third_party/yescrypt/yescrypt-platform.c
	$(CC) -std=gnu99 -O2 -fomit-frame-pointer -Ithird_party/yescrypt -c $< -o $@

src/parser.tab.c src/parser.tab.h: src/parser.y include/ast.h include/lexer.h include/diagnostics.h include/parse_ctx.h
	bison -d -o src/parser.tab.c src/parser.y

# See the PREFIX note at the top of this file. Kept here rather than up there so
# it cannot become the default goal.
.stdlibdir-stamp: FORCE
	@printf '%s' '$(STDLIBDIR)' | cmp -s - $@ 2>/dev/null || printf '%s' '$(STDLIBDIR)' > $@
FORCE:
.PHONY: FORCE

src/main.o: src/main.c include/ast.h include/eval.h include/lexer.h include/builtins.h include/gbasic.h include/diagnostics.h include/repl.h include/platform.h .stdlibdir-stamp

src/repl.o: src/repl.c include/ast.h include/eval.h include/gbasic.h include/diagnostics.h include/repl.h include/lineedit.h include/platform.h

src/lineedit.o: src/lineedit.c include/lineedit.h include/platform.h
	$(CC) $(CFLAGS) -c $< -o $@

src/platform_posix.o: src/platform_posix.c include/platform.h

src/platform_win32.o: src/platform_win32.c include/platform.h

src/lexer.o: src/lexer.c include/lexer.h
	$(CC) $(CFLAGS) -c $< -o $@

src/parser.tab.o: src/parser.tab.c src/parser.tab.h include/ast.h include/lexer.h include/diagnostics.h include/parse_ctx.h
	$(CC) $(CFLAGS) -c src/parser.tab.c -o $@

src/ast.o: src/ast.c include/ast.h
	$(CC) $(CFLAGS) -c $< -o $@

src/eval.o: src/eval.c src/modules/xml.c src/modules/smtp.c src/modules/ldap.c src/modules/rowmodel.c src/modules/xlsx.c include/eval.h include/ast.h include/builtins.h include/actor.h include/diagnostics.h include/platform.h include/posix_compat.h .stdlibdir-stamp
	$(CC) $(CFLAGS) -c $< -o $@

src/builtins.o: src/builtins.c include/builtins.h
	$(CC) $(CFLAGS) -c $< -o $@

src/diagnostics.o: src/diagnostics.c include/diagnostics.h
	$(CC) $(CFLAGS) -c $< -o $@

src/frontend.o: src/frontend.c include/gbasic.h include/diagnostics.h include/ast.h
	$(CC) $(CFLAGS) -c $< -o $@

src/actor.o: src/actor.c include/actor.h include/platform.h
	$(CC) $(CFLAGS) -c $< -o $@

install: gbasic
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 gbasic $(DESTDIR)$(BINDIR)/gbasic
	install -d $(DESTDIR)$(STDLIBDIR)
	install -m 0644 stdlib/*.bas $(DESTDIR)$(STDLIBDIR)/
	install -d $(DESTDIR)$(STDLIBDIR)/gtksourceview
	install -m 0644 stdlib/gtksourceview/gbasic.lang $(DESTDIR)$(STDLIBDIR)/gtksourceview/
	install -d $(DESTDIR)$(DOCDIR)
	install -m 0644 LICENSE NOTICE LICENSING.md $(DESTDIR)$(DOCDIR)/
	@echo "Installed gbasic to $(DESTDIR)$(BINDIR), stdlib to $(DESTDIR)$(STDLIBDIR),"
	@echo "and LICENSE/NOTICE to $(DESTDIR)$(DOCDIR)"

# The language server installs SEPARATELY, mirroring the decision that keeps it
# out of `all`: a plain install stays lean, and someone wiring up an editor asks
# for it explicitly. Without this target the binary `make dev` produces has no
# supported way to reach a PATH, which for an editor integration is most of the
# point of building it.
install-lsp: gbasic-lsp
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 gbasic-lsp $(DESTDIR)$(BINDIR)/gbasic-lsp
	@echo "Installed gbasic-lsp to $(DESTDIR)$(BINDIR)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/gbasic
	rm -f $(DESTDIR)$(BINDIR)/gbasic-lsp
	rm -rf $(DESTDIR)$(DATADIR)
	rm -rf $(DESTDIR)$(DOCDIR)
	@echo "Removed gbasic from $(DESTDIR)$(BINDIR), $(DESTDIR)$(DATADIR) and $(DESTDIR)$(DOCDIR)"

clean:
	rm -f gbasic libgbasic.a $(OBJS) src/parser.tab.c src/parser.tab.h src/gbasic_win32.res.o
	rm -f third_party/yescrypt/*.o
	rm -f gbasic-lsp $(LSP_OBJS)
	rm -f .stdlibdir-stamp
