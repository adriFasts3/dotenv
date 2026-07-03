/* dotenv -- a minimal, dependency-free .env parser

SPDX-License-Identifier: BSD-3-Clause

Copyright (C) 2026, Adriana Castro <adriana.castro@fasts3.io>

Parse .env data in the spirit of Node.js' built-in dotenv support
(https://nodejs.org/api/environment_variables.html#dotenv), 
Extended with shell-style "$VAR" / "${VAR}" expansion resolved through a
caller-supplied callback.

*/

#ifndef DOTENV_H
#define DOTENV_H

#define DOTENV_VERSION "0.1.0"
#define DOTENV_VERSION_NUMBER 100
#define DOTENV_VERSION_MAJOR 0
#define DOTENV_VERSION_MINOR 1
#define DOTENV_VERSION_PATCH 0

#include <stdbool.h>
#include <stddef.h>

/* Look up the current value of the variable named `key`, used to resolve a
   "$key" / "${key}" reference while parsing. Return the value (kept only for
   the duration of the call; the parser copies what it needs) or NULL when the
   name is unknown, in which case the reference expands to the empty string.
   `user` is the opaque pointer handed to dotenv_init.

   A NULL get callback disables expansion: "$" is then left verbatim. */
typedef const char *(*DotEnvGet)(void *user, const char *key);

/* Store one parsed KEY=VALUE pair. `key` and `value` are NUL-terminated and
   valid only for the duration of the call (the callback must copy them if it
   keeps them). Return true to continue parsing, false to abort the parse with
   DOTENV_ERROR_SET. `user` is the opaque pointer handed to dotenv_init. */
typedef bool (*DotEnvSet)(void *user, const char *key, const char *value);

typedef enum {
	DOTENV_OK = 0,
	DOTENV_ERROR_INVALID_ARGS = 1,   /* NULL de/string or missing set */
	DOTENV_ERROR_INVALID_LINE = 2,   /* non-blank line without a KEY= */
	DOTENV_ERROR_UNTERMINATED_QUOTE = 3,
	DOTENV_ERROR_SET = 4,            /* set callback returned false */
	DOTENV_ERROR_NOMEM = 5,          /* expansion buffer allocation failed */
	DOTENV_ERROR_UNKNOWN = 6,
} DotEnvError;

typedef struct DotEnv {
	DotEnvGet get;      /* optional; NULL disables "$VAR" expansion */
	DotEnvSet set;      /* required */
	void *user;         /* opaque, forwarded to get/set */
	DotEnvError err;    /* parse status after dotenv_parse */
	int lineno;         /* line where parsing stopped (1-based) */
} DotEnv;

/* Visibility for exported symbols (paired with a "hidden" default visibility
   in the build so internal helpers stay private). */
#ifndef DOTENV_API
#if defined(__GNUC__) && __GNUC__ >= 4
#define DOTENV_API __attribute__ ((visibility ("default")))
#else
#define DOTENV_API
#endif
#endif

/* Build a DotEnv value wiring the get/set callbacks and the opaque user
   pointer. `set` is required; `get` may be NULL to disable expansion. The
   returned value is trivially copyable and owns no resources. */
DOTENV_API DotEnv dotenv_init(DotEnvGet get, DotEnvSet set, void *user);

/* Parse a zero-terminated, writable string of .env data, invoking de->set for
   each KEY=VALUE pair. The buffer is parsed and modified in place: key and
   value tokens are NUL-terminated within `string`, so it must be writable and
   must outlive the callbacks.

   Recognized syntax, per line:
     - blank lines and full-line '#' comments are ignored;
     - an optional shell-style "export " prefix on the key is dropped;
     - whitespace around KEY and around VALUE is trimmed;
     - a VALUE in single quotes is taken verbatim (no expansion, no escapes);
     - a VALUE in double quotes may span lines, honours the escapes
       \n \r \t \\ \" \$ and expands "$VAR" / "${VAR}" via de->get;
     - an unquoted VALUE ends at end of line or an inline '#' comment (a '#'
       at value start or preceded by whitespace), expands "$VAR" / "${VAR}"
       and honours \$ to emit a literal '$';
     - an unknown "$name" (or NULL from get) expands to the empty string.

   Before calling, set de->set (required), de->get (optional) and de->user.
   On return de->lineno holds the line at which parsing stopped and de->err
   holds the status code. Returns true on success, false on the first error
   (see DotEnvError) or when de->set returns false. */
DOTENV_API bool dotenv_parse(DotEnv *de, char *string);

#endif /* DOTENV_H */
