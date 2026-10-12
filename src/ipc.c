#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <r_util.h>
#include "ipc.h"

#ifdef MSG_NOSIGNAL
#define R2D_SEND_FLAGS MSG_NOSIGNAL
#else
#define R2D_SEND_FLAGS 0
#endif

#define R2D_MAX_MSG (64 * 1024)
#define R2D_PUMP_MAX 16

static int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int connect_path(const char *path) {
	struct sockaddr_un sa;
	if (strlen (path) >= sizeof sa.sun_path) {
		return -1;
	}
	int fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}
	fcntl (fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
	int one = 1;
	setsockopt (fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
	struct timeval tv = { 0, 500000 };
	setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
	memset (&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	strcpy (sa.sun_path, path);
	if (connect (fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		close (fd);
		return -1;
	}
	return fd;
}

int r2d_ipc_connect(void) {
	const char *over = getenv ("R2DISCORD_IPC_PATH");
	if (over && *over) {
		return connect_path (over);
	}
	static const char *envs[] = { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" };
	static const char *prefixes[] = { "", "app/com.discordapp.Discord/", "snap.discord/" };
	const char *bases[5];
	int nb = 0;
	for (size_t i = 0; i < sizeof envs / sizeof envs[0]; i++) {
		const char *v = getenv (envs[i]);
		if (v && *v) {
			bases[nb++] = v;
		}
	}
	bases[nb++] = "/tmp";
	char path[512];
	for (int b = 0; b < nb; b++) {
		for (size_t p = 0; p < sizeof prefixes / sizeof prefixes[0]; p++) {
			for (int i = 0; i < 10; i++) {
				snprintf (path, sizeof path, "%s/%sdiscord-ipc-%d", bases[b], prefixes[p], i);
				int fd = connect_path (path);
				if (fd >= 0) {
					return fd;
				}
			}
		}
	}
	return -1;
}

static void put32(uint8_t *p, uint32_t v) {
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static uint32_t get32(const uint8_t *p) {
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool write_all(int fd, const void *buf, size_t n) {
	const char *p = buf;
	while (n > 0) {
		ssize_t w = send (fd, p, n, R2D_SEND_FLAGS);
		if (w < 0) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		p += w;
		n -= w;
	}
	return true;
}

static bool read_all(int fd, void *buf, size_t n, int64_t deadline) {
	uint8_t *p = buf;
	while (n > 0) {
		struct pollfd pf = { fd, POLLIN, 0 };
		int64_t left = deadline - now_ms ();
		if (left < 0) {
			left = 0;
		}
		int r = poll (&pf, 1, (int)left);
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r <= 0) {
			return false;
		}
		ssize_t got = read (fd, p, n);
		if (got < 0 && errno == EINTR) {
			continue;
		}
		if (got <= 0) {
			return false;
		}
		p += got;
		n -= got;
	}
	return true;
}

bool r2d_ipc_send(int fd, uint32_t op, const char *json) {
	size_t n = strlen (json);
	uint8_t *buf = malloc (8 + n);
	if (!buf) {
		return false;
	}
	put32 (buf, op);
	put32 (buf + 4, (uint32_t)n);
	memcpy (buf + 8, json, n);
	bool ok = write_all (fd, buf, 8 + n);
	free (buf);
	return ok;
}

int r2d_ipc_recv(int fd, uint32_t *op, char **json, int timeout_ms) {
	struct pollfd pf = { fd, POLLIN, 0 };
	int r = poll (&pf, 1, timeout_ms);
	if (r == 0 || (r < 0 && errno == EINTR)) {
		return 0;
	}
	if (r < 0) {
		return -1;
	}
	uint8_t h[8];
	int64_t deadline = now_ms () + 1000;
	if (!read_all (fd, h, 8, deadline)) {
		return -1;
	}
	uint32_t n = get32 (h + 4);
	if (n > R2D_MAX_MSG) {
		return -1;
	}
	char *buf = malloc (n + 1);
	if (!buf || !read_all (fd, buf, n, deadline)) {
		free (buf);
		return -1;
	}
	buf[n] = 0;
	*op = get32 (h);
	*json = buf;
	return 1;
}

static void copy_message(const char *json, char *err, size_t errlen) {
	if (!err || !errlen) {
		return;
	}
	RJson *root = r_json_parsedup (json);
	const char *msg = NULL;
	if (root) {
		msg = r_json_get_str (root, "message");
		if (!msg) {
			const RJson *data = r_json_get (root, "data");
			msg = data? r_json_get_str (data, "message"): NULL;
		}
	}
	snprintf (err, errlen, "%s", msg? msg: json);
	r_json_free (root);
}

static bool is_error_evt(const char *json) {
	RJson *root = r_json_parsedup (json);
	const char *evt = root? r_json_get_str (root, "evt"): NULL;
	bool res = evt && !strcmp (evt, "ERROR");
	r_json_free (root);
	return res;
}

R2DIpcStatus r2d_ipc_handshake(int fd, const char *appid, char *err, size_t errlen) {
	PJ *pj = pj_new ();
	pj_o (pj);
	pj_kn (pj, "v", 1);
	pj_ks (pj, "client_id", appid);
	pj_end (pj);
	char *msg = pj_drain (pj);
	bool ok = r2d_ipc_send (fd, R2D_OP_HANDSHAKE, msg);
	free (msg);
	if (!ok) {
		return R2D_IPC_IOERR;
	}
	uint32_t op;
	char *reply = NULL;
	if (r2d_ipc_recv (fd, &op, &reply, 1000) <= 0) {
		return R2D_IPC_IOERR;
	}
	R2DIpcStatus st = R2D_IPC_IOERR;
	if (op == R2D_OP_CLOSE || is_error_evt (reply)) {
		copy_message (reply, err, errlen);
		st = R2D_IPC_REJECTED;
	} else if (op == R2D_OP_FRAME) {
		RJson *root = r_json_parsedup (reply);
		const char *evt = root? r_json_get_str (root, "evt"): NULL;
		if (evt && !strcmp (evt, "READY")) {
			st = R2D_IPC_OK;
		}
		r_json_free (root);
	}
	free (reply);
	return st;
}

bool r2d_ipc_pump(int fd, char *err, size_t errlen) {
	for (int i = 0; i < R2D_PUMP_MAX; i++) {
		uint32_t op;
		char *json = NULL;
		int r = r2d_ipc_recv (fd, &op, &json, 0);
		if (r == 0) {
			return true;
		}
		if (r < 0) {
			return false;
		}
		bool alive = true;
		if (op == R2D_OP_PING) {
			alive = r2d_ipc_send (fd, R2D_OP_PONG, json);
		} else if (op == R2D_OP_CLOSE) {
			copy_message (json, err, errlen);
			alive = false;
		} else if (op == R2D_OP_FRAME && is_error_evt (json)) {
			copy_message (json, err, errlen);
		}
		free (json);
		if (!alive) {
			return false;
		}
	}
	return true;
}

void r2d_ipc_close(int fd) {
	if (fd < 0) {
		return;
	}
	r2d_ipc_send (fd, R2D_OP_CLOSE, "{}");
	close (fd);
}
