#ifndef REVERSI_WEB_AI_WORKER_EXPORTS_H
#define REVERSI_WEB_AI_WORKER_EXPORTS_H

/*
 * The single entry point web/ai_worker_boot.js calls in the AI Web Worker
 * module: takes a text request, returns a text reply owned by the module
 * (valid until the next call).
 */
const char *reversi_ai_worker_request(const char *request);

#endif
