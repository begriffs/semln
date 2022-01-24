#include <locale.h>
#include <stdint.h>
#include <stdlib.h>

#include <unicode/ubrk.h>
#include <unicode/uloc.h>
#include <unicode/ustdio.h>
#include <unicode/uregex.h>
#include <unicode/ustring.h>

char locale[100] = "en_US@ss=standard";

void u_assert(UErrorCode status, char *loc)
{
	if (U_FAILURE(status)) {
		fprintf(stderr, "%s: %s\n", loc, u_errorName(status));
		exit(EXIT_FAILURE);
	}
}

void init_locale(void)
{
	char *posix_loc;
	UErrorCode status;
	if (!(posix_loc = setlocale(LC_ALL, "")))
	{
		fputs("Cannot determine system locale.\n", stderr);
		fputs("HINT: set the LC_ALL environment variable.\n", stderr);
		exit(EXIT_FAILURE);
	}
	uloc_canonicalize(posix_loc, locale, sizeof locale, &status);
	u_assert(status, "uloc_canonicalize");

	/* use segmentation suppression (preventing sentence breaks in English
	 * after abbreviations such as "Mr." or "Est.", for example) */
	uloc_setKeywordValue("ss", "standard", locale, sizeof locale, &status);
	u_assert(status, "uloc_setKeywordValue");
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

	/* sentence breaks are locale-specific, so we'll our language
	 * from the environment, as specified in
	 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_02
	 */
	//init_locale();

	if (!(in = u_finit(stdin, NULL, NULL)))
	{
		fputs("Error opening stdin as UFILE\n", stderr);
		return EXIT_FAILURE;
	}
	out = u_get_stdout();

	UErrorCode status = U_ZERO_ERROR;
	UBreakIterator
		*brk = ubrk_open(UBRK_SENTENCE, locale, NULL, -1, &status);
	u_assert(status, "ubrk_open");


	/* at least two newlines or one paragraph separator in a row */
	static URegularExpression *para_re;
	para_re = uregex_openC(
		"(.+?)((\u2028|\n|\r\n){2,}|\u2029+|$)", UREGEX_DOTALL, NULL, &status);
	u_assert(status, "para_re");

	int32_t len;
	UChar buf[BUFSIZ], para[BUFSIZ];
	while ((len = u_file_read_safe(buf, BUFSIZ, in)) > 0)
	{
		int32_t type;
		uregex_setText(para_re, buf, len, &status);
		u_assert(status, "uregex_setText");

		/* iterate paragraphs */
		while (uregex_findNext(para_re, &status))
		{
			u_assert(status, "uregex_findNext");
			int32_t start = uregex_start(para_re, 1, &status),
			        end   = uregex_end(para_re, 1, &status);
			u_assert(status, "uregex_start/end");
			u_snprintf(para, end-start, "%S", buf + start);
			u_unlines(para, BUFSIZ, &status);
			//u_assert(status, "u_unlines");
			status = U_ZERO_ERROR;

			/* iterate sentences */
			ubrk_setText(brk, para, end-start, &status);
			u_assert(status, "ubrk_setText");
			int32_t from_s = ubrk_first(brk), to_s;
			while ((to_s = ubrk_next(brk)) != UBRK_DONE)
			{
				u_fprintf(out, "%.*S", to_s - from_s, para + from_s);
				type = ubrk_getRuleStatus(brk);
				if (UBRK_SENTENCE_TERM <= type && type < UBRK_SENTENCE_TERM_LIMIT)
					u_fprintf(out, "S\n");
				from_s = to_s;
			}
			u_fputc('\n', out);
			//u_fprintf(out, "P(%d-%d)\n", start, end);
		}
	}
	u_fclose(in);
	return 0;
}
