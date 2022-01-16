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

int32_t u_file_read_safe(UChar *buf, int32_t n, UFILE *f)
{
	int32_t ret = u_file_read(buf, n-1, f);

	/* if final code unit is an unpaired surrogate,
	 * put it back for the next read */
	if (ret > 0 && U16_IS_LEAD(buf[ret-1]) && !u_feof(f))
		u_fungetc(buf[--ret], f);
	buf[ret] = '\0';
	return ret;
}

void u_unlines(UChar *buf, int32_t bufsz, UErrorCode *status)
{
	static URegularExpression *newlines_re;
	static UChar space[] = {' '};

	if (!newlines_re)
	{
		newlines_re = uregex_openC(
			"[\n\r\u2028]+", 0, NULL, status);
		u_assert(*status, "newlines_re");
	}
	uregex_setText(newlines_re, buf, -1, status);
	u_assert(*status, "uregex_setText");
	uregex_replaceAll(newlines_re, space, 1, buf, bufsz, status);
}

int main(void)
{
	UFILE *in, *out;
	char *locale;

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

	int32_t len;
	UChar buf[BUFSIZ];
	while ((len = u_file_read_safe(buf, BUFSIZ, in)) > 0)
	{
		UErrorCode status = U_ZERO_ERROR;
		u_unlines(buf, BUFSIZ, &status);
		u_assert(status, "u_unlines");
		u_fprintf(out, "%.*S", len, buf);
	}
	u_fclose(in);
	return 0;
}
