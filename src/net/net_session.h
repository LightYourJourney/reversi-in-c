#ifndef REVERSI_NET_NET_SESSION_H
#define REVERSI_NET_NET_SESSION_H

#include "core/history.h"
#include "net/net_client.h"

#include <stdbool.h>

typedef enum NetSessionPhase {
    NET_SESSION_IDLE = 0,
    NET_SESSION_CONNECTING,
    NET_SESSION_LOBBY,
    NET_SESSION_WAITING,
    NET_SESSION_PLAYING,
    NET_SESSION_FINISHED,
    NET_SESSION_DISCONNECTED,
    NET_SESSION_FAILED
} NetSessionPhase;

/*
 * Client-side room/game state. The UI reads the fields and never sends
 * sockets; all results still come from the server. Move requests are only
 * sent when the local view says it is this player's turn.
 */
typedef struct NetSession {
    NetClient *client;
    NetSessionPhase phase;
    char host[64];
    int port;
    char code[REVERSI_NET_CODE_LEN + 1];
    char token[REVERSI_NET_TOKEN_LEN + 1];
    int player;
    bool is_host;
    Stone color;
    bool ready;
    GameState game;
    int seq;
    /* Moves the server accepted, in order, for a post-game review. The log is
     * only complete when every state arrived in sequence. */
    Move moves[REVERSI_HISTORY_MAX_MOVES];
    int moves_count;
    bool moves_complete;
    bool has_last;
    Move last;
    bool pass;
    GameResult result;
    char reason[24];
    char opponent_status[24];
    char error[32];
    bool state_dirty;
    bool started;
    bool active;
    int latency_ms;
    float ping_timer;
    float reconnect_timer;
    int reconnect_attempts;
    bool rejoin_pending;
    uint64_t ping_sent_ms;
} NetSession;

NetSession *net_session_create(void);
void net_session_destroy(NetSession *session);

bool net_session_connect(NetSession *session, const char *host, int port);
void net_session_disconnect(NetSession *session);
void net_session_update(NetSession *session, float dt);

bool net_session_create_room(NetSession *session, Stone color);
bool net_session_join_room(NetSession *session, const char *code,
                           Stone color);
bool net_session_ready(NetSession *session);
bool net_session_move(NetSession *session, Move move);
bool net_session_resign(NetSession *session);
void net_session_leave(NetSession *session);

bool net_session_can_move(const NetSession *session);
const char *net_session_status_text(const NetSession *session);

#endif
