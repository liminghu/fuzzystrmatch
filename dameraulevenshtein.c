/*
 * dameraulevenshtein.c
 *
 * Functions for "fuzzy" comparison of strings
 *
 * Joe Conway <mail@joeconway.com>
 *
 * Copyright (c) 2001-2011, PostgreSQL Global Development Group
 * ALL RIGHTS RESERVED;
 *
 * levenshtein()
 * -------------
 * Written based on a description of the algorithm by Michael Gilleland
 * found at http://www.merriampark.com/ld.htm
 * Also looked at levenshtein.c in the PHP 4.0.6 distribution for
 * inspiration.
 * Configurable penalty costs extension is introduced by Volkan
 * YAZICI <volkan.yazici@gmail.com>. 
 *
 * dameraulevenshtein()
 * Liming Hu <dawninghu@gmail.com>
 * description of the algorithm:
 *  http://en.wikipedia.org/wiki/Damerau%E2%80%93Levenshtein_distance
 * and:
 * http://tomoyo.sourceforge.jp/cgi-bin/lxr/source/tools/perf/util/levenshtein.c
 *
 * -------------
 * followed the same style and Written based on levenshtein.c 
 * (by Joe Conway <mail@joeconway.com> and volkan.yazici@gmail.com )
 * in fuzzystrmatch contribution.
 */

/*
 * External declarations for exported functions
 */
#ifdef DAMERAU_LEVENSHTEIN_LESS_EQUAL
static int dameraulevenshtein_less_equal_internal(text *s, text *t,
								int ins_c, int del_c, int sub_c, int trans_c, int max_d);
#else
static int dameraulevenshtein_internal(text *s, text *t,
					 int ins_c, int del_c, int sub_c, int trans_c);
#endif

#define MAX_DAMERAU_LEVENSHTEIN_STRLEN		255


/*
 * Calculates Damerau-Levenshtein distance metric between supplied strings. Generally
 * (1, 1, 1, 1) penalty costs suffices for common cases, but your mileage may
 * vary.
 *
 * One way to compute Damerau-Levenshtein distance is to incrementally construct
 * an (m+1)x(n+1) matrix where cell (i, j) represents the minimum number
 * of operations required to transform the first i characters of s into
 * the first j characters of t.  The last column of the final row is the
 * answer.
 *
 * We use that algorithm here with some modification.  In lieu of holding
 * the entire array in memory at once, we'll just use two arrays of size
 * m+1 for storing accumulated values. At each step one array represents
 * the "previous" row and one is the "current" row of the notional large
 * array.
 *
 * If max_d >= 0, we only need to provide an accurate answer when that answer
 * is less than or equal to the bound.	From any cell in the matrix, there is
 * theoretical "minimum residual distance" from that cell to the last column
 * of the final row.  This minimum residual distance is zero when the
 * untransformed portions of the strings are of equal length (because we might
 * get lucky and find all the remaining characters matching) and is otherwise
 * based on the minimum number of insertions or deletions needed to make them
 * equal length.  The residual distance grows as we move toward the upper
 * right or lower left corners of the matrix.  When the max_d bound is
 * usefully tight, we can use this property to avoid computing the entirety
 * of each row; instead, we maintain a start_column and stop_column that
 * identify the portion of the matrix close to the diagonal which can still
 * affect the final answer.
 */
static int
#ifdef DAMERAU_LEVENSHTEIN_LESS_EQUAL
dameraulevenshtein_less_equal_internal(text *s, text *t,
								int ins_c, int del_c, int sub_c, int trans_c, int max_d)
#else
dameraulevenshtein_internal(text *s, text *t,
					 int ins_c, int del_c, int sub_c, int trans_c)
