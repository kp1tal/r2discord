/* r2discord - MIT - Copyright 2026 - kp1tal */
#include <r_util.h>
#include <r_version.h>
#include "t.h"
#include "../src/presence.h"

static R2DText text_stats(R2DMode m, const char *fmt, const char *arch, int bits,
		const char *file, const char *fcn, bool show, int fns, int bps) {
	R2DPresenceInput in = { m, fmt, arch, bits, file, fcn, show, fns, bps };
	R2DText t;
	r2d_presence_text (&in, &t);
	return t;
}

static R2DText text(R2DMode m, const char *fmt, const char *arch, int bits,
		const char *file, const char *fcn, bool show) {
	return text_stats (m, fmt, arch, bits, file, fcn, show, 0, 0);
}

static void test_modes_hidden(void) {
	R2DText t = text (R2D_MODE_NOFILE, NULL, NULL, 0, NULL, NULL, false);
	CHECK_STR (t.details, "radare2");
	CHECK_STR (t.state, "📂 No file loaded");
	CHECK_STR (t.idle_state, "💤 Idle");

	t = text (R2D_MODE_PROMPT, "elf", "x86", 64, "ls", NULL, false);
	CHECK_STR (t.details, "Reversing an x86-64 ELF");
	CHECK_STR (t.state, "⌨️ Prompt");
	CHECK_STR (t.idle_state, "💤 Idle");

	t = text (R2D_MODE_VISUAL, "pe", "x86", 32, "a.exe", "sym.main", false);
	CHECK_STR (t.details, "Reversing an x86-32 PE");
	CHECK_STR (t.state, "👁 Visual mode"); // function hidden without showname

	t = text (R2D_MODE_DEBUG, "mach064", "arm", 64, "x", NULL, false);
	CHECK_STR (t.details, "Reversing an ARM64 Mach-O");
	CHECK_STR (t.state, "🐞 Debugging");
}

static void test_modes_shown(void) {
	R2DText t = text (R2D_MODE_PROMPT, "elf", "x86", 64, "ls", NULL, true);
	CHECK_STR (t.details, "Reversing ls (x86-64 ELF)");
	CHECK_STR (t.state, "⌨️ Prompt");

	t = text (R2D_MODE_VISUAL, "elf", "x86", 64, "ls", "sym.main", true);
	CHECK_STR (t.state, "👁 sym.main");

	t = text (R2D_MODE_VISUAL, "elf", "x86", 64, "ls", NULL, true);
	CHECK_STR (t.state, "👁 Visual mode");

	t = text (R2D_MODE_DEBUG, "elf", "arm", 32, "fw", NULL, true);
	CHECK_STR (t.details, "Reversing fw (ARM ELF)");
}

static void test_stats(void) {
	R2DText t = text_stats (R2D_MODE_DEBUG, "elf", "x86", 64, "x", NULL, false, 312, 4);
	CHECK_STR (t.state, "🐞 Debugging · 312 functions · 4 breakpoints");
	CHECK_STR (t.idle_state, "💤 Idle · 312 functions · 4 breakpoints");

	t = text_stats (R2D_MODE_DEBUG, "elf", "x86", 64, "x", NULL, false, 1, 1);
	CHECK_STR (t.state, "🐞 Debugging · 1 function · 1 breakpoint");

	t = text_stats (R2D_MODE_DEBUG, "elf", "x86", 64, "x", NULL, false, 0, 0);
	CHECK_STR (t.state, "🐞 Debugging"); // nothing analyzed yet: no stats

	t = text_stats (R2D_MODE_DEBUG, "elf", "x86", 64, "x", NULL, false, 0, 2);
	CHECK_STR (t.state, "🐞 Debugging · 2 breakpoints");

	t = text_stats (R2D_MODE_VISUAL, "elf", "x86", 64, "x", NULL, false, 312, 4);
	CHECK_STR (t.state, "👁 Visual mode · 312 functions"); // breakpoints only when debugging

	t = text_stats (R2D_MODE_VISUAL, "elf", "x86", 64, "x", "sym.main", true, 312, 0);
	CHECK_STR (t.state, "👁 sym.main · 312 functions");

	t = text_stats (R2D_MODE_PROMPT, "elf", "x86", 64, "x", NULL, false, 312, 0);
	CHECK_STR (t.state, "⌨️ Prompt · 312 functions");
	CHECK_STR (t.idle_state, "💤 Idle · 312 functions");
}

