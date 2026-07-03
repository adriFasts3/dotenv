/* Self-contained test harness for dotenv. Each case parses a .env string and
 * compares the resulting KEY=VALUE pairs (and error code) against expectations.
 * Exits non-zero on the first failure so CTest reports it. */

#include "dotenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Record parsed pairs into a fixed flat array so a test can assert on them. */
#define MAX_PAIRS 64
typedef struct {
	char *k[MAX_PAIRS];
	char *v[MAX_PAIRS];
	int n;
	bool fail_at;      /* if set, set() returns false on this key */
	const char *fail_key;
} Store;

static const char *store_get(void *user, const char *key) {
	Store *s = user;
	for (int i = s->n - 1; i >= 0; i--) {   /* last write wins */
		if (strcmp (s->k[i], key) == 0) {
			return s->v[i];
		}
	}
	return NULL;
}

static bool store_set(void *user, const char *key, const char *value) {
	Store *s = user;
	if (s->fail_at && s->fail_key && strcmp (key, s->fail_key) == 0) {
		return false;
	}
	if (s->n >= MAX_PAIRS) {
		return false;
	}
	s->k[s->n] = strdup (key);
	s->v[s->n] = strdup (value);
	s->n++;
	return true;
}

static void store_free(Store *s) {
	for (int i = 0; i < s->n; i++) {
		free (s->k[i]);
		free (s->v[i]);
	}
}

static int failures = 0;

/* Assert that parsing `input` (with or without expansion) yields exactly the
 * pipe-separated "K=V|K=V" pairs in `expect`, and the given return + error. */
static void check(const char *name, const char *input, bool with_get,
		bool expect_ret, DotEnvError expect_err, const char *expect) {
	char *buf = strdup (input);
	Store s = {0};
	DotEnv de = dotenv_init (with_get ? store_get : NULL, store_set, &s);

	bool ret = dotenv_parse (&de, buf);

	char got[1024];
	got[0] = '\0';
	for (int i = 0; i < s.n; i++) {
		if (i) {
			strncat (got, "|", sizeof (got) - strlen (got) - 1);
		}
		strncat (got, s.k[i], sizeof (got) - strlen (got) - 1);
		strncat (got, "=", sizeof (got) - strlen (got) - 1);
		strncat (got, s.v[i], sizeof (got) - strlen (got) - 1);
	}

	bool ok = (ret == expect_ret) && (de.err == expect_err)
			&& (strcmp (got, expect) == 0);
	if (!ok) {
		failures++;
		printf ("FAIL %s\n", name);
		printf ("  ret=%d (want %d)  err=%d (want %d)\n",
				ret, expect_ret, de.err, expect_err);
		printf ("  got   : [%s]\n", got);
		printf ("  expect: [%s]\n", expect);
	} else {
		printf ("ok   %s\n", name);
	}

	store_free (&s);
	free (buf);
}

