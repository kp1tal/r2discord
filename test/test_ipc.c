/* r2discord - MIT - Copyright 2026 - kp1tal */
#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "t.h"
#include "fake_discord.h"
#include "../src/ipc.h"

static char tmpdir[256];

static void sock_path(char *buf, size_t len, const char *name) {
	snprintf (buf, len, "%s/%s", tmpdir, name);
}

static void test_handshake_ok(void) {
	char p[300];
	sock_path (p, sizeof p, "ok.sock");
	FakeDiscord *f = fd_start (p, false);
	setenv ("R2DISCORD_IPC_PATH", p, 1);
	int fd = r2d_ipc_connect ();
	CHECK (fd >= 0);
	char err[128] = "";
	CHECK (r2d_ipc_handshake (fd, "123", err, sizeof err) == R2D_IPC_OK);
	CHECK (fd_count (f, R2D_OP_HANDSHAKE, "\"client_id\":\"123\"") == 1);
	CHECK (fd_count (f, R2D_OP_HANDSHAKE, "\"v\":1") == 1);

	CHECK (r2d_ipc_send (fd, R2D_OP_FRAME, "{\"cmd\":\"SET_ACTIVITY\"}"));
	CHECK (fd_wait (f, R2D_OP_FRAME, "SET_ACTIVITY", 1, 1000));

	fd_send_ping (f);
	for (int i = 0; i < 50 && fd_count (f, R2D_OP_PONG, NULL) == 0; i++) {
		usleep (20000);
		CHECK (r2d_ipc_pump (fd, err, sizeof err));
	}
	CHECK (fd_count (f, R2D_OP_PONG, "\"ping\":1") == 1);

	r2d_ipc_close (fd);
	CHECK (fd_wait (f, R2D_OP_CLOSE, NULL, 1, 1000));
	fd_stop (f);
}

static void test_rejected(void) {
	char p[300];
	sock_path (p, sizeof p, "rej.sock");
	FakeDiscord *f = fd_start (p, true);
	setenv ("R2DISCORD_IPC_PATH", p, 1);
	int fd = r2d_ipc_connect ();
	char err[128] = "";
	CHECK (r2d_ipc_handshake (fd, "bad", err, sizeof err) == R2D_IPC_REJECTED);
	CHECK (strstr (err, "Invalid Client ID") != NULL);
	close (fd);
	fd_stop (f);
}

static void test_server_gone(void) {
	char p[300];
	sock_path (p, sizeof p, "gone.sock");
	FakeDiscord *f = fd_start (p, false);
	setenv ("R2DISCORD_IPC_PATH", p, 1);
	int fd = r2d_ipc_connect ();
	char err[128] = "";
	CHECK (r2d_ipc_handshake (fd, "1", err, sizeof err) == R2D_IPC_OK);
	fd_stop (f);
	bool alive = true;
	for (int i = 0; i < 50 && alive; i++) {
		usleep (20000);
		alive = r2d_ipc_pump (fd, err, sizeof err);
	}
	CHECK (!alive);
	// sending on a dead socket must fail, not kill us with SIGPIPE
	CHECK (!r2d_ipc_send (fd, R2D_OP_FRAME, "{}") || !r2d_ipc_send (fd, R2D_OP_FRAME, "{}"));
	close (fd);
	CHECK (r2d_ipc_connect () == -1);
}

