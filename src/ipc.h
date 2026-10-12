/* r2discord - MIT - Copyright 2026 - kp1tal */
#ifndef R2D_IPC_H
#define R2D_IPC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	R2D_OP_HANDSHAKE = 0,
	R2D_OP_FRAME = 1,
	R2D_OP_CLOSE = 2,
	R2D_OP_PING = 3,
	R2D_OP_PONG = 4,
};

typedef enum {
	R2D_IPC_OK,
	R2D_IPC_NOSOCKET,
	R2D_IPC_IOERR,
	R2D_IPC_REJECTED,
} R2DIpcStatus;

// Returns a connected socket or -1. If R2DISCORD_IPC_PATH is set it is the
// only path tried.
int r2d_ipc_connect(void);
R2DIpcStatus r2d_ipc_handshake(int fd, const char *appid, char *err, size_t errlen);
// false means the connection is unusable and must be closed
bool r2d_ipc_send(int fd, uint32_t op, const char *json);
// 1 = message stored in *json (caller frees), 0 = timeout, -1 = error/EOF
int r2d_ipc_recv(int fd, uint32_t *op, char **json, int timeout_ms);
// Handles everything already waiting on the socket without blocking: answers
// PING, copies ERROR/CLOSE messages into err. false = connection is gone.
bool r2d_ipc_pump(int fd, char *err, size_t errlen);
void r2d_ipc_close(int fd);

#endif
