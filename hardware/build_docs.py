#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
HTML-версии описаний платы: docs/*.md -> docs/*.html (открываются двойным щелчком, интернет не нужен).
Запуск:  python hardware/build_docs.py
"""
import os, re
import markdown

DOCS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'docs')
PAGES = [('hardware-carrier-board', 'Плата-носитель'), ('bom', 'Список покупок'), ('assembly', 'Сборка')]

CSS = """
:root { --bg:#f6f7f5; --paper:#fff; --ink:#1f2a27; --muted:#5f6f6a; --line:#dfe5e2; --accent:#1b6b4f; --warn:#fff6db; --warnline:#e8b93a; }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--ink); font:16px/1.6 "Segoe UI", system-ui, sans-serif; }
nav { position:sticky; top:0; z-index:2; background:var(--paper); border-bottom:1px solid var(--line); display:flex; gap:4px; padding:8px 16px; flex-wrap:wrap; }
nav a { text-decoration:none; color:var(--muted); padding:6px 12px; border-radius:6px; font-weight:600; }
nav a:hover { background:var(--bg); color:var(--ink); }
nav a.on { background:#e3f1ea; color:var(--accent); }
main { max-width:1080px; margin:0 auto; padding:24px 20px 80px; }
h1 { font-size:30px; line-height:1.2; margin:8px 0 16px; }
h2 { font-size:23px; margin:40px 0 12px; padding-top:12px; border-top:1px solid var(--line); }
h3 { font-size:18px; margin:28px 0 8px; }
p, li { max-width:78ch; }
a { color:var(--accent); }
img { max-width:100%; height:auto; display:block; margin:12px 0; border:1px solid var(--line); border-radius:8px; background:#fff; }
.tbl { overflow-x:auto; margin:12px 0; }
table { border-collapse:collapse; background:var(--paper); font-size:15px; min-width:60%; }
th, td { border:1px solid var(--line); padding:6px 10px; text-align:left; vertical-align:top; }
th { background:#eef2f0; }
code { font-family:Consolas, "Cascadia Mono", monospace; font-size:14px; background:#eef2f0; padding:1px 5px; border-radius:4px; }
pre { background:#1f2a27; color:#e8efec; padding:12px 14px; border-radius:8px; overflow-x:auto; }
pre code { background:none; color:inherit; padding:0; }
blockquote { margin:14px 0; padding:10px 14px; background:var(--warn); border-left:4px solid var(--warnline); border-radius:0 8px 8px 0; }
blockquote p { margin:4px 0; }
hr { border:0; border-top:1px solid var(--line); margin:28px 0; }
"""


def build(slug, title):
    src = open(os.path.join(DOCS, slug + '.md'), encoding='utf-8').read()
    body = markdown.markdown(src, extensions=['tables', 'fenced_code', 'sane_lists', 'toc'])
    body = re.sub(r'href="([\w\-]+)\.md(#[^"]*)?"', lambda m: 'href="%s.html%s"' % (m.group(1), m.group(2) or ''), body)
    body = body.replace('<table>', '<div class="tbl"><table>').replace('</table>', '</table></div>')
    nav = ''.join('<a href="%s.html"%s>%s</a>' % (s, ' class="on"' if s == slug else '', t) for s, t in PAGES)
    html = ('<!doctype html><html lang="ru"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width, initial-scale=1">'
            '<title>%s — DripIrrigation</title><style>%s</style></head>'
            '<body><nav>%s</nav><main>%s</main></body></html>') % (title, CSS, nav, body)
    out = os.path.join(DOCS, slug + '.html')
    open(out, 'w', encoding='utf-8').write(html)
    return out


for slug, title in PAGES:
    print('ok', build(slug, title))
