/* Regexp: A C regular expression library, version 0.1
   Created by Derrick Coetzee in January 2005

   The authors irrevocably grant this work into the public domain and
   allow it to be used indefinitely by anyone in any manner. The authors
   also disclaim any express or implied warranties with regard to this
   software. In no event shall the authors be liable for any special,
   indirect or consequential damages or any damages whatsoever resulting
   from loss of use, data or profits, whether in an action of contract,
   negligence or other tortious action, arising out of or in connection
   with the use or performance of this software.
*/


#include "regexp.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define ARRAY_SIZE(a)  (sizeof(a)/sizeof(*(a)))

/**************************************************************************
   First we define a compact BIT ARRAY abstraction that we use
   to represent character sets and other things.
**************************************************************************/
#define CEILING_DIV(x,y)  (((x)+(y)-1)/(y))
#define BITS_PER_INT      (sizeof(unsigned int)*CHAR_BIT)
#define BitArray(name,num_bits) \
  unsigned int name[CEILING_DIV(num_bits, BITS_PER_INT)]
typedef unsigned int* BitArrayRef;

#define BitArrayContains(set, idx) \
  ((set)[(idx) / BITS_PER_INT] & (1 << ((idx) % BITS_PER_INT)))

void ZeroOutBitArray(BitArrayRef set, int num_bits) {
    memset(set, 0, CEILING_DIV(num_bits, CHAR_BIT));
}

void SetAllBitArray(BitArrayRef set, int num_bits) {
    memset(set, UCHAR_MAX, CEILING_DIV(num_bits, CHAR_BIT));
}

void CopyBitArray(BitArrayRef dest, BitArrayRef source, int num_bits) {
    memmove(dest, source, CEILING_DIV(num_bits, CHAR_BIT));
}

void BitArrayInsert(BitArrayRef set, unsigned int idx) {
    set[idx / BITS_PER_INT]  |=  1 << (idx % BITS_PER_INT);
}

void BitArrayInsertRange(BitArrayRef set,
			 unsigned int lower_idx, unsigned int upper_idx) {
    /* This could be improved, but should do fine */
    unsigned int i;
    for(i = lower_idx; i <= upper_idx; i++) {
	BitArrayInsert(set, i);
    }
}

void NegateBitArray(BitArrayRef set, int num_bits) {
    int i;
    for(i=0; i<CEILING_DIV(num_bits, BITS_PER_INT); i++) {
	set[i] = ~set[i];
    }
}

#define BEFORE_STRING_CHAR  (UCHAR_MAX + 1)
#define AFTER_STRING_CHAR   (UCHAR_MAX + 2)
#define NUM_SPECIAL_CHARS   2   /* beginning and end of string */
#define TOTAL_CHARS         (UCHAR_MAX + 1 + NUM_SPECIAL_CHARS)

typedef BitArray(CharSet, TOTAL_CHARS);
#define ZeroOutCharSet(set)        (ZeroOutBitArray(set, TOTAL_CHARS))
#define SetAllCharSet(set)         (SetAllBitArray(set, TOTAL_CHARS))
#define CopyCharSet(dest, source)  (CopyBitArray(dest, source, TOTAL_CHARS))
#define CharSetContains(set, idx)  (BitArrayContains(set, (unsigned char)(idx)))
#define CharSetInsert(set, idx)    (BitArrayInsert(set, (unsigned char)idx))
#define CharSetInsertRange(set,lower,upper) \
   (BitArrayInsertRange(set, (unsigned char)lower, (unsigned char)upper))
#define NegateCharSet(set)         (NegateBitArray(set, TOTAL_CHARS))

/**************************************************************************
   Now we define the LEXER, which converts a regexp to a sequence of
   tokens. Positive tokens are literal characters of the same value,
   while negative ones are the special tokens listed below.
**************************************************************************/

#define ANY_CHAR      	     (-1)   /* . */
#define UNION         	     (-2)   /* | */
#define ZERO_OR_MORE  	     (-3)   /* * */
#define ONE_OR_MORE   	     (-4)   /* + */
#define ZERO_OR_ONE  	     (-5)   /* ? */
#define GROUP_OPEN    	     (-6)   /* ( */
#define GROUP_CLOSE   	     (-7)   /* ) */
#define SET_OPEN      	     (-8)   /* [ */
#define SET_CLOSE     	     (-9)   /* ] */
#define RANGE_DASH    	     (-10)  /* - */
#define SET_INVERSE   	     (-11)  /* ^ in char set */
#define LINE_START    	     (-12)  /* ^ not in char set */
#define LINE_END      	     (-13)  /* $ */
#define SPECIAL_SET_PREFIX   (-14)  /* \ as in \W, \d, etc. */
#define NONGREEDY_MARK	     (-15)  /* ? after a quantity expression */
#define REPEAT_OPEN    	     (-16)  /* { */
#define REPEAT_CLOSE   	     (-17)  /* } */
#define REPEAT_COMMA   	     (-18)  /* , inside {} */

#define IS_LITERAL_TOKEN(i)  ((i) > 0)

typedef short RegexpTokenType;

struct predefined_char_class {
    RegexpTokenType* token_string;
};
struct predefined_char_class predefined_char_classes[UCHAR_MAX + 1];

int regexp_lib_initialized = 0;

