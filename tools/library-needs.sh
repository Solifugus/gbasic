#!/usr/bin/env sh
# WHAT EACH STANDARD LIBRARY NEEDS FROM THE BUILD, derived from its own source.
#
# Prints one line per library:   <library> <capability>,<capability>,...
# with a bare library name and no second field when it needs nothing.
#
# WHY THIS IS DERIVED AND NOT A LIST. A hand-kept table of "which libraries
# need libxml2" is a second statement of a fact `stdlib/*.bas` already carries
# in its `load` lines, and two statements of one fact drift -- which is exactly
# how 13 of 65 libraries came to be unloadable on a published archive with
# nothing in the tree saying so. Three instances of that class had been reported
# (a stale pin, the GUI libraries, then this) before anything asked the question
# mechanically.
#
# THE DEPENDENCY IS TRANSITIVE AND THAT IS THE HALF A GREP MISSES. `gtk` loads
# `gi`; `finio_all` loads `finio_camt` which loads `xml`; `ari_advisor` loads
# `llm` which loads `webclient`. A direct scan reports `finio_all` as needing
# nothing, and it cannot be loaded at all without libxml2. So this iterates to
# a fixpoint rather than reading one level.
#
# THE LABEL IS THE DEPENDENCY TO INSTALL, which is a correction rather than a
# first choice: the modules refuse in their OWN words, and "WebClient", "HTTP"
# and "SMTP" are three faces of libcurl, so a table in those terms tells a
# reader three things are missing where one is. The module names are kept in
# the mapping so tests/run_library_tiers.sh can assert it covers exactly the
# modules a `load` can name -- read out of eval_module_needs_load in
# src/eval.c, which is the authoritative list -- so a new optional module
# cannot be added without appearing here.
#
# COMMENT LINES ARE SKIPPED, and that is load-bearing rather than tidy: a
# library's header prose names modules it does NOT use. `rules.bas` mentions
# `ldap.bind` in an argument about reporting failure as a value, and reading
# that as a dependency would report a pure-gBASIC library as needing OpenLDAP.
#
# ONE awk PASS, not a shell loop. The first version ran an `awk` per LINE per
# file -- 66 files of a thousand lines each, so tens of thousands of processes --
# and took long enough that running it three times in one command timed out.
# A derivation the release build runs has to be cheap enough that nobody is
# tempted to cache it, since a cached derivation is the hand-kept list again.
#
# POSIX SHELL AND awk ONLY, no python3: tools/build-release-tarball.sh runs
# this, and a release step is the worst place to add a dependency.
set -eu
cd "$(dirname "$0")/.."
MODE=loads
if [ "${1:-}" = "--calls" ]; then MODE=calls; shift; fi
STD="${1:-stdlib}"

