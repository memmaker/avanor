/*
 * Avanor in the browser (RVIP stage 5): windows from rvip-wm.js, saves
 * through rvip-app.js. The game (port/be_web.cpp) sends every window its
 * own content via Module.av: the whole level for the Map (sprite slots or
 * glyph + colour per cell; page scrolls it with the hero), status lines,
 * message lines, the prompt row, a pop-up for menus and full screens, and
 * the Inventory and Visible lines built from game data. Text lines carry
 * colour runs "\x05#rrggbb". JS only lays out, scrolls, zooms and stores.
 */
(function () {
	'use strict';

	var DIR = RvipApp.dir;                       /* IDBFS mount; the game's HOME_DIR ("/avanor/") */
	var SAVE = DIR + '/avanor.svg.zst', LAYOUT_FILE = DIR + '/web-layout.json';
	var FONT = '"DejaVu Sans Mono", Menlo, Consolas, "Liberation Mono", monospace';
	/* Tile sets offered, then None (text). Slot layout: web/mkdawn.py.
	 * Credit: DawnLike by DragonDePlatino, palette DawnBringer (CC BY 4.0). */
	var SETS = [['DawnLike', 'tiles-dawn.png']];
	/* how the sheet is cut and the cell each slot shows (null: slot = cell): av.tileset from the rec */
	var cut = { w: 16, h: 16, ox: 0, oy: 0, gx: 0, gy: 0 }, cells = null;
	function cellXY(t) {
		var c = cells ? cells[t] : t, cols;
		if (!(c >= 0)) return null;
		cols = Math.max(1, Math.floor((sheet.width - cut.ox + cut.gx) / (cut.w + cut.gx)));
		return [cut.ox + c % cols * (cut.w + cut.gx), cut.oy + Math.floor(c / cols) * (cut.h + cut.gy)];
	}
	var CELLS = [8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 64];

	function $(id) { return document.getElementById(id); }
	var L = null, wm = null, app, rects = {};
	var sheet = null, sheetGen = 0;

	/* ---------- map ---------- */
	var cv = document.querySelector('#t-map canvas'), ctx = cv.getContext('2d');
	var M = { len: 0, hgt: 0, ch: null, rgb: null, tl: null, hx: 0, hy: 0, cur: -1 }, drawn = null;
	function hex(v) { return '#' + ('00000' + (v >>> 0).toString(16)).slice(-6); }
	function cell() { return L ? L.cell : 24; }
	function useTiles() { return !!sheet && L && L.tiles !== 'None'; }

	function drawCell(i) {
		var c = cell(), x = i % M.len, y = (i / M.len) | 0, px = x * c, py = y * c, t = M.tl[i];
		ctx.fillStyle = '#000'; ctx.fillRect(px, py, c, c);
		if (useTiles() && t >= 0) {
			var bg = t & 0xfff, fg = (t >> 12) & 0xfff, p;
			if (bg !== 0xfff && (p = cellXY(bg))) ctx.drawImage(sheet, p[0], p[1], cut.w, cut.h, px, py, c, c);
			if (fg !== 0xfff && (p = cellXY(fg))) ctx.drawImage(sheet, p[0], p[1], cut.w, cut.h, px, py, c, c);
			if (t & (1 << 24)) { ctx.fillStyle = 'rgba(0,0,0,0.55)'; ctx.fillRect(px, py, c, c); }
		} else if (M.ch[i] > 32) {
			ctx.fillStyle = hex(M.rgb[i]);
			ctx.fillText(String.fromCharCode(M.ch[i]), px + c / 2, py + c / 2 + 1);
		}
		if (i === M.cur) { ctx.fillStyle = '#dcdcdc'; ctx.fillRect(px, py + c - 2, c, 2); }
	}
	function setFont() {
		ctx.imageSmoothingEnabled = false;
		ctx.font = (L && L.mapFace ? '' : 'bold ') + Math.round(cell() * 0.8) + 'px ' + (L && L.mapFace ? '"' + L.mapFace + '", ' : '') + FONT;
		ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
	}
	/* full redraw (size, tile set, font), else only the cells that changed */
	function drawMap(full) {
		if (!M.ch) return;
		var c = cell(), w = M.len * c, h = M.hgt * c;
		if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; full = true; }
		setFont();
		var n = M.len * M.hgt;
		if (full || !drawn || drawn.n !== n) {
			for (var i = 0; i < n; i++) drawCell(i);
		} else {
			for (var j = 0; j < n; j++)
				if (drawn.ch[j] !== M.ch[j] || drawn.rgb[j] !== M.rgb[j] || drawn.tl[j] !== M.tl[j] || j === drawn.cur || j === M.cur) drawCell(j);
		}
		drawn = { n: n, ch: M.ch, rgb: M.rgb, tl: M.tl, cur: M.cur };
		center();
	}
	/* camera: the hero centred, clamped (RvipWM.center) */
	function center() {
		if (!M.ch) return;
		var c = cell();
		cv.style.width = M.len * c + 'px'; cv.style.height = M.hgt * c + 'px';
		RvipWM.center(cv, (M.hx + 0.5) * c, (M.hy + 0.5) * c, M.len * c, M.hgt * c);
	}
	function zoomMap(d) {
		var i = CELLS.indexOf(cell()); if (i < 0) i = 6;
		L.cell = CELLS[Math.max(0, Math.min(CELLS.length - 1, i + d))];
		drawMap(true); saveLayout();
	}

	/* ---------- text ---------- */
	function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;'); }
	/* a line with colour runs as HTML; cx = cursor column or -1 */
	function lineHtml(s, cx) {
		var out = '', open = false, k = 0, parts = s.split('\x05');
		if (cx >= 0) { var len = parts.reduce(function (a, p, i) { return a + (i ? p.length - 7 : p.length); }, 0); while (len++ <= cx) parts[parts.length - 1] += ' '; }
		parts.forEach(function (p, i) {
			if (i) { if (open) out += '</span>'; out += '<span style="color:' + p.slice(0, 7) + '">'; open = true; p = p.slice(7); }
			for (var j = 0; j < p.length; j++, k++) out += k === cx ? '<span class="cur">' + esc(p[j]) + '</span>' : esc(p[j]);
		});
		return out + (open ? '</span>' : '');
	}
	function plain(s) { return s.replace(/\x05#[0-9a-f]{6}/g, ''); }
	function firstColor(s) { var m = /\x05(#[0-9a-f]{6})/.exec(s); return m ? m[1] : null; }
	function setPre(el, s, cy, cx) {
		el.innerHTML = s.split('\n').map(function (l, y) { return '<div>' + (lineHtml(l, y === cy ? cx : -1) || ' ') + '</div>'; }).join('');
	}

	/* sprite for the lists: a 16 px CSS sprite from the sheet (tiles on), else null */
	function icon(t) {
		var p = useTiles() && t >= 0 && cellXY(t);
		if (!p) return null;
		var d = document.createElement('span');
		d.className = 'ic';
		d.style.backgroundImage = 'url(' + sheet.src + ')';
		d.style.backgroundPosition = -p[0] + 'px ' + -p[1] + 'px';   /* ponytail: the .ic span is 16 px, other tile sizes crop */
		return d;
	}
	var invStr = '', visStr = '';
	function drawInv() {
		var b = document.querySelector('#t-inv .body');
		b.textContent = '';
		invStr.split('\n').forEach(function (l) {
			if (!l) return;
			var d = document.createElement('div');
			if (l[0] === '=') { d.className = 'h'; d.textContent = l.slice(1); b.appendChild(d); return; }
			var f = l.split('\t');   /* letter, glyph, name, css, tile */
			d.style.color = f[3];
			d.appendChild(document.createTextNode(f[0] + ')'));
			var ic = icon(+f[4]);
			if (!ic) { ic = document.createElement('b'); ic.textContent = f[1]; }
			d.appendChild(ic);
			d.appendChild(document.createTextNode(f[2]));
			b.appendChild(d);
		});
	}
	function drawVis() { var b = document.querySelector('#t-vis .body'); b._vis = null; RvipWM.visible(b, visStr, icon); }

	var popOpen = false;
	function placePop() { if (popOpen && rects.map) RvipWM.popup($('pop'), { center: true }); }
	function popFont() { $('pop').style.fontSize = RvipWM.fontSize('msg') + 'px'; placePop(); }

	/* ---------- audio ---------- */
	/* events come from game actions (RVIP_SOUND -> port/be_web.cpp); web/mksounds.py
	 * synthesizes one wav per event; rvip-sound.js plays them. Off by default;
	 * nothing is fetched until Sound effects is on. Avanor has no music. */
	window.avAudio = function () { return audio; };
	var audio = { cfg: null, loading: false, played: 0 };
	function play(name) {
		if (!L || !L.sound) return;
		if (!audio.cfg) {
			if (!audio.loading) {
				audio.loading = true;
				fetch('sound/sounds.json').then(function (r) { return r.json(); })
					.then(function (c) { audio.cfg = c; }).catch(function () { audio.loading = false; });
			}
			return;
		}
		var f = audio.cfg[name];
		if (!f || !f.length) return;
		audio.played++;                          /* testing */
		RVIPSound.play([f[0]], 0.6);
	}
	function renderAudio() { $('chk-sound').checked = !!(L && L.sound); if (L && L.sound) play(''); }   /* fetch sounds.json now, not on the first event */

	/* ---------- called by the game ---------- */
	var av = {
		tileset: function (file, w, h, ox, oy, gx, gy, c) {
			SETS[0][1] = file;
			cut = { w: w, h: h, ox: ox, oy: oy, gx: gx, gy: gy };
			cells = c;
			if (L) useSet(L.tiles);
		},
		sound: play,
		map: function (ch, rgb, tl, len, hgt, hx, hy) {
			M.len = len; M.hgt = hgt; M.ch = ch; M.rgb = rgb; M.tl = tl; M.hx = hx; M.hy = hy;
			if (M.cur >= len * hgt) M.cur = -1;
			drawMap(false);
		},
		mapCursor: function (x, y) {
			var c = x < 0 || !M.len ? -1 : y * M.len + x;
			if (c === M.cur) return;
			var o = M.cur; M.cur = c;
			if (M.ch) { setFont(); if (o >= 0) drawCell(o); if (c >= 0) drawCell(c); }
		},
		text: function (w, s, cy, cx) {
			if (w === 0) setPre(document.querySelector('#t-stat pre'), s, -1, -1);
			else if (w === 1) RvipWM.prompt.text(plain(s));
			else if (w === 2) {
				popOpen = !!s;
				$('pop').hidden = !popOpen;
				if (popOpen) { setPre($('pop').firstElementChild, s.replace(/\n$/, ''), cy, cx); placePop(); }
			}
			else if (w === 3) RvipWM.log(document.querySelector('#t-msg .body'), { t: plain(s), color: firstColor(s) });
			else if (w === 4) { invStr = s; drawInv(); }
			else if (w === 5) { visStr = s; drawVis(); }
		},
		atCmd: function (on) { RvipWM.prompt.wait(on); },
		saved: function () { app.sync(); },
		exit: function () {
			app.running = false;
			app.status('The game has ended. Starting again…');
			app.sync(function () { setTimeout(function () { location.reload(); }, 1000); });
		}
	};

	/* ---------- layout ---------- */
	function loadLayout() {
		L = { cell: 24, tiles: 'DawnLike', face: '', mapFace: '', sound: false };
		try {
			var s = JSON.parse(Module.FS.readFile(LAYOUT_FILE, { encoding: 'utf8' }));
			if (s && CELLS.indexOf(s.cell) >= 0) L.cell = s.cell;
			if (s && typeof s.tiles === 'string') L.tiles = s.tiles;
			if (s && typeof s.face === 'string') L.face = s.face;
			if (s && typeof s.mapFace === 'string') L.mapFace = s.mapFace;
			if (s && s.wm) L.wm = s.wm;
			if (s && s.sound === true) L.sound = true;
		} catch (e) { /* nothing saved yet */ }
	}
	var saveTimer = 0;
	function saveLayout() {
		clearTimeout(saveTimer);
		saveTimer = setTimeout(function () {
			try { Module.FS.writeFile(LAYOUT_FILE, JSON.stringify(L)); app.sync(); } catch (e) { console.warn('layout not saved', e); }
		}, 400);
	}
	function makeWM() {
		wm = RvipWM({
			area: $('game'), menu: $('btn-layout'),
			wins: [{ id: 'map', title: 'Map' }, { id: 'msg', title: 'Messages' }, { id: 'stat', title: 'Status' },
				{ id: 'inv', title: 'Inventory' }, { id: 'vis', title: 'Visible' }],
			multi: { d: 'h', r: 0.68, a: { d: 'v', r: 0.72, a: 'map', b: 'msg' }, b: { d: 'v', r: 0.2, a: 'stat', b: { d: 'v', r: 0.6, a: 'inv', b: 'vis' } } },
			single: { d: 'v', r: 0.85, a: 'map', b: 'stat' },
			state: L.wm,
			save: function (st) { L.wm = st; saveLayout(); },
			layout: function (r) { rects = r; center(); placePop(); },
			zoom: { map: function (px, d) { zoomMap(d); }, msg: popFont },
			onReset: function () { L.cell = 24; drawMap(true); popFont(); saveLayout(); }
		});
		wm.apply();
		renderMapSel();
	}

	/* ---------- tiles, fonts ---------- */
	function useSet(name) {
		var gen = ++sheetGen, set = SETS.filter(function (s) { return s[0] === name; })[0];
		L.tiles = set ? name : 'None';
		$('btn-tiles').textContent = 'Tiles: ' + L.tiles;
		sheet = null;
		var redraw = function () { drawMap(true); drawInv(); drawVis(); };
		if (!set) { redraw(); return; }
		var img = new Image();
		img.onload = function () { if (gen === sheetGen) { sheet = img; redraw(); } };
		img.src = set[1];
	}
	var mapSel = document.createElement('select');
	mapSel.title = 'Map font (text mode)';
	mapSel.innerHTML = '<option value="">Default font</option>';
	mapSel.addEventListener('pointerdown', function (e) { e.stopPropagation(); });
	function renderMapSel() {
		var bs = document.querySelector('#t-map .wm-btns');
		if (bs && mapSel.parentNode !== bs) bs.insertBefore(mapSel, bs.firstChild);
		mapSel.value = (L && L.mapFace) || '';
	}
	function applyFace() {
		['#t-stat .body', '#t-msg .body', '#t-inv .body', '#t-vis .body', '#pop'].forEach(function (q) {
			var e = document.querySelector(q); if (e) e.style.fontFamily = L.face ? '"' + L.face + '", ' + FONT : FONT; });
	}
	function loadFace(n, now) {
		var redraw = function () { applyFace(); drawMap(true); };
		if (!n) { if (now) redraw(); return; }
		var ff = new FontFace(n, 'url(../fonts/' + n + '.woff)');
		ff.load().then(function () { document.fonts.add(ff); redraw(); }).catch(function () { app.status('Could not load the font ' + n + '.', true); });
	}

	/* ---------- saves ---------- */
	function hasSave() { try { Module.FS.stat(SAVE); return true; } catch (e) { return false; } }
	app = RvipApp({
		name: 'avanor',
		save: function () { return hasSave() ? SAVE : null; },
		clear: function () { if (hasSave()) Module.FS.unlink(SAVE); },
		put: function (file, data) { Module.FS.writeFile(SAVE, data); },
		noSave: 'No saved game yet: save with S in the game (it also saves before every stair down).',
		helpText: 'Press ? in the game for its own help.'
	});

	window.Module = {
		av: av,
		preRun: [function () {
			Module.addRunDependency('idbfs');
			RvipApp.mount(function (err) {
				if (err) app.status('Could not read saved games from IndexedDB (' + err + ').', true);
				loadLayout(); renderAudio();
				$('game').hidden = false;
				makeWM(); applyFace(); popFont();
				useSet(L.tiles);
				$('sel-font').value = L.face || '';
				loadFace(L.face); loadFace(L.mapFace);
				Module.removeRunDependency('idbfs');
			});
		}],
		onRuntimeInitialized: function () { app.running = true; app.status(''); },
		print: function (s) { console.log(s); },
		printErr: function (s) { console.warn(s); },
		setStatus: function (s) { if (s && !app.running) app.status(s.replace(/\(\d+\/\d+\)/, '').trim() || 'Loading…'); },
		onAbort: function (what) { app.crashed(what); }
	};

	/* ---------- keys: the game's own codes (engine/global.h: 0x8000 | DOS scancode) ---------- */
	var EXT = 0x8000;
	var named = {
		ArrowUp: EXT | 72, ArrowDown: EXT | 80, ArrowLeft: EXT | 75, ArrowRight: EXT | 77,
		Home: EXT | 71, End: EXT | 79, PageUp: EXT | 73, PageDown: EXT | 81, Clear: EXT | 76,
		Enter: 13, Escape: 27, Backspace: 8, Delete: 127, Tab: 9
	};
	document.addEventListener('keydown', function (e) {
		if (!app.running || !Module._be_pushkey || e.isComposing || e.metaKey) return;
		var t = e.target && e.target.tagName;
		if (t === 'INPUT' || t === 'TEXTAREA' || t === 'SELECT') return;
		var k = -1;
		if (e.key in named) k = named[e.key];
		else if (e.key.length === 1) {
			k = e.key.charCodeAt(0);
			if (e.ctrlKey && /[a-z]/i.test(e.key)) k = e.key.toUpperCase().charCodeAt(0) - 64;
			else if (e.ctrlKey || e.altKey) return;
		}
		if (k < 0 || k > 0xffff) return;
		e.preventDefault();
		Module._be_pushkey(k);
	});

	/* write-back: saves call av.saved(); also every 15 s and when hidden */
	setInterval(function () { if (app.running) app.sync(); }, 15000);
	document.addEventListener('visibilitychange', function () { if (document.hidden) app.sync(); });
	window.addEventListener('pagehide', function () { app.sync(); });
	window.addEventListener('beforeunload', function (e) { if (app.running) { e.preventDefault(); e.returnValue = ''; } });

	document.addEventListener('DOMContentLoaded', function () {
		RvipWM.dropdown($('btn-file'), $('menu-file'));
		RvipWM.dropdown($('btn-audio'), $('menu-audio'));
		$('chk-sound').onchange = function () {
			if (!L) { this.checked = false; return; }
			L.sound = this.checked; if (L.sound) play(''); saveLayout();
		};
		RvipWM.fontOptions($('sel-font')); RvipWM.fontOptions(mapSel);
		[[$('sel-font'), 'face'], [mapSel, 'mapFace']].forEach(function (a) {
			a[0].onchange = function () { if (!L) return; L[a[1]] = this.value; saveLayout(); loadFace(this.value, true); this.blur(); };
		});
		$('btn-tiles').onclick = function () {
			if (!L) return;
			var names = SETS.map(function (s) { return s[0]; }).concat(['None']);
			useSet(names[(names.indexOf(L.tiles) + 1) % names.length]);
			saveLayout();
		};
		document.querySelectorAll('button').forEach(function (b) {
			b.addEventListener('mousedown', function (e) { e.preventDefault(); });
		});
	});
})();
