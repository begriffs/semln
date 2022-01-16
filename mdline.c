#include <locale.h>
#include <stdint.h>
#include <stdlib.h>

#include <unicode/ubrk.h>
#include <unicode/ustdio.h>
#include <unicode/uregex.h>
#include <unicode/ustring.h>

void u_assert(UErrorCode status, char *loc)
{
	if (U_FAILURE(status)) {
		fprintf(stderr, "%s: %s\n", loc, u_errorName(status));
		exit(EXIT_FAILURE);
	}
}

int32_t utext_file_read_safe(UText *ut, int32_t n, UFILE *f)
{
	UErrorCode status = U_ZERO_ERROR;
	UChar *buf = malloc(n * sizeof *buf);
	if (!buf)
		return -1;
	int32_t ret = u_file_read(buf, n, f);

	/* if final code unit is an unpaired surrogate,
	 * put it back for the next read */
	if (ret > 0 && U16_IS_LEAD(buf[ret-1]) && !u_feof(f))
		u_fungetc(buf[--ret], f);
	utext_openUChars(ut, buf, ret, &status);
	u_assert(status, "utext_openUChars");
	return ret;
}

UText *utext_unlines(UText *t, UErrorCode *status)
{
	static URegularExpression *newlines_re;
	static UText *space;
	if (!newlines_re)
	{
		newlines_re = uregex_openC(
			"[\\n\\r\\u2028]+", 0, NULL, status);
		u_assert(*status, "newlines_re");
	}
	if (!space)
	{
		space = utext_openUTF8(NULL, " ", -1, status);
		u_assert(*status, "utext_openUTF8");
	}
	uregex_setUText(newlines_re, t, status);
	u_assert(*status, "uregex_setUText");
	return uregex_replaceAllUText(newlines_re, space, t, status);
}

int32_t utext_fputs(UText *t, UFILE *f)
{
	int32_t len;
	UChar *native;
	UErrorCode status = U_ZERO_ERROR;

	/* preflight to get len */
	len = utext_extract(t, 0, INT32_MAX, NULL, 0, &status);
	status = U_ZERO_ERROR;
	native = malloc((len+1) * sizeof *native);
	if (!native)
		return -1;
	len = utext_extract(t, 0, INT32_MAX, native, len+1, &status);
	u_assert(status, "utext_extract");
	len = u_fputs(native, f);
	free(native);
	return len;
}

int main(void)
{
	UFILE *in, *out;
	char *locale;
	//UBreakIterator *brk;
	int32_t len;

	/* sentence breaks are locale-specific, so we'll obtain
	 * LC_CTYPE from the environment */
	if (!(locale = setlocale(LC_CTYPE, "")))
	{
		fputs("Cannot determine system locale\n", stderr);
		return EXIT_FAILURE;
	}

	if (!(in = u_finit(stdin, NULL, NULL)))
	{
		fputs("Error opening stdin as UFILE\n", stderr);
		return EXIT_FAILURE;
	}
	out = u_get_stdout();

	UText chunk = UTEXT_INITIALIZER,
		  buf = UTEXT_INITIALIZER;
	while ((len = utext_file_read_safe(&chunk, BUFSIZ, in)) > 0)
	{
		UErrorCode status = U_ZERO_ERROR;

		utext_clone(&buf, &chunk, TRUE, FALSE, &status);
		u_assert(status, "utext_clone");

		utext_unlines(&buf, &status);
		u_assert(status, "u_unlines");
		utext_fputs(&buf, out);
	}
	utext_close(&chunk);
	utext_close(&buf);
	u_fclose(in);
	return 0;
}
