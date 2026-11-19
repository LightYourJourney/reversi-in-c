#ifndef REVERSI_PLATFORM_WEB_WEB_CALLBACKS_H
#define REVERSI_PLATFORM_WEB_WEB_CALLBACKS_H

/*
 * C functions that the JavaScript glue in the Web build calls back into:
 * WebSocket events for the network client and Web Worker replies for the
 * AI. Each is exported with EMSCRIPTEN_KEEPALIVE.
 */

/* net_client_web.c */
void reversi_net_client_web_message(const char *text);
void reversi_net_client_web_status(int status, const char *reason);

/* ai_worker_web.c */
void reversi_ai_worker_complete(unsigned generation, int ok, int row, int col,
                                double nodes, int depth, int cancelled);
void reversi_ai_worker_analysis_complete(unsigned generation, int ok,
                                         const char *text);

#endif
