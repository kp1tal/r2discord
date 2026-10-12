#ifndef FAKE_DISCORD_H
#define FAKE_DISCORD_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct FakeDiscord FakeDiscord;

// Listen on a Unix socket at path. reject=true answers every handshake
// with CLOSE {"code":4000,"message":"Invalid Client ID"}.
FakeDiscord *fd_start(const char *path, bool reject);
void fd_stop(FakeDiscord *f);
// Print every received frame as "<op> <json>\n" (flushed).
void fd_set_log(FakeDiscord *f, FILE *out);
int fd_count(FakeDiscord *f, uint32_t op, const char *substr);
bool fd_wait(FakeDiscord *f, uint32_t op, const char *substr, int n, int timeout_ms);
char *fd_last_json(FakeDiscord *f, uint32_t op);
int fd_connections(FakeDiscord *f);
void fd_send_ping(FakeDiscord *f);
// Send one frame to the current client; len overrides the header length
// field while the body is strlen(json) bytes.
void fd_send_raw(FakeDiscord *f, uint32_t op, const char *json, uint32_t len);

#endif
