/* Boots the AI Web Worker module and forwards search requests.
 *
 * Messages can arrive before the Emscripten module finishes loading, so they
 * are queued until the module is ready.
 */
importScripts('ai_worker.js');

// null while loading, the module once loaded, false if it failed to load.
var moduleReady = null;
var queue = [];

function handle(data) {
  var request = String(data.request || '');
  var kind = request.charAt(0) === 'A' ? 'analysis' : 'move';
  var reply = 'err';
  if (moduleReady) {
    try {
      reply = moduleReady.ccall('reversi_ai_worker_request', 'string',
                                ['string'], [request]);
    } catch (error) {
      reply = 'err';
    }
  }
  // Every reply carries its request's generation: the page drops replies
  // for any other generation, so a reply without one would leave the
  // request waiting forever.
  self.postMessage({ generation: data.generation, raw: reply, kind: kind });
}

self.onmessage = function (event) {
  if (moduleReady !== null) {
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
  // Fail each pending and future request instead of leaving it unanswered.
  moduleReady = false;
  while (queue.length > 0) {
    handle(queue.shift());
  }
});
