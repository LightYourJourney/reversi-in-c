/* Boots the AI Web Worker module and forwards search requests.
 *
 * Messages can arrive before the Emscripten module finishes loading, so they
 * are queued until the module is ready.
 */
importScripts('ai_worker.js');

var moduleReady = null;
var queue = [];

function handle(data) {
  var reply = 'err';
  try {
    reply = moduleReady.ccall('reversi_ai_worker_request', 'string',
                              ['string', 'number'],
                              [String(data.request || ''),
                               Number(data.generation) >>> 0]);
  } catch (error) {
    reply = 'err';
  }
  self.postMessage({ generation: data.generation, raw: reply });
}

self.onmessage = function (event) {
  if (moduleReady) {
    handle(event.data || {});
  } else {
    queue.push(event.data || {});
  }
};

createAiWorker().then(function (instance) {
  moduleReady = instance;
  while (queue.length > 0) {
    handle(queue.shift());
  }
}).catch(function () {
  moduleReady = null;
  self.postMessage({ generation: 0, raw: 'err' });
});
