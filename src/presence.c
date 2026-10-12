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
} FormatName;

static const FormatName formats[] = {
	{ "elf", "ELF" },
	{ "elf64", "ELF" },
	{ "pe", "PE" },
	{ "pe64", "PE" },
	{ "mach0", "Mach-O" },
	{ "mach064", "Mach-O" },
	{ "dex", "DEX" },
	{ "wasm", "Wasm" },
	{ "mz", "MZ" },
	{ "any", "raw" },
	{ NULL, NULL }
};

static void format_label(const char *fmt, char *buf, size_t len) {
	buf[0] = 0;
	if (!fmt || !*fmt) {
		return;
	}
	for (const FormatName *f = formats; f->name; f++) {
		if (!strcmp (f->name, fmt)) {
			snprintf (buf, len, "%s", f->label);
			return;
		}
	}
	size_t i = 0;
	for (; fmt[i] && i + 1 < len; i++) {
		buf[i] = toupper ((unsigned char)fmt[i]);
	}
	buf[i] = 0;
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

// " · 312 fns · 4 bps"; abbreviated because Discord's profile card only
// shows ~33 characters per line
static void stats_suffix(const R2DPresenceInput *in, char *buf, size_t len) {
	buf[0] = 0;
	size_t n = 0;
	if (in->functions > 0) {
		n += snprintf (buf + n, len - n, " · %d fn%s",
			in->functions, in->functions == 1? "": "s");
	}
	if (in->mode == R2D_MODE_DEBUG && in->breakpoints > 0 && n < len) {
		snprintf (buf + n, len - n, " · %d bp%s",
			in->breakpoints, in->breakpoints == 1? "": "s");
	}
}

void r2d_presence_text(const R2DPresenceInput *in, R2DText *out) {
	char buf[512];
	r2d_utf8_copy (out->idle_state, sizeof out->idle_state, "💤 Idle");
	if (in->mode == R2D_MODE_NOFILE) {
		r2d_utf8_copy (out->details, sizeof out->details, "radare2");
		r2d_utf8_copy (out->state, sizeof out->state, "📂 No file loaded");
		return;
	}
	// details: "[file · ]x86-64 ELF · 312 fns · 4 bps"
	char fmt[32], arch[64], desc[100], stats[64];
	format_label (in->format, fmt, sizeof fmt);
	arch_label (in->arch, in->bits, arch, sizeof arch);
	if (*arch && *fmt) {
		snprintf (desc, sizeof desc, "%s %s", arch, fmt);
	} else if (*arch || *fmt) {
		snprintf (desc, sizeof desc, "%s%s", *arch? arch: fmt, *fmt? "": " binary");
	} else {
		snprintf (desc, sizeof desc, "binary");
	}
	stats_suffix (in, stats, sizeof stats);
	bool named = in->showname && in->filename && *in->filename;
	if (named && !*arch && !*fmt) {
		snprintf (buf, sizeof buf, "%s%s", in->filename, stats);
	} else if (named) {
		snprintf (buf, sizeof buf, "%s · %s%s", in->filename, desc, stats);
	} else {
		snprintf (buf, sizeof buf, "%s%s", desc, stats);
	}
	r2d_utf8_copy (out->details, sizeof out->details, buf);

	// state: just the mode, short enough to never be cut
	switch (in->mode) {
	case R2D_MODE_DEBUG:
		snprintf (buf, sizeof buf, "🐞 Debugging");
		break;
	case R2D_MODE_VISUAL:
		if (in->showname && in->function && *in->function) {
			snprintf (buf, sizeof buf, "👁 %s", in->function);
		} else {
			snprintf (buf, sizeof buf, "👁 Visual mode");
		}
		break;
	default:
		snprintf (buf, sizeof buf, "⌨️ Prompt");
		break;
	}
	r2d_utf8_copy (out->state, sizeof out->state, buf);
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
