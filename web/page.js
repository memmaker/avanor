// RVIP stage 1 page for Avanor: draws the game's 80x25 grid as HTML text
// and forwards keys. Windows (rvip-wm.js) come in stage 5.
(function () {
  'use strict';
  var scr = document.getElementById('screen');
  var status = document.getElementById('status');
  var last = null, cur = [-1, -1], dims = [80, 25];
  var SAVE = '/home/web_user/.avanor';

  function esc(c) { return c === '<' ? '&lt;' : c === '>' ? '&gt;' : c === '&' ? '&amp;' : c; }
  function hex(v) { return '#' + ('00000' + v.toString(16)).slice(-6); }

  function draw() {
    if (!last) return;
    var ch = last[0], rgb = last[1], w = dims[0], h = dims[1], out = [];
    for (var y = 0; y < h; y++) {
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
    scr.innerHTML = out.join('\n');
  }

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
      Module.FS.syncfs(true, function () { Module.removeRunDependency('idbfs'); });
    }],
    print: function (t) { console.log(t); },
    printErr: function (t) { console.warn(t); },
    setStatus: function (t) { status.textContent = t; },
    rvipInit: function (w, h) { dims = [w, h]; status.textContent = ''; },
    rvipPresent: function (ch, rgb, w, h) {
      dims = [w, h];
      last = [ch.slice(), rgb.slice()];
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
