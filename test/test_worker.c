/* r2discord - MIT - Copyright 2026 - kp1tal */
#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "t.h"
#include "fake_discord.h"
#include "../src/ipc.h"
#include "../src/presence.h"
#include "../src/worker.h"

static char tmpdir[256];
static const R2DWorkerTiming fast = { 50, 50, 200, 0 };

static FakeDiscord *start_fake(const char *name, bool reject, char *path, size_t len) {
	snprintf (path, len, "%s/%s", tmpdir, name);
	setenv ("R2DISCORD_IPC_PATH", path, 1);
	return fd_start (path, reject);
}

static void upd(R2DWorker *w, const char *details, const char *state) {
	R2DText t;
	r2d_utf8_copy (t.details, sizeof t.details, details);
	r2d_utf8_copy (t.state, sizeof t.state, state);
	r2d_worker_update (w, &t, 1700000000);
}

static uint64_t ms(void) {
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void test_basic_and_free(void) {
	char p[300];
	FakeDiscord *f = start_fake ("basic.sock", false, p, sizeof p);
	R2DWorker *w = r2d_worker_new (&fast);
	r2d_worker_set_app (w, "1559252980589535242", "r2");
	upd (w, "Analyzing an ELF x86-64 binary", "At the prompt");
	CHECK (fd_wait (f, R2D_OP_HANDSHAKE, "1559252980589535242", 1, 2000));
	CHECK (fd_wait (f, R2D_OP_FRAME, "Analyzing an ELF x86-64 binary", 1, 2000));
	CHECK (fd_count (f, R2D_OP_FRAME, "\"large_image\":\"r2\"") == 1);
	char st[512];
	r2d_worker_status (w, st, sizeof st);
	CHECK (strstr (st, "connected") && !strstr (st, "not connected"));
	CHECK (strstr (st, "At the prompt") != NULL);

	r2d_worker_clear (w);
	CHECK (fd_wait (f, R2D_OP_FRAME, "\"activity\":null", 1, 2000));

	upd (w, "Analyzing again", "At the prompt");
	CHECK (fd_wait (f, R2D_OP_FRAME, "Analyzing again", 1, 2000));
	r2d_worker_free (w);
	CHECK (fd_wait (f, R2D_OP_FRAME, "\"activity\":null", 2, 2000));
	CHECK (fd_wait (f, R2D_OP_CLOSE, NULL, 1, 2000));
	fd_stop (f);
}

static void test_coalesce(void) {
	char p[300];
	FakeDiscord *f = start_fake ("burst.sock", false, p, sizeof p);
	R2DWorkerTiming slow = { 300, 50, 200, 0 };
	R2DWorker *w = r2d_worker_new (&slow);
	r2d_worker_set_app (w, "1", "r2");
	upd (w, "d", "s0");
	CHECK (fd_wait (f, R2D_OP_FRAME, "s0", 1, 2000));
	for (int i = 1; i < 20; i++) {
		char s[8];
		snprintf (s, sizeof s, "s%d", i);
		upd (w, "d", s);
	}
	CHECK (fd_wait (f, R2D_OP_FRAME, "\"s19\"", 1, 2000));
	CHECK (fd_count (f, R2D_OP_FRAME, "\"details\"") <= 3);
	r2d_worker_free (w);
	fd_stop (f);
}

static void test_idle(void) {
	char p[300];
	FakeDiscord *f = start_fake ("idle.sock", false, p, sizeof p);
	R2DWorker *w = r2d_worker_new (&fast);
	r2d_worker_set_app (w, "1", "r2");
	r2d_worker_set_idle (w, 1);
	r2d_worker_touch (w);
	upd (w, "d", "At the prompt");
	CHECK (fd_wait (f, R2D_OP_FRAME, "At the prompt", 1, 1000));
	CHECK (fd_count (f, R2D_OP_FRAME, "\"Idle\"") == 0);
	CHECK (fd_wait (f, R2D_OP_FRAME, "\"Idle\"", 1, 2500));
	CHECK (fd_count (f, R2D_OP_FRAME, "\"details\":\"d\"") >= 2);
	char *last = fd_last_json (f, R2D_OP_FRAME);
	CHECK (last && strstr (last, "\"Idle\"") && strstr (last, "\"details\":\"d\""));
	free (last);
	r2d_worker_touch (w);
	CHECK (fd_wait (f, R2D_OP_FRAME, "At the prompt", 2, 1000));
	r2d_worker_free (w);
	fd_stop (f);
}

static void test_idle_disabled(void) {
	char p[300];
	FakeDiscord *f = start_fake ("noidle.sock", false, p, sizeof p);
	R2DWorker *w = r2d_worker_new (&fast);
	r2d_worker_set_app (w, "1", "r2");
	r2d_worker_set_idle (w, 0);
	r2d_worker_touch (w);
	upd (w, "d", "At the prompt");
	CHECK (fd_wait (f, R2D_OP_FRAME, "At the prompt", 1, 1000));
	usleep (1500000);
	CHECK (fd_count (f, R2D_OP_FRAME, "\"Idle\"") == 0);
	r2d_worker_free (w);
	fd_stop (f);
}

// r2 runs commands while it is still opening the file, so the first snapshot
// is often "No file loaded"; it must be replaced before anything is sent.
static void test_settle_first_send(void) {
	char p[300];
	FakeDiscord *f = start_fake ("settle.sock", false, p, sizeof p);
	R2DWorkerTiming settle = { 50, 50, 200, 300 };
	R2DWorker *w = r2d_worker_new (&settle);
	r2d_worker_set_app (w, "1", "r2");
	upd (w, "radare2", "No file loaded");
	CHECK (fd_wait (f, R2D_OP_HANDSHAKE, NULL, 1, 2000));
	usleep (100000);
	upd (w, "Analyzing an ELF x86-64 binary", "At the prompt");
	CHECK (fd_wait (f, R2D_OP_FRAME, "Analyzing an ELF x86-64 binary", 1, 2000));
	CHECK (fd_count (f, R2D_OP_FRAME, "No file loaded") == 0);
	r2d_worker_free (w);
	fd_stop (f);
}

static void test_bounded_free(void) {
	char p[300];
	snprintf (p, sizeof p, "%s/mute.sock", tmpdir);
	setenv ("R2DISCORD_IPC_PATH", p, 1);
	int ls = socket (AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	CHECK (strlen (p) < sizeof a.sun_path);
	memcpy (a.sun_path, p, strlen (p) < sizeof a.sun_path? strlen (p): sizeof a.sun_path - 1);
	CHECK (ls >= 0 && bind (ls, (struct sockaddr *)&a, sizeof a) == 0 && listen (ls, 4) == 0);
	R2DWorker *w = r2d_worker_new (&fast);
	r2d_worker_set_app (w, "1", "r2");
	upd (w, "d", "s");
	usleep (200000); // handshake is in flight, peer never answers
	uint64_t t0 = ms ();
	r2d_worker_free (w);
	CHECK (ms () - t0 < 1500);
	close (ls);
	unlink (p);
}

static void test_reconnect(void) {
	char p[300];
	FakeDiscord *f = start_fake ("re.sock", false, p, sizeof p);
	R2DWorker *w = r2d_worker_new (&fast);
	r2d_worker_set_app (w, "1", "r2");
	upd (w, "d", "first");
	CHECK (fd_wait (f, R2D_OP_FRAME, "first", 1, 2000));
	fd_stop (f);
	usleep (200000);
	f = fd_start (p, false);
	// the same presence must be re-sent after reconnecting
	CHECK (fd_wait (f, R2D_OP_FRAME, "first", 1, 3000));
	r2d_worker_free (w);
	fd_stop (f);
}

static void test_rejected(void) {
	char p[300];
	FakeDiscord *f = start_fake ("rej.sock", true, p, sizeof p);
	R2DWorker *w = r2d_worker_new (&fast);
	r2d_worker_set_app (w, "bad", "r2");
	upd (w, "d", "s");
	CHECK (fd_wait (f, R2D_OP_HANDSHAKE, NULL, 1, 2000));
	usleep (600000);
	CHECK (fd_connections (f) == 1); // no retry loop after a rejection
	char st[512];
	r2d_worker_status (w, st, sizeof st);
	CHECK (strstr (st, "Invalid Client ID") != NULL);
	r2d_worker_set_app (w, "other", "r2");
	CHECK (fd_wait (f, R2D_OP_HANDSHAKE, "\"other\"", 1, 2000));
	r2d_worker_free (w);
	fd_stop (f);
}

static void test_no_discord(void) {
	char p[300];
	snprintf (p, sizeof p, "%s/nothing.sock", tmpdir);
	setenv ("R2DISCORD_IPC_PATH", p, 1);
	R2DWorker *w = r2d_worker_new (&fast);
	r2d_worker_set_app (w, "1", "r2");
	upd (w, "d", "s");
	usleep (300000);
	char st[512];
	r2d_worker_status (w, st, sizeof st);
	CHECK (strstr (st, "not connected") != NULL);
	uint64_t t0 = ms ();
	r2d_worker_free (w);
	CHECK (ms () - t0 < 1500);
}

int main(void) {
	snprintf (tmpdir, sizeof tmpdir, "/tmp/r2d-test-XXXXXX");
	if (!mkdtemp (tmpdir)) {
		return 2;
	}
	test_basic_and_free ();
	test_coalesce ();
	test_idle ();
	test_idle_disabled ();
	test_reconnect ();
	test_rejected ();
	test_no_discord ();
	test_bounded_free ();
	test_settle_first_send ();
	char cmd[300];
	snprintf (cmd, sizeof cmd, "rm -rf '%s'", tmpdir);
	(void)!system (cmd);
	return T_END ();
}
