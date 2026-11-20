#!/usr/bin/env python3
"""Text for a syntax definition to highlight, made from the definition itself:
its keywords, the strings and characters its rules look for, numbers,
identifiers, white space and empty lines, in a seeded random order, so every
context a definition has is likely to be entered and left. Read with
Python's own XML parser, never src/ksyntax.c.

    ksh_corpus.py DEFINITION.xml SEED LINES > text
"""
import random
import sys
import xml.etree.ElementTree as ET


def tokens(path):
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError:
        return []
    out = []
    for item in root.iter('item'):
        if item.text and item.text.strip():
            out.append(item.text.strip())
    for el in root.iter():
        a = el.attrib
        if el.tag in ('StringDetect', 'WordDetect') and a.get('String'):
            if not a.get('dynamic'):
                out.append(a['String'])
        if el.tag in ('DetectChar', 'Detect2Chars', 'RangeDetect', 'LineContinue'):
            for k in ('char', 'char1'):
                if a.get(k) and not (el.tag == 'DetectChar' and a.get('dynamic')):
                    out.append(a[k])
        if el.tag == 'AnyChar' and a.get('String'):
            out.extend(list(a['String']))
    return out


def main():
    path, seed, lines = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    rnd = random.Random(seed)
    toks = tokens(path)
    stock = ['0', '42', '3.14', '1e5', '0x1F', '017', '"text"', "'c'", '\\n', '\\', 'name', '_x1',
             '(', ')', '{', '}', '[', ']', ';', ',', '.', ':', '=', '#', '//', '/*', '*/', '<!--',
             '-->', '<', '>', '@', '$', '%', '&', '|', '?', '!', 'é', 'ünï', '→']
    for _ in range(lines):
        r = rnd.random()
        if r < 0.08:
            print('')
            continue
        parts = []
        if rnd.random() < 0.3:
            parts.append(rnd.choice([' ', '  ', '\t', '    ']))
        for _ in range(rnd.randint(1, 9)):
            pool = toks if toks and rnd.random() < 0.7 else stock
            parts.append(rnd.choice(pool))
            parts.append(rnd.choice(['', ' ', ' ', ' ', '\t', '']))
        print(''.join(parts).rstrip('\n'))


main()