/* Sets up some fixed data structures used by the library */
static void initialize() {
    static RegexpTokenType w[] =
	{SET_OPEN, 'A', RANGE_DASH, 'Z', 'a', RANGE_DASH, 'z',
	 '0', RANGE_DASH, '9', '_', SET_CLOSE, 0};
    static RegexpTokenType W[] =
	{SET_OPEN, SET_INVERSE, 'A', RANGE_DASH, 'Z',
	 'a', RANGE_DASH, 'z', '0', RANGE_DASH, '9', '_', SET_CLOSE, 0};
    static RegexpTokenType s[] =
	{SET_OPEN, ' ', '\t', '\r', '\n', '\v', SET_CLOSE, 0};
    static RegexpTokenType S[] =
	{SET_OPEN, SET_INVERSE, ' ', '\t', '\r', '\n', '\v', SET_CLOSE, 0};
    static RegexpTokenType d[] =
	{SET_OPEN, '0', RANGE_DASH, '9', SET_CLOSE, 0};
    static RegexpTokenType D[] =
	{SET_OPEN, SET_INVERSE, '0', RANGE_DASH, '9', SET_CLOSE, 0};
    int i;
    for(i=0; i<=UCHAR_MAX; i++) {
	predefined_char_classes[i].token_string = NULL;
    }
    predefined_char_classes['w'].token_string = w;
    predefined_char_classes['W'].token_string = W;
    predefined_char_classes['s'].token_string = s;
    predefined_char_classes['S'].token_string = S;
    predefined_char_classes['d'].token_string = d;
    predefined_char_classes['D'].token_string = D;

    regexp_lib_initialized = 1;
}

/* Turns a regular expression into a string of lexical tokens with specific
   semantic meanings, facilitating easier parsing. (Why not use (f)lex? Don't
   want to inflict nasty dependencies.) */
RegexpTokenType* lex_regexp(char* regexp) {
    /* This might be too much space, but no more than twice needed */
    int re_len = strlen(regexp);
    RegexpTokenType* result = malloc(sizeof(RegexpTokenType)*(re_len + 1));

    int re_idx = 0;
    int result_idx = 0;

    int in_escape = 0;          /* Right after a backslash */
    int in_charset = 0;         /* Inside unescaped []s */
    int in_repeat_counter = 0;  /* Inside a repeat counter like {1,4} */
    int beginning_charset = 0;  /* Right after opening [ of a charset */
    int after_quantity = 0;     /* Right after a *, +, etc*/

    if (!regexp_lib_initialized) {
	initialize();
    }

    for( ; re_idx < re_len; re_idx++) {
	if (in_escape) {
	    if (predefined_char_classes[(int)regexp[re_idx]].token_string != NULL) {
		result[result_idx] = SPECIAL_SET_PREFIX;
		result_idx++;
		result[result_idx] = (unsigned char)regexp[re_idx];
		goto after_default;
	    }
	    /* Zero-width assertions */
	    if (regexp[re_idx] == 'b' || regexp[re_idx] == 'B') {
		result[result_idx] = SPECIAL_SET_PREFIX;
		result_idx++;
		result[result_idx] = (unsigned char)regexp[re_idx];
		goto after_default;
	    }
	    /* Maybe it's an octal or hex escape code */
	    if (regexp[re_idx] == '0' && isdigit(regexp[re_idx+1])) {
		char buf[3] = {'\0', '\0', '\0'};
		char *endptr;
		buf[0] = regexp[re_idx+1];
		buf[1] = regexp[re_idx+2];
		result[result_idx] = strtol(buf, &endptr, 8);
		re_idx += endptr - buf;
	    } else if (regexp[re_idx] == 'x' && regexp[re_idx+1] != '\0') {
		char buf[3] = {'\0', '\0', '\0'};
		char *endptr;
		buf[0] = regexp[re_idx+1];
		buf[1] = regexp[re_idx+2];
		result[result_idx] = strtol(buf, &endptr, 16);
		if (endptr - buf > 0) {
		    re_idx += endptr - buf;
		} else {
		    result[result_idx] = (unsigned char)regexp[re_idx];
		}
	    } else {
	        /* Not special, just write next char literally */
		result[result_idx] = (unsigned char)regexp[re_idx];
	    }
	after_default:
	    if (result[result_idx] != 0) { /* Don't support embedded NULLs */
		result_idx++;
	    }
	    in_escape = 0;
	    continue;
	}

	if (in_charset) {
	    switch (regexp[re_idx]) {
	    case '\\':
		in_escape = 1;
		break;
	    case '-':
		result[result_idx] = RANGE_DASH;
		break;
	    case '^':
		if (beginning_charset) {
		    result[result_idx] = SET_INVERSE;
		} else {
		    result[result_idx] = '^';
		}
		break;
	    case ']':
		result[result_idx] = SET_CLOSE;
		in_charset = 0;
		break;
	    default:
		result[result_idx] = (unsigned char)regexp[re_idx];
	    }
	    if (!in_escape) {
		result_idx++;
	    }
	    beginning_charset = 0;
	    continue;
	}

	if (in_repeat_counter) {
	    if (regexp[re_idx] == ',') {
		result[result_idx] = REPEAT_COMMA;
	    } else if (regexp[re_idx] == '}') {
		result[result_idx] = REPEAT_CLOSE;
		in_repeat_counter = 0;
		after_quantity = 1;
	    } else {
		result[result_idx] = (unsigned char)regexp[re_idx];
	    }
	    result_idx++;
	    continue;
	}

	switch (regexp[re_idx]) {
	case '.':  result[result_idx] = ANY_CHAR;     break;
	case '|':  result[result_idx] = UNION;        break;
	case '(':  result[result_idx] = GROUP_OPEN;   break;
	case ')':  result[result_idx] = GROUP_CLOSE;  break;
	case '^':  result[result_idx] = LINE_START;   break;
	case '$':  result[result_idx] = LINE_END;     break;
	case '*':
	    result[result_idx] = ZERO_OR_MORE;
	    after_quantity = 1;
	    result_idx++;
	    continue;
	case '+': 
	    result[result_idx] = ONE_OR_MORE;
	    after_quantity = 1;
	    result_idx++;
	    continue;
	case '?':
	    if (after_quantity) {
		result[result_idx] = NONGREEDY_MARK;
	    } else {
		result[result_idx] = ZERO_OR_ONE;
		after_quantity = 1;
		result_idx++;
		continue;
	    }
	    break;
	case '[':
	    result[result_idx] = SET_OPEN;
	    in_charset = 1;
	    beginning_charset = 1;
	    break;
	case '\\':
	    in_escape = 1;
	    break;
	case '{':
	    result[result_idx] = REPEAT_OPEN;
	    in_repeat_counter = 1;
	    break;
	default:
	    result[result_idx] = (unsigned char)regexp[re_idx];
	}
	if (!in_escape) {
	    result_idx++;
	}
	if (after_quantity > 0) {
	    after_quantity = 0;
	}
    }
    result[result_idx] = 0;
    return result;
}

