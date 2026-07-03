/* env_to_kvs -- turn a .env file into a flat key/value array.
 *
 * Demonstrates the intended use of dotenv: the parser stays agnostic about
 * storage; the caller's get/set callbacks build whatever it needs. Here they
 * build a "kvs": a NULL-terminated char* array shaped as
 *
 *     ["K1", "V1", "K2", "V2", ..., NULL]
 *
 * The get callback resolves "$VAR" references against the kvs first and then
 * against the process environment, so a later line can expand an earlier one
 * (e.g. FOO=123 then BAR=$FOO/x), exactly like the dotenv.txt sketch.
 *
 * Usage: env_to_kvs [file.env]   (reads stdin when no file is given)
 */

#include "dotenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A minimal growable kvs. Only what the example needs; not part of the lib. */
typedef struct {
	char **arr;   /* NULL-terminated: [k0, v0, k1, v1, ..., NULL] */
	size_t len;   /* stored strings, excluding the NULL terminator */
	size_t cap;   /* allocated slots, including the terminator slot */
} Kvs;

static bool kvs_reserve(Kvs *kvs, size_t extra) {
	if (kvs->len + extra + 1 <= kvs->cap) {
		return true;
	}
	size_t ncap = kvs->cap ? kvs->cap * 2 : 8;
	while (ncap < kvs->len + extra + 1) {
		ncap *= 2;
	}
	char **tmp = realloc (kvs->arr, ncap * sizeof *tmp);
	if (!tmp) {
		return false;
	}
	kvs->arr = tmp;
	kvs->cap = ncap;
	return true;
}

/* Index of `key`'s value slot, or SIZE_MAX if absent. */
static size_t kvs_index(const Kvs *kvs, const char *key) {
	for (size_t i = 0; i + 1 < kvs->len; i += 2) {
		if (strcmp (kvs->arr[i], key) == 0) {
			return i + 1;
		}
	}
	return (size_t) -1;
}

/* dotenv get callback: kvs first, then the process environment. */
static const char *kvs_get(void *user, const char *key) {
	Kvs *kvs = user;
	size_t vi = kvs_index (kvs, key);
	if (vi != (size_t) -1) {
		return kvs->arr[vi];
	}
	return getenv (key);
}

/* dotenv set callback: last write wins, so the array stays duplicate-free. */
static bool kvs_set(void *user, const char *key, const char *value) {
	Kvs *kvs = user;

	size_t vi = kvs_index (kvs, key);
	if (vi != (size_t) -1) {
		char *copy = strdup (value);
		if (!copy) {
			return false;
		}
		free (kvs->arr[vi]);
		kvs->arr[vi] = copy;
		return true;
	}

	if (!kvs_reserve (kvs, 2)) {
		return false;
	}
	char *k = strdup (key);
	char *v = strdup (value);
	if (!k || !v) {
		free (k);
		free (v);
		return false;
	}
	kvs->arr[kvs->len++] = k;
	kvs->arr[kvs->len++] = v;
	kvs->arr[kvs->len] = NULL;
	return true;
}

static void kvs_free(Kvs *kvs) {
	for (size_t i = 0; i < kvs->len; i++) {
		free (kvs->arr[i]);
	}
	free (kvs->arr);
}

/* Read a whole stream into a freshly malloc'd, NUL-terminated buffer. */
static char *slurp(FILE *f) {
	size_t cap = 4096, len = 0;
	char *buf = malloc (cap);
	if (!buf) {
		return NULL;
	}
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			char *tmp = realloc (buf, cap);
			if (!tmp) {
				free (buf);
				return NULL;
			}
			buf = tmp;
		}
		size_t n = fread (buf + len, 1, cap - 1 - len, f);
		len += n;
		if (n == 0) {
			break;
		}
	}
	buf[len] = '\0';
	return buf;
}

int main(int argc, char **argv) {
	FILE *f = stdin;
	if (argc > 1) {
		f = fopen (argv[1], "rb");
		if (!f) {
			fprintf (stderr, "env_to_kvs: cannot open %s\n", argv[1]);
			return 1;
		}
	}

	char *text = slurp (f);
	if (f != stdin) {
		fclose (f);
	}
	if (!text) {
		fprintf (stderr, "env_to_kvs: out of memory\n");
		return 1;
	}

	Kvs kvs = {0};
	DotEnv de = dotenv_init (kvs_get, kvs_set, &kvs);

	int rc = 0;
	if (!dotenv_parse (&de, text)) {
		fprintf (stderr, "env_to_kvs: parse error %d at line %d\n",
				de.err, de.lineno);
		rc = 1;
	} else {
		for (size_t i = 0; i + 1 < kvs.len; i += 2) {
			printf ("%s=%s\n", kvs.arr[i], kvs.arr[i + 1]);
		}
	}

	kvs_free (&kvs);
	free (text);
	return rc;
}
