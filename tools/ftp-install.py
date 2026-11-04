#!/usr/bin/env python3
"""ftp-install.py - install or update u3-3ds on a 3DS running ftpd, over Wi-Fi.

Usage: ftp-install.py <sd-staging-dir> [host] [port] [--only PATH ...]
  <sd-staging-dir> mirrors the SD card root (contains 3ds/u3-3ds/...).
  Default host 192.168.1.140, port 5000 (ftpd's default port).

Uploads only files whose size differs from the 3DS copy, so re-running it is cheap and
resumes after a dropped connection. Game files first, then the app, then the books.
Afterwards it prints the app's performance log (3ds/u3-3ds/debug.log) if one exists.
"""
import ftplib, io, os, sys, time

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    only = sys.argv[sys.argv.index('--only') + 1:] if '--only' in sys.argv else None
    root = args[0]
    host = args[1] if len(args) > 1 else '192.168.1.140'
    port = int(args[2]) if len(args) > 2 else 5000

    ftp = ftplib.FTP()
    ftp.connect(host, port, timeout=40)
    ftp.login()

    files = []
    base = os.path.join(root, '3ds', 'u3-3ds')
    for dp, _, fn in os.walk(base):
        for f in fn:
            files.append(os.path.relpath(os.path.join(dp, f), root).replace(os.sep, '/'))
    if only:
        files = [f for f in files if f in only]
    files.sort(key=lambda p: (0 if '/game/' in p else 1 if p.count('/') == 2 else 2, p))

    made, listing = set(), {}
    def ensure_dir(d):
        cur = ''
        for part in d.strip('/').split('/'):
            cur += '/' + part
            if cur not in made:
                try: ftp.mkd(cur)
                except ftplib.error_perm: pass
                made.add(cur)
    def sizes(d):
        out, lines = {}, []
        try: ftp.retrlines('LIST ' + d, lines.append)
        except ftplib.error_perm: return out
        for l in lines:
            p = l.split(None, 8)
            if len(p) == 9 and p[4].isdigit(): out[p[8]] = int(p[4])
        return out

    sent = skipped = 0
    t0 = time.time()
    for rel in files:
        d, name = '/' + os.path.dirname(rel), os.path.basename(rel)
        if d not in listing:
            ensure_dir(d); listing[d] = sizes(d)
        local = os.path.join(root, rel)
        if listing[d].get(name) == os.path.getsize(local):
            skipped += 1; continue
        with open(local, 'rb') as fh:
            ftp.storbinary('STOR /' + rel, fh, 65536)
        sent += 1
        print('sent', rel, flush=True)
    print('%d sent, %d already current, %ds' % (sent, skipped, time.time() - t0))

    buf = io.BytesIO()
    try:
        ftp.retrbinary('RETR /3ds/u3-3ds/debug.log', buf.write)
        print('--- debug.log (last 40 lines) ---')
        print('\n'.join(buf.getvalue().decode(errors='replace').splitlines()[-40:]))
    except ftplib.error_perm:
        pass
    ftp.quit()

if __name__ == '__main__':
    main()
