#ifndef R2D_PRESENCE_H
#define R2D_PRESENCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	R2D_MODE_NOFILE,
	R2D_MODE_PROMPT,
	R2D_MODE_VISUAL,
	R2D_MODE_DEBUG,
} R2DMode;

typedef struct {
	R2DMode mode;
	const char *format;   // r2 bin class, e.g. "elf", "pe", "mach064"; may be NULL
	const char *arch;     // r2 arch name, e.g. "x86", "arm"; may be NULL
	int bits;             // 0 if unknown
	const char *filename; // basename; only shown with showname
	const char *function; // current function; only shown with showname in visual mode
	bool showname;
} R2DPresenceInput;

// Discord limits details/state to 128 bytes
#define R2D_TEXT_MAX 128

typedef struct {
	char details[R2D_TEXT_MAX];
	char state[R2D_TEXT_MAX];
} R2DText;

void r2d_presence_text(const R2DPresenceInput *in, R2DText *out);
// details == NULL builds a clear request ("activity": null). Caller frees.
char *r2d_activity_json(const char *details, const char *state, uint64_t start,
		const char *image, int pid, uint64_t nonce);
void r2d_utf8_copy(char *dst, size_t dstlen, const char *src);

#endif
