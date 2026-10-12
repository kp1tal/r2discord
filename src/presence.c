/* r2discord - MIT - Copyright 2026 - kp1tal */
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <r_util.h>
#include "presence.h"

typedef struct {
	const char *name;
	const char *label;
	const char *article;
} FormatName;

static const FormatName formats[] = {
	{ "elf", "ELF", "an" },
	{ "elf64", "ELF", "an" },
	{ "pe", "PE", "a" },
	{ "pe64", "PE", "a" },
	{ "mach0", "Mach-O", "a" },
	{ "mach064", "Mach-O", "a" },
	{ "dex", "DEX", "a" },
	{ "wasm", "Wasm", "a" },
	{ "mz", "MZ", "an" },
	{ "any", "raw", "a" },
	{ NULL, NULL, NULL }
};

static const char *vowel_article(const char *s) {
	return (s && strchr ("AEIOUaeiou", s[0]))? "an": "a";
}

static void format_label(const char *fmt, char *buf, size_t len, const char **article) {
	*article = "a";
	buf[0] = 0;
	if (!fmt || !*fmt) {
		return;
	}
	for (const FormatName *f = formats; f->name; f++) {
		if (!strcmp (f->name, fmt)) {
			snprintf (buf, len, "%s", f->label);
			*article = f->article;
			return;
		}
	}
	size_t i = 0;
	for (; fmt[i] && i + 1 < len; i++) {
		buf[i] = toupper ((unsigned char)fmt[i]);
	}
	buf[i] = 0;
	*article = vowel_article (buf);
}

static void arch_label(const char *arch, int bits, char *buf, size_t len) {
	buf[0] = 0;
	if (!arch || !*arch) {
		return;
	}
	if (!strcmp (arch, "arm")) {
		snprintf (buf, len, bits == 64? "ARM64": "ARM");
	} else if (bits > 0) {
		snprintf (buf, len, "%s-%d", arch, bits);
	} else {
		snprintf (buf, len, "%s", arch);
	}
}

void r2d_utf8_copy(char *dst, size_t dstlen, const char *src) {
	if (!dstlen) {
		return;
	}
	size_t n = strlen (src);
	if (n < dstlen) {
		memcpy (dst, src, n + 1);
		return;
	}
	n = dstlen - 1;
	// step back over continuation bytes, then drop the partial char
	size_t i = n;
	while (i > 0 && ((unsigned char)src[i] & 0xC0) == 0x80) {
		i--;
	}
	// src[i] is the lead byte of the char that would be split (or a complete
	// ASCII char that starts right at the cut): keep everything before it
	memcpy (dst, src, i);
	dst[i] = 0;
}

void r2d_presence_text(const R2DPresenceInput *in, R2DText *out) {
	char buf[512];
	if (in->mode == R2D_MODE_NOFILE) {
		r2d_utf8_copy (out->details, sizeof out->details, "radare2");
		r2d_utf8_copy (out->state, sizeof out->state, "No file loaded");
		return;
	}
	const char *verb = in->mode == R2D_MODE_DEBUG? "Debugging": "Analyzing";
	char fmt[32], arch[64], desc[100];
	const char *article;
	format_label (in->format, fmt, sizeof fmt, &article);
	arch_label (in->arch, in->bits, arch, sizeof arch);
	snprintf (desc, sizeof desc, "%s%s%s", fmt, (*fmt && *arch)? " ": "", arch);
	if (!*fmt) {
		article = vowel_article (arch);
	}
	if (in->showname && in->filename && *in->filename) {
		if (*desc) {
			snprintf (buf, sizeof buf, "%s %s (%s)", verb, in->filename, desc);
		} else {
			snprintf (buf, sizeof buf, "%s %s", verb, in->filename);
		}
	} else if (*desc) {
		snprintf (buf, sizeof buf, "%s %s %s binary", verb, article, desc);
	} else {
		snprintf (buf, sizeof buf, "%s a binary", verb);
	}
	r2d_utf8_copy (out->details, sizeof out->details, buf);

	const char *state;
	switch (in->mode) {
	case R2D_MODE_DEBUG:
		state = "Debugger attached";
		break;
	case R2D_MODE_VISUAL:
		if (in->showname && in->function && *in->function) {
			snprintf (buf, sizeof buf, "In %s", in->function);
			state = buf;
		} else {
			state = "In visual mode";
		}
		break;
	default:
		state = "At the prompt";
		break;
	}
	r2d_utf8_copy (out->state, sizeof out->state, state);
}

char *r2d_activity_json(const char *details, const char *state, uint64_t start,
		const char *image, int pid, uint64_t nonce) {
	char n[24];
	snprintf (n, sizeof n, "%" PRIu64, nonce);
	PJ *pj = pj_new ();
	pj_o (pj);
	pj_ks (pj, "cmd", "SET_ACTIVITY");
	pj_ko (pj, "args");
	pj_kn (pj, "pid", (ut64)pid);
	if (details) {
		pj_ko (pj, "activity");
		pj_ks (pj, "details", details);
		pj_ks (pj, "state", state? state: "");
		pj_ko (pj, "timestamps");
		pj_kn (pj, "start", start);
		pj_end (pj);
		if (image && *image) {
			pj_ko (pj, "assets");
			pj_ks (pj, "large_image", image);
			pj_ks (pj, "large_text", "radare2");
			pj_end (pj);
		}
		pj_end (pj);
	} else {
		pj_knull (pj, "activity");
	}
	pj_end (pj);
	pj_ks (pj, "nonce", n);
	pj_end (pj);
	return pj_drain (pj);
}
