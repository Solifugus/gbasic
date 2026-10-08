# tests/portable.sh -- GNU coreutils behaviour on a machine that ships BSD tools.
#
# Sourced, like tests/valgrind_tier.sh:  . "$(dirname "$0")/portable.sh"
#
# WHY THIS EXISTS. `timeout` appears in 90 of 154 suites and is GNU coreutils --
# it is NOT on a stock macOS. Measured before writing anything: the other real
# gaps are `stat -c` (four format specifiers, three files) and `sha256sum` (four
# files, one identical shape). `ls --long-option` looked like an 18-file problem
# and is a FALSE POSITIVE -- those are `ls` followed by a `-----` separator line
# in a suite's own output.
#
# ON LINUX THIS FILE DEFINES NOTHING. Every shim is installed only when the
# native tool is ABSENT, so sourcing it on a machine with coreutils changes
# nothing at all -- which is what makes adding it to 90 suites a safe edit
# rather than a behavioural one. run_portable.sh asserts that.
#
# THE LOAD-BEARING RULE IS THAT `timeout` MAY NEVER BECOME A NO-OP. Most of
# those 90 uses exist because A HANG IS NOT A FAILURE: without a bound a hung
# fixture sits there until run_all.sh's per-suite cap, reporting nothing, which
# is the third way this gate can go quiet (the others being a suite that SKIPS
# and a discovery pass that SHRINKS). A shim that quietly ran the command
# unbounded would be worse than no shim, because the suite would still print
# PASS. So if no bound can be provided, this REFUSES.
#
# GB_PORTABLE_FORCE names a shim to install even where the native tool exists,
# which is the only way to exercise the fallback on the machine we develop on.
# A fallback that has never run is a fallback that does not work.

GB_PORTABLE_SHIMS=""
_gb_portable_forced() {
    case ":${GB_PORTABLE_FORCE:-}:" in (*":$1:"*) return 0 ;; esac
    return 1
}
_gb_portable_need() {   # $1 = tool name -- true when a shim must be installed
    _gb_portable_forced "$1" && return 0
    command -v "$1" >/dev/null 2>&1 && return 1
    return 0
}
_gb_portable_note() { GB_PORTABLE_SHIMS="${GB_PORTABLE_SHIMS:+$GB_PORTABLE_SHIMS }$1"; }

# --- timeout ------------------------------------------------------------------
# Accepts the two shapes this tree uses: `timeout SECS cmd...` and
# `timeout -k KILL_AFTER SECS cmd...`. The -k form is not decoration: this
# interpreter installs a SIGTERM handler for pool drain, so a bound that might
# not fire is not a bound.
#
# THE TIMEOUT IS REPORTED BY A MARKER, NOT BY GUESSING FROM THE EXIT STATUS.
# Mapping 143 (128+SIGTERM) to GNU's 124 would call every command a caller
# legitimately SIGTERMs a timeout, and this tree has suites that do exactly
# that on purpose.
if _gb_portable_need timeout; then
    if command -v gtimeout >/dev/null 2>&1 && ! _gb_portable_forced timeout_shell; then
        timeout() { gtimeout "$@"; }
        _gb_portable_note "timeout=gtimeout"
    else
        timeout() {
            local kill_after="" dur fired status=0 cmd_pid wd_pid
            while [ $# -gt 0 ]; do
                case "$1" in
                    -k)  kill_after="$2"; shift 2 ;;
                    -k*) kill_after="${1#-k}"; shift ;;
                    --)  shift; break ;;
                    -*)  shift ;;
                    *)   break ;;
                esac
            done
            dur="$1"; shift
            [ $# -gt 0 ] || { echo "portable.sh: timeout: no command given" >&2; return 125; }
            fired="$(mktemp)" || { echo "portable.sh: timeout: no mktemp -- refusing to run unbounded" >&2; return 125; }
            rm -f "$fired"
            "$@" & cmd_pid=$!
            (
                sleep "$dur"
                kill -0 "$cmd_pid" 2>/dev/null || exit 0
                : > "$fired"
                kill -TERM "$cmd_pid" 2>/dev/null
                if [ -n "$kill_after" ]; then
                    sleep "$kill_after"
                    kill -KILL "$cmd_pid" 2>/dev/null
                fi
            ) & wd_pid=$!
            wait "$cmd_pid" 2>/dev/null || status=$?
            kill -TERM "$wd_pid" 2>/dev/null
            wait "$wd_pid" 2>/dev/null || :
            if [ -e "$fired" ]; then rm -f "$fired"; return 124; fi
            rm -f "$fired"
            return "$status"
        }
        _gb_portable_note "timeout=shell"
    fi
fi

# --- stat ---------------------------------------------------------------------
# Only the four specifiers this tree uses, named for what they MEAN rather than
# wrapping `stat` itself: BSD stat spells the format differently AND orders its
# arguments differently, so a wrapper pretending to be `stat -c` would be a
# second thing to get wrong.
if _gb_portable_need stat || _gb_portable_forced stat; then
    gb_stat_mode()   { stat -f '%Lp' "$1"; }
    gb_stat_device() { stat -f '%d'  "$1"; }
    gb_stat_inode()  { stat -f '%i'  "$1"; }
    gb_stat_size()   { stat -f '%z'  "$1"; }
    _gb_portable_note "stat=bsd"
else
    gb_stat_mode()   { stat -c '%a' "$1"; }
    gb_stat_device() { stat -c '%d' "$1"; }
    gb_stat_inode()  { stat -c '%i' "$1"; }
    gb_stat_size()   { stat -c '%s' "$1"; }
fi

# --- sha256sum ----------------------------------------------------------------
if _gb_portable_need sha256sum; then
    if command -v shasum >/dev/null 2>&1; then
        sha256sum() { shasum -a 256 "$@"; }
        _gb_portable_note "sha256sum=shasum"
    elif command -v openssl >/dev/null 2>&1; then
        # openssl prints "SHA2-256(file)= hex"; the callers all take field 1 of
        # `cut -d' ' -f1`, so the hex must come FIRST.
        sha256sum() { for f in "$@"; do printf '%s  %s\n' "$(openssl dgst -sha256 -r "$f" | cut -d' ' -f1)" "$f"; done; }
        _gb_portable_note "sha256sum=openssl"
    else
        sha256sum() { echo "portable.sh: no sha256 tool (sha256sum, shasum, openssl)" >&2; return 127; }
        _gb_portable_note "sha256sum=MISSING"
    fi
fi

# --- /proc ---------------------------------------------------------------------
# NOT SHIMMED, DELIBERATELY. Six suites read /proc to measure something about a
# live process -- a peak RSS from VmHWM, an open-descriptor audit, a pid's
# status. Those are not tool differences, they are a different mechanism (macOS
# needs proc_pidinfo or ps), and a shim returning a plausible wrong number is
# how a measurement tier starts measuring nothing. A suite asks this and SKIPS
# with a reason, which run_all.sh already reports separately from a pass.
# Forceable (GB_PORTABLE_FORCE=noproc) for the same reason every shim here is:
# the branch that matters runs only on a machine we do not have, and a branch
# that has never run does not work.
# MSYS2/Cygwin HAS a /proc, and it is the same trap one level down: it
# describes MSYS processes, so a NATIVE interpreter's VmHWM and descriptors are
# not in it -- xml_bigfile "never sampled VmHWM" and the fd audit would count
# nothing. Cygwin marks its emulation with /proc/self/winpid, so that answers no.
gb_have_proc() {
    _gb_portable_forced noproc && return 1
    [ -r /proc/self/status ] && [ ! -e /proc/self/winpid ]
}

export GB_PORTABLE_SHIMS
