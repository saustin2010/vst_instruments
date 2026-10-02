/* Standard-layout animated page for Pixel Walkers. MIDI timing remains in the
 * DSP. The canvas advances a visual copy between snapshots so a late parameter
 * read cannot visibly freeze every walker. */
(function () {
  "use strict";
  var WALK_FRAMES = [
    [0x08,0x1e,0x1e,0x1c,0x0c,0x1c,0x1c,0x1e,0x1c],
    [0x1c,0x1e,0x1e,0x1c,0x0c,0x2e,0x3c,0x1e,0x0e],
    [0x00,0x0e,0x0e,0x1c,0x1c,0x0e,0x1e,0x3e,0x36],
    [0x00,0x0c,0x1e,0x1e,0x0c,0x1e,0x1e,0x1f,0x1a],
    [0x00,0x1c,0x1e,0x1e,0x0c,0x1c,0x1e,0x1e,0x0c],
    [0x18,0x1e,0x1e,0x1c,0x0c,0x3c,0x3c,0x16,0x0e],
    [0x00,0x0c,0x0e,0x1c,0x1c,0x1c,0x1c,0x3e,0x36],
    [0x00,0x0c,0x1e,0x1e,0x0c,0x1c,0x1c,0x1e,0x1a]
  ];

  function byteAt(text, at) {
    if (!text || at + 2 > text.length) return 0;
    var n = parseInt(text.slice(at, at + 2), 16);
    return isFinite(n) ? n : 0;
  }

  function u32At(text, at) {
    var n = parseInt(text.slice(at, at + 8), 16);
    return isFinite(n) ? (n >>> 0) : 0;
  }

  function drawWalker(ctx, x, y, frame) {
    var rows = WALK_FRAMES[frame & 7];
    for (var ry = 0; ry < 9; ++ry) {
      var bits = rows[ry];
      for (var rx = 0; rx < 6; ++rx) {
        if (bits & (1 << rx)) ctx.setPixel(x + rx, y + ry, 1);
      }
    }
  }

  var model = { instance: -1, serial: -1, platforms: [], walkers: [], lastMs: 0 };

  function signedByte(n) { return n > 127 ? n - 256 : n; }

  function readSnapshot(text) {
    var at = 0;
    if (text.charAt(at++) !== "I") return null;
    var instance = u32At(text, at); at += 8;
    if (text.charAt(at++) !== "T") return null;
    var serial = u32At(text, at); at += 8;
    if (text.charAt(at++) !== "P") return null;
    var platformCount = byteAt(text, at); at += 2;
    var platforms = [];
    for (var i = 0; i < platformCount; ++i) {
      platforms.push({
        x: byteAt(text, at),
        y: byteAt(text, at + 2),
        w: byteAt(text, at + 4)
      });
      at += 6;
    }
    if (text.charAt(at++) !== "W") return null;
    var walkerCount = byteAt(text, at); at += 2;
    var walkers = [];
    for (var j = 0; j < walkerCount; ++j) {
      var id = byteAt(text, at); at += 2;
      var x = byteAt(text, at); at += 2;
      var y = byteAt(text, at); at += 2;
      var vy = signedByte(byteAt(text, at)); at += 2;
      var motion = byteAt(text, at); at += 2;
      var physics = byteAt(text, at); at += 2;
      var supportCode = motion & 0x3f;
      var rebounding = (motion & 0x40) !== 0;
      var encodedSurface = supportCode === 0 ? -2 :
                           (supportCode === 1 ? -1 : supportCode - 2);
      walkers.push({
        id: id, x: x, y: y, vy: vy,
        dir: (motion & 0x80) ? -1 : 1,
        surface: rebounding ? -2 : encodedSurface,
        rebounding: rebounding,
        reboundSurface: rebounding ? encodedSurface : -2,
        bounce: ((physics >> 4) & 15) / 15,
        hardness: (physics & 15) / 15,
        reboundCount: 0,
        active: true
      });
    }
    return { instance: instance, serial: serial, platforms: platforms, walkers: walkers };
  }

  function stepWalker(w, platforms, dt, height) {
    if (!w.active) return;
    if (w.surface !== -2) {
      w.x += w.dir * 15 * dt;
      if (w.surface >= 0 && w.surface < platforms.length) {
        var support = platforms[w.surface];
        var foot = w.dir > 0 ? w.x + 5 : w.x;
        if (foot < support.x || foot > support.x + support.w) {
          w.surface = -2;
          w.vy = 0;
        }
      }
      if (w.x < -6 || w.x > 128) w.active = false;
      return;
    }

    var oldBottom = w.y + 9;
    w.vy += 42 * dt;
    w.y += w.vy * dt;
    var newBottom = w.y + 9;
    var landing = -2;
    var landingY = height - 1;
    for (var i = 0; i < platforms.length; ++i) {
      var p = platforms[i];
      if (w.rebounding && w.reboundSurface !== i) continue;
      if (w.x + 6 >= p.x && w.x <= p.x + p.w &&
          oldBottom < p.y && newBottom >= p.y && w.vy > 0 && p.y < landingY) {
        landing = i;
        landingY = p.y;
      }
    }
    if (landing === -2 && (!w.rebounding || w.reboundSurface === -1) &&
        oldBottom <= height - 1 && newBottom >= height - 1) {
      landing = -1;
      landingY = height - 1;
    }
    if (landing !== -2) {
      w.y = landingY - 9;
      if (!w.rebounding) w.reboundCount = 0;
      else w.reboundCount += 1;
      var maxRebounds = 3 + Math.round(17 * w.hardness);
      var coefficient = w.rebounding
        ? 0.55 + 0.37 * w.hardness
        : (w.bounce > 0.001 ? 0.15 + 0.55 * w.bounce : 0);
      var reboundSpeed = Math.min(26, w.vy * coefficient);
      if (reboundSpeed >= 2.5 && w.reboundCount < maxRebounds) {
        w.vy = -reboundSpeed;
        w.surface = -2;
        w.rebounding = true;
        w.reboundSurface = landing;
      } else {
        w.vy = 0;
        w.surface = landing;
        w.rebounding = false;
        w.reboundSurface = -2;
      }
    }
  }

  function advance(nowMs, height) {
    if (!model.lastMs) { model.lastMs = nowMs; return; }
    var remaining = Math.max(0, Math.min(6000, nowMs - model.lastMs)) / 1000;
    while (remaining > 0) {
      var dt = Math.min(0.04, remaining);
      for (var i = 0; i < model.walkers.length; ++i)
        stepWalker(model.walkers[i], model.platforms, dt, height);
      remaining -= dt;
    }
    model.lastMs = nowMs;
  }

  function reconcile(snap, nowMs) {
    var prior = {};
    for (var i = 0; i < model.walkers.length; ++i)
      prior[model.walkers[i].id] = model.walkers[i];
    var next = [];
    for (var j = 0; j < snap.walkers.length; ++j) {
      var incoming = snap.walkers[j];
      var current = prior[incoming.id];
      if (!current) {
        next.push(incoming);
        continue;
      }

      /* Leaving the screen is terminal for this walker identity. A delayed
       * observation must never resurrect it at an earlier ground position. */
      if (!current.active) continue;

      /* Keep the sub-pixel position the canvas has already advanced. Replacing
       * it with each integer DSP observation made walkers repeatedly jump
       * backward, which looked like short loops. Snap only after a real state
       * transition or meaningful divergence. */
      var changedSurface = current.surface !== incoming.surface;
      var changedBounce = current.rebounding !== incoming.rebounding ||
                          current.reboundSurface !== incoming.reboundSurface;
      var farAway = Math.abs(current.x - incoming.x) > 3 ||
                    Math.abs(current.y - incoming.y) > 3;
      if (changedSurface || changedBounce || farAway) {
        current.x = incoming.x;
        current.y = incoming.y;
      }
      current.vy = incoming.vy;
      current.dir = incoming.dir;
      current.surface = incoming.surface;
      current.rebounding = incoming.rebounding;
      current.reboundSurface = incoming.reboundSurface;
      current.bounce = incoming.bounce;
      current.hardness = incoming.hardness;
      if (!incoming.rebounding) current.reboundCount = 0;
      current.active = true;
      next.push(current);
    }
    model.platforms = snap.platforms;
    model.walkers = next;
    model.instance = snap.instance;
    model.serial = snap.serial;
    model.lastMs = nowMs;
  }

  function isNewerSerial(candidate, current) {
    if (current < 0) return true;
    var delta = (candidate - current) >>> 0;
    return delta !== 0 && delta < 0x80000000;
  }

  globalThis.canvas_overlay = {
    drawPage: function (ctx, page) {
      var text = page && page.values ? String(page.values.viz_state || "") : "";
      var nowMs = page && typeof page.nowMs === "number" ? page.nowMs : Date.now();
      /* Advance on every draw, including a draw that also receives a fresh
       * snapshot. Otherwise frequent snapshots steal that frame's elapsed
       * time and make the local model alternately lag and jump. */
      advance(nowMs, page && page.height ? page.height : 43);
      var snap = readSnapshot(text);
      if (snap && snap.instance !== model.instance) {
        model.instance = snap.instance;
        model.serial = -1;
        model.platforms = [];
        model.walkers = [];
        model.lastMs = nowMs;
      }
      if (snap && isNewerSerial(snap.serial, model.serial)) {
        reconcile(snap, nowMs);
      }

      for (var i = 0; i < model.platforms.length; ++i) {
        var p = model.platforms[i];
        ctx.fillRect(p.x, p.y, p.w, 2, 1);
      }
      var phase = Math.floor(nowMs / 70) & 3;
      for (var j = 0; j < model.walkers.length; ++j) {
        var w = model.walkers[j];
        if (!w.active) continue;
        drawWalker(ctx, Math.round(w.x), Math.round(w.y), phase + (w.dir < 0 ? 4 : 0));
      }
    }
  };
})();
