#!/usr/bin/env python3
"""What each captured client needs from a server (docs/gsql_pgwire_design.md §2).

Reads corpus/*.jsonl (written by proxy.py) and prints, per client: the
statements it sent, how many touch pg_catalog or information_schema, whether
it asked for binary formats (and for which type OIDs in results), its startup
parameters, and the PostgreSQL-only syntax it used. Then the union of catalog
relations and functions across all clients."""
import collections, glob, json, os, re, sys

corpus = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), 'corpus')
CAT = re.compile(r'\b(pg_catalog\.)?(pg_[a-z_]+)\b|information_schema\.[a-z_]+')
NOT_FN = set('select in exists values any cast coalesce count not and or from as on when then over filter case where min max trim array row using returning into join'.split())
rels = collections.defaultdict(set); fns = collections.defaultdict(set)

def features(s):
    f = set()
    if re.search(r'::\s*(pg_catalog\.)?reg', s): f.add('::reg casts')
    elif '::' in s: f.add(':: casts')
    if 'OPERATOR(' in s: f.add('OPERATOR()')
    if re.search(r"!~|~\s*'", s): f.add('regex ~')
    if re.search(r'\bANY\s*\(', s, re.I): f.add('= ANY(array)')
    if re.search(r'generate_series|generate_subscripts|unnest|_pg_expandarray', s, re.I): f.add('set-returning functions')
    if re.search(r'array_agg|array_upper|array_length|array_to_string|\{0\}', s): f.add('arrays')
    if re.search(r'^\s*SHOW\b', s, re.I): f.add('SHOW')
    if re.search(r'^\s*(SAVEPOINT|RELEASE)\b', s, re.I): f.add('savepoints')
    if re.search(r'^\s*DEALLOCATE\b', s, re.I): f.add('DEALLOCATE')
    return f

print('%-13s %5s %5s  %-28s %s' % ('client', 'stmts', 'catlg', 'binary', 'postgres-only syntax'))
for path in sorted(glob.glob(os.path.join(corpus, '*.jsonl'))):
    name = os.path.basename(path)[:-6]
    recs = [json.loads(l) for l in open(path)]
    n = cat = 0; binary = set(); feat = set(); start = {}
    for r in recs:
        if r['dir'] == 'F' and r['t'] == 'Startup':
            start = r['params']
        if r['dir'] == 'F' and r['t'] in ('Q', 'P'):
            s = r['sql']; n += 1
            if CAT.search(s):
                cat += 1
                for m in re.finditer(r'\b(pg_[a-z_]+)\b(?!\s*\()', s): rels[m.group(1)].add(name)
                for m in re.finditer(r'\b((?:pg_catalog\.)?[a-z_]+)\s*\(', s, re.I):
                    w = m.group(1).lower().replace('pg_catalog.', '')
                    if w not in NOT_FN: fns[w].add(name)
            feat |= features(s)
        if r['dir'] == 'F' and r['t'] == 'B' and any(r['param_formats']): binary.add('params')
        if r['dir'] == 'B' and r['t'] == 'T':
            binary |= {'oid %d' % c[1] for c in r['cols'] if c[2]}
    print('%-13s %5d %5d  %-28s %s' % (name, n, cat, ','.join(sorted(binary)) or '-', ', '.join(sorted(feat)) or '-'))
    extra = {k: v for k, v in start.items() if k not in ('user', 'database')}
    if extra: print('%-13s startup: %s' % ('', extra))
print('\ncatalog relations (%d):' % len(rels))
for k in sorted(rels): print('  %-26s %s' % (k, ' '.join(sorted(rels[k]))))
print('\nfunctions in catalog queries (%d):' % len(fns))
for k in sorted(fns): print('  %-30s %s' % (k, ' '.join(sorted(fns[k]))))
