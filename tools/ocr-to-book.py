#!/usr/bin/env python3
"""ocr-to-book.py - join tesseract page halves into the book text format the 3DS reader uses.

Usage: ocr-to-book.py <ocr-dir> <prefix> <out.txt>
Reads <prefix>-NN_H.txt (NN = page, H = 0 left / 1 right half) and writes paragraphs with
"@page N H" anchors (used by the reader's "See page" jump). Prose lines are unwrapped;
short lines (tables, lists) are kept; OCR'd list markers become bullets.
"""
import glob, os, re, sys

def clean(t):
    for a, b in (('’', "'"), ('‘', "'"), ('“', '"'), ('”', '"'),
                 ('—', '-'), ('ﬁ', 'fi'), ('ﬂ', 'fl')):
        t = t.replace(a, b)
    t = re.sub(r'-\n(\w)', r'\1', t)
    out = []
    for p in (p.strip() for p in re.split(r'\n\s*\n', t)):
        if not p:
            continue
        lines = p.split('\n')
        if sum(len(l) for l in lines) / len(lines) > 45:
            p = ' '.join(l.strip() for l in lines)
        out.append(re.sub(r'[ \t]+', ' ', p))
    return '\n\n'.join(out)

def main():
    src, pre, dst = sys.argv[1:4]
    parts = []
    for f in sorted(glob.glob(os.path.join(src, pre + '-*_*.txt'))):
        m = re.search(pre + r'-(\d+)_(\d)\.txt$', f)
        t = open(f, encoding='utf-8').read()
        if len(t.split()) < 3:
            continue
        parts.append('@page %d %d\n' % (int(m.group(1)), int(m.group(2))) + clean(t))
    text = '\n\n'.join(parts) + '\n'
    text = re.sub(r'(?m)^(e|@|©|®|°|\*|«) (?=[A-Z])', '• ', text)
    open(dst, 'w', encoding='utf-8').write(text)
    print(dst, sum(len(p.split()) for p in parts), 'words')

if __name__ == '__main__':
    main()