static void test_discovery(void) {
	unsetenv ("R2DISCORD_IPC_PATH");
	setenv ("XDG_RUNTIME_DIR", tmpdir, 1);
	setenv ("TMPDIR", tmpdir, 1);
	setenv ("TMP", tmpdir, 1);
	setenv ("TEMP", tmpdir, 1);
	char p[300];
	sock_path (p, sizeof p, "discord-ipc-3");
	FakeDiscord *f = fd_start (p, false);
	int fd = r2d_ipc_connect ();
	CHECK (fd >= 0);
	for (int i = 0; i < 50 && fd_connections (f) == 0; i++) {
		usleep (10000);
	}
	CHECK (fd_connections (f) == 1);
	close (fd);
	fd_stop (f);

	char d[300];
	snprintf (d, sizeof d, "%s/app", tmpdir);
	mkdir (d, 0700);
	snprintf (d, sizeof d, "%s/app/com.discordapp.Discord", tmpdir);
	mkdir (d, 0700);
	sock_path (p, sizeof p, "app/com.discordapp.Discord/discord-ipc-0");
	f = fd_start (p, false);
	fd = r2d_ipc_connect ();
	CHECK (fd >= 0);
	for (int i = 0; i < 50 && fd_connections (f) == 0; i++) {
		usleep (10000);
	}
	CHECK (fd_connections (f) == 1);
	close (fd);
	fd_stop (f);
}

static int connect_fake(FakeDiscord **f, const char *name) {
	char p[300];
	sock_path (p, sizeof p, name);
	*f = fd_start (p, false);
	setenv ("R2DISCORD_IPC_PATH", p, 1);
	int fd = r2d_ipc_connect ();
	char err[64];
	CHECK (fd >= 0);
	CHECK (r2d_ipc_handshake (fd, "1", err, sizeof err) == R2D_IPC_OK);
	return fd;
}

static void pump_until(int fd, char *err, size_t len, bool *alive, int *tries) {
	for (*tries = 0; *tries < 100; (*tries)++) {
		usleep (10000);
		*alive = r2d_ipc_pump (fd, err, len);
		if (!*alive || *err) {
			break;
		}
	}
}

static void test_pump_error_frame(void) {
	FakeDiscord *f;
	int fd = connect_fake (&f, "perr.sock");
	char err[128] = "";
	bool alive = true;
	int t;
	const char *j = "{\"evt\":\"ERROR\",\"data\":{\"message\":\"boom\"}}";
	fd_send_raw (f, R2D_OP_FRAME, j, strlen (j));
	pump_until (fd, err, sizeof err, &alive, &t);
	CHECK (alive);
	CHECK (!strcmp (err, "boom"));
	close (fd);
	fd_stop (f);
}

static void test_pump_close(void) {
	FakeDiscord *f;
	int fd = connect_fake (&f, "pclose.sock");
	char err[128] = "";
	bool alive = true;
	int t;
	const char *j = "{\"code\":4000,\"message\":\"bye\"}";
	fd_send_raw (f, R2D_OP_CLOSE, j, strlen (j));
	pump_until (fd, err, sizeof err, &alive, &t);
	CHECK (!alive);
	CHECK (strstr (err, "bye") != NULL);
	close (fd);
	fd_stop (f);
}

static void test_recv_timeout(void) {
	FakeDiscord *f;
	int fd = connect_fake (&f, "rto.sock");
	uint32_t op;
	char *json = NULL;
	CHECK (r2d_ipc_recv (fd, &op, &json, 50) == 0);
	close (fd);
	fd_stop (f);
}

static void test_recv_oversize(void) {
	FakeDiscord *f;
	int fd = connect_fake (&f, "big.sock");
	uint32_t op;
	char *json = NULL;
	fd_send_raw (f, R2D_OP_FRAME, "{}", 0x7fffffff);
	CHECK (r2d_ipc_recv (fd, &op, &json, 1000) == -1);
	close (fd);
	fd_stop (f);
}

int main(void) {
	snprintf (tmpdir, sizeof tmpdir, "/tmp/r2d-test-XXXXXX");
	if (!mkdtemp (tmpdir)) {
		return 2;
	}
	test_handshake_ok ();
	test_rejected ();
	test_server_gone ();
	test_pump_error_frame ();
	test_pump_close ();
	test_recv_timeout ();
	test_recv_oversize ();
	test_discovery ();
	char cmd[300];
	snprintf (cmd, sizeof cmd, "rm -rf '%s'", tmpdir);
	(void)!system (cmd);
	return T_END ();
}
