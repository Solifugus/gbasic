# Shipping a gBASIC application

How to turn a `.bas` program into something a customer installs. Everything
here was measured on Ubuntu 26.04 while packaging `packaging/example-app`,
which is a working service you can build and run.

The short version:

```sh
packaging/build-deb.sh packaging/example-app
sudo apt install packaging/notesd_0.1.0_amd64.deb
curl localhost:8099/health
```

---

## 1. The model: a vendored runtime, system libraries

A gBASIC application ships **its own interpreter and its own copy of the
stdlib**, under `/usr/lib/<app>/`. It does not depend on a system `gbasic`
package. That costs about 2 MB and buys three things:

- **The dependency set is the application's.** gBASIC's optional modules are
  compile-time gated, so a build for a server drops GTK, GObject-introspection,
  PostgreSQL, libxml2 and libcurl. Measured on the example: **48 shared
  libraries down to 10**, 1.7 MB to 1.1 MB. Installing a file-transfer service
  should not pull a desktop toolkit onto a server, and a customer's security
  review will ask.
- **The application cannot change underneath the operator.** A system-wide
  interpreter upgrade cannot alter a shipped service's behaviour.
- **The stdlib path is compiled in.** `GBASIC_DEFAULT_STDLIB` comes from
  `STDLIBDIR` at build time, so nothing depends on `GBASIC_PATH` being right in
  a unit file.

What is **not** vendored: `libssl`, `libcrypto`, `libsqlite3`, `zlib`. Those are
the distribution's, patched by the distribution. For a security product that is
the entire point — an auditor can see you are not shipping a frozen OpenSSL. It
is also why "one big static binary" is the wrong answer here, quite apart from
being impractical to build.

### Choosing the build flags

Start from nothing and add what the application actually loads:

| Flag | Turns off | Needed when the app uses |
|---|---|---|
| `GTK_AVAILABLE=0` | GTK 3 `gui` | a GTK 3 desktop UI |
| `GIR_AVAILABLE=0` `GIO_AVAILABLE=0` | the `gi` bridge, `gtk`, `gtkui`, `datagrid`, `sourceeditor` | any GTK 4 desktop UI |
| `LIBPQ_AVAILABLE=0` | `pg` | PostgreSQL |
| `LIBXML2_AVAILABLE=0` | `xml`, and `xlsx` | XML or spreadsheets |
| `LIBCURL_AVAILABLE=0` | `webclient`, and the EDGAR/LLM network paths | outbound HTTP |
| `SQLITE3_AVAILABLE=0` | `sqlite` | SQLite |
| `LIBCRYPTO_AVAILABLE=0` | the crypto builtins, `crypto` | hashing, HMAC, JWT, signed cookies |
| `LIBSSL_AVAILABLE=0` | TLS in `webserver` | serving HTTPS directly |
| `ZLIB_AVAILABLE=0` | `xlsx` | spreadsheets |

A module compiled out is a **clean runtime error**, never a build failure, so
getting this wrong fails loudly the first time the app loads that module —
which is why the smoke test in §6 matters.

---

## 2. How a library is found — and the hazard

This is the part that bites, so it comes before the layout.

**Native modules** — `sqlite`, `pg`, `webclient`, `webserver`, `xml`, `gui`,
`gi` — are compiled into the interpreter. `load sqlite` takes no path and none
of the following applies to them.

**Stdlib libraries** — `stats`, `web`, `chart`, `dates`, `crypto`, `frame`,
`matrix`, and the rest — are `.bas` files, and `load NAME` searches, in order:

1. the **source file's own directory**, for `NAME.bas`
2. the **source file's own directory, recursively**, for `NAME.bas`
3. each directory in `GBASIC_PATH`
4. the compiled-in `GBASIC_DEFAULT_STDLIB`
5. …then the same places again, reading *file contents* for a matching
   `library NAME` block regardless of filename

Step 2 is the hazard. **Anything named `NAME.bas` anywhere beneath the
application directory silently takes precedence over the shipped stdlib** —
ahead of both `GBASIC_PATH` and the compiled-in path. Demonstrated:

