/* r2discord - MIT - Copyright 2026 - kp1tal */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "fake_discord.h"

static volatile sig_atomic_t done = 0;

static void on_sig(int s) {
	(void)s;
	done = 1;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf (stderr, "usage: %s <socket-path>\n", argv[0]);
		return 2;
	}
	signal (SIGTERM, on_sig);
	signal (SIGINT, on_sig);
	signal (SIGPIPE, SIG_IGN);
	FakeDiscord *f = fd_start (argv[1], false);
	fd_set_log (f, stdout);
	while (!done) {
		usleep (50000);
	}
	fd_stop (f);
	return 0;
}