#endif
{
	int			m,
				n,
				s_bytes,
				t_bytes;
	int		   *prev;
	int		   *curr;
	int		   *s_char_len = NULL;
	int			i,
				j;
	const char *s_data;
	const char *t_data;
	const char *y;

	/*
	 * For dameraulevenshtein_less_equal_internal, we have real variables called
	 * start_column and stop_column; otherwise it's just short-hand for 0 and
	 * m.
	 */
#ifdef DAMERAU_LEVENSHTEIN_LESS_EQUAL
	int			start_column,
				stop_column;

#undef START_COLUMN
#undef STOP_COLUMN
#define START_COLUMN start_column
#define STOP_COLUMN stop_column
#else
#undef START_COLUMN
#undef STOP_COLUMN
#define START_COLUMN 0
#define STOP_COLUMN m
#endif

	/* Extract a pointer to the actual character data. */
	s_data = VARDATA_ANY(s);
	t_data = VARDATA_ANY(t);

	/* Determine length of each string in bytes and characters. */
	s_bytes = VARSIZE_ANY_EXHDR(s);
	t_bytes = VARSIZE_ANY_EXHDR(t);
	m = pg_mbstrlen_with_len(s_data, s_bytes);
	n = pg_mbstrlen_with_len(t_data, t_bytes);

	/*
	 * We can transform an empty s into t with n insertions, or a non-empty t
	 * into an empty s with m deletions.
	 */
	if (!m)
		return n * ins_c;
	if (!n)
		return m * del_c;

	/*
	 * For security concerns, restrict excessive CPU+RAM usage. (This
	 * implementation uses O(m) memory and has O(mn) complexity.)
	 */
	if (m > MAX_DAMERAU_LEVENSHTEIN_STRLEN || 
		n > MAX_DAMERAU_LEVENSHTEIN_STRLEN)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("argument exceeds the maximum length of %d bytes",
						MAX_DAMERAU_LEVENSHTEIN_STRLEN)));

#ifdef DAMERAU_LEVENSHTEIN_LESS_EQUAL
	/* Initialize start and stop columns. */
	start_column = 0;
	stop_column = m + 1;

	/*
	 * If max_d >= 0, determine whether the bound is impossibly tight.	If so,
	 * return max_d + 1 immediately.  Otherwise, determine whether it's tight
	 * enough to limit the computation we must perform.  If so, figure out
	 * initial stop column.
	 */
	if (max_d >= 0)
	{
		int			min_theo_d; /* Theoretical minimum distance. */
		int			max_theo_d; /* Theoretical maximum distance. */
		int			net_inserts = n - m;

		min_theo_d = net_inserts < 0 ?
			-net_inserts * del_c : net_inserts * ins_c;
		if (min_theo_d > max_d)
			return max_d + 1;
		if (ins_c + del_c < sub_c)
			sub_c = ins_c + del_c;
		max_theo_d = min_theo_d + sub_c * Min(m, n);
		if (max_d >= max_theo_d)
			max_d = -1;
		else if (ins_c + del_c > 0)
		{
			/*
			 * Figure out how much of the first row of the notional matrix we
			 * need to fill in.  If the string is growing, the theoretical
			 * minimum distance already incorporates the cost of deleting the
			 * number of characters necessary to make the two strings equal in
			 * length.	Each additional deletion forces another insertion, so
			 * the best-case total cost increases by ins_c + del_c. If the
			 * string is shrinking, the minimum theoretical cost assumes no
			 * excess deletions; that is, we're starting no futher right than
			 * column n - m.  If we do start further right, the best-case
			 * total cost increases by ins_c + del_c for each move right.
			 */
			int			slack_d = max_d - min_theo_d;
			int			best_column = net_inserts < 0 ? -net_inserts : 0;

			stop_column = best_column + (slack_d / (ins_c + del_c)) + 1;
			if (stop_column > m)
				stop_column = m + 1;
		}
	}
