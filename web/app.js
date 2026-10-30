/* Browser shell for the Emscripten build.
 *
 * Responsibilities are limited to the page: module startup, canvas sizing,
 * audio unlock, save export/import, and error/status reporting. All game
 * rules, AI, and persistence formats stay in the C/WASM modules.
 */
(function () {
  'use strict';

  var canvas = document.getElementById('canvas');
  var stage = document.getElementById('stage');
  var statusBox = document.getElementById('status');
  var errorBox = document.getElementById('error');
  var audioHint = document.getElementById('audio-hint');

  function showError(message) {
    errorBox.textContent = String(message);
    errorBox.hidden = false;
  }

  function clearError() {
    errorBox.hidden = true;
    errorBox.textContent = '';
  }

  /* Track every AudioContext created by the module so the first user gesture
     can resume a context that started suspended (autoplay policy). */
  var audioContexts = [];
  var NativeAudioContext = window.AudioContext || window.webkitAudioContext;
  if (NativeAudioContext) {
    var PatchedAudioContext = function () {
      var context = new (Function.prototype.bind.apply(
        NativeAudioContext, [null].concat(Array.prototype.slice.call(arguments))))();
      audioContexts.push(context);
      return context;
    };
    PatchedAudioContext.prototype = NativeAudioContext.prototype;
    window.AudioContext = PatchedAudioContext;
    window.webkitAudioContext = PatchedAudioContext;
  }

  if (!NativeAudioContext && audioHint) {
    audioHint.hidden = true;
  }

  function unlockAudio() {
    if (audioHint) {
      audioHint.hidden = true;
    }
    for (var i = 0; i < audioContexts.length; i += 1) {
      var context = audioContexts[i];
      if (context.state === 'suspended') {
        context.resume().catch(function () { /* keep silent */ });
      }
    }
  }

  ['pointerdown', 'touchstart', 'keydown'].forEach(function (eventName) {
    window.addEventListener(eventName, unlockAudio, { passive: true });
  });

  function storageAvailable() {
    try {
      var probe = '__reversi_ui_probe__';
      window.localStorage.setItem(probe, '1');
      window.localStorage.removeItem(probe);
      return true;
    } catch (error) {
      return false;
    }
  }

  if (!storageAvailable()) {
    showError('Browser storage is unavailable. Settings, stats, and saves '
      + 'will not persist in this session.');
  }

  if (!canvas || !stage) {
    showError('The game canvas is missing from the page.');
    return;
  }

  var module = null;
  var resizeTimer = 0;

  function resizeGame() {
    var rect = stage.getBoundingClientRect();
    var width = Math.max(320, Math.floor(rect.width));
    var height = Math.max(480, Math.floor(rect.height));
    if (module && module._reversi_web_resize) {
      module._reversi_web_resize(width, height);
    }
  }

  function scheduleResize() {
    window.clearTimeout(resizeTimer);
    resizeTimer = window.setTimeout(resizeGame, 120);
  }

  window.addEventListener('resize', scheduleResize);
  window.addEventListener('orientationchange', scheduleResize);

  function persistNow() {
    if (module && module._reversi_web_persist) {
      module._reversi_web_persist();
    }
  }

  window.addEventListener('pagehide', persistNow);
  document.addEventListener('visibilitychange', function () {
    if (document.visibilityState === 'hidden') {
      persistNow();
    }
  });

  ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'Enter', 'Escape']
    .forEach(function (key) {
      canvas.addEventListener('keydown', function (event) {
        if (event.key === ' ' || event.key.indexOf('Arrow') === 0) {
          event.preventDefault();
        }
      });
    });

  function exportSaves() {
    var data = {};
    try {
      for (var i = 0; i < window.localStorage.length; i += 1) {
        var key = window.localStorage.key(i);
        if (key && key.indexOf('reversi:') === 0) {
          data[key] = window.localStorage.getItem(key);
        }
      }
    } catch (error) {
      showError('Could not read browser storage: ' + error.message);
      return;
    }
    var payload = JSON.stringify({ format: 'reversi-web-saves', version: 1,
      exportedAt: new Date().toISOString(), data: data }, null, 2);
    var blob = new Blob([payload], { type: 'application/json' });
    var url = URL.createObjectURL(blob);
    var link = document.createElement('a');
    link.href = url;
    link.download = 'reversi-saves.json';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  }

  function importSaves(file) {
    if (!file) {
      return;
    }
    var reader = new FileReader();
    reader.onload = function () {
      var parsed;
      try {
        parsed = JSON.parse(String(reader.result));
      } catch (error) {
        showError('Import failed: the file is not valid JSON.');
        return;
      }
      if (!parsed || parsed.format !== 'reversi-web-saves'
          || typeof parsed.data !== 'object' || parsed.data === null) {
        showError('Import failed: this is not a Reversi save export.');
        return;
      }
      var keys = Object.keys(parsed.data);
      try {
        for (var i = 0; i < keys.length; i += 1) {
          var key = keys[i];
          if (key.indexOf('reversi:') !== 0
              || typeof parsed.data[key] !== 'string') {
            continue;
          }
          window.localStorage.setItem(key, parsed.data[key]);
        }
      } catch (error) {
        showError('Import failed while writing storage: ' + error.message);
        return;
      }
      window.location.reload();
    };
    reader.onerror = function () {
      showError('Import failed while reading the file.');
    };
    reader.readAsText(file);
  }

  document.getElementById('export').addEventListener('click', exportSaves);
  document.getElementById('import').addEventListener('change', function (event) {
    importSaves(event.target.files && event.target.files[0]);
  });
  document.getElementById('reload').addEventListener('click', function () {
    persistNow();
    window.location.reload();
  });
  document.getElementById('fullscreen').addEventListener('click', function () {
    if (document.fullscreenElement) {
      document.exitFullscreen();
    } else if (stage.requestFullscreen) {
      stage.requestFullscreen().catch(function () { /* ignored */ });
    }
  });

  if (typeof createReversi !== 'function') {
    showError('The WebAssembly loader did not run. Serve this directory over '
      + 'HTTP instead of opening index.html from disk.');
    return;
  }

  var startTime = performance.now();
  createReversi({
    canvas: canvas,
    print: function () { /* keep the console quiet */ },
    printErr: function (text) { console.error(text); },
    onAbort: function (what) {
      showError('Runtime aborted: ' + what);
    }
  }).then(function (instance) {
    module = instance;
    console.info('[reversi] ready in '
      + Math.round(performance.now() - startTime) + ' ms');
    console.info('[reversi] resources '
      + JSON.stringify(performance.getEntriesByType('resource')
        .filter(function (entry) {
          return /reversi|ai_worker/.test(entry.name);
        })
        .map(function (entry) {
          return { name: entry.name.split('/').pop(),
                   duration: Math.round(entry.duration) };
        })));
    statusBox.hidden = true;
    canvas.focus({ preventScroll: true });
    resizeGame();
    window.setTimeout(resizeGame, 60);
  }).catch(function (error) {
    statusBox.hidden = true;
    showError('Failed to start Reversi: '
      + (error && error.message ? error.message : error));
  });

  // Exposed for browser automation; not used by the game.
  window.__reversiTest = {
    ready: function () { return module !== null; },
    module: function () { return module; },
    frames: function () {
      return module && module._reversi_web_frames
        ? module._reversi_web_frames() : -1;
    },
    mouse: function () {
      return module && module._reversi_web_mouse_x
        ? { x: module._reversi_web_mouse_x(),
            y: module._reversi_web_mouse_y() } : { x: -1, y: -1 };
    },
    screen: function () {
      return module && module._reversi_web_screen_w
        ? { w: module._reversi_web_screen_w(),
            h: module._reversi_web_screen_h() } : { w: -1, h: -1 };
    },
    gameOver: function () {
      return !!(module && module._reversi_web_game_over
        && module._reversi_web_game_over());
    },
    result: function () {
      return module && module._reversi_web_result
        ? module._reversi_web_result() : -1;
    },
    screenIndex: function () {
      return module && module._reversi_web_screen_index
        ? module._reversi_web_screen_index() : -1;
    },
    netCode: function () {
      return module && module.ccall
        ? module.ccall('reversi_web_net_code', 'string', [], []) : '';
    },
    netPhase: function () {
      return module && module._reversi_web_net_phase
        ? module._reversi_web_net_phase() : -1;
    },
    netSeq: function () {
      return module && module._reversi_web_net_seq
        ? module._reversi_web_net_seq() : -1;
    },
    aiActive: function () {
      return !!(module && module._reversi_web_ai_active
        && module._reversi_web_ai_active());
    },
    mode: function () {
      return module && module._reversi_web_match_mode
        ? module._reversi_web_match_mode() : -1;
    },
    cancelAi: function () {
      if (module && module._reversi_web_cancel_ai_test) {
        module._reversi_web_cancel_ai_test();
        return true;
      }
      return false;
    },
    heapBytes: function () {
      return module && module._reversi_web_heap_bytes
        ? module._reversi_web_heap_bytes() : -1;
    },
    audioReady: function () {
      return !!(module && module._reversi_web_audio_ready
        && module._reversi_web_audio_ready());
    },
    autoplay: function (seed) {
      if (module && module._reversi_web_debug_autoplay) {
        module._reversi_web_debug_autoplay(seed || 7);
        return true;
      }
      return false;
    },
    version: document.getElementById('version').textContent
  };
}());