```
app/main.bas                       load stats  →  sharpe_ratio(...) = 999
app/vendor/thirdparty/stats.bas    a three-line fake
```

The warning you get says the *correct* library was "additional … match
ignored", which reads backwards unless you already know the precedence. For a
product where one writable file under the app directory can replace a library,
that is a supply-chain problem, not a style question.

**Two defences, use both:**

1. **Load stdlib libraries by absolute path.** Write `@RUNTIME@` and let the
   packager substitute it, so the app is still relocatable:

   ```basic
   load stats from "@RUNTIME@/stdlib/stats.bas"
   ```

   `build-deb.sh` substitutes it and fails the build if any `@RUNTIME@`
   survives.

2. **Keep the application directory root-owned and containing only the
   application.** `/usr/lib/<app>/app/` is written by the package and by
   nothing else; the service user cannot write there.

---

## 3. Layout

```
/usr/lib/<app>/gbasic            the app's own interpreter (stdlib path compiled in)
/usr/lib/<app>/stdlib/*.bas      its stdlib, at the compiled-in path
/usr/lib/<app>/app/*.bas         the application. root-owned, nothing else in it
/etc/<app>/<app>.conf            configuration. a dpkg conffile
/var/lib/<app>/                  state. the ONLY thing the service may write
/lib/systemd/system/<app>.service
/usr/share/doc/<app>/            LICENSE, NOTICE, LICENSING.md
```

A dedicated system user `<app>`, no login shell, no home beyond `/var/lib/<app>`.

---

## 4. The unit

`packaging/service.template` is the starting point. Three things in it are not
decoration:

- **`--line-buffered`.** A gBASIC process's stdout is block-buffered on a pipe,
  so without this the journal shows nothing until the service exits — and
  anything still buffered when a service is killed is lost outright.
- **`ProtectSystem=strict` + `ReadWritePaths=/var/lib/<app>`.** The service
  writes to exactly one place. Everything else, including `/etc`, is read-only.
- **`RestrictAddressFamilies`.** Loopback and outbound only. Exposing the
  service is the reverse proxy's job (§5), not the application's.

Also set: `NoNewPrivileges`, `PrivateTmp`, `PrivateDevices`, `ProtectHome`,
`ProtectKernel*`, `RestrictSUIDSGID`, `LockPersonality`,
`MemoryDenyWriteExecute`.

---

## 5. Exposure

**Bind loopback by default and make exposure opt-in.** The `server` block takes
`address:`; leave it `127.0.0.1` and put nginx in front for TLS, or use the
`cert:`/`key:` options to terminate TLS in gBASIC. The pool suite specifically
covers terminating TLS *in the worker*, which makes certificate rotation a
rolling reload and nothing more.

`examples/gbasic_site/deploy/gbasic-site.nginx.conf` is a working reverse-proxy
config to start from.

---

## 6. Prove it before you ship it

The example builds a package rooted anywhere, so the whole path can be
exercised without root:

```sh
RUNTIME=/tmp/rt packaging/build-deb.sh packaging/example-app
dpkg-deb -x packaging/notesd_0.1.0_amd64.deb /tmp/x
NOTESD_CONF=/tmp/notesd.conf /tmp/rt/gbasic --line-buffered /tmp/rt/app/notesd.bas &
curl -s localhost:8099/health          # → ok
```

Make the configuration path come from argv or an environment variable, as the
example does. A service whose config path is hardcoded cannot be smoke-tested
before install and cannot run twice on one host.

**Start-up must be silent.** Bind the result of `serve` — `h = serve(myapp)`.
A bare call discards a non-`nothing` return, so the `unused-result` warning
fires on every start and lands in the operator's journal; and `h.port` is how
you learn the port when you bound `port: 0`.

---

## 7. What this does not cover yet

- **RPM.** The same layout applies; only the metadata differs.
- **A container image.** For evaluation, an OCI image is lower friction than
  any package — the layout above is what would go in it.
- **Upgrades that migrate state.** The example has no schema migration.
- **Signing.** Neither the package nor a repository is signed here.