/**************************************************************************
   Now we define the PARSER, which creates an abstract syntax tree for
   the regexp. We define a node type for each type of expression.
**************************************************************************/

#define NO_GROUP  (-1)
struct Expression {
    unsigned char typecode; /* describes which of the below this expression is */
    short group_number;  /* Group the match of this expr is placed in or NOGROUP */
};

/* An expression repeated a number of times */
#define INFINITY (-1)   /* For allowing unbounded repetitions */
#define REPEATED_EXPRESSION_TYPE         1
struct RepeatedExpression {
    struct Expression base;
    int lower_bound;  /* Minimum repeat count, 0 to INT_MAX */
    int upper_bound;  /* Max repeat count, 1 to INFINITY, >= lower_bound */
    int is_greedy;    /* 1 if greedy match, 0 if not */
    struct Expression* expression_repeated;
};

/*write olorin Gives a choice of one of two expressions */
#define UNION_EXPRESSION_TYPE            2
struct UnionExpression {
    struct Expression base;
    struct Expression* left_expression;
    struct Expression* right_expression;
};

/* Matches the concatenation of two expressions (side-by-side) */
#define CONCATENATE_EXPRESSION_TYPE      3
struct ConcatenateExpression {
    struct Expression base;
    struct Expression* left_expression;
    struct Expression* right_expression;
};

/* Represents one of a set of characters */
#define CHARSET_EXPRESSION_TYPE          4
struct CharSetExpression {
    struct Expression base;
    CharSet set; /* Matches any char in this set */
};

/* Represents a zero-width assertion */
#define ZERO_WIDTH_EXPRESSION_TYPE       5
struct ZeroWidthExpression {
    struct Expression base;
    CharSet preceding_set; /* Valid preceding characters */
    CharSet following_set; /* Valid following characters */
};

/* Represents an exact literal string, possibly including ANY_CHAR tokens */
#define LITERAL_STRING_EXPRESSION_TYPE   6
struct LiteralStringExpression {
    struct Expression base;
    RegexpTokenType* literal_string;
};

/* Frees all storage associated with an Expression object, based on typecode */
void destroy_expression(struct Expression* expr) {
    if (expr == NULL) return;

    switch(expr->typecode) {
    case REPEATED_EXPRESSION_TYPE: {
	struct RepeatedExpression* repExpr = (struct RepeatedExpression*)expr;
	destroy_expression(repExpr->expression_repeated);
	break;
    }
    case UNION_EXPRESSION_TYPE: {
	struct UnionExpression* unionExpr = (struct UnionExpression*)expr;
	destroy_expression(unionExpr->left_expression);
	destroy_expression(unionExpr->right_expression);
	break;
    }
    case CONCATENATE_EXPRESSION_TYPE: {
	struct ConcatenateExpression* concatExpr = (struct ConcatenateExpression*)expr;
	destroy_expression(concatExpr->left_expression);
	destroy_expression(concatExpr->right_expression);
	break;
    }
    case CHARSET_EXPRESSION_TYPE: {
	/* nothing to do */
	break;
    }
    case LITERAL_STRING_EXPRESSION_TYPE: {
	struct LiteralStringExpression* litStringExpr =
	    (struct LiteralStringExpression*)expr;
	free(litStringExpr->literal_string);
	break;
    }
    }
    
    free(expr); /* free remembers the original malloc'ed size */
}

/* Here's the LL(1) grammar for our regexps:

  Expression := RepeatedExpression UNION Expression       (Union, like foo|bar)
              | RepeatedExpression Expression             (Concatenation)
              | RepeatedExpression

  RepeatedExpression := Atom ZERO_OR_MORE {NONGREEDY_MARK}
                      | Atom ONE_OR_MORE {NONGREEDY_MARK}
                      | Atom ZERO_OR_ONE {NONGREEDY_MARK}
                      | Atom REPEAT_OPEN OptNumber REPEAT_COMMA
                                         OptNumber REPEAT_CLOSE {NONGREEDY_MARK}
                      | Atom

  Atom := string of ANY_CHAR/literal characters           (Like b.na.a)
        | START_LINE                                      (^)
        | END_LINE                                        ($)
        | SPECIAL_SET_PREFIX <letter>                     (\W, \d, \b)
        | character set                                   (Like [^a-z0-9\\.,])
        | GROUP_OPEN Expression GROUP_CLOSE               ( (some regexp) )

  Following is the recursive descent parser for it. (Why not use a real parser?
  Infliction of nasty dependencies and/or much more complicated code.)
*/

/* All these take a string of tokens, the index of the first unparsed token in that string,
   and the smallest group number not yet used. If there is a parse error, they return NULL
   and *startpos is unmodified. */
