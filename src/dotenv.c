/* dotenv -- a minimal, dependency-free .env parser

SPDX-License-Identifier: BSD-3-Clause

Copyright (C) 2026, Adriana Castro <adriana.castro@fasts3.io>

See dotenv.h for the public contract.

*/

#include "dotenv.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Growable output buffer for an expanded value. It is allocated lazily: a
   value that needs neither "$VAR" expansion nor escape processing is handed to
   the caller straight out of the source buffer, so a plain .env file parses
   without touching the heap at all. */
typedef struct {
	char *buf;
	size_t len;
	size_t cap;
} Scratch;

/* Ensure `extra` more bytes (plus the trailing NUL) fit; grow if needed. The
   size arithmetic is overflow-checked so a pathologically large request can
   never wrap into a short allocation or spin the doubling loop forever. */
static bool scratch_reserve(Scratch *sc, size_t extra) {
	if (extra > SIZE_MAX - 1 - sc->len) {
		return false;   /* sc->len + extra + 1 would overflow size_t */
	}
	size_t need = sc->len + extra + 1;
	if (need <= sc->cap) {
		return true;
	}
	size_t ncap = sc->cap ? sc->cap : 64;
	while (ncap < need) {
		if (ncap > SIZE_MAX / 2) {
			ncap = need;   /* doubling would overflow; take exactly what we need */
			break;
		}
		ncap *= 2;
	}
	char *tmp = realloc (sc->buf, ncap);
	if (!tmp) {
		return false;
	}
	sc->buf = tmp;
	sc->cap = ncap;
	return true;
}

static bool scratch_put(Scratch *sc, const char *s, size_t n) {
	if (!scratch_reserve (sc, n)) {
		return false;
	}
	memcpy (sc->buf + sc->len, s, n);
	sc->len += n;
	return true;
}

static bool scratch_putc(Scratch *sc, char c) {
	return scratch_put (sc, &c, 1);
}

/* Spaces and tabs only: used to trim within a line without crossing newlines. */
static bool is_inline_space(char c) {
	return c == ' ' || c == '\t';
}

static bool is_name_start(char c) {
	return isalpha ((unsigned char) c) || c == '_';
}

static bool is_name_char(char c) {
	return isalnum ((unsigned char) c) || c == '_';
}

/* Append the value of a "$NAME" / "${NAME}" reference to sc by looking the
   name up through de->get. `p` points at the character just after the '$';
   `end` bounds the enclosing value. Returns the pointer just past the parsed
   reference (or past a lone '$' emitted verbatim), or NULL on allocation
   failure. The name is NUL-terminated in place across the get call and then
   restored, so the source buffer is left unchanged. */
static char *expand(DotEnv *de, char *p, char *end, Scratch *sc) {
	if (p < end && *p == '{') {
		char *name = p + 1;
		char *close = memchr (name, '}', (size_t) (end - name));
		if (!close) {
			return scratch_putc (sc, '$') ? p : NULL;
		}
		char saved = *close;
		*close = '\0';
		const char *val = de->get (de->user, name);
		*close = saved;
		if (val && !scratch_put (sc, val, strlen (val))) {
			return NULL;
		}
		return close + 1;
	}

	if (p < end && is_name_start (*p)) {
		char *ne = p;
		while (ne < end && is_name_char (*ne)) {
			ne++;
		}
		char saved = *ne;
		*ne = '\0';
		const char *val = de->get (de->user, p);
		*ne = saved;
		if (val && !scratch_put (sc, val, strlen (val))) {
			return NULL;
		}
		return ne;
	}

	/* A '$' that starts no valid reference is emitted literally. */
	return scratch_putc (sc, '$') ? p : NULL;
}

/* Build the processed value from src[0..len) into sc. `escapes` enables the
   backslash escapes of double-quoted values; otherwise only "\$" is honoured
   (unquoted values). "$VAR" expansion runs when de->get is non-NULL. Returns
   false on allocation failure. */
static bool build_value(DotEnv *de, char *src, size_t len, bool escapes,
		Scratch *sc) {
	sc->len = 0;
	char *p = src;
	char *end = src + len;

	while (p < end) {
		char c = *p;

		if (c == '\\' && p + 1 < end) {
			char n = p[1];
			if (escapes) {
				char out;
				switch (n) {
				case 'n': out = '\n'; break;
				case 'r': out = '\r'; break;
				case 't': out = '\t'; break;
				case '\\': out = '\\'; break;
				case '"': out = '"'; break;
				case '$': out = '$'; break;
				default:
					/* Keep an unknown escape verbatim (backslash + char). */
					if (!scratch_putc (sc, '\\')) {
						return false;
					}
					out = n;
					break;
				}
				if (!scratch_putc (sc, out)) {
					return false;
				}
				p += 2;
				continue;
			}
			if (n == '$') {
				/* Unquoted: "\$" escapes expansion. */
				if (!scratch_putc (sc, '$')) {
					return false;
				}
				p += 2;
				continue;
			}
			/* Other backslashes are literal in unquoted values. */
			if (!scratch_putc (sc, '\\')) {
				return false;
			}
			p++;
			continue;
		}

		if (c == '$' && de->get) {
			char *next = expand (de, p + 1, end, sc);
			if (!next) {
				return false;
			}
			p = next;
			continue;
		}

		if (!scratch_putc (sc, c)) {
			return false;
		}
		p++;
	}

	if (!scratch_reserve (sc, 0)) {
		return false;
	}
	sc->buf[sc->len] = '\0';
	return true;
}

/* Count newlines in [a, b). Used to keep de->lineno accurate across values
   that a branch consumes past the resume point. */
static int count_newlines(const char *a, const char *b) {
	int n = 0;
	for (; a < b; a++) {
		if (*a == '\n') {
			n++;
		}
	}
	return n;
}

