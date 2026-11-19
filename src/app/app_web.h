#ifndef REVERSI_APP_APP_WEB_H
#define REVERSI_APP_APP_WEB_H

/*
 * Entry points the Web shell (web/app.js) and the browser end-to-end tests
 * call through Emscripten. They exist only in the WebAssembly build; the
 * reversi_web_debug_autoplay hook only in its Debug variant.
 */

int reversi_web_frames(void);
int reversi_web_mouse_x(void);
int reversi_web_mouse_y(void);
int reversi_web_screen_w(void);
int reversi_web_screen_h(void);
#ifndef NDEBUG
void reversi_web_debug_autoplay(unsigned seed);
#endif
int reversi_web_game_over(void);
int reversi_web_result(void);
int reversi_web_screen_index(void);
const char *reversi_web_net_code(void);
int reversi_web_net_phase(void);
int reversi_web_net_seq(void);
int reversi_web_ai_active(void);
int reversi_web_match_mode(void);
void reversi_web_cancel_ai_test(void);
int reversi_web_heap_bytes(void);
int reversi_web_audio_ready(void);
void reversi_web_resize(int width, int height);
void reversi_web_persist(void);

#endif