# ---------------------------------------------------------------------------
# `--calls`: A LIBRARY CAN LOAD PERFECTLY AND STILL NOT WORK, which is the half
# the load-time answer misses and the worse half, because the failure is
# deferred to the call. MEASURED on the published lean archive: `load crypto`
# and `load otp` both succeed, and then every function of either answers
# *HMAC requires OpenSSL (libcrypto); rebuild with it installed*. `gpdf` is the
# third, needing zlib for a compressed stream.
#
# These libraries have no `load` of an optional module -- they call a BUILTIN
# whose availability moves with the build, so no amount of looking at `load`
# lines finds them. The names to look for are READ OUT OF src/builtins.c's own
# `#if` guards rather than listed here, so a builtin that becomes optional (or
# stops being) moves this answer with it.
if [ "$MODE" = calls ]; then
    guarded="$(awk '
      /^#if +(HAVE_LIBCRYPTO|HAVE_ZLIB|HAVE_LIBXCRYPT|HAVE_LIBXCRYPT \|\| HAVE_YESCRYPT)/ {
          on = 1
          cap = "libcrypto"
          if ($0 ~ /HAVE_ZLIB/) cap = "zlib"
          else if ($0 ~ /HAVE_LIBXCRYPT/) cap = "libxcrypt"
          next
      }
      /^#endif/ { on = 0; next }
      on && match($0, /"[a-z_0-9]+"/) { print substr($0, RSTART+1, RLENGTH-2) " " cap }
    ' src/builtins.c)"
    [ -n "$guarded" ] || { echo "FAIL: read no guarded builtins out of src/builtins.c" >&2; exit 1; }
    for f in "$STD"/*.bas; do
        lib="$(basename "$f" .bas)"
        caps=""
        printf '%s\n' "$guarded" | while read -r b cap; do
            [ -n "$b" ] || continue
            # A gBASIC comment starts the line; strip those before looking, for
            # the same reason the load scan does -- a header arguing about
            # `sha256` is not a use of it.
            if sed "s/^[[:space:]]*'.*$//" "$f" | grep -qE "(^|[^a-z_])$b *\(" ; then
                printf '%s\n' "$cap"
            fi
        done | sort -u | tr '\n' ',' | sed 's/,$//' > /tmp/lc.$$ || true
        caps="$(cat /tmp/lc.$$ 2>/dev/null || true)"; rm -f /tmp/lc.$$
        if [ -n "$caps" ]; then printf '%s %s\n' "$lib" "$caps"; else printf '%s\n' "$lib"; fi
    done | sort
    exit 0
fi

awk '
  FNR == 1 {
      # basename without .bas
      lib = FILENAME
      sub(/^.*\//, "", lib)
      sub(/\.bas$/, "", lib)
      order[++n] = lib
      caps[lib] = ""
      edges[lib] = ""
      known[lib] = 1
  }
  {
      line = $0
      sub(/^[ \t]+/, "", line)
      if (substr(line, 1, 1) == "'"'"'") next      # a gBASIC comment
      if (line !~ /^load[ \t]+[A-Za-z_][A-Za-z0-9_]*/) next
      dep = line
      sub(/^load[ \t]+/, "", dep)
      sub(/[^A-Za-z0-9_].*$/, "", dep)

      # THE LABEL IS WHAT TO INSTALL, not what the error says, and that was a
      # correction. The modules own refusal words are "WebClient", "HTTP" and
      # "SMTP" for three faces of ONE library, so a table in those terms makes
      # a reader think three things are missing. What they need is the package.
      # tests/run_library_tiers.sh asserts this covers exactly the modules a
      # `load` can name, read out of eval_module_needs_load in src/eval.c.
      c = ""
      if (dep == "sqlite")         c = "libsqlite3"
      else if (dep == "pg")        c = "libpq"
      else if (dep == "odbc")      c = "unixODBC"
      else if (dep == "ldap")      c = "OpenLDAP"
      else if (dep == "webclient") c = "libcurl"
      else if (dep == "http")      c = "libcurl"
      else if (dep == "smtp")      c = "libcurl"
      else if (dep == "xml")       c = "libxml2"
      else if (dep == "gui")       c = "GTK 3"
      else if (dep == "gi")        c = "GObject-Introspection"
      else if (dep == "webserver") c = ""     # always built; no dependency

      if (c != "") {
          if (index(" " caps[lib] " ", " " c " ") == 0) caps[lib] = caps[lib] " " c
      } else {
          edges[lib] = edges[lib] " " dep
      }
  }
  END {
      changed = 1
      while (changed) {
          changed = 0
          for (i = 1; i <= n; i++) {
              L = order[i]
              m = split(edges[L], e, " ")
              for (j = 1; j <= m; j++) {
                  d = e[j]
                  if (d == "" || !(d in known)) continue
                  k = split(caps[d], cs, " ")
                  for (p = 1; p <= k; p++) {
                      if (cs[p] == "") continue
                      if (index(" " caps[L] " ", " " cs[p] " ") == 0) {
                          caps[L] = caps[L] " " cs[p]
                          changed = 1
                      }
                  }
              }
          }
      }
      for (i = 1; i <= n; i++) {
          L = order[i]
          k = split(caps[L], cs, " ")
          cnt = 0
          delete arr
          for (p = 1; p <= k; p++) if (cs[p] != "") arr[++cnt] = cs[p]
          # sorted, so two runs agree and a diff against the declaration is stable
          for (a = 1; a <= cnt; a++)
              for (b = a + 1; b <= cnt; b++)
                  if (arr[b] < arr[a]) { t = arr[a]; arr[a] = arr[b]; arr[b] = t }
          out = ""
          for (a = 1; a <= cnt; a++) out = out (a > 1 ? "," : "") arr[a]
          if (out == "") print L; else print L " " out
      }
  }
' "$STD"/*.bas | sort
