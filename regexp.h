#ifndef _REGEXP_H_
#define _REGEXP_H_

#include <stdio.h>

struct regexp;
/* Generates internal regexp object from a regexp string, using specified options */
#define REGEXP_OPTION_CASE_INSENSITIVE   1
#define REGEXP_OPTION_MULTILINE          2
struct regexp *regexp_new (char *regex, int flags);
/* Frees storage associated with regexp object */
void regexp_free(struct regexp* regexp);

/* Gets number of capture groups captured by a regexp */
int regexp_num_capture_groups(struct regexp* regexp);

struct regexp_iterator;
/* Updates subpatterns to reflect group values of first regexp match in a string, or returns 0 if none */
int regexp_find_first_str (char *in, char ***subpatterns, struct regexp*, struct regexp_iterator**);
/* Updates subpatterns to reflect group values of first regexp match in a file, or returns 0 if none */
int regexp_find_first_file (FILE *in, char ***subpatterns, struct regexp*, struct regexp_iterator**);
/* Updates subpatterns to reflect group values of next regexp match, or returns 0 if no more */
int regexp_find_next (char ***subpatterns, struct regexp*, struct regexp_iterator**);
/* Frees iterator and subpatterns storage */
void regexp_find_free (char ***subpatterns, struct regexp* regexp, struct regexp_iterator** iter);

#endif /* #ifndef _REGEXP_H_ */