#endif

	/*
	 * In order to avoid calling pg_mblen() repeatedly on each character in s,
	 * we cache all the lengths before starting the main loop -- but if all
	 * the characters in both strings are single byte, then we skip this and
	 * use a fast-path in the main loop.  If only one string contains
	 * multi-byte characters, we still build the array, so that the fast-path
	 * needn't deal with the case where the array hasn't been initialized.
	 */
	if (m != s_bytes || n != t_bytes)
	{
		int			i;
		const char *cp = s_data;

		s_char_len = (int *) palloc((m + 1) * sizeof(int));
		for (i = 0; i < m; ++i)
		{
			s_char_len[i] = pg_mblen(cp);
			cp += s_char_len[i];
		}
		s_char_len[i] = 0;
	}

	/* One more cell for initialization column and row. */
	++m;
	++n;

	/* Previous and current rows of notional array. */
	prev = (int *) palloc(2 * m * sizeof(int));
	curr = prev + m;

	/*
	 * To transform the first i characters of s into the first 0 characters of
	 * t, we must perform i deletions.
	 */
	for (i = START_COLUMN; i < STOP_COLUMN; i++)
		prev[i] = i * del_c;

	/* Loop through rows of the notional array */
	for (y = t_data, j = 1; j < n; j++)
	{
		int		   *temp;
		const char *x = s_data;
		int			y_char_len = n != t_bytes + 1 ? pg_mblen(y) : 1;

#ifdef DAMERAU_LEVENSHTEIN_LESS_EQUAL

		/*
		 * In the best case, values percolate down the diagonal unchanged, so
		 * we must increment stop_column unless it's already on the right end
		 * of the array.  The inner loop will read prev[stop_column], so we
		 * have to initialize it even though it shouldn't affect the result.
		 */
		if (stop_column < m)
		{
			prev[stop_column] = max_d + 1;
			++stop_column;
		}

		/*
		 * The main loop fills in curr, but curr[0] needs a special case: to
		 * transform the first 0 characters of s into the first j characters
		 * of t, we must perform j insertions.	However, if start_column > 0,
		 * this special case does not apply.
		 */
		if (start_column == 0)
		{
			curr[0] = j * ins_c;
			i = 1;
		}
		else
			i = start_column;
#else
		curr[0] = j * ins_c;
		i = 1;
#endif

		/*
		 * This inner loop is critical to performance, so we include a
		 * fast-path to handle the (fairly common) case where no multibyte
		 * characters are in the mix.  The fast-path is entitled to assume
		 * that if s_char_len is not initialized then BOTH strings contain
		 * only single-byte characters.
		 */
		if (s_char_len != NULL)
		{
			for (; i < STOP_COLUMN; i++)
			{
				int			ins;
				int			del;
				int			sub;
                                int                     trans; 
				int			x_char_len = s_char_len[i - 1];

				/*
				 * Calculate costs for insertion, deletion, and substitution.
				 *
				 * When calculating cost for substitution, we compare the last
				 * character of each possibly-multibyte character first,
				 * because that's enough to rule out most mis-matches.  If we
				 * get past that test, then we compare the lengths and the
				 * remaining bytes.
				 */
				ins = prev[i] + ins_c;
				del = curr[i - 1] + del_c;
				if (x[x_char_len - 1] == y[y_char_len - 1]
					&& x_char_len == y_char_len &&
					(x_char_len == 1 || rest_of_char_same(x, y, x_char_len)))
					sub = prev[i - 1];
				else
					sub = prev[i - 1] + sub_c;



				/* Take the one with minimum cost. */
				curr[i] = Min(ins, del);
				curr[i] = Min(curr[i], sub);

				/* Point to next character. */
				x += x_char_len;
			}
		}
		else
		{
			for (; i < STOP_COLUMN; i++)
			{
				int			ins;
				int			del;
				int			sub;
                                int                     trans;
				/* Calculate costs for insertion, deletion, and substitution. */
				ins = prev[i] + ins_c;
				del = curr[i - 1] + del_c;
				sub = prev[i - 1] + ((*x == *y) ? 0 : sub_c);

				/* Take the one with minimum cost. */
				curr[i] = Min(ins, del);
				curr[i] = Min(curr[i], sub);

				/* Point to next character. */
				x++;
			}
		}

		/* Swap current row with previous row. */
		temp = curr;
		curr = prev;
		prev = temp;

		/* Point to next character. */
		y += y_char_len;

#ifdef DAMERAU_LEVENSHTEIN_LESS_EQUAL

		/*
		 * This chunk of code represents a significant performance hit if used
		 * in the case where there is no max_d bound.  This is probably not
		 * because the max_d >= 0 test itself is expensive, but rather because
		 * the possibility of needing to execute this code prevents tight
		 * optimization of the loop as a whole.
		 */
		if (max_d >= 0)
		{
			/*
			 * The "zero point" is the column of the current row where the
			 * remaining portions of the strings are of equal length.  There
			 * are (n - 1) characters in the target string, of which j have
			 * been transformed.  There are (m - 1) characters in the source
			 * string, so we want to find the value for zp where where (n - 1)
			 * - j = (m - 1) - zp.
			 */
			int			zp = j - (n - m);

			/* Check whether the stop column can slide left. */
			while (stop_column > 0)
			{
				int			ii = stop_column - 1;
				int			net_inserts = ii - zp;

				if (prev[ii] + (net_inserts > 0 ? net_inserts * ins_c :
								-net_inserts * del_c) <= max_d)
					break;
				stop_column--;
			}

			/* Check whether the start column can slide right. */
			while (start_column < stop_column)
			{
				int			net_inserts = start_column - zp;

				if (prev[start_column] +
					(net_inserts > 0 ? net_inserts * ins_c :
					 -net_inserts * del_c) <= max_d)
					break;

				/*
				 * We'll never again update these values, so we must make sure
				 * there's nothing here that could confuse any future
				 * iteration of the outer loop.
				 */
				prev[start_column] = max_d + 1;
				curr[start_column] = max_d + 1;
				if (start_column != 0)
					s_data += (s_char_len != NULL) ? s_char_len[start_column - 1] : 1;
				start_column++;
			}

			/* If they cross, we're going to exceed the bound. */
			if (start_column >= stop_column)
				return max_d + 1;
		}
#endif
	}

	/*
	 * Because the final value was swapped from the previous row to the
	 * current row, that's where we'll find it.
	 */
	return prev[m - 1];
}