/* Parse the value that begins at *pp. On success *out points at a
   NUL-terminated value (either in the source buffer or in sc), *pp is advanced
   to where the main loop should resume, and *nlines receives the number of
   newlines consumed before the resume point. Returns a DotEnvError. */
static DotEnvError parse_value(DotEnv *de, char **pp, Scratch *sc,
		const char **out, int *nlines) {
	char *p = *pp;
	*nlines = 0;

	if (*p == '\'') {
		/* Single-quoted: literal, no expansion or escapes. */
		char *start = p + 1;
		char *close = strchr (start, '\'');
		if (!close) {
			return DOTENV_ERROR_UNTERMINATED_QUOTE;
		}
		*nlines = count_newlines (start, close);
		*close = '\0';
		*out = start;
		char *nl = strchr (close + 1, '\n');
		*pp = nl ? nl : close + 1 + strlen (close + 1);
		return DOTENV_OK;
	}

	if (*p == '"') {
		/* Double-quoted: escapes and expansion; may span newlines. */
		char *start = p + 1;
		char *close = start;
		while (*close && *close != '"') {
			close += (*close == '\\' && close[1]) ? 2 : 1;
		}
		if (*close != '"') {
			return DOTENV_ERROR_UNTERMINATED_QUOTE;
		}
		size_t len = (size_t) (close - start);
		bool need = memchr (start, '\\', len) != NULL
				|| (de->get && memchr (start, '$', len) != NULL);
		if (need) {
			if (!build_value (de, start, len, true, sc)) {
				return DOTENV_ERROR_NOMEM;
			}
			*out = sc->buf;
		} else {
			*close = '\0';
			*out = start;
		}
		*nlines = count_newlines (start, close);
		char *nl = strchr (close + 1, '\n');
		*pp = nl ? nl : close + 1 + strlen (close + 1);
		return DOTENV_OK;
	}

	/* Unquoted: value runs to end of line or an inline '#' comment. */
	char *nl = strchr (p, '\n');
	char *lineend = nl ? nl : p + strlen (p);
	char *vend = lineend;
	for (char *c = p; c < lineend; c++) {
		if (*c == '#' && (c == p || is_inline_space (c[-1]))) {
			vend = c;
			break;
		}
	}
	while (vend > p && (is_inline_space (vend[-1]) || vend[-1] == '\r')) {
		vend--;   /* also drop a trailing CR so CRLF files parse cleanly */
	}

	size_t len = (size_t) (vend - p);
	bool need = de->get && memchr (p, '$', len) != NULL;
	if (need) {
		if (!build_value (de, p, len, false, sc)) {
			return DOTENV_ERROR_NOMEM;
		}
		*out = sc->buf;
	} else {
		*vend = '\0';   /* may overwrite the trailing newline; see below */
		*out = p;
	}

	/* Resume past the newline (it may have just been overwritten above) and
	   count it here so the main loop's leading skip does not miss it. */
	if (nl) {
		*pp = nl + 1;
		*nlines = 1;
	} else {
		*pp = lineend;
	}
	return DOTENV_OK;
}

DotEnv dotenv_init(DotEnvGet get, DotEnvSet set, void *user) {
	DotEnv de = {
		.get = get,
		.set = set,
		.user = user,
		.err = DOTENV_OK,
		.lineno = 0,
	};
	return de;
}

bool dotenv_parse(DotEnv *de, char *string) {
	if (!de || !string || !de->set) {
		if (de) {
			de->err = DOTENV_ERROR_INVALID_ARGS;
		}
		return false;
	}

	Scratch sc = {0};
	int lineno = 1;
	char *p = string;

	de->err = DOTENV_ERROR_UNKNOWN;
	de->lineno = 0;   /* reset so a reused struct never reports a stale line */

	/* Skip an optional UTF-8 BOM. */
	if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB
			&& (unsigned char) p[2] == 0xBF) {
		p += 3;
	}

	for (;;) {
		/* Skip blank lines, leading whitespace and full-line comments. */
		for (;;) {
			while (*p && isspace ((unsigned char) *p)) {
				if (*p == '\n') {
					lineno++;
				}
				p++;
			}
			if (*p == '#') {
				while (*p && *p != '\n') {
					p++;
				}
				continue;
			}
			break;
		}
		if (*p == '\0') {
			break;
		}

		de->lineno = lineno;

		/* Optional shell-style "export " prefix. */
		if (strncmp (p, "export", 6) == 0 && is_inline_space (p[6])) {
			p += 6;
			while (is_inline_space (*p)) {
				p++;
			}
		}

		/* Key: up to '=' on this line. */
		char *key = p;
		char *eq = NULL;
		for (char *c = p; *c && *c != '\n'; c++) {
			if (*c == '=') {
				eq = c;
				break;
			}
		}
		if (!eq) {
			de->err = DOTENV_ERROR_INVALID_LINE;
			goto fail;
		}

		char *kend = eq;
		while (kend > key && is_inline_space (kend[-1])) {
			kend--;
		}
		if (kend == key) {
			de->err = DOTENV_ERROR_INVALID_LINE;
			goto fail;
		}
		*kend = '\0';

		/* Value. */
		p = eq + 1;
		while (is_inline_space (*p)) {
			p++;
		}

		const char *value = NULL;
		int nlines = 0;
		DotEnvError verr = parse_value (de, &p, &sc, &value, &nlines);
		if (verr != DOTENV_OK) {
			de->err = verr;
			goto fail;
		}
		lineno += nlines;

		if (!de->set (de->user, key, value)) {
			de->err = DOTENV_ERROR_SET;
			goto fail;
		}
	}

	free (sc.buf);
	de->err = DOTENV_OK;
	return true;

fail:
	free (sc.buf);
	return false;
}
