#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "fake_discord.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MAXF 512

struct FakeDiscord {
	char path[108];
	bool reject;
	int lfd;
	int cfd;
	bool stop;
	bool ping;
	int conns;
	int nframes;
	uint32_t ops[MAXF];
	char *jsons[MAXF];
	FILE *log;
	pthread_t th;
	pthread_mutex_t mu;
};

static void put32(uint8_t *p, uint32_t v) {
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static uint32_t get32(const uint8_t *p) {
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void sendf(int fd, uint32_t op, const char *json) {
	uint8_t h[8];
	uint32_t n = strlen (json);
	put32 (h, op);
	put32 (h + 4, n);
	if (send (fd, h, 8, MSG_NOSIGNAL) == 8) {
		(void)!send (fd, json, n, MSG_NOSIGNAL);
	}
}

static bool readn(int fd, void *buf, size_t n) {
	uint8_t *p = buf;
	while (n > 0) {
		ssize_t r = read (fd, p, n);
		if (r <= 0) {
			return false;
		}
		p += r;
		n -= r;
	}
	return true;
}

static void record(FakeDiscord *f, uint32_t op, char *json) {
	pthread_mutex_lock (&f->mu);
	if (f->nframes < MAXF) {
		f->ops[f->nframes] = op;
		f->jsons[f->nframes++] = json;
	} else {
		free (json);
	}
	if (f->log) {
		fprintf (f->log, "%u %s\n", op, json);
		fflush (f->log);
	}
	pthread_mutex_unlock (&f->mu);
}

static void drop_client(FakeDiscord *f) {
	if (f->cfd >= 0) {
		close (f->cfd);
		f->cfd = -1;
	}
}

static void *run(void *arg) {
	FakeDiscord *f = arg;
	while (true) {
		pthread_mutex_lock (&f->mu);
		bool stop = f->stop;
		bool ping = f->ping;
		f->ping = false;
		pthread_mutex_unlock (&f->mu);
		if (stop) {
			break;
		}
		if (ping && f->cfd >= 0) {
			sendf (f->cfd, 3, "{\"ping\":1}");
		}
		struct pollfd p = { f->cfd >= 0? f->cfd: f->lfd, POLLIN, 0 };
		if (poll (&p, 1, 20) <= 0) {
			continue;
		}
		if (f->cfd < 0) {
			int c = accept (f->lfd, NULL, NULL);
			if (c >= 0) {
				f->cfd = c;
				pthread_mutex_lock (&f->mu);
				f->conns++;
				pthread_mutex_unlock (&f->mu);
			}
			continue;
		}
		uint8_t h[8];
		if (!readn (f->cfd, h, 8)) {
			drop_client (f);
			continue;
		}
		uint32_t op = get32 (h), n = get32 (h + 4);
		char *json = calloc (1, n + 1);
		if (!json || !readn (f->cfd, json, n)) {
			free (json);
			drop_client (f);
			continue;
		}
		record (f, op, json);
		if (op == 0) {
			if (f->reject) {
				sendf (f->cfd, 2, "{\"code\":4000,\"message\":\"Invalid Client ID\"}");
				drop_client (f);
			} else {
				sendf (f->cfd, 1, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{\"v\":1}}");
			}
		} else if (op == 1) {
			sendf (f->cfd, 1, "{\"cmd\":\"SET_ACTIVITY\",\"evt\":null,\"data\":{}}");
		} else if (op == 2) {
			drop_client (f);
		}
	}
	drop_client (f);
	return NULL;
}

FakeDiscord *fd_start(const char *path, bool reject) {
	signal (SIGPIPE, SIG_IGN);
	FakeDiscord *f = calloc (1, sizeof *f);
	snprintf (f->path, sizeof f->path, "%s", path);
	f->reject = reject;
	f->cfd = -1;
	pthread_mutex_init (&f->mu, NULL);
	unlink (path);
	f->lfd = socket (AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	snprintf (sa.sun_path, sizeof sa.sun_path, "%s", path);
	if (f->lfd < 0 || bind (f->lfd, (struct sockaddr *)&sa, sizeof sa) || listen (f->lfd, 4)) {
		perror ("fake_discord");
		exit (2);
	}
	pthread_create (&f->th, NULL, run, f);
	return f;
}

void fd_stop(FakeDiscord *f) {
	pthread_mutex_lock (&f->mu);
	f->stop = true;
	pthread_mutex_unlock (&f->mu);
	pthread_join (f->th, NULL);
	close (f->lfd);
	unlink (f->path);
	for (int i = 0; i < f->nframes; i++) {
		free (f->jsons[i]);
	}
	pthread_mutex_destroy (&f->mu);
	free (f);
}

void fd_set_log(FakeDiscord *f, FILE *out) {
	pthread_mutex_lock (&f->mu);
	f->log = out;
	pthread_mutex_unlock (&f->mu);
}

int fd_count(FakeDiscord *f, uint32_t op, const char *substr) {
	int n = 0;
	pthread_mutex_lock (&f->mu);
	for (int i = 0; i < f->nframes; i++) {
		if (f->ops[i] == op && (!substr || strstr (f->jsons[i], substr))) {
			n++;
		}
	}
	pthread_mutex_unlock (&f->mu);
	return n;
}

bool fd_wait(FakeDiscord *f, uint32_t op, const char *substr, int n, int timeout_ms) {
	for (int t = 0; t < timeout_ms; t += 10) {
		if (fd_count (f, op, substr) >= n) {
			return true;
		}
		usleep (10000);
	}
	return fd_count (f, op, substr) >= n;
}

char *fd_last_json(FakeDiscord *f, uint32_t op) {
	char *r = NULL;
	pthread_mutex_lock (&f->mu);
	for (int i = f->nframes - 1; i >= 0; i--) {
		if (f->ops[i] == op) {
			r = strdup (f->jsons[i]);
			break;
		}
	}
	pthread_mutex_unlock (&f->mu);
	return r;
}

int fd_connections(FakeDiscord *f) {
	pthread_mutex_lock (&f->mu);
	int n = f->conns;
	pthread_mutex_unlock (&f->mu);
	return n;
}

void fd_send_ping(FakeDiscord *f) {
	pthread_mutex_lock (&f->mu);
	f->ping = true;
	pthread_mutex_unlock (&f->mu);
}