struct Expression* ParseExpression(const RegexpTokenType* token_stream, int* pos);
struct Expression* ParseRepeatedExpression(const RegexpTokenType* token_stream, int* pos);
struct Expression* ParseAtom(const RegexpTokenType* token_stream, int* pos);
int ParseSet(const RegexpTokenType* token_stream, int* pos, CharSet set);

struct Expression* ParseExpression(const RegexpTokenType* token_stream, int* pos) {
    struct Expression * left = NULL, * right = NULL;
    int origpos = *pos;
    left = ParseRepeatedExpression(token_stream, pos);
    if (left == NULL) {  /* All Expression terms start with RepeatedExpression */
	goto parse_error;
    }
    if (token_stream[*pos] == UNION) {
	(*pos)++;
	right = ParseExpression(token_stream, pos);
	if (right == NULL) {
	    goto parse_error;
	}
        /* Expression := RepeatedExpression UNION Expression */
	{ 
	    struct UnionExpression* result =
		malloc(sizeof(struct UnionExpression));
	    result->base.typecode = UNION_EXPRESSION_TYPE;
	    result->base.group_number = NO_GROUP;
	    result->left_expression = left;
	    result->right_expression = right;
	    return (struct Expression*)result;
	}
    }
    right = ParseExpression(token_stream, pos);
    if (right == NULL) {
        /* Expression := RepeatedExpression */
	return left;
    }
    /* Expression := RepeatedExpression Expression */
    { 
	struct ConcatenateExpression* result =
	    malloc(sizeof(struct ConcatenateExpression));
	result->base.typecode = CONCATENATE_EXPRESSION_TYPE;
	result->base.group_number = NO_GROUP;
	result->left_expression = left;
	result->right_expression = right;
	return (struct Expression*)result;
    }
parse_error:
    if (left  != NULL) destroy_expression(left);
    if (right != NULL) destroy_expression(right);
    *pos = origpos;
    return NULL;
}

int tokens_to_nat(const RegexpTokenType* token_string) {
    int result = 0;
    int i;
    for (i=0; isdigit(token_string[i]); i++) {
	int digit = token_string[i] - '0';
	if ((INT_MAX - digit)/10 < result) {
	    return -1; /* overflow */
	}
	result = result*10 + digit;
    }
    return result;
}

struct Expression* ParseRepeatedExpression(const RegexpTokenType* token_stream, int* pos) {
    struct Expression * atom = NULL;
    int origpos = *pos;
    int is_greedy = 1;
    int lower_bound, upper_bound;

    atom = ParseAtom(token_stream, pos);
    if (atom == NULL) {  /* All RepeatedExpression terms start with Atom */
	goto parse_error;
    }
    switch (token_stream[*pos]) {
    case ZERO_OR_MORE: lower_bound = 0; upper_bound = INFINITY; (*pos)++; break;
    case ONE_OR_MORE:  lower_bound = 1; upper_bound = INFINITY; (*pos)++; break;
    case ZERO_OR_ONE:  lower_bound = 0; upper_bound = 1;        (*pos)++; break;
    case REPEAT_OPEN: {
	int i, upper_bound_pos;
	/* Validity check */
	for(i=*pos + 1; token_stream[i] != REPEAT_COMMA && token_stream[i] != REPEAT_CLOSE; i++) {
	    if (!isdigit(token_stream[i])) /* including == 0 */
		goto parse_error;
	}
	if (token_stream[i] == REPEAT_CLOSE) {
	    upper_bound_pos = *pos + 1;
	} else {
	    upper_bound_pos = i + 1;
	    for(i++; token_stream[i] != REPEAT_CLOSE; i++) {
		if (!isdigit(token_stream[i])) /* including == 0 */
		    goto parse_error;
	    }
	}
	lower_bound = tokens_to_nat(token_stream + *pos + 1);
	if (lower_bound == -1) {
	    goto parse_error;
	}
	if (token_stream[upper_bound_pos] == REPEAT_CLOSE) {
	    upper_bound = INFINITY;
	} else {
	    upper_bound = tokens_to_nat(token_stream + upper_bound_pos);
	    if (upper_bound == -1 || upper_bound < lower_bound) {
		goto parse_error;
	    }
	}
	(*pos) = i + 1;
	break;
    }
    default:
	/* RepeatedExpression := Atom */
	return atom;
    }
    /* Check for nongreedy mark */
    if (token_stream[*pos] == NONGREEDY_MARK) {
	is_greedy = 0;
	(*pos)++;
    }
    /* All other RepeatedExpression rules */
    { 
	struct RepeatedExpression* result =
	    malloc(sizeof(struct RepeatedExpression));
	result->base.typecode = REPEATED_EXPRESSION_TYPE;
	result->base.group_number = NO_GROUP;
	result->expression_repeated = atom;
	result->lower_bound = lower_bound;
	result->upper_bound = upper_bound;
	result->is_greedy   = is_greedy;
	return (struct Expression*)result;
    }
parse_error:
    if (atom  != NULL) destroy_expression(atom);
    *pos = origpos;
    return NULL;
}

struct Expression* ParseAtom(const RegexpTokenType* token_stream, int* pos) {
    int origpos = *pos;
    char c = token_stream[*pos];
    if(IS_LITERAL_TOKEN(c)) {
	/* Atom := string of literal characters/ANY_CHAR/LINE_START/LINE_END */
	struct LiteralStringExpression* result =
	    malloc(sizeof(struct LiteralStringExpression));
	int num_tokens;
	result->base.typecode = LITERAL_STRING_EXPRESSION_TYPE;
	result->base.group_number = NO_GROUP;

	for( ; token_stream[*pos] != 0; (*pos)++) {
	    if (!IS_LITERAL_TOKEN(token_stream[*pos])) {
		break;
	    }
	}
	num_tokens = (*pos) - origpos;
	result->literal_string = malloc((num_tokens + 1) * sizeof(RegexpTokenType));
	result->literal_string[num_tokens] = 0; /* null terminate */
	memmove(result->literal_string, &token_stream[origpos], num_tokens*sizeof(RegexpTokenType));
	return (struct Expression*)result;
    }

