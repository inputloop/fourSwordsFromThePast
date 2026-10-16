#!/usr/bin/env python3
"""books: the two books (the player's manual and the developer's guide), from one Markdown
source each, to Windows help (.chm), HTML pages, one-page HTML and plain text; and the check
that a game book's routes are sound.

usage: python3 tools/books.py check [BOOK...]     the routes, the pages the help key needs
       python3 tools/books.py build [--out help]  every target (CHM needs chmcmd; HTML needs pandoc)
       python3 tools/books.py graph BOOK          the routes as TSV: from, to (data for the lab)
       python3 tools/books.py voice FILE...       numbers for "consistency of voice > tone of voice" (TSV)

A BOOK is docs/books/<name>.md. Its form (the C4 game-book markers, so c4/h/gb1md can lint it too):
  <!-- GB:book title="..." author="..." -->     once, at the top
  ## §N · Title                                   a section; N counts up from 1
  <!-- page: NAME -->                             (optional) the section's page name; the help
                                                  key opens help/<book>/NAME.html
  <!-- end -->                                    (optional) this section may have no way on
  "turn to §N", or any §N, in the text            a route; it becomes a link
The checks (exit 1 on any): every §N named exists; every section can be reached from §1; every
section has a way on unless it says <!-- end -->; section numbers are 1..n with none missing;
page names are unique; every page Helpbook_Topic() can return (src/helpbook.c) is a page of
the manual.
"""
import html, os, re, shutil, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
BOOKS = {'manual': 'FourSwords-Manual', 'devguide': 'FourSwords-DevGuide'}
SEC = re.compile(r'^## §(\d+) · (.+)$', re.M)
REF = re.compile(r'§(\d+)')

def parse(name):
    path = os.path.join(ROOT, 'docs', 'books', name + '.md')
    text = open(path, encoding='utf-8').read()
    m = re.search(r'<!-- GB:book title="([^"]*)" author="([^"]*)" -->', text)
    title, author = (m.group(1), m.group(2)) if m else (name, '')
    heads = list(SEC.finditer(text))
    front = text[:heads[0].start()] if heads else text
    secs = []
    for i, h in enumerate(heads):
        body = text[h.end():heads[i + 1].start() if i + 1 < len(heads) else len(text)]
        pm = re.search(r'<!-- page: ([a-z0-9-]+) -->', body)
        secs.append(dict(n=int(h.group(1)), title=h.group(2).strip(), body=body,
                         page=pm.group(1) if pm else f's{h.group(1)}', end='<!-- end -->' in body,
                         line=text[:h.start()].count('\n') + 1,
                         refs=sorted({int(x) for x in REF.findall(body)} - {int(h.group(1))})))
    return dict(name=name, path=path, title=title, author=author, front=front, secs=secs)

def helpbook_topics():
    src = os.path.join(ROOT, 'src', 'helpbook.c')
    if not os.path.exists(src):
        return []
    code = open(src, encoding='utf-8').read()
    body = code[code.index('Helpbook_Topic(void) {'):]
    body = body[:body.index('\n}\n')]
    return sorted(set(re.findall(r'return "([a-z-]+)";', body)))

def check(book):
    errs, rel = [], os.path.relpath(book['path'], ROOT)
    nums = [s['n'] for s in book['secs']]
    if nums != list(range(1, len(nums) + 1)):
        errs.append(f'{rel}: sections are not numbered 1..{len(nums)} in order: {nums}')
    have = set(nums)
    for s in book['secs']:
        for r in s['refs']:
            if r not in have:
                errs.append(f'{rel}:{s["line"]}: §{s["n"]} sends the reader to §{r}, which does not exist')
        if not s['refs'] and not s['end']:
            errs.append(f'{rel}:{s["line"]}: §{s["n"]} has no way on (add a route, or <!-- end -->)')
    seen, todo = set(), [1] if 1 in have else []
    by = {s['n']: s for s in book['secs']}
    while todo:
        n = todo.pop()
        if n in seen or n not in by:
            continue
        seen.add(n)
        todo += by[n]['refs']
    for n in sorted(have - seen):
        errs.append(f'{rel}:{by[n]["line"]}: §{n} cannot be reached from §1')
    pages = [s['page'] for s in book['secs']]
    for p in sorted({p for p in pages if pages.count(p) > 1}):
        errs.append(f'{rel}: the page name "{p}" is used twice')
    if book['name'] == 'manual':
        for t in helpbook_topics():
            if t not in pages:
                errs.append(f'{rel}: the help key can open the page "{t}" (src/helpbook.c), but no section is <!-- page: {t} -->')
    return errs