int main(void) {
	/* --- basic key/value --- */
	check ("simple", "FOO=bar\n", false, true, DOTENV_OK, "FOO=bar");
	check ("no trailing newline", "FOO=bar", false, true, DOTENV_OK, "FOO=bar");
	check ("multiple", "A=1\nB=2\nC=3\n", false, true, DOTENV_OK,
			"A=1|B=2|C=3");
	check ("empty value", "FOO=\n", false, true, DOTENV_OK, "FOO=");
	check ("whitespace trim", "  FOO  =  bar  \n", false, true, DOTENV_OK,
			"FOO=bar");
	check ("value with spaces (unquoted trims ends)", "FOO=a b c\n", false,
			true, DOTENV_OK, "FOO=a b c");

	/* --- comments and blanks --- */
	check ("blank lines", "\n\nFOO=bar\n\n", false, true, DOTENV_OK, "FOO=bar");
	check ("full-line comment", "# c\nFOO=bar\n", false, true, DOTENV_OK,
			"FOO=bar");
	check ("indented comment", "   # c\nFOO=bar\n", false, true, DOTENV_OK,
			"FOO=bar");
	check ("inline comment", "FOO=bar # note\n", false, true, DOTENV_OK,
			"FOO=bar");
	check ("hash without space kept", "FOO=a#b\n", false, true, DOTENV_OK,
			"FOO=a#b");
	check ("value is only a comment", "FOO= # c\n", false, true, DOTENV_OK,
			"FOO=");

	/* --- export prefix --- */
	check ("export prefix", "export FOO=bar\n", false, true, DOTENV_OK,
			"FOO=bar");
	check ("export not a false match", "exported=1\n", false, true, DOTENV_OK,
			"exported=1");

	/* --- quoting --- */
	check ("double quotes", "FOO=\"a b\"\n", false, true, DOTENV_OK, "FOO=a b");
	check ("single quotes", "FOO='a b'\n", false, true, DOTENV_OK, "FOO=a b");
	check ("double quotes keep hash", "FOO=\"a#b\"\n", false, true, DOTENV_OK,
			"FOO=a#b");
	check ("double quote comment after", "FOO=\"a\" # c\n", false, true,
			DOTENV_OK, "FOO=a");
	check ("empty double quotes", "FOO=\"\"\n", false, true, DOTENV_OK, "FOO=");
	check ("escape newline in dquote", "FOO=\"a\\nb\"\n", false, true,
			DOTENV_OK, "FOO=a\nb");
	check ("escape tab in dquote", "FOO=\"a\\tb\"\n", false, true, DOTENV_OK,
			"FOO=a\tb");
	check ("single quotes literal backslash", "FOO='a\\nb'\n", false, true,
			DOTENV_OK, "FOO=a\\nb");
	check ("multiline double quote", "FOO=\"line1\nline2\"\nBAR=x\n", false,
			true, DOTENV_OK, "FOO=line1\nline2|BAR=x");

	/* --- expansion (get callback enabled) --- */
	check ("expand later line", "FOO=123\nBAR=$FOO/x\n", true, true, DOTENV_OK,
			"FOO=123|BAR=123/x");
	check ("expand self-reference", "FOO=123\nFOO=$FOO+BAR\n", true, true,
			DOTENV_OK, "FOO=123|FOO=123+BAR");
	check ("expand braces", "FOO=123\nBAR=${FOO}9\n", true, true, DOTENV_OK,
			"FOO=123|BAR=1239");
	check ("expand in double quotes", "FOO=1\nBAR=\"v=$FOO\"\n", true, true,
			DOTENV_OK, "FOO=1|BAR=v=1");
	check ("no expand in single quotes", "FOO=1\nBAR='v=$FOO'\n", true, true,
			DOTENV_OK, "FOO=1|BAR=v=$FOO");
	check ("unknown expands empty", "BAR=[$NOPE]\n", true, true, DOTENV_OK,
			"BAR=[]");
	check ("escaped dollar", "BAR=\\$FOO\n", true, true, DOTENV_OK,
			"BAR=$FOO");
	check ("lone dollar literal", "BAR=a$ b\n", true, true, DOTENV_OK,
			"BAR=a$ b");
	check ("expansion disabled without get", "FOO=1\nBAR=$FOO\n", false, true,
			DOTENV_OK, "FOO=1|BAR=$FOO");

	/* --- tricky / edge cases --- */
	/* only the first '=' splits key from value */
	check ("value contains equals", "KEY=a=b=c\n", false, true, DOTENV_OK,
			"KEY=a=b=c");
	check ("value contains spaced equals", "EXPR=a = b\n", false, true,
			DOTENV_OK, "EXPR=a = b");
	check ("realistic url with = : / @ ?",
			"export DB=postgres://u:p@h/db?x=1\n", false, true, DOTENV_OK,
			"DB=postgres://u:p@h/db?x=1");
	/* keys are taken verbatim: dots and dashes are fine */
	check ("key with dots and dashes", "my.key-1=v\n", false, true, DOTENV_OK,
			"my.key-1=v");
	check ("export separated by a tab", "export\tFOO=bar\n", false, true,
			DOTENV_OK, "FOO=bar");

	/* CRLF (Windows) line endings: the trailing CR must not leak in */
	check ("crlf line endings", "A=1\r\nB=2\r\n", false, true, DOTENV_OK,
			"A=1|B=2");
	check ("crlf empty value", "A=\r\nB=2\r\n", false, true, DOTENV_OK,
			"A=|B=2");
	check ("crlf with inline comment", "A=1 # c\r\nB=2\r\n", false, true,
			DOTENV_OK, "A=1|B=2");
	check ("crlf quoted value", "A=\"x\"\r\nB=2\r\n", false, true, DOTENV_OK,
			"A=x|B=2");

	/* a leading UTF-8 BOM is skipped (split literal so \xBF doesn't eat 'F') */
	check ("utf-8 bom", "\xEF\xBB\xBF" "FOO=bar\n", false, true, DOTENV_OK,
			"FOO=bar");

	/* whitespace: trimmed when unquoted, preserved inside quotes */
	check ("whitespace preserved in dquotes", "X=\"  s  \"\n", false, true,
			DOTENV_OK, "X=  s  ");
	check ("tab before inline comment", "FOO=bar\t# c\n", false, true,
			DOTENV_OK, "FOO=bar");
	/* '#' is literal inside single quotes */
	check ("hash inside single quotes", "X='a # b'\n", false, true, DOTENV_OK,
			"X=a # b");

	/* double-quote escapes for the quote and backslash themselves */
	check ("escaped quote in dquotes", "X=\"a\\\"b\"\n", false, true,
			DOTENV_OK, "X=a\"b");
	check ("escaped backslash in dquotes", "X=\"a\\\\b\"\n", false, true,
			DOTENV_OK, "X=a\\b");

	/* --- tricky expansion (get enabled) --- */
	check ("adjacent bare expansions", "A=1\nB=2\nC=$A$B\n", true, true,
			DOTENV_OK, "A=1|B=2|C=12");
	check ("adjacent brace expansions", "A=x\nB=y\nC=${A}${B}\n", true, true,
			DOTENV_OK, "A=x|B=y|C=xy");
	check ("scheme + host expansion", "S=https\nH=x\nU=$S://$H\n", true, true,
			DOTENV_OK, "S=https|H=x|U=https://x");
	check ("empty braces expand empty", "X=${}\n", true, true, DOTENV_OK, "X=");
	check ("unclosed brace is literal", "X=${FOO\n", true, true, DOTENV_OK,
			"X=${FOO");
	check ("bare trailing dollar", "X=$\n", true, true, DOTENV_OK, "X=$");
	check ("escaped dollar in dquotes", "A=1\nX=\"\\$A\"\n", true, true,
			DOTENV_OK, "A=1|X=$A");
	/* the result of an expansion is NOT expanded again */
	check ("no recursive expansion", "A='$B'\nB=2\nC=$A\n", true, true,
			DOTENV_OK, "A=$B|B=2|C=$B");

	/* --- errors --- */
	check ("missing equals", "FOOBAR\n", false, false, DOTENV_ERROR_INVALID_LINE,
			"");
	check ("empty key", "=bar\n", false, false, DOTENV_ERROR_INVALID_LINE, "");
	check ("unterminated double quote", "FOO=\"abc\n", false, false,
			DOTENV_ERROR_UNTERMINATED_QUOTE, "");
	check ("unterminated single quote", "FOO='abc\n", false, false,
			DOTENV_ERROR_UNTERMINATED_QUOTE, "");

	/* --- set callback abort --- */
	{
		char *buf = strdup ("A=1\nB=2\nC=3\n");
		Store s = {0};
		s.fail_at = true;
		s.fail_key = "B";
		DotEnv de = dotenv_init (NULL, store_set, &s);
		bool ret = dotenv_parse (&de, buf);
		bool ok = !ret && de.err == DOTENV_ERROR_SET && de.lineno == 2
				&& s.n == 1;
		printf ("%s set-abort (ret=%d err=%d line=%d n=%d)\n",
				ok ? "ok  " : "FAIL", ret, de.err, de.lineno, s.n);
		if (!ok) {
			failures++;
		}
		store_free (&s);
		free (buf);
	}

	/* --- invalid args --- */
	{
		DotEnv de = dotenv_init (NULL, NULL, NULL);
		char buf[] = "FOO=bar\n";
		bool ret = dotenv_parse (&de, buf);
		bool ok = !ret && de.err == DOTENV_ERROR_INVALID_ARGS;
		printf ("%s invalid-args\n", ok ? "ok  " : "FAIL");
		if (!ok) {
			failures++;
		}
	}

	/* --- lineno on error --- */
	{
		char buf[] = "A=1\nB=2\nBROKEN\nC=3\n";
		Store s = {0};
		DotEnv de = dotenv_init (NULL, store_set, &s);
		bool ret = dotenv_parse (&de, buf);
		bool ok = !ret && de.err == DOTENV_ERROR_INVALID_LINE
				&& de.lineno == 3;
		printf ("%s lineno (line=%d)\n", ok ? "ok  " : "FAIL", de.lineno);
		if (!ok) {
			failures++;
		}
		store_free (&s);
	}

	if (failures) {
		printf ("\n%d test(s) FAILED\n", failures);
		return 1;
	}
	printf ("\nall tests passed\n");
	return 0;
}
