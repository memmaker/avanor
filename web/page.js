// RVIP stage 1 page for Avanor: draws the game's 80x25 grid as HTML text
// (map rows with sprites on a canvas, stage 4) and forwards keys. Windows (rvip-wm.js) come in stage 5.
(function () {
  'use strict';
  var scr = document.getElementById('screen');
  var status = document.getElementById('status');
  var last = null, cur = [-1, -1], dims = [80, 25];
  var SAVE = '/home/web_user/.avanor';
  var below = document.getElementById('below');
  var cv = document.getElementById('map'), ctx = cv.getContext('2d');
  var tilesBtn = document.getElementById('tiles');
  // Tile sets offered, then None (text). Slot layout: web/mkdawn.py.
  // Credit: DawnLike by DragonDePlatino, palette DawnBringer (CC BY 4.0).
  var SETS = [['DawnLike', 'tiles-dawn.png']];
  var setName = 'DawnLike', zoom = 2, sheet = null, sheetGen = 0, TS = 16;
  var tl = null;

  function esc(c) { return c === '<' ? '&lt;' : c === '>' ? '&gt;' : c === '&' ? '&amp;' : c; }
  function hex(v) { return '#' + ('00000' + v.toString(16)).slice(-6); }

  function rowsHtml(y0, y1) {
    var ch = last[0], rgb = last[1], w = dims[0], out = [];
    for (var y = y0; y < y1; y++) {
      var line = '', run = '', col = -1;
      for (var x = 0; x < w; x++) {
        var i = y * w + x, c = String.fromCharCode(ch[i] || 32), v = rgb[i];
        var isCur = (x === cur[0] && y === cur[1]);
        if (v !== col || isCur) {
          if (run) line += '<span style="color:' + hex(col) + '">' + run + '</span>';
          run = ''; col = v;
        }
        if (isCur) { line += '<span class="cur" style="color:' + hex(v) + '">' + esc(c) + '</span>'; col = -1; continue; }
        run += esc(c);
      }
      if (run) line += '<span style="color:' + hex(col) + '">' + run + '</span>';
      out.push(line);
    }
    return out.join('\n');
  }

  // The game marks map cells (-2 text, else sprites; port/rvip_tiles.cpp);
  // the map rows go on a canvas, the rest stay text. No sprite, or tiles
  // off: all text.
  function draw() {
    if (!last) return;
    var w = dims[0], h = dims[1], y0 = -1, y1 = -1;
    if (sheet && tl) {
      var any = false;
      for (var i = 0; i < w * h; i++) if (tl[i] !== -1) { var y = (i / w) | 0; if (y0 < 0) y0 = y; y1 = y + 1; if (tl[i] >= 0) any = true; }
      if (!any) y0 = -1;
    }
    if (y0 < 0) {
      cv.hidden = true; below.hidden = true;
      scr.innerHTML = rowsHtml(0, h);
      return;
    }
    scr.innerHTML = rowsHtml(0, y0);
    below.innerHTML = rowsHtml(y1, h);
    cv.hidden = false; below.hidden = false;
    var c = TS * zoom, cw = w * c, ch = (y1 - y0) * c;
    if (cv.width !== cw || cv.height !== ch) { cv.width = cw; cv.height = ch; }
    ctx.imageSmoothingEnabled = false;
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, cw, ch);
    ctx.font = Math.round(c * 0.8) + 'px monospace';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    var sc = sheet.width / TS;
    for (var yy = y0; yy < y1; yy++) for (var x = 0; x < w; x++) {
      var k = yy * w + x, t = tl[k], px = x * c, py = (yy - y0) * c;
      if (t < 0) {
        var g = last[0][k];
        if (g > 32) { ctx.fillStyle = hex(last[1][k]); ctx.fillText(String.fromCharCode(g), px + c / 2, py + c / 2); }
      } else {
        var bg = t & 0xfff, fg = (t >> 12) & 0xfff;
        if (bg !== 0xfff) ctx.drawImage(sheet, bg % sc * TS, (bg / sc | 0) * TS, TS, TS, px, py, c, c);
        if (fg !== 0xfff) ctx.drawImage(sheet, fg % sc * TS, (fg / sc | 0) * TS, TS, TS, px, py, c, c);
        if (t & (1 << 24)) { ctx.fillStyle = 'rgba(0,0,0,0.55)'; ctx.fillRect(px, py, c, c); }
      }
      if (x === cur[0] && yy === cur[1]) { ctx.strokeStyle = '#c0c0c0'; ctx.strokeRect(px + 0.5, py + 0.5, c - 1, c - 1); }
    }
  }

  // Tile set by name (and zoom) in the game's IDBFS folder, read before the
  // first sheet loads.
  var PREF = SAVE + '/web-tiles';
  function savePref() {
    try { Module.FS.writeFile(PREF, setName + ' ' + zoom); } catch (e) {}
    sync();
  }
  function useSet(name) {
    setName = name;
    var gen = ++sheetGen, set = SETS.filter(function (s) { return s[0] === name; })[0];
    tilesBtn.textContent = 'Tiles: ' + (set ? name : 'None');
    sheet = null;
    if (!set) { draw(); return; }
    var img = new Image();
    img.onload = function () { if (gen === sheetGen) { sheet = img; draw(); } };
    img.src = set[1];
  }
  tilesBtn.onclick = function () {
    var names = SETS.map(function (s) { return s[0]; }).concat(['None']);
    useSet(names[(names.indexOf(setName) + 1) % names.length]);
    savePref(); tilesBtn.blur();
  };
  document.getElementById('zin').onclick = function () { if (zoom < 6) zoom++; savePref(); draw(); this.blur(); };
  document.getElementById('zout').onclick = function () { if (zoom > 1) zoom--; savePref(); draw(); this.blur(); };

  var syncing = false;
  function sync() {
    if (syncing || !Module.FS) return;
    syncing = true;
    Module.FS.syncfs(false, function () { syncing = false; });
  }

  window.Module = {
    preRun: [function () {
      Module.FS.mkdir('/home/web_user/.avanor');
      Module.FS.mount(Module.IDBFS, {}, SAVE);
      Module.addRunDependency('idbfs');
      Module.FS.syncfs(true, function () {
        try {
          var p = Module.FS.readFile(PREF, { encoding: 'utf8' }).trim().split(' ');
          setName = p[0]; zoom = Math.max(1, Math.min(6, parseInt(p[1], 10) || 2));
        } catch (e) {}
        useSet(setName);
        Module.removeRunDependency('idbfs');
      });
    }],
    print: function (t) { console.log(t); },
    printErr: function (t) { console.warn(t); },
    setStatus: function (t) { status.textContent = t; },
    rvipInit: function (w, h) { dims = [w, h]; status.textContent = ''; },
    rvipPresent: function (ch, rgb, w, h, tiles) {
      dims = [w, h];
      last = [ch.slice(), rgb.slice()];
      tl = tiles ? tiles.slice() : null;
      draw();
    },
    rvipCursor: function (x, y) { cur = [x, y]; draw(); },
    rvipExit: function () { sync(); status.textContent = 'The game has ended. Reload to play again.'; }
  };

  // Keys, in the game's own codes (engine/global.h: extended keys are
  // 0x8000 | DOS scancode).
  var EXT = 0x8000;
  var named = {
    ArrowUp: EXT | 72, ArrowDown: EXT | 80, ArrowLeft: EXT | 75, ArrowRight: EXT | 77,
    Home: EXT | 71, End: EXT | 79, PageUp: EXT | 73, PageDown: EXT | 81, Clear: EXT | 76,
    Enter: 13, Escape: 27, Backspace: 8, Delete: 127, Tab: 9
  };
  document.addEventListener('keydown', function (e) {
    if (!Module._be_pushkey) return;
    var k = -1;
    if (e.key in named) k = named[e.key];
    else if (e.key.length === 1) {
      k = e.key.charCodeAt(0);
      if (e.ctrlKey && /[a-z]/i.test(e.key)) k = e.key.toUpperCase().charCodeAt(0) - 64;
      else if (e.ctrlKey || e.altKey || e.metaKey) return;
    }
    if (k < 0 || k > 0xffff) return;
    e.preventDefault();
    Module._be_pushkey(k);
  });
  document.addEventListener('visibilitychange', function () { if (document.hidden) sync(); });
  setInterval(sync, 30000);
})();
