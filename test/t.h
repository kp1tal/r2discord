/* r2discord - MIT - Copyright 2026 - kp1tal */
#ifndef R2D_T_H
#define R2D_T_H
#include <stdio.h>
#include <string.h>

static int t_fail = 0;

#define CHECK(c) do { if (!(c)) { \
	fprintf (stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); t_fail++; } } while (0)

#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); \
	if (!_a || !_b || strcmp (_a, _b)) { \
		fprintf (stderr, "%s:%d: expected \"%s\" got \"%s\"\n", __FILE__, __LINE__, \
			_b? _b: "(null)", _a? _a: "(null)"); t_fail++; } } while (0)

#define T_END() (t_fail \
	? (fprintf (stderr, "%s: %d failure(s)\n", __FILE__, t_fail), 1) \
	: (printf ("%s: OK\n", __FILE__), 0))

#endif