    switch (c) {
    case SPECIAL_SET_PREFIX: {
	/* Atom := SPECIAL_SET_PREFIX <letter>, just invoke its regexp */
	(*pos)++;
	if (predefined_char_classes[token_stream[*pos]].token_string != NULL) {
	    int pre_pos = 0;
	    struct Expression* result =
		ParseExpression(predefined_char_classes[token_stream[*pos]].token_string, &pre_pos);
	    (*pos)++;
	    return result;
	}
	switch(token_stream[*pos]) {
	case 'B':   /* Between \w and \w OR between \W and \W */
	case 'b': { /* Between \w and \W OR between \W and \w */
	    int temp_pos = 0;
	    struct ZeroWidthExpression* left =
		malloc(sizeof(struct ZeroWidthExpression));
	    struct ZeroWidthExpression* right =
		malloc(sizeof(struct ZeroWidthExpression));
	    struct UnionExpression* result =
		malloc(sizeof(struct UnionExpression));

	    left->base.typecode = ZERO_WIDTH_EXPRESSION_TYPE;
	    left->base.group_number = NO_GROUP;
	    temp_pos=0; ParseSet(predefined_char_classes['w'].token_string,
				 &temp_pos, left->preceding_set);
	    if (token_stream[*pos] == 'b') {
		temp_pos=0; ParseSet(predefined_char_classes['W'].token_string,
				     &temp_pos, left->following_set);
	    } else {
		temp_pos=0; ParseSet(predefined_char_classes['w'].token_string,
				     &temp_pos, left->following_set);
	    }

	    right->base.typecode = ZERO_WIDTH_EXPRESSION_TYPE;
	    right->base.group_number = NO_GROUP;
	    temp_pos=0; ParseSet(predefined_char_classes['W'].token_string,
				 &temp_pos, right->preceding_set);
	    if (token_stream[*pos] == 'b') {
		temp_pos=0; ParseSet(predefined_char_classes['w'].token_string,
				     &temp_pos, right->following_set);
	    } else {
		temp_pos=0; ParseSet(predefined_char_classes['W'].token_string,
				     &temp_pos, right->following_set);
	    }

	    result->base.typecode = UNION_EXPRESSION_TYPE;
	    result->base.group_number = NO_GROUP;
	    result->left_expression = (struct Expression*)left;
	    result->right_expression = (struct Expression*)right;
	    (*pos)++;
	    return (struct Expression*)result;
	}
	}
	goto parse_error;
    }
    case ANY_CHAR: {
	struct CharSetExpression* result =
	    malloc(sizeof(struct CharSetExpression));
	result->base.typecode = CHARSET_EXPRESSION_TYPE;
	result->base.group_number = NO_GROUP;
	SetAllCharSet(result->set);
	(*pos)++;
	return (struct Expression*)result;
    }
    case LINE_START: {
	struct ZeroWidthExpression* result =
	    malloc(sizeof(struct ZeroWidthExpression));
	result->base.typecode = ZERO_WIDTH_EXPRESSION_TYPE;
	result->base.group_number = NO_GROUP;
	ZeroOutCharSet(result->preceding_set);
	BitArrayInsert(result->preceding_set, BEFORE_STRING_CHAR);
	SetAllCharSet(result->following_set);
	(*pos)++;
	return (struct Expression*)result;
    }
    case LINE_END: {
	struct ZeroWidthExpression* result =
	    malloc(sizeof(struct ZeroWidthExpression));
	result->base.typecode = ZERO_WIDTH_EXPRESSION_TYPE;
	result->base.group_number = NO_GROUP;
	SetAllCharSet(result->preceding_set);
	ZeroOutCharSet(result->following_set);
	BitArrayInsert(result->following_set, AFTER_STRING_CHAR);
	(*pos)++;
	return (struct Expression*)result;
    }
    case SET_OPEN: {
	/* Atom := character set */
	struct CharSetExpression* result =
	    malloc(sizeof(struct CharSetExpression));
	result->base.typecode = CHARSET_EXPRESSION_TYPE;
	result->base.group_number = NO_GROUP;
	if (ParseSet(token_stream, pos, result->set) == 0) {
	    destroy_expression((struct Expression*)result);
	    goto parse_error;
	}
	return (struct Expression*)result;
    }
    case GROUP_OPEN: {
        /* Atom := GROUP_OPEN Expression GROUP_CLOSE */
	int open_pos = *pos;
	int group_num = 0;
	struct Expression* expr =
	    ((*pos)++, ParseExpression(token_stream, pos));
	int i;
	if (expr == NULL || token_stream[*pos] != GROUP_CLOSE) {
	    destroy_expression(expr);
	    goto parse_error;
	}
	/* Find group number by counting preceding (s */
	for(i=0; i<open_pos; i++) {
	    if (token_stream[i] == GROUP_OPEN) {
		group_num++;
	    }
	}
	expr->group_number = group_num;
	(*pos)++;
	return expr;
    }
    default:
	/* Unexpected char, parse error */
	goto parse_error;
    }
parse_error:
    *pos = origpos;
    return NULL;
}