def link(book, text):
    by = {s['n']: s['page'] for s in book['secs']}
    return REF.sub(lambda m: f'[§{m.group(1)}]({by[int(m.group(1))]}.html)' if int(m.group(1)) in by else m.group(0), text)

CSS = """:root{--ink:#1d1b16;--paper:#fbf8f1;--rule:#d8cfbd;--accent:#2f6b3a;--muted:#6b6455}
@media (prefers-color-scheme: dark){:root{--ink:#ece6d8;--paper:#1b1a17;--rule:#3c382f;--accent:#8fc79a;--muted:#a59d8b}}
body{background:var(--paper);color:var(--ink);font:17px/1.6 Georgia,'Times New Roman',serif;max-width:40em;margin:2em auto;padding:0 16px}
h1,h2{font-family:'Segoe UI',system-ui,sans-serif;line-height:1.25} h2{border-bottom:1px solid var(--rule);padding-bottom:.2em}
a{color:var(--accent)} code,pre{font-family:Consolas,'DejaVu Sans Mono',monospace;font-size:.92em}
pre{background:rgba(127,127,127,.12);padding:.7em 1em;overflow-x:auto;border-radius:4px}
nav{font:14px 'Segoe UI',system-ui,sans-serif;color:var(--muted);border-top:1px solid var(--rule);margin-top:2.5em;padding-top:.8em}
.byline{color:var(--muted);font-style:italic} table{border-collapse:collapse} td,th{border:1px solid var(--rule);padding:.25em .6em}
"""

def pandoc(md, fmt='html'):
    r = subprocess.run(['pandoc', '-f', 'gfm', '-t', fmt, '--wrap=none'], input=md, capture_output=True, text=True, check=True)
    return r.stdout

def page_html(book, title, body_html, nav):
    return (f'<!DOCTYPE html>\n<html lang="en"><head><meta charset="utf-8">'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">'
            f'<title>{html.escape(title)} · {html.escape(book["title"])}</title>'
            f'<link rel="stylesheet" href="book.css"></head><body>\n{body_html}\n<nav>{nav}</nav></body></html>\n')

