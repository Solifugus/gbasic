# tests/premise.sh -- CAN THIS MACHINE BUILD THE PREMISE A TIER NEEDS?
#
# Sourced, like tests/portable.sh and tests/valgrind_tier.sh:
#     . "$(dirname "$0")/premise.sh"
#
# NAMED FOR THE WORDS THESE TIERS ALREADY USE -- "a tier that cannot create its
# own premise must say so rather than fail or pass". It was `capabilities.sh`
# for an hour and renamed before it shipped, being one character from
# `tests/run_capabilities.sh`, which is about the unrelated `gbasic
# --capabilities` flag; this tree renamed `scoring` away from `scorecard` for
# the same reason.
#
# WHY THIS EXISTS. Six suites failed on Windows for reasons that were facts
# about the HARNESS and not about gBASIC (docs/windows_port_status.md §30):
# `chmod 000` does not bite where permissions are ACLs, `mkfifo` does not
# exist, `/usr/bin/time -f %M` is not installed, and the stack size is the
# linker's rather than `ulimit`'s. A suite that fails for the wrong reason is
# one people learn to ignore, which is the failure every tripwire in this tree
# exists to prevent -- and a tier that cannot create its own premise must SAY SO
# rather than fail or pass, which is the rule run_dir_builtins and
# run_web_routes already follow in their own words.
#
# THE PROBES ASK THE MACHINE, NOT `platform()`, and that is deliberate rather
# than lazy. It is also stronger: `chmod 000` fails to bite on a Linux FAT or
# CIFS mount and under any root shell, a container may ship no `/usr/bin/time`,
# and MSYS `ln -s` silently makes a COPY -- so a test on the platform NAME would
# be right about Windows and wrong about all four. `platform()` is for the
# LANGUAGE's own divergences, where the answer really is which platform this is;
# a harness probes the capability it needs.
#
# ON A FULL LINUX MACHINE EVERY PROBE ANSWERS YES, so sourcing this changes
# nothing here -- the same property portable.sh has, and the reason adding it to
# a suite is a safe edit rather than a behavioural one.
#
# NOT THE SAME QUESTION AS tests/build_has.sh, and the two must not be merged.
# `build_has` asks what THIS BINARY can do -- which modules are compiled in,
# which platform features it will refuse -- and says in as many words why it
# asks the binary and not the machine. This file asks what the MACHINE AND ITS
# FILESYSTEM can demonstrate, which is a fact about neither the binary nor the
# language. A tier usually needs both: `build_has sqlite` to know the feature
# exists, `premise_mode_bits` to know the premise can be built.
#
# Each answers 0 for "yes", so a caller reads `if premise_mode_bits; then`. Each
# caches its answer, probes in a directory of its own and leaves nothing behind.
#
# AND THE CACHE IS THE WAY THE SKIP PATH GETS PROVEN, which matters because on
# the machine these were written on every probe answers yes, so every skip is
# UNREACHABLE and therefore untested -- the shape this tree distrusts most. Each
# reads its cache from the environment, so forcing one exercises the branch:
#
#     _premise_rss=no       ./tests/run_read_at.sh     # the memory tiers skip
#     _premise_mode_bits=no ./tests/run_metadata.sh    # the permission tiers skip
#
# That is a deliberate seam, not a side effect, and it is how every skip added
# with these was checked to say the right thing rather than merely to compile.

# Does `chmod` actually govern who may read a file? NO under root (which
# bypasses the bits), and no on a filesystem whose permissions are an ACL or
# which has none at all.
premise_mode_bits() {
    if [ -z "${_premise_mode_bits:-}" ]; then
        _premise_mode_bits=no
        if [ "$(id -u)" != "0" ]; then
            _premise_probe="$(mktemp -d)"
            printf 'x' > "$_premise_probe/f"
            chmod 000 "$_premise_probe/f" 2>/dev/null
            [ -r "$_premise_probe/f" ] || _premise_mode_bits=yes
            chmod 644 "$_premise_probe/f" 2>/dev/null
            rm -rf "$_premise_probe"
        fi
    fi
    [ "$_premise_mode_bits" = yes ]
}

# Can a tier read a process's peak resident size from outside? This is the only
# way a memory claim can be measured at all -- a program cannot watch its own
# high-water mark -- so where it is absent the claim is NOT measured and the
# tier must say which claim went unchecked.
premise_rss_meter() {
    if [ -z "${_premise_rss:-}" ]; then
        _premise_rss=no
        if /usr/bin/time -f '%M' true >/dev/null 2>&1; then
            _premise_rss=yes
        fi
    fi
    [ "$_premise_rss" = yes ]
}

# A named pipe, which several tiers use to hold a child's stdin open.
premise_fifo() {
    if [ -z "${_premise_fifo:-}" ]; then
        _premise_fifo=no
        _premise_probe="$(mktemp -d)"
        if mkfifo "$_premise_probe/p" 2>/dev/null; then
            _premise_fifo=yes
        fi
        rm -rf "$_premise_probe"
    fi
    [ "$_premise_fifo" = yes ]
}

# A REAL symlink, and this one is the trap rather than the convenience. MSYS
# `ln -s` COPIES by default, so a tier asserting that something does not follow
# a link gets handed a copy and PASSES VACUOUSLY -- deleting a copy leaves the
# original alone, which is exactly what such a tier concludes. Asking whether
# the thing made is a link is the whole probe, and the same question has to be
# asked of each link a tier makes, not just of this one.
premise_real_symlink() {
    if [ -z "${_premise_symlink:-}" ]; then
        _premise_symlink=no
        _premise_probe="$(mktemp -d)"
        printf 'x' > "$_premise_probe/target"
        if ln -s "$_premise_probe/target" "$_premise_probe/link" 2>/dev/null \
           && [ -L "$_premise_probe/link" ]; then
            _premise_symlink=yes
        fi
        rm -rf "$_premise_probe"
    fi
    [ "$_premise_symlink" = yes ]
}

# Can this shell lower AND raise the stack limit? Where it cannot -- Windows,
# where the reserve is set by the linker -- a tier about recursion depth is
# measuring the machine's default and not the bound it names.
premise_stack_limit() {
    if [ -z "${_premise_stack:-}" ]; then
        _premise_stack=no
        if ( ulimit -s 8192 ) >/dev/null 2>&1; then
            _premise_stack=yes
        fi
    fi
    [ "$_premise_stack" = yes ]
}