/* Parses a character set into arg set; returns nonzero if succeeds, 0 otherwise */
int ParseSet(const RegexpTokenType* token_stream, int* pos, CharSet set) {
    int invert = 0; /* invert at end? */
    int origpos = *pos;
    int i;

    ZeroOutCharSet(set);
    if (token_stream[*pos] != SET_OPEN) return 0;
    (*pos)++;

    if (token_stream[*pos] == SET_INVERSE) {
	invert = 1;
	(*pos)++;
    }

    /* Validity check, simplifies the rest */
    for(i = *pos; token_stream[i] != SET_CLOSE; i++) {
	if (token_stream[i] == '\0') {
	    /* End of string with no SET_CLOSE, no good */
	    return 0;
	}
	if (token_stream[i] == RANGE_DASH &&
	    (i == *pos ||
	     !IS_LITERAL_TOKEN(token_stream[i-1]) ||
	     !IS_LITERAL_TOKEN(token_stream[i+1]))) {
	    /* Need literal chars on both sides of the dash */
	    return 0;
	}
    }
    while(token_stream[*pos] != SET_CLOSE) {
	int c = token_stream[*pos];

	if (token_stream[*pos + 1] == RANGE_DASH) {
	    CharSetInsertRange(set, c, token_stream[*pos + 2]);
	    (*pos) += 3;
	    continue;
	}

	if (IS_LITERAL_TOKEN(c)) {
	    CharSetInsert(set, (char)c);
	    (*pos)++;
	    continue;
	} else {
	    /* Illegal token or end of string with no SET_CLOSE, no good */
	    *pos = origpos;
	    return 0;
	}
    }
    (*pos)++; /* Skip SET_CLOSE */
    if (invert) {
	NegateCharSet(set);
    }
    return 1;
}

/* Parses a regular expression token string produced by lex_regexp
   and returns the root of the abstract syntax tree. */
struct Expression* parse_regexp(const RegexpTokenType* token_stream) {
    int startpos = 0;
    struct Expression* result = ParseExpression(token_stream, &startpos);
    if (token_stream[startpos] != '\0') {
	/* Must consume whole string */
	destroy_expression(result);
	return NULL;
    }
    return result;
}

/**************************************************************************
  We now define an abstract NFA data structure, a converter from the
  regexp AST to NFAs, and a way to determine if a string matches the NFA
  while keeping track of capture groups.
**************************************************************************/

/* This is a node in a special form of NFA where the edges out
   of each node are either:
   - One or two epsilon edges
   - One edge with a character set on it
   NFAs produced from regular expressions have this form.
*/
struct nfa_node {
    short group_start_number;
    short group_end_number;

    CharSet preceding_set;        /* Valid preceding characters */
    CharSet following_set;        /* Valid following characters */
    struct nfa_node* set_next;    /* Following this edge consumes one character */

    struct nfa_node* epsilon_next1; /* The preferred epsilon transition */
    struct nfa_node* epsilon_next2;
    unsigned char eat_char;       /* Indicates whether to consume a character
				     when traversing this edge */
};

#define SET_NFA_NODE_TYPE             1
#define ZERO_WIDTH_NFA_NODE_TYPE      2
#define EPSILON_NFA_NODE_TYPE         3

struct nfa {
    struct nfa_node* start_node;
    struct nfa_node* accept_node;
};

struct nfa_node* new_nfa_node(void) {
    struct nfa_node* result = malloc(sizeof(struct nfa_node));
    result->group_start_number = NO_GROUP;
    result->group_end_number = NO_GROUP;
    result->eat_char = 1;
    ZeroOutCharSet(result->preceding_set);
    ZeroOutCharSet(result->following_set);
    result->set_next = NULL;
    result->epsilon_next1 = NULL;
    result->epsilon_next2 = NULL;
    return result;
}

/* Adds an epsilon edge out of a node, up to two out of source */
void add_epsilon_edge(struct nfa_node* source, struct nfa_node* dest) {
    if (source->epsilon_next1 == NULL) {
	source->epsilon_next1 = dest;
    } else {
	source->epsilon_next2 = dest;
    }
}

/* Converts an abstract syntax tree of a regexp to an NFA (nondeterministic
   finite automaton) which the matching algorithm uses. */
/* TODO: Make sure there are no epsilon-cycles */
struct nfa regexp_to_nfa(struct Expression* expr) {
    struct nfa result;
    if (expr->typecode != LITERAL_STRING_EXPRESSION_TYPE) {
	result.start_node  = new_nfa_node();
	result.accept_node = new_nfa_node();
    }

