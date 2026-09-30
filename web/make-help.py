#!/usr/bin/env python3
"""Writes the in-page game guide (dist/help.html) for the web build.

The game content comes from the desktop key guides in
~/Desktop/Games/Roguelikes/Docs (build-docs.py + guides.py), so both guides
stay in sync; only the saving and "playing in the browser" parts are
written here, because they differ on the web."""
import html, importlib.util, os, sys

DOCS = os.path.expanduser('~/Desktop/Games/Roguelikes/Docs')
PAGE = "avanor.html"

sys.path.insert(0, DOCS)
spec = importlib.util.spec_from_file_location('build_docs', os.path.join(DOCS, 'build-docs.py'))
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)
from guides import GUIDES   # noqa: E402
import guides as guides_mod   # noqa: E402

game = next(g for g in docs.GAMES if g['file'] == PAGE)
guide = dict(GUIDES.get(PAGE, {}))
info = dict(game['info'])
kbd = docs.kbd
esc = html.escape

SAVING = guides_mod.SAVING[PAGE]

WEB = '''<ul>
<li>The map is the game's own screen (DawnLike tiles, or text after <em>Tiles</em> in the top bar); status, messages and the other panels are separate windows. <em>A−</em> / <em>A+</em> on a title bar (shown on hover) change that window's size of text or tiles; <em>Windows</em> shows, hides and rearranges them (drag a title bar or the gap between windows). Menus and pop-ups appear as text boxes over the map.</li>
<li><strong>Keys:</strong> the numeric keypad or the arrow keys move you; keypad <kbd>5</kbd> waits. <kbd>H</kbd> explores, <kbd>&lt;</kbd> / <kbd>&gt;</kbd> walk to the nearest known stairs (press again there to take them), <kbd>Enter</kbd> opens a menu of every command, <kbd>i</kbd> shows the pack with a cursor (Enter on an item lists what you can do with it).</li>
<li><strong>Audio ▾:</strong> sound effects made for this port (Avanor has no sound of its own), off by default; the choice is remembered.</li>
<li>Browsers keep a few shortcuts for themselves (<kbd>Ctrl+W</kbd>, <kbd>Ctrl+T</kbd>, <kbd>Ctrl+N</kbd>, and <kbd>Cmd</kbd> shortcuts on a Mac), so those never reach the game.</li>
<li>If the game ever crashes, a message appears at the top; reload the page to continue from the last save.</li>
</ul>'''

KEY_HINTS = [
    ('?', "The game's manual"),
    ('H', 'Auto-explore: walk to unexplored places (stops when a hostile is in view; any key stops)'),
    ('Enter', 'Menu of all commands'),
    ('i', 'Inventory with a cursor: Enter = everything you can do with the item'),
    ('<', 'Go up (walks to the nearest known stairs up; press again there)'),
    ('>', 'Go down (walks to the nearest known stairs down; press again there)'),
    ('S', 'Save the game'),
]


def dl(items):
    return '<dl>' + ''.join(f'<dt>{kbd(k)}</dt><dd>{esc(d)}</dd>' for k, d in items) + '</dl>'


def section(anchor, title, body):
    return f'<h2 id="h-{anchor}">{esc(title)}</h2>{body}'


parts = []
toc = [('about', 'About the game'), ('keys', 'Keyboard controls'), ('saving', 'Saving your game'),
       ('tips', 'Tips'), ('guide', "New player's guide"), ('web', 'Playing in the browser')]
parts.append('<p>' + esc(game['tagline']) + '</p>' + '<ul class="toc">' +
             ''.join(f'<li><a href="#h-{a}">{esc(t)}</a></li>' for a, t in toc) + '</ul>')


parts.append(section('about', 'About the game',
                     info['About the game']))

ess = ''.join(f'<div class="box"><h3>{esc(cat)}</h3>{dl(items)}</div>' for cat, items in game['essentials'])
all_keys = game['all']() if callable(game['all']) else game['all']
full = ''.join(f'<div>{kbd(k)}<span>{esc(d)}</span></div>' for k, d in all_keys)
parts.append(section('keys', 'Keyboard controls',
                     '<div class="box key"><h3>The keys to remember</h3>' + dl(KEY_HINTS) + '</div>'
                     '<h3>Essential keys</h3><div class="grid">' + ess + '</div>'
                     '<details><summary>Complete key list (' + str(len(all_keys)) + ' commands)</summary>'
                     '<div class="all">' + full + '</div></details>'))

parts.append(section('saving', 'Saving your game', SAVING))
parts.append(section('tips', 'Tips', info.get('Tips') or guide.pop('Tips', '')))
parts.append(section('guide', "New player's guide",
                     ''.join(f'<h3>{esc(t)}</h3>{b}' for t, b in guide.items())))
parts.append(section('web', 'Playing in the browser', WEB))

# RVIP: About this version
parts.append('<h2 id="h-version">About this version</h2><ul>'
             '<li>Based on <strong>Avanor 0.6.0</strong> (the revival of Vadim Gaidukevich\'s 2001 game by Joachim de Groot), upstream commit <a href="https://github.com/jaydg/avanor/tree/20a0f59">jaydg/avanor @ 20a0f59</a>, GPL-2.0-or-later. Original authors: Vadim Gaidukevich and Serge Semashko, with many contributors (see the manual\'s credits, <kbd>?</kbd>). Classic site: <a href="https://avanor.sourceforge.net/">avanor.sourceforge.net</a>.</li>'
             '<li>Tiles: <strong>DawnLike</strong> by DragonDePlatino, palette by DawnBringer, CC BY 4.0. Sound effects synthesized for this port. Built with Emscripten.</li>'
             '<li>Our changes (auto-explore, stairs walking, command menu, inventory cursor and item menus, sound, web build) '
             'are on GitHub: <a href="https://github.com/memmaker/avanor">memmaker/avanor</a> (<a href="https://github.com/memmaker/avanor/compare/20a0f59...main">all changes</a>).</li></ul>')
out = sys.argv[1] if len(sys.argv) > 1 else None
text = '\n'.join(parts)
if out:
    open(out, 'w').write(text)
else:
    print(text)
