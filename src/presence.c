/* r2discord - MIT - Copyright 2026 - kp1tal */
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <r_util.h>
#include <r_version.h>
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

// "an x86", "an ARM64", "an ELF" but "a PE", "a mips"
static const char *article_for(const char *s) {
	if (!s || !*s) {
		return "a";
	}
	if (strchr ("AEIOUaeiou", s[0])) {
		return "an";
	}
	return ((s[0] == 'x' || s[0] == 'X') && isdigit ((unsigned char)s[1]))? "an": "a";
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
	*article = article_for (buf);
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

// " · 312 functions · 4 breakpoints"
static void stats_suffix(const R2DPresenceInput *in, char *buf, size_t len) {
	buf[0] = 0;
	size_t n = 0;
	if (in->functions > 0) {
		n += snprintf (buf + n, len - n, " · %d function%s",
			in->functions, in->functions == 1? "": "s");
	}
	if (in->mode == R2D_MODE_DEBUG && in->breakpoints > 0 && n < len) {
		snprintf (buf + n, len - n, " · %d breakpoint%s",
			in->breakpoints, in->breakpoints == 1? "": "s");
	}
}

void r2d_presence_text(const R2DPresenceInput *in, R2DText *out) {
	char buf[512];
	if (in->mode == R2D_MODE_NOFILE) {
		r2d_utf8_copy (out->details, sizeof out->details, "radare2");
		r2d_utf8_copy (out->state, sizeof out->state, "📂 No file loaded");
		r2d_utf8_copy (out->idle_state, sizeof out->idle_state, "💤 Idle");
		return;
	}
	char fmt[32], arch[64], desc[100];
	const char *article;
	format_label (in->format, fmt, sizeof fmt, &article);
	arch_label (in->arch, in->bits, arch, sizeof arch);
	// "x86-64 ELF"; without a format it reads "x86-64 binary"
	snprintf (desc, sizeof desc, "%s%s%s", arch, (*fmt && *arch)? " ": "", fmt);
	if (*arch) {
		article = article_for (arch);
	}
	if (in->showname && in->filename && *in->filename) {
		if (*desc) {
			snprintf (buf, sizeof buf, "Reversing %s (%s)", in->filename, desc);
		} else {
			snprintf (buf, sizeof buf, "Reversing %s", in->filename);
		}
	} else if (*fmt) {
		snprintf (buf, sizeof buf, "Reversing %s %s", article, desc);
	} else if (*desc) {
		snprintf (buf, sizeof buf, "Reversing %s %s binary", article, desc);
	} else {
		snprintf (buf, sizeof buf, "Reversing a binary");
	}
	r2d_utf8_copy (out->details, sizeof out->details, buf);

	char stats[128];
	stats_suffix (in, stats, sizeof stats);
	switch (in->mode) {
	case R2D_MODE_DEBUG:
		snprintf (buf, sizeof buf, "🐞 Debugging%s", stats);
		break;
	case R2D_MODE_VISUAL:
		if (in->showname && in->function && *in->function) {
			snprintf (buf, sizeof buf, "👁 %s%s", in->function, stats);
		} else {
			snprintf (buf, sizeof buf, "👁 Visual mode%s", stats);
		}
		break;
	default:
		snprintf (buf, sizeof buf, "⌨️ Prompt%s", stats);
		break;
	}
	r2d_utf8_copy (out->state, sizeof out->state, buf);
	snprintf (buf, sizeof buf, "💤 Idle%s", stats);
	r2d_utf8_copy (out->idle_state, sizeof out->idle_state, buf);
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
			pj_ks (pj, "large_text", "radare2 " R2_VERSION);
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
