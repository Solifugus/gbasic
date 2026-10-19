#!/usr/bin/env python3
"""A BACKTICK INSIDE A DOUBLE-QUOTED SHELL STRING IS COMMAND SUBSTITUTION.

ELEVEN INSTANCES IN THIS TREE, which is why there is a scanner rather than
another paragraph. The shape is always the same: a message written for a reader
quotes a name the way this project quotes names -- `qt.confirm`, `load`,
`unreachable` -- and the shell runs it. What makes it expensive is that the
damage is INVISIBLE WHERE IT IS WRITTEN: the substitution usually fails
quietly, so the line prints with the quoted word MISSING and reads as an
ordinary sentence, and in a `fail` message it only ever appears on the day a
tier legitimately goes red.

FOUR WERE LIVE WHEN THIS WAS WRITTEN and none of them was mine: two in `fail`
messages (run_ldap, run_library_tiers), two in a tool's `printf` (three words
run as commands), plus one that had been WORKED AROUND rather than fixed, by
spelling the word as `printf '%s' check` so the substitution happened to
produce it.

NARROW ON PURPOSE. It reads only the lines that carry a suite's own messages --
`pass`, `fail`, `printf`, `echo`, AND EVERY FUNCTION THE FILE ITSELF DEFINES --
because that is where the whole class lives and because a general scan drowns
in false positives: a `'`-prefixed gBASIC comment inside a heredoc, an awk
pattern matching a Markdown fence, a deliberate command substitution. A
tripwire whose false positives look exactly like its true ones is one somebody
turns off.

THE FUNCTION HALF WAS ADDED AFTER A MEASURED MISS, which is the twelfth
instance: a new suite wrote three offenders and this caught two of them, both
on `pass`/`fail` lines, and missed the one on a `refuse ... \\` line. Nearly
every suite in this tree has a helper of its own -- `refuse`, `check`, `expect`
-- whose first argument IS the message `pass` and `fail` will print, so the
four names were never the boundary of the class, they were just the names that
had been noticed. DERIVED FROM THE FILE rather than listed, because a list is
the thing a new helper silently stays off -- the argument run_library_tiers.sh
makes for deriving its own table. Measured across all 190 suites afterwards:
no new findings beyond the real one, so the widening costs nothing.

Exit 1 and name every offender. `--selftest` proves it can fail.
"""
import re
import sys

BUILT_IN = ('pass', 'fail', 'printf', 'echo')
MSG = re.compile(r'^\s*(pass|fail|printf|echo)\s')
DEFN = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(\)\s*\{')


def message_matcher(text):
    """The message-carrying commands of ONE file: the four built in, plus
    every function the file defines, since a suite's own `refuse` or `check`
    helper takes the message its `pass`/`fail` will print."""
    names = set(BUILT_IN)
    for line in text.split('\n'):
        m = DEFN.match(line)
        if m:
            names.add(m.group(1))
    return re.compile(r'^\s*(%s)\s' % '|'.join(sorted(re.escape(n)
                                                      for n in names)))


def offending(line, msg=MSG):
    """A bare backtick inside a double-quoted span on a message line."""
    if not msg.match(line) or line.lstrip().startswith('#'):
        return False
    i, in_dq, in_sq = 0, False, False
    while i < len(line):
        c = line[i]
        if c == '\\' and i + 1 < len(line):
            i += 2                      # an escaped backtick is literal
            continue
        if c == "'" and not in_dq:
            in_sq = not in_sq
        elif c == '"' and not in_sq:
            in_dq = not in_dq
        elif c == '`' and in_dq:
            return True
        i += 1
    return False


def scan(paths):
    bad = []
    for p in paths:
        try:
            lines = open(p, errors='replace').read().split('\n')
        except OSError:
            continue
        msg = message_matcher('\n'.join(lines))
        for n, line in enumerate(lines, 1):
            if offending(line, msg):
                bad.append((p, n, line.strip()))
    return bad


if __name__ == '__main__':
    args = sys.argv[1:]
    if args and args[0] == '--selftest':
        # THE VACUITY CONTROL: "no offenders" is equally satisfied by a scanner
        # that reads nothing, so it must flag a planted one and leave the two
        # safe spellings beside it alone.
        planted = [
            ('fail "a message naming `load` lines"', True),
            ("fail 'a message naming `load` lines'", False),
            ('fail "a message naming \\`load\\` lines"', False),
            ('# fail "a comment naming `load`"', False),
        ]
        ok = True
        # AND THE HALF THE FOUR NAMES MISSED: a suite's own helper, which is
        # only a message line because the file defines it -- so the same text
        # must be flagged WITH that definition in scope and ignored without it,
        # or the widening would just be a longer list.
        helper_src = 'refuse() {\n    pass "$1"\n}\nrefuse "naming `closed` here" \\'
        helper_line = 'refuse "naming `closed` here" \\'
        if not offending(helper_line, message_matcher(helper_src)):
            print('SELFTEST FAIL a defined helper was not read as a message line')
            ok = False
        if offending(helper_line, message_matcher('nothing defined here')):
            print('SELFTEST FAIL an undefined word was read as a message line')
            ok = False
        for text, want in planted:
            got = offending(text)
            if got != want:
                print('SELFTEST FAIL want %s got %s for: %s' % (want, got, text))
                ok = False
        print('selftest ok' if ok else 'selftest FAILED')
        sys.exit(0 if ok else 1)
    bad = scan(args)
    for p, n, line in bad:
        print('%s:%d: a backtick inside a double-quoted message is command '
              'substitution -- escape it as \\` or single-quote the string\n'
              '    %s' % (p, n, line[:120]))
    sys.exit(1 if bad else 0)
