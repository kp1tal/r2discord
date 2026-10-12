#define _GNU_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ipc.h"
#include "worker.h"

#define KEY_MAX 512

struct R2DWorker {
	pthread_mutex_t mu;
	pthread_cond_t cv;
	pthread_t th;
	R2DWorkerTiming t;
	bool stop;
	bool done; // thread finished, set under mu
	// desired state (written by r2, read by the thread)
	char appid[32];
	char image[64];
	bool app_changed;
	bool have;
	R2DText text;
	uint64_t start;
	int idle_s;
	uint64_t last_touch_ms; // atomic
	// connection state (thread only, except where noted)
	int fd;
	bool rejected;
	uint64_t next_retry_ms;
	int backoff_ms;
	uint64_t last_send_ms;
	char sent_key[KEY_MAX]; // "" = nothing sent, "null" = cleared
	uint64_t nonce;
	// status (read by r2 under mu)
	char err[256];
	char sent_details[R2D_TEXT_MAX];
	char sent_state[R2D_TEXT_MAX];
};

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void disconnect(R2DWorker *w, uint64_t now) {
	if (w->fd >= 0) {
		close (w->fd);
		w->fd = -1;
	}
	w->sent_key[0] = 0;
	w->next_retry_ms = now + w->backoff_ms;
}

static void try_connect(R2DWorker *w, uint64_t now) {
	char appid[sizeof w->appid];
	memcpy (appid, w->appid, sizeof appid);
	char err[sizeof w->err] = "";
	pthread_mutex_unlock (&w->mu);
	int fd = r2d_ipc_connect ();
	R2DIpcStatus st = fd < 0? R2D_IPC_NOSOCKET: r2d_ipc_handshake (fd, appid, err, sizeof err);
	pthread_mutex_lock (&w->mu);
	now = now_ms ();
	if (w->stop) {
		if (fd >= 0) {
			close (fd);
		}
		return;
	}
	if (st == R2D_IPC_OK && !w->app_changed) {
		w->fd = fd;
		w->backoff_ms = w->t.backoff_min_ms;
		w->sent_key[0] = 0;
		w->err[0] = 0;
		return;
	}
	if (fd >= 0) {
		close (fd);
	}
	if (st == R2D_IPC_REJECTED && !w->app_changed) {
		w->rejected = true;
		snprintf (w->err, sizeof w->err, "%s", err);
		return;
	}
	w->next_retry_ms = now + w->backoff_ms;
	w->backoff_ms *= 2;
	if (w->backoff_ms > w->t.backoff_max_ms) {
		w->backoff_ms = w->t.backoff_max_ms;
	}
}

static void maybe_send(R2DWorker *w, uint64_t now) {
	if (!w->have && !w->sent_key[0]) {
		return; // nothing shown, nothing to clear
	}
	uint64_t lt = __atomic_load_n (&w->last_touch_ms, __ATOMIC_RELAXED);
	bool idle = w->have && w->idle_s > 0 && lt <= now
		&& now - lt > (uint64_t)w->idle_s * 1000;
	const char *state = idle? "Idle": w->text.state;
	char key[KEY_MAX];
	if (w->have) {
		snprintf (key, sizeof key, "%s\n%s\n%llu\n%s", w->text.details, state,
			(unsigned long long)w->start, w->image);
	} else {
		snprintf (key, sizeof key, "null");
	}
	if (!strcmp (key, w->sent_key)) {
		return;
	}
	if (w->sent_key[0] && now - w->last_send_ms < (uint64_t)w->t.min_interval_ms) {
		return;
	}
	char *json = w->have
		? r2d_activity_json (w->text.details, state, w->start, w->image, getpid (), ++w->nonce)
		: r2d_activity_json (NULL, NULL, 0, NULL, getpid (), ++w->nonce);
	char details[R2D_TEXT_MAX], st[R2D_TEXT_MAX];
	snprintf (details, sizeof details, "%s", w->have? w->text.details: "");
	snprintf (st, sizeof st, "%s", w->have? state: "");
	int fd = w->fd;
	pthread_mutex_unlock (&w->mu);
	bool ok = r2d_ipc_send (fd, R2D_OP_FRAME, json);
	pthread_mutex_lock (&w->mu);
	now = now_ms ();
	free (json);
	if (!ok) {
		disconnect (w, now);
		return;
	}
	snprintf (w->sent_key, sizeof w->sent_key, "%s", key);
	memcpy (w->sent_details, details, sizeof details);
	memcpy (w->sent_state, st, sizeof st);
	w->last_send_ms = now;
}

static void pump(R2DWorker *w, uint64_t now) {
	char err[sizeof w->err] = "";
	int fd = w->fd;
	pthread_mutex_unlock (&w->mu);
	bool alive = r2d_ipc_pump (fd, err, sizeof err);
	pthread_mutex_lock (&w->mu);
	now = now_ms ();
	if (*err) {
		snprintf (w->err, sizeof w->err, "%s", err);
	}
	if (!alive) {
		disconnect (w, now);
	}
}