    switch (expr->typecode) {
    case REPEATED_EXPRESSION_TYPE: {
	struct RepeatedExpression* repExpr = (struct RepeatedExpression*)expr;
	struct nfa_node* start_node = result.start_node;
	struct nfa_node* end_node = NULL;

	/* First, reduce to one of {0,0}, {0,1}, {0,}, or {1,} */
	int lower_bound = repExpr->lower_bound;
	int upper_bound = repExpr->upper_bound;
	while (!((lower_bound == 0 && upper_bound == 0)  ||
		 (lower_bound == 0 && upper_bound == 1)  ||
		 (lower_bound == 0 && upper_bound == INFINITY)  ||
                 (lower_bound == 1 && upper_bound == INFINITY)))
	{
	    struct nfa nfa_arg_clone = regexp_to_nfa(repExpr->expression_repeated);
	    end_node = new_nfa_node();
	    add_epsilon_edge(start_node, nfa_arg_clone.start_node);
	    add_epsilon_edge(nfa_arg_clone.accept_node, end_node);
	    if (lower_bound == 0) {
		if (repExpr->is_greedy) {
		    start_node->epsilon_next2 = end_node;
		} else {
		    /* Give 0 times priority */
		    start_node->epsilon_next2 = start_node->epsilon_next1;
		    start_node->epsilon_next1 = end_node;
		}
	    } else {
		lower_bound--;
	    }
	    if (upper_bound != INFINITY) {
		upper_bound--;
	    }
	    start_node = end_node;
	}

	/* Now do {0,0}, {0,1}, {0,}, or {1,} */
	if (upper_bound > lower_bound || upper_bound == INFINITY)
	{
	    struct nfa nfa_arg = regexp_to_nfa(repExpr->expression_repeated);
	    end_node = new_nfa_node();
	    start_node->epsilon_next1 = nfa_arg.start_node;
	    if (lower_bound == 0) {
		if (repExpr->is_greedy) {
		    start_node->epsilon_next2 = end_node;
		} else {
		    /* Give 0 times priority */
		    start_node->epsilon_next2 = start_node->epsilon_next1;
		    start_node->epsilon_next1 = end_node;
		}
	    }
	    add_epsilon_edge(nfa_arg.accept_node, end_node);
	    if (upper_bound == INFINITY) {
		if (repExpr->is_greedy) {
		    end_node->epsilon_next1 = nfa_arg.start_node;
		} else {
		    end_node->epsilon_next2 = nfa_arg.start_node;
		}
	    }
	}
	add_epsilon_edge(end_node, result.accept_node);
	break;
    }
    case UNION_EXPRESSION_TYPE: {
	struct UnionExpression* unionExpr = (struct UnionExpression*)expr;
	struct nfa nfa_left_arg = regexp_to_nfa(unionExpr->left_expression);
	struct nfa nfa_right_arg = regexp_to_nfa(unionExpr->right_expression);
	result.start_node->epsilon_next1 = nfa_left_arg.start_node;
	result.start_node->epsilon_next2 = nfa_right_arg.start_node;
	add_epsilon_edge(nfa_left_arg.accept_node, result.accept_node);
	add_epsilon_edge(nfa_right_arg.accept_node, result.accept_node);
	break;
    }
    case CONCATENATE_EXPRESSION_TYPE: {
	struct ConcatenateExpression* concatExpr = (struct ConcatenateExpression*)expr;
	struct nfa nfa_left_arg = regexp_to_nfa(concatExpr->left_expression);
	struct nfa nfa_right_arg = regexp_to_nfa(concatExpr->right_expression);
	/* Need to use new start/end node because the concatenation might have a
           separate group number from the components. */
	result.start_node->epsilon_next1 = nfa_left_arg.start_node;
	add_epsilon_edge(nfa_left_arg.accept_node, nfa_right_arg.start_node);
	add_epsilon_edge(nfa_right_arg.accept_node, result.accept_node);
	break;
    }
    case CHARSET_EXPRESSION_TYPE: {
	struct CharSetExpression* charSetExpr = (struct CharSetExpression*)expr;
	CopyCharSet(result.start_node->following_set, charSetExpr->set);
	result.start_node->set_next = result.accept_node;
	SetAllCharSet(result.start_node->preceding_set);
	result.start_node->eat_char = 1;
	break;
    }
    case ZERO_WIDTH_EXPRESSION_TYPE: {
	struct ZeroWidthExpression* zeroWidthExpr = (struct ZeroWidthExpression*)expr;
	CopyCharSet(result.start_node->preceding_set, zeroWidthExpr->preceding_set);
	CopyCharSet(result.start_node->following_set, zeroWidthExpr->following_set);
	result.start_node->set_next = result.accept_node;
	result.start_node->eat_char = 0;
	break;
    }
    case LITERAL_STRING_EXPRESSION_TYPE: {
	struct LiteralStringExpression* litStringExpr = (struct LiteralStringExpression*)expr;
	RegexpTokenType* str = litStringExpr->literal_string;
	int i;

	if (str[0] == 0) {
	    /* Must have separate start and end state */
	    result.start_node = new_nfa_node();
	    result.accept_node = new_nfa_node();
	    result.start_node->epsilon_next1 = result.accept_node;
	} else {
	    struct nfa_node* current_node = new_nfa_node();
	    result.start_node = current_node;
	    for(i=0; str[i] != 0; i++) {
		struct nfa_node* next_node = new_nfa_node();
		CharSetInsert(current_node->following_set, (char)str[i]);
		SetAllCharSet(current_node->preceding_set);
		current_node->eat_char = 1;
		current_node->set_next = next_node;
		current_node = next_node;
	    }
	    result.accept_node = current_node;
	}
	break;
    }
    }

    result.start_node->group_start_number = expr->group_number;
    result.accept_node->group_end_number = expr->group_number;
    return result;
}

struct range {
    int begin;
    int end;
};

/* Gets a character from a string, returning special symbols for
   indexes outside the string. */
int get_char(const char* str, const int index) {
    if (index < 0) {
	return BEFORE_STRING_CHAR;
    } else if (index >= strlen(str)) {
	return AFTER_STRING_CHAR;
    } else {
	return (unsigned char)str[index];
    }
}

