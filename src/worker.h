/* r2discord - MIT - Copyright 2026 - kp1tal */
#ifndef R2D_WORKER_H
#define R2D_WORKER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "presence.h"

typedef struct R2DWorker R2DWorker;

typedef struct {
	int min_interval_ms; // min time between SET_ACTIVITY sends
	int backoff_min_ms;  // first reconnect delay
	int backoff_max_ms;  // reconnect delay cap
} R2DWorkerTiming;

R2DWorker *r2d_worker_new(const R2DWorkerTiming *t);
// A different appid drops the connection and clears a previous rejection.
void r2d_worker_set_app(R2DWorker *w, const char *appid, const char *image);
void r2d_worker_set_idle(R2DWorker *w, int seconds);
void r2d_worker_update(R2DWorker *w, const R2DText *text, uint64_t start);
void r2d_worker_clear(R2DWorker *w);
// Lock-free; safe to call on every r2 command.
void r2d_worker_touch(R2DWorker *w);
void r2d_worker_status(R2DWorker *w, char *buf, size_t len);
// Clears the presence, closes the connection and joins the thread.
// Returns false if the thread did not stop in time and was detached (the
// struct is then leaked on purpose); true otherwise, also for NULL.
bool r2d_worker_free(R2DWorker *w);

#endif
