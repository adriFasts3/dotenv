# dotenv

A minimal, dependency-free `.env` parser in C, in the spirit of Node.js'
built-in [dotenv support](https://nodejs.org/api/environment_variables.html#dotenv),
extended with shell-style `$VAR` / `${VAR}` expansion.

Design goals:

- **Small and self-contained.** One `.c`/`.h` pair, no external dependencies.
- **No hidden state.** No globals, no `FILE*`: the parser works on a
  caller-provided, writable `char*` and reports status through the `DotEnv`
  struct.
- **Heap-frugal.** The buffer is parsed in place; the heap is touched only when
  a value actually needs `$VAR` expansion or escape processing, so a plain
  `.env` file parses with zero allocations.
- **Storage-agnostic.** You provide two callbacks — `get` (to resolve `$VAR`
  references) and `set` (to store each pair) — so the parser never dictates
  where the data lives.

## API

```c
#include "dotenv.h"

const char *my_get(void *user, const char *key);          /* resolve $VAR   */
bool        my_set(void *user, const char *k, const char *v); /* store a pair */

DotEnv de = dotenv_init (my_get, my_set, my_user_ptr);

char buf[] = "FOO=123\nBAR=$FOO/x\n";   /* must be writable */
if (!dotenv_parse (&de, buf)) {
    fprintf (stderr, "parse error %d at line %d\n", de.err, de.lineno);
}
```

### Functions

```c
DotEnv dotenv_init (DotEnvGet get, DotEnvSet set, void *user);
```

Builds a `DotEnv` value wiring the two callbacks and the opaque `user` pointer
(forwarded to both callbacks). `set` is required; `get` may be `NULL` to disable
expansion. The returned value is trivially copyable and owns no resources.

```c
bool dotenv_parse (DotEnv *de, char *string);
```

Parses a zero-terminated, **writable** `string` of `.env` data in place,
invoking `de->set` once per `KEY=VALUE` pair. Key and value tokens are
NUL-terminated inside `string`, so it must be writable and must outlive the
callbacks. Returns `true` on success, or `false` on the first error; either way
`de->err` holds the status (a `DotEnvError`) and `de->lineno` the 1-based line
at which parsing stopped.

### Callbacks

```c
const char *my_get (void *user, const char *key);
```

Resolves a `$key` / `${key}` reference to its current value. Return the value
(read-only, needed only for the duration of the call) or `NULL` when the name is
unknown, in which case the reference expands to the empty string.

```c
bool my_set (void *user, const char *key, const char *value);
```

Stores one parsed pair. `key` and `value` are NUL-terminated and valid only for
the duration of the call, so copy them if you keep them. Return `true` to
continue parsing, or `false` to abort with `DOTENV_ERROR_SET`.

### Status codes

`de->err` is one of:

| Code | Meaning |
| --- | --- |
| `DOTENV_OK` | parse succeeded |
| `DOTENV_ERROR_INVALID_ARGS` | `NULL` `de`/`string`, or missing `set` |
| `DOTENV_ERROR_INVALID_LINE` | non-blank line without a `KEY=` |
| `DOTENV_ERROR_UNTERMINATED_QUOTE` | a quote was never closed |
| `DOTENV_ERROR_SET` | the `set` callback returned `false` |
| `DOTENV_ERROR_NOMEM` | expansion buffer allocation failed |

## `.env` file syntax

This is the grammar of the `.env` data `dotenv_parse` accepts — the file
format, not the C API. Each line is one `KEY=VALUE` pair, interpreted as
follows:

- blank lines and full-line `#` comments are ignored;
- a leading UTF-8 BOM and CRLF (`\r\n`) line endings are handled;
- an optional `export ` prefix on the key is dropped;
- the key runs up to the **first** `=`, so the value may itself contain `=`;
- the key is otherwise taken verbatim (e.g. dots and dashes are allowed) but
  may not be empty;
- whitespace around the key and around the value is trimmed;
- the value may be empty (`KEY=`);
- `'single quotes'` are literal — no expansion, no escapes;
- `"double quotes"` may span lines, honour `\n \r \t \\ \" \$` (an unknown
  escape keeps its backslash), and expand `$VAR` / `${VAR}`;
- an unquoted value ends at end of line or an inline `#` comment (a `#` at the
  value start or preceded by whitespace; a `#` glued to other text, like
  `a#b`, is kept), expands `$VAR` / `${VAR}`, and honours `\$` for a literal
  `$`;
- an unknown variable (or `NULL` from `get`) expands to the empty string.

A non-blank line without a `=`, or an unterminated quote, stops parsing with an
error in `de.err` and the line number in `de.lineno`.

[`src/examples/tricky.env`](src/examples/tricky.env) is an annotated file that
exercises all of the above; run it through the `env_to_kvs` example (see
[Building](#building)) to see how each line is parsed.

## Building

```sh
make                # build the library (and examples)
make test           # build and run the test suite
make install        # install libs, header and pkg-config file
make dist           # -> build/dist/dotenv-<version>.tar.gz
make clean          # remove the build directory
```

The `env_to_kvs` example turns a `.env` file into a flat
`["K1","V1","K2","V2",...]` key/value array:

```sh
make examples
./build/src/examples/env_to_kvs src/examples/tricky.env
```

## License

BSD-3-Clause. See [LICENSE.txt](LICENSE.txt).
