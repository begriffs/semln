#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <unicode/ubrk.h>
#include <unicode/uloc.h>
#include <unicode/ustdio.h>
#include <unicode/uregex.h>
#include <unicode/ustring.h>

#define ARR_LEN(a) ((sizeof(a))/sizeof(*(a)))

char g_locale[100] = "en_US@ss=standard";

struct match_stats
{
	bool para_sep;
	int breaks;
	int len;
};

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
	uloc_canonicalize(posix_loc, g_locale, ARR_LEN(g_locale), &status);
	u_assert(status, "uloc_canonicalize");

	/* use segmentation suppression (preventing sentence breaks in English
	 * after abbreviations such as "Mr." or "Est.", for example) */
	uloc_setKeywordValue("ss", "standard", g_locale, ARR_LEN(g_locale), &status);
	u_assert(status, "uloc_setKeywordValue");
}

struct match_stats
get_match_stats(URegularExpression *re, UChar *buf, int32_t n)
{
	;
	UErrorCode status = U_ZERO_ERROR;
	struct match_stats ret = (struct match_stats){0};
	static UBreakIterator *brk;
   
	if (!brk)
	{
		brk = ubrk_open(UBRK_LINE, g_locale, NULL, -1, &status);
		u_assert(status, "ubrk_open");
	}

	/***** get length ******/
	uregex_setText(re, buf, n, &status);
	u_assert(status, "uregex_setText");
	if (!uregex_find(re, 0, &status))
		return ret;
	int32_t start = uregex_start(re, 0, &status),
			end   = uregex_end(re, 0, &status);
	if (start == end)
		return ret;
	ret.len = end - start;

	/***** count breaks ******/
	ubrk_setText(brk, buf+start, end-start, &status);
	u_assert(status, "ubrk_setText");
   	ubrk_first(brk);
	while (ret.breaks < 2 /* 2 implies a para break */
	       && ubrk_next(brk) != UBRK_DONE)
	{
		int32_t type = ubrk_getRuleStatus(brk);
		if (UBRK_LINE_HARD <= type && type < UBRK_LINE_HARD_LIMIT)
			ret.breaks++;
	}

	/***** detect explicit paragraph sep ******/
	UChar temp = buf[end];
	buf[end] = '\0';
	ret.para_sep = u_strchr32(buf+start, 0x2029) != NULL;
	buf[end] = temp;

	return ret;
}

int32_t u_file_read_safe(UChar *buf, int32_t n, UFILE *f)
{
	int32_t ret = u_file_read(buf, n-1, f);

	/* if final code unit is an unpaired surrogate, or
	 * a carriage return, put it back for the next read */
	if (ret > 0 && !u_feof(f) &&
		(U16_IS_LEAD(buf[ret-1]) || buf[ret-1] == '\r'))
	{
		u_fungetc(buf[--ret], f);
	}
	buf[ret > 0 ? ret : 0] = '\0';
	return ret;
}

void u_unlines(UChar *buf, int32_t bufsz, UErrorCode *status)
{
	static URegularExpression *newlines_re;
	static UChar space[] = {'-'};

	UChar *temp = malloc(bufsz * sizeof *buf);
	if (!newlines_re)
	{
		newlines_re = uregex_openC("\\s+", 0, NULL, status);
		u_assert(*status, "newlines_re");
	}
	uregex_setText(newlines_re, buf, -1, status);
	u_assert(*status, "u_unlines, uregex_setText");
	uregex_replaceAll(newlines_re, space, 1, temp, bufsz, status);
	u_assert(*status, "u_unlines, uregex_replaceAll");
	u_strcpy(buf, temp);
	free(temp);
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
		*brk = ubrk_open(UBRK_SENTENCE, g_locale, NULL, -1, &status);
	u_assert(status, "ubrk_open");

	static URegularExpression *pad_start_re, *pad_end_re;
	pad_start_re = uregex_openC("^\\s*", 0, NULL, &status);
	pad_end_re   = uregex_openC("\\s*$", 0, NULL, &status);
	u_assert(status, "uregex_openC");

	int32_t len;
	UChar buf[BUFSIZ], sentence[BUFSIZ];
	while ((len = u_file_read_safe(buf, BUFSIZ, in)) > 0)
	{
		/* iterate sentences */
		ubrk_setText(brk, buf, len, &status);
		u_assert(status, "ubrk_setText");
		int32_t from_s = ubrk_first(brk), to_s;
		while ((to_s = ubrk_next(brk)) != UBRK_DONE)
		{
			struct match_stats
				start_stats =
					get_match_stats(pad_start_re, buf+from_s, to_s - from_s),
				end_stats =
					get_match_stats(pad_end_re, buf+from_s, to_s - from_s);

			/* even one newline at start signals new para */
			//if (start_stats.para_sep || start_stats.breaks > 0)
			//	u_fputc('\n', out);

			/* extract sentence from match, sans padding */
			if ((to_s - from_s) > (start_stats.len + end_stats.len))
			{
				sentence[0] = '\0';
				u_strncat(
					sentence,
					buf + from_s + start_stats.len,
					to_s - from_s - start_stats.len - end_stats.len
				);

				//u_unlines(sentence, ARR_LEN(sentence), &status);
				u_fprintf(out, "{{{%S}}}", sentence);
			}
			int32_t type = ubrk_getRuleStatus(brk);
			u_fprintf(out, "--(%d)--", type);
			if (UBRK_SENTENCE_TERM <= type && type < UBRK_SENTENCE_TERM_LIMIT)
				u_fprintf(out, "***\n");
			else if (UBRK_SENTENCE_SEP <= type && type < UBRK_SENTENCE_SEP_LIMIT)
				u_fprintf(out, "!!!");

			//if (end_stats.para_sep || end_stats.breaks > 1)
			//	u_fputc('\n', out);

			from_s = to_s;
		}
	}
	u_fclose(in);
	return 0;
}