#ifdef DAMERAU_LEVENSHTEIN_NONCOMPATIBLE
static int dameraulevenshtein_internal_noncompatible(text *s, text *t,
					 int ins_c, int del_c, int sub_c, int trans_c);
/*copied from:
  * http://tomoyo.sourceforge.jp/cgi-bin/lxr/source/tools/perf/util/levenshtein.c
  */
 
 /*
  * This function implements the Damerau-Levenshtein algorithm to
  * calculate a distance between strings.
  *
  * Basically, it says how many letters need to be swapped, substituted,
  * deleted from, or added to string1, at least, to get string2.
  *
  * The idea is to build a distance matrix for the substrings of both
  * strings.  To avoid a large space complexity, only the last three rows
  * are kept in memory (if swaps had the same or higher cost as one deletion
  * plus one insertion, only two rows would be needed).
  *
  * At any stage, "i + 1" denotes the length of the current substring of
  * string1 that the distance is calculated for.
  *
  * row2 holds the current row, row1 the previous row (i.e. for the substring
  * of string1 of length "i"), and row0 the row before that.
  *
  * In other words, at the start of the big loop, row2[j + 1] contains the
  * Damerau-Levenshtein distance between the substring of string1 of length
  * "i" and the substring of string2 of length "j + 1".
  *
  * All the big loop does is determine the partial minimum-cost paths.
  *
  * It does so by calculating the costs of the path ending in characters
  * i (in string1) and j (in string2), respectively, given that the last
  * operation is a substition, a swap, a deletion, or an insertion.
  *
  * This implementation allows the costs to be weighted:
  *
  * - w (as in "sWap")
  * - s (as in "Substitution")
  * - a (for insertion, AKA "Add")
  * - d (as in "Deletion")
  *
  * Note that this algorithm calculates a distance _iff_ d == a.
  */
static int dameraulevenshtein_internal_noncompatible(text *s, text *t,
					 int ins_c, int del_c, int sub_c, int trans_c)
 {
      
	const char *s_data;
	const char *t_data;
       
        int			m,
				n,
				s_bytes,
				t_bytes;

	/* Extract a pointer to the actual character data. */
	s_data = VARDATA_ANY(s);
	t_data = VARDATA_ANY(t);

        const char *string1 = s_data;
        const char *string2 = t_data;

	/* Determine length of each string in bytes and characters. */
	s_bytes = VARSIZE_ANY_EXHDR(s);
	t_bytes = VARSIZE_ANY_EXHDR(t);
        /* returns the length (counted in wchars) of a multibyte string
         * (not necessarily NULL terminated)
         */
	m = pg_mbstrlen_with_len(s_data, s_bytes);
	n = pg_mbstrlen_with_len(t_data, t_bytes);



         int len1 = m, len2 = n;
         int *row0 = malloc(sizeof(int) * (len2 + 1));
         int *row1 = malloc(sizeof(int) * (len2 + 1));
         int *row2 = malloc(sizeof(int) * (len2 + 1));
         int i, j;
 
         for (j = 0; j <= len2; j++)
                 row1[j] = j * ins_c;
         for (i = 0; i < len1; i++) {
                 int *dummy;
 
                 row2[0] = (i + 1) * del_c;
                 for (j = 0; j < len2; j++) {
                         /* substitution */
                         row2[j + 1] = row1[j] + sub_c * (string1[i] != string2[j]);
                         /* swap */
                         if (i > 0 && j > 0 && string1[i - 1] == string2[j] &&
                                         string1[i] == string2[j - 1] &&
                                         row2[j + 1] > row0[j - 1] + trans_c)
                                 row2[j + 1] = row0[j - 1] + trans_c;
                         /* deletion */
                         if (row2[j + 1] > row1[j + 1] + del_c)
                                 row2[j + 1] = row1[j + 1] + del_c;
                         /* insertion */
                         if (row2[j + 1] > row2[j] + ins_c)
                                 row2[j + 1] = row2[j] + ins_c;
                 }
 
                 dummy = row0;
                 row0 = row1;
                 row1 = row2;
                 row2 = dummy;
         }
 
         i = row1[len2];
         free(row0);
         free(row1);
         free(row2);
 
         return i;  
  }
#endif