static void test_labels(void) {
	R2DText t = text (R2D_MODE_PROMPT, "omf", "z80", 8, "x", NULL, false);
	CHECK_STR (t.details, "Reversing a z80-8 OMF");
	t = text (R2D_MODE_PROMPT, "foo", "mips", 0, "x", NULL, false);
	CHECK_STR (t.details, "Reversing a mips FOO");
	t = text (R2D_MODE_PROMPT, "elf", NULL, 0, "x", NULL, false);
	CHECK_STR (t.details, "Reversing an ELF");
	t = text (R2D_MODE_PROMPT, "pe", NULL, 0, "x", NULL, false);
	CHECK_STR (t.details, "Reversing a PE");
	t = text (R2D_MODE_PROMPT, NULL, "x86", 64, "x", NULL, false);
	CHECK_STR (t.details, "Reversing an x86-64 binary");
	t = text (R2D_MODE_PROMPT, NULL, NULL, 0, "x", NULL, false);
	CHECK_STR (t.details, "Reversing a binary");
	t = text (R2D_MODE_PROMPT, NULL, NULL, 0, "x", NULL, true);
	CHECK_STR (t.details, "Reversing x");
}

static void test_truncation(void) {
	char name[400] = {0};
	for (int i = 0; i < 150; i++) {
		strcat (name, "é"); // 2 bytes each
	}
	R2DText t = text (R2D_MODE_PROMPT, "elf", "x86", 64, name, NULL, true);
	size_t n = strlen (t.details);
	CHECK (n <= 127);
	CHECK (n > 100);
	// last char must be complete: no dangling lead byte
	CHECK (((unsigned char)t.details[n - 1] & 0xC0) == 0x80);
	CHECK (((unsigned char)t.details[n - 2] & 0xE0) == 0xC0);

	// emoji prefix + long function name in the state line
	t = text_stats (R2D_MODE_VISUAL, "elf", "x86", 64, "x", name, true, 312, 0);
	n = strlen (t.state);
	CHECK (n <= 127 && n > 100);
	CHECK (((unsigned char)t.state[n - 1] & 0xC0) == 0x80);

	char dst[4];
	r2d_utf8_copy (dst, sizeof dst, "aé"); // fits exactly: 'a' + 2 bytes
	CHECK_STR (dst, "aé");
	r2d_utf8_copy (dst, sizeof dst, "abé"); // 'é' would be cut
	CHECK_STR (dst, "ab");
}

static void test_json(void) {
	const char *evil = "q\"b\\s\nnl ü";
	char *js = r2d_activity_json (evil, "At the prompt", 1700000000, "r2", 42, 7);
	RJson *root = r_json_parsedup (js);
	CHECK (root != NULL);
	if (root) {
		CHECK_STR (r_json_get_str (root, "cmd"), "SET_ACTIVITY");
		CHECK_STR (r_json_get_str (root, "nonce"), "7");
		const RJson *args = r_json_get (root, "args");
		CHECK (args && r_json_get_num (args, "pid") == 42);
		const RJson *act = args? r_json_get (args, "activity"): NULL;
		CHECK (act && act->type == R_JSON_OBJECT);
		if (act && act->type == R_JSON_OBJECT) {
			CHECK_STR (r_json_get_str (act, "details"), evil);
			CHECK_STR (r_json_get_str (act, "state"), "At the prompt");
			const RJson *ts = r_json_get (act, "timestamps");
			CHECK (ts && r_json_get_num (ts, "start") == 1700000000);
			const RJson *as = r_json_get (act, "assets");
			CHECK (as && !strcmp (r_json_get_str (as, "large_image"), "r2"));
			CHECK_STR (as? r_json_get_str (as, "large_text"): NULL, "radare2 " R2_VERSION);
		}
		r_json_free (root);
	}
	free (js);

	js = r2d_activity_json (NULL, NULL, 0, NULL, 42, 8);
	root = r_json_parsedup (js);
	const RJson *args = root? r_json_get (root, "args"): NULL;
	const RJson *act = args? r_json_get (args, "activity"): NULL;
	CHECK (act && act->type == R_JSON_NULL);
	r_json_free (root);
	free (js);
}

int main(void) {
	test_modes_hidden ();
	test_modes_shown ();
	test_stats ();
	test_labels ();
	test_truncation ();
	test_json ();
	return T_END ();
}
