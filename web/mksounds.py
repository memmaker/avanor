#!/usr/bin/env python3
"""Synthesize Avanor's sound effects at build time: one <event>.wav per RVIP_SOUND("event")
in the game (*/*.cpp, asserted) into <out>, plus <out>/sounds.json {event: [file]}.
Avanor ships no audio upstream (none found in a web search), so these are
made for it. Stdlib only.
Usage (repo root): python3 web/mksounds.py <out>"""
import glob, json, math, os, random, re, struct, sys, wave

R = 22050
rnd = random.Random(2001)

def tone(f0, f1, dur, vol=.45, dec=2.0, fm=1.0):
    """sine with a 2:1 modulator; fm = modulation depth (0 = pure sine)"""
    out, ph, n = [], 0.0, int(R * dur)
    for i in range(n):
        t = i / n
        ph += f0 * (f1 / f0) ** t / R
        x = math.sin(2 * math.pi * ph + fm * (1 - t) * math.sin(4 * math.pi * ph))
        out.append(x * vol * (1 - t) ** dec)
    return out

def noise(dur, vol=.5, dec=2.0, lp=.3, swell=False):
    out, y, n = [], 0.0, int(R * dur)
    for i in range(n):
        t = i / n
        y += lp * (rnd.uniform(-1, 1) - y)          # one-pole low-pass: small lp = duller
        out.append(y * vol * (math.sin(math.pi * t) if swell else (1 - t) ** dec))
    return out

def mix(*parts):
    out = [0.0] * max(map(len, parts))
    for p in parts:
        for i, x in enumerate(p):
            out[i] += x
    return out

def notes(fs, d=.07, **k):
    return sum((tone(f, f, d, **k) for f in fs), [])

SOUNDS = {
    'hit':     lambda: mix(noise(.09, .7, 3, .3), tone(200, 80, .1, .4, 2, 2)),
    'miss':    lambda: noise(.18, .35, lp=.5, swell=True),
    'kill':    lambda: mix(noise(.12, .5, 2, .2), tone(300, 60, .35, .4, 1, 3)),
    'mon_hit': lambda: mix(noise(.14, .8, 2, .1), tone(130, 45, .14, .45, 2, 2)),
    'shoot':   lambda: noise(.12, .3, lp=.8, swell=True) + tone(900, 500, .05, .15, 2, 0),
    'pickup':  lambda: tone(500, 800, .06, .3, 1, .5),
    'drop':    lambda: mix(noise(.07, .6, 4, .1), tone(150, 90, .07, .35, 3, 1)),
    'level':   lambda: notes([523, 659, 784], .08, vol=.35, dec=.3, fm=.8) + tone(1047, 1047, .4, .35, 1.5, .8),
    'spell':   lambda: notes([988, 1319, 1661, 1976], .04, vol=.25, dec=.4, fm=2) + tone(1976, 988, .25, .2, 1.5, 1),
    'quaff':   lambda: sum((tone(f, f * 1.8, .05, .35, 1, 0) for f in (320, 410, 360, 480)), []),
    'eat':     lambda: sum((noise(.06, .5, 3, .2) + [0.0] * 1500 for _ in range(3)), []),
    'read':    lambda: noise(.12, .3, lp=.7, swell=True) + noise(.1, .25, lp=.8, swell=True),
    'wear':    lambda: noise(.1, .4, 2, .15) + noise(.08, .3, 2, .2),
    'pray':    lambda: mix(tone(392, 392, 1.0, .2, 1, .3), tone(494, 494, 1.0, .15, 1, .3), tone(587, 587, 1.0, .15, 1, .3)),
    'stairs':  lambda: notes([587, 554, 494, 440], .06, vol=.3, dec=.5, fm=.6),
    'death':   lambda: notes([349, 330, 311], .25, vol=.35, dec=.3, fm=1.5) + tone(294, 110, 1.0, .35, 1.2, 2),
}

events = set()
for f in glob.glob('*/*.cpp'):
    events |= set(re.findall(r'RVIP_SOUND\s*\("(\w+)"', open(f, encoding='utf-8', errors='replace').read()))
    events |= {e for p in re.findall(r'RVIP_SOUND\([^;]*\?\s*"(\w+)"\s*:\s*"(\w+)"\)', open(f, encoding='utf-8', errors='replace').read()) for e in p}
assert events and events <= set(SOUNDS), 'events without a sound: %s' % sorted(events - set(SOUNDS))

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for ev in sorted(events):
    s = SOUNDS[ev]()
    with wave.open(os.path.join(out, ev + '.wav'), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(R)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, x)) * 32000)) for x in s))
json.dump({ev: [ev + '.wav'] for ev in sorted(events)}, open(os.path.join(out, 'sounds.json'), 'w'))