def build(out):
    os.makedirs(out, exist_ok=True)
    have_chm = shutil.which('chmcmd')
    for name, chm in BOOKS.items():
        book = parse(name)
        errs = check(book)
        if errs:
            print('\n'.join(errs), file=sys.stderr)
            raise SystemExit(f'books: {name} has route faults; not building it')
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, 'book.css'), 'w').write(CSS)
        front = re.sub(r'<!--.*?-->', '', book['front'], flags=re.S)
        files = []
        open(os.path.join(d, 'index.html'), 'w', encoding='utf-8').write(page_html(
            book, 'Start', pandoc(link(book, front)),
            f'<a href="{book["secs"][0]["page"]}.html">Begin at §1</a> · <a href="all.html">The whole book on one page</a>'))
        files.append(('Start', 'index.html'))
        for s in book['secs']:
            body = f'## §{s["n"]} · {s["title"]}\n' + re.sub(r'<!--.*?-->', '', s['body'], flags=re.S)
            nav = '<a href="index.html">Start</a> · ' + ' · '.join(
                f'<a href="{x["page"]}.html">§{x["n"]}</a>' for x in book['secs'])
            open(os.path.join(d, s['page'] + '.html'), 'w', encoding='utf-8').write(
                page_html(book, f'§{s["n"]} {s["title"]}', pandoc(link(book, body)), nav))
            files.append((f'§{s["n"]} · {s["title"]}', s['page'] + '.html'))
        whole = front + ''.join(f'\n<a id="{s["page"]}"></a>\n## §{s["n"]} · {s["title"]}\n' +
                                re.sub(r'<!--.*?-->', '', s['body'], flags=re.S) for s in book['secs'])
        anchors = {x['n']: x['page'] for x in book['secs']}
        whole_l = REF.sub(lambda m: '[§%s](#%s)' % (m.group(1), anchors.get(int(m.group(1)), '')), whole)
        open(os.path.join(d, 'all.html'), 'w', encoding='utf-8').write(page_html(book, 'The whole book', pandoc(whole_l), '<a href="index.html">Start</a>'))
        open(os.path.join(out, name + '.txt'), 'w', encoding='utf-8').write(pandoc(re.sub(r'<!--.*?-->', '', whole, flags=re.S), 'plain'))
        if have_chm:
            write_chm_project(d, chm, book, files)
            r = subprocess.run(['chmcmd', chm + '.hhp'], cwd=d, capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(os.path.join(d, chm + '.chm')):
                raise SystemExit(f'books: chmcmd failed for {name}:\n{r.stdout}{r.stderr}')
            shutil.move(os.path.join(d, chm + '.chm'), os.path.join(out, chm + '.chm'))
        print(f'books: {name}: {len(book["secs"])} sections -> {d}/' + (f', {out}/{chm}.chm' if have_chm else ' (no chmcmd: no .chm)'))

def write_chm_project(d, chm, book, files):
    hhp = [ '[OPTIONS]', 'Compatibility=1.1 or later', f'Compiled file={chm}.chm', f'Contents file={chm}.hhc',
            f'Index file={chm}.hhk', 'Default topic=index.html', 'Display compile progress=No',
            'Full-text search=Yes', 'Language=0x409 English (United States)', f'Title={book["title"]}', '',
            '[FILES]'] + [f for _, f in files] + ['book.css', 'all.html', '']
    open(os.path.join(d, chm + '.hhp'), 'w', newline='\r\n').write('\n'.join(hhp))
    def obj(name, local):
        return (f'<LI><OBJECT type="text/sitemap"><param name="Name" value="{html.escape(name)}">'
                f'<param name="Local" value="{local}"></OBJECT>')
    items = '\n'.join(obj(n, f) for n, f in files)
    open(os.path.join(d, chm + '.hhc'), 'w', newline='\r\n').write(
        f'<HTML><BODY><OBJECT type="text/site properties"></OBJECT><UL>\n{items}\n</UL></BODY></HTML>\n')
    idx = '\n'.join(obj(s['title'], s['page'] + '.html') for s in sorted(book['secs'], key=lambda s: s['title'].lower()))
    open(os.path.join(d, chm + '.hhk'), 'w', newline='\r\n').write(f'<HTML><BODY><UL>\n{idx}\n</UL></BODY></HTML>\n')

def voice(paths):
    """Numbers for the claim "consistency of voice > tone of voice": what should match across the
    books (voice) and what should differ by reader (tone). Counts only; no verdict."""
    tax = os.path.join(ROOT, 'CONTEXT-MODELS', 'TAXONOMY.tsv')
    lab = []
    if os.path.exists(tax):
        for line in open(tax, encoding='utf-8'):
            f = line.rstrip('\n').split('\t')
            if len(f) == 6 and f[1] in ('lab', 'branch') and not line.startswith('#'):
                lab += [a.strip() for a in re.split(r'[/,(]', f[0]) if len(a.strip()) > 3]
    print('file\twords\tsentences\twords_per_sentence\tyou_per_100\tI_per_100\troutes\tlab_terms\tem_dashes')
    for p in paths:
        text = open(p, encoding='utf-8').read()
        text = re.sub(r'<!--.*?-->', '', text, flags=re.S)
        text = re.sub(r'```.*?```', '', text, flags=re.S)
        text = re.sub(r'(?im)^[ \t]*#\+begin_src.*?^[ \t]*#\+end_src[^\n]*', '', text, flags=re.S)
        text = re.sub(r'(?m)^[ \t]*(#\+[A-Z_]+:.*|\|.*)$', '', text)  # Org keywords and table rows are not sentences
        words = re.findall(r"[A-Za-z][A-Za-z']*", text)
        n = max(len(words), 1)
        sents = [s for s in re.split(r'(?<=[.!?])\s+', text) if re.search(r'[A-Za-z]', s)]
        low = [w.lower() for w in words]
        you = sum(1 for w in low if w in ('you', 'your', "you're", 'yours'))
        me = sum(1 for w in low if w in ('i', "i'm", 'my', 'me'))
        routes = len(re.findall(r'turn to §\d+', text, re.I))
        labn = sum(len(re.findall(r'(?<![A-Za-z])' + re.escape(t) + r'(?![A-Za-z])', text, re.I)) for t in lab)
        print(f'{os.path.relpath(p, ROOT)}\t{len(words)}\t{len(sents)}\t{len(words)/max(len(sents),1):.1f}\t'
              f'{100*you/n:.1f}\t{100*me/n:.1f}\t{routes}\t{labn}\t{text.count(chr(0x2014))}')

def main(argv):
    cmd = argv[0] if argv else 'check'
    if cmd == 'check':
        errs = [e for n in (argv[1:] or BOOKS) for e in check(parse(n))]
        print('\n'.join(errs) if errs else 'books: every route goes somewhere, every section can be reached, every help page exists')
        return 1 if errs else 0
    if cmd == 'build':
        out = argv[argv.index('--out') + 1] if '--out' in argv else os.path.join(ROOT, 'help')
        build(out)
        return 0
    if cmd == 'voice' and len(argv) >= 2:
        voice(argv[1:])
        return 0
    if cmd == 'graph' and len(argv) == 2:
        b = parse(argv[1])
        print('from\tto')
        for s in b['secs']:
            for r in s['refs']:
                print(f'{s["n"]}\t{r}')
        return 0
    print(__doc__, file=sys.stderr)
    return 2

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