int search_nfa(struct nfa_node* root, const char* input, int* input_idx,
	       struct range group_ranges[], int num_group_ranges, struct nfa_node* accept) {
    int result = 0;
    int old_input_idx = *input_idx;
    struct range* old_group_ranges = malloc(sizeof(struct range)*num_group_ranges);
    memmove(old_group_ranges, group_ranges, sizeof(struct range)*num_group_ranges);
    while (root != NULL) {
	if (root->group_start_number != NO_GROUP)
	    group_ranges[root->group_start_number].begin = *input_idx;
	if (root->group_end_number != NO_GROUP)
	    group_ranges[root->group_end_number].end = *input_idx;

	if (root == accept) {
	    result = 1;
	    break;
	}
	if (root->epsilon_next1 != NULL &&  root->epsilon_next2 == NULL) {
	    root = root->epsilon_next1;
	    continue;
	}
	if (root->epsilon_next1 != NULL && root->epsilon_next1 != root &&
	    search_nfa(root->epsilon_next1, input, input_idx,
		       group_ranges, num_group_ranges, accept)) {
	    result = 1;
	    break;
	}
	if (root->epsilon_next2 != NULL)
	    root = root->epsilon_next2;
	else if (BitArrayContains(root->following_set, get_char(input,*input_idx)) &&
	         BitArrayContains(root->preceding_set, get_char(input,*input_idx-1)))
	{
	    if (root->eat_char) {
		(*input_idx)++;
	    }
	    root = root->set_next;
	}
	else break;
    }
    if (result == 0) {
	memmove(group_ranges, old_group_ranges, sizeof(struct range)*num_group_ranges);
	*input_idx = old_input_idx;
    }
    free(old_group_ranges);
    return result;
}

/* Now we define the interpreter, which uses backtracking search on
   the NFA to find a match. This can be quite slow, but it's very
   simple. Returns 1 if succeeds, 0 otherwise. */
int interpret_regexp(struct nfa nfa, const char* input, int* start_index, struct range group_ranges[],
		     int num_group_ranges) {
    int i;
    for(i=0; i<num_group_ranges; i++) {
	group_ranges[i].begin = group_ranges[i].end = -1;
    }
    /* Try each possible starting position */
    for( ; input[*start_index] != '\0'; (*start_index)++) {
	if (search_nfa(nfa.start_node, input, start_index, group_ranges,
		       num_group_ranges, nfa.accept_node)) {
	    return 1;
	}
    }
    return 0;
}

/**************************************************************************
   The interface functions
**************************************************************************/

struct regexp {
    struct nfa nfa;
    int num_groups;
};

struct regexp_iterator {
    char* input;
    int start_position;
};

struct regexp *regexp_new (char *regex, int flags) {
    struct regexp* result = malloc(sizeof(struct regexp));
    RegexpTokenType* token_string;
    struct Expression* parsed_regexp;
    int i;

    if (result == NULL) goto failed;
    token_string = lex_regexp(regex);
    if (token_string == NULL) goto failed;
    parsed_regexp = parse_regexp(token_string);
    if (parsed_regexp == NULL) goto failed;
    result->nfa = regexp_to_nfa(parsed_regexp);

    /* Count groups */
    result->num_groups = 0;
    for(i=0; token_string[i] != '\0'; i++) {
	if (token_string[i] == GROUP_OPEN) {
	    result->num_groups++;
	}
    }

    /* Success if we get here*/
    destroy_expression(parsed_regexp);
    free(token_string);
    return result;
 failed:
    free(result);
    return NULL;
}

/* Frees storage associated with regexp object */
void regexp_free(struct regexp* regexp) {
    /* destroy_nfa(regexp->nfa); TODO: implement destroy_nfa */
    free(regexp);
}

int regexp_num_capture_groups(struct regexp* regexp) {
    return regexp->num_groups;
}

/* Updates subpatterns to reflect group values of first regexp match in a string, or returns 0 if none */
int regexp_find_first_str (char *in, char ***subpatterns,
			   struct regexp* regexp, struct regexp_iterator** iter) {
    *iter = malloc(sizeof(struct regexp_iterator));
    (*iter)->input = malloc((strlen(in)+1)*sizeof(char));
    memmove((*iter)->input, in, strlen(in)+1);
    (*iter)->start_position = 0;
    *subpatterns = NULL;
    return regexp_find_next(subpatterns, regexp, iter);
}

/* Updates subpatterns to reflect group values of next regexp match, or returns 0 if no more */
int regexp_find_next (char ***subpatterns, struct regexp* regexp, struct regexp_iterator** iter) {
    struct range* ranges = malloc(sizeof(struct range)*regexp->num_groups);
    int result = interpret_regexp(regexp->nfa, (*iter)->input,
				  &((*iter)->start_position), ranges,
				  regexp->num_groups);
    int i;

    if(result == 0) {
	return 0;
    }

    if (*subpatterns != NULL) {
	for(i=0; i<regexp->num_groups; i++) {
	    if ((*subpatterns)[i])
		free((*subpatterns)[i]);
	}
	free(*subpatterns);
    }
    *subpatterns = malloc(sizeof(char*) * regexp->num_groups);
    for(i=0; i<regexp->num_groups; i++) {
	if (ranges[i].begin == -1) {
	    (*subpatterns)[i] = 0;
	} else {
	    (*subpatterns)[i] = calloc((ranges[i].end-ranges[i].begin) + 1, sizeof(char));
	    memmove((*subpatterns)[i], (*iter)->input + ranges[i].begin, ranges[i].end-ranges[i].begin);
	}
    }

    free(ranges);
    return result;
}

/* Frees iterator and subpatterns storage */
void regexp_find_free (char ***subpatterns, struct regexp* regexp, struct regexp_iterator** iter) {
    int i;
    if (*subpatterns != NULL) {
	for(i=0; i<regexp->num_groups; i++) {
	    if ((*subpatterns)[i])
		free((*subpatterns)[i]);
	}
	free(*subpatterns);
	*subpatterns = NULL;
    }
    free((*iter)->input);
    free(*iter);
    *iter = NULL;
}

/* Future directions: a full compilation to a minimal DFA table which
   is then interpreted would be much faster. A preprocessor to emit
   C that stores the same information in its program counter would be
   faster still. But these each require a lot more code, and capturing
   groups is complicated with DFAs. */