static void *run(void *arg) {
	R2DWorker *w = arg;
	pthread_mutex_lock (&w->mu);
	while (!w->stop) {
		uint64_t now = now_ms ();
		if (w->app_changed) {
			w->app_changed = false;
			if (w->fd >= 0) {
				disconnect (w, now);
			}
			w->rejected = false;
			w->err[0] = 0;
			w->backoff_ms = w->t.backoff_min_ms;
			w->next_retry_ms = 0;
		}
		if (w->fd < 0 && w->have && !w->rejected && w->appid[0] && now >= w->next_retry_ms) {
			try_connect (w, now);
			now = now_ms ();
		}
		if (w->fd >= 0 && !w->stop) {
			maybe_send (w, now);
			now = now_ms ();
		}
		if (w->fd >= 0 && !w->stop) {
			pump (w, now);
		}
		struct timespec ts;
		clock_gettime (CLOCK_REALTIME, &ts);
		ts.tv_nsec += 100 * 1000000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000;
		}
		pthread_cond_timedwait (&w->cv, &w->mu, &ts);
	}
	if (w->fd >= 0) {
		if (w->sent_key[0] && strcmp (w->sent_key, "null")) {
			char *json = r2d_activity_json (NULL, NULL, 0, NULL, getpid (), ++w->nonce);
			r2d_ipc_send (w->fd, R2D_OP_FRAME, json);
			free (json);
		}
		r2d_ipc_close (w->fd);
		w->fd = -1;
	}
	w->done = true;
	pthread_cond_broadcast (&w->cv);
	pthread_mutex_unlock (&w->mu);
	return NULL;
}

R2DWorker *r2d_worker_new(const R2DWorkerTiming *t) {
	R2DWorker *w = calloc (1, sizeof *w);
	if (!w) {
		return NULL;
	}
	w->t = *t;
	w->fd = -1;
	w->backoff_ms = t->backoff_min_ms;
	w->last_touch_ms = now_ms ();
	pthread_mutex_init (&w->mu, NULL);
	pthread_cond_init (&w->cv, NULL);
	if (pthread_create (&w->th, NULL, run, w)) {
		pthread_mutex_destroy (&w->mu);
		pthread_cond_destroy (&w->cv);
		free (w);
		return NULL;
	}
	return w;
}

void r2d_worker_set_app(R2DWorker *w, const char *appid, const char *image) {
	pthread_mutex_lock (&w->mu);
	if (strcmp (w->appid, appid? appid: "")) {
		snprintf (w->appid, sizeof w->appid, "%s", appid? appid: "");
		w->app_changed = true;
	}
	snprintf (w->image, sizeof w->image, "%s", image? image: "");
	pthread_cond_signal (&w->cv);
	pthread_mutex_unlock (&w->mu);
}

void r2d_worker_set_idle(R2DWorker *w, int seconds) {
	pthread_mutex_lock (&w->mu);
	w->idle_s = seconds < 0? 0: seconds;
	pthread_mutex_unlock (&w->mu);
}

void r2d_worker_update(R2DWorker *w, const R2DText *text, uint64_t start) {
	pthread_mutex_lock (&w->mu);
	w->text = *text;
	w->start = start;
	w->have = true;
	pthread_cond_signal (&w->cv);
	pthread_mutex_unlock (&w->mu);
}

void r2d_worker_clear(R2DWorker *w) {
	pthread_mutex_lock (&w->mu);
	w->have = false;
	pthread_cond_signal (&w->cv);
	pthread_mutex_unlock (&w->mu);
}

void r2d_worker_touch(R2DWorker *w) {
	__atomic_store_n (&w->last_touch_ms, now_ms (), __ATOMIC_RELAXED);
}

void r2d_worker_status(R2DWorker *w, char *buf, size_t len) {
	pthread_mutex_lock (&w->mu);
	const char *conn = w->fd >= 0? "connected"
		: w->rejected? "rejected by Discord (change discord.appid to retry)"
		: (!w->have || !w->appid[0])? "not connected (nothing to show yet)"
		: "not connected (is Discord running?)";
	snprintf (buf, len, "status: %s%s%s\nappid: %s\ndetails: %s\nstate: %s",
		conn, w->err[0]? "\nerror: ": "", w->err, w->appid,
		w->sent_details[0]? w->sent_details: "-",
		w->sent_state[0]? w->sent_state: "-");
	pthread_mutex_unlock (&w->mu);
}

bool r2d_worker_free(R2DWorker *w) {
	if (!w) {
		return true;
	}
	pthread_mutex_lock (&w->mu);
	w->stop = true;
	pthread_cond_signal (&w->cv);
	uint64_t deadline = now_ms () + 1000;
	while (!w->done) {
		uint64_t n = now_ms ();
		if (n >= deadline) {
			// Intentional leak: the still-running thread must never touch
			// freed memory. Only happens with an unresponsive Discord at exit.
			pthread_detach (w->th);
			pthread_mutex_unlock (&w->mu);
			return false;
		}
		struct timespec ts;
		clock_gettime (CLOCK_REALTIME, &ts);
		uint64_t left = deadline - n;
		ts.tv_sec += left / 1000;
		ts.tv_nsec += (long)(left % 1000) * 1000000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000;
		}
		pthread_cond_timedwait (&w->cv, &w->mu, &ts);
	}
	pthread_mutex_unlock (&w->mu);
	pthread_join (w->th, NULL);
	pthread_mutex_destroy (&w->mu);
	pthread_cond_destroy (&w->cv);
	free (w);
	return true;
}
