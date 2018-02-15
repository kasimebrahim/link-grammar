/*************************************************************************/
/* Copyright (c) 2004                                                    */
/* Daniel Sleator, David Temperley, and John Lafferty                    */
/* Copyright (c) 2009, 2013 Linas Vepstas                                */
/* All rights reserved                                                   */
/*                                                                       */
/* Use of the link grammar parsing system is subject to the terms of the */
/* license set forth in the LICENSE file included with this software.    */
/* This license allows free redistribution and use in source and binary  */
/* forms, with or without modification, subject to certain conditions.   */
/*                                                                       */
/*************************************************************************/

#ifndef _LINK_GRAMMAR_CONNECTORS_H_
#define _LINK_GRAMMAR_CONNECTORS_H_

#include <ctype.h>   // for islower()
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>  // for uint8_t

#include "api-types.h"
#include "lg_assert.h"
#include "string-set.h"

/* MAX_SENTENCE cannot be more than 254, because word MAX_SENTENCE+1 is
 * BAD_WORD -- it is used to indicate that nothing can connect to this
 * connector, and this should fit in one byte (because the word field
 * of a connector is an uint8_t, see below).
 */
#define MAX_SENTENCE 254        /* Maximum number of words in a sentence */

/* For faster comparisons, the connector lc part is encoded into a number
 * and a mask. Each letter is encoded using LC_BITS bits. With 7 bits, it
 * is possible to encode up to 9 letters in an uint64_t.
 * FIXME: Validate that lc length <= 9. */
#define LC_BITS 7
#define LC_MASK ((1<<LC_BITS)-1)
typedef uint64_t lc_enc_t;

typedef uint16_t connector_hash_size; /* Change to uint32_t if needed. */

typedef struct
{
	lc_enc_t lc_letters;
	lc_enc_t lc_mask;

	const char *string;  /* The connector name w/o the direction mark, e.g. AB */
	// double *cost; /* Array of cost by length_limit (cost[0]: default) */
	connector_hash_size str_hash;
	union
	{
		connector_hash_size uc_hash;
		connector_hash_size uc_num;
	};
	uint8_t length_limit;
	                      /* If not 0, it gives the limit of the length of the
	                       * link that can be used on this connector type. The
	                       * value UNLIMITED_LEN specifies no limit.
	                       * If 0, short_length (a Parse_Option) is used. If
	                       * all_short==true (a Parse_Option), length_limit
	                       * is clipped to short_length. */
	char head_dependent;   /* 'h' for head, 'd' for dependent, or '\0' if none */

	/* The following are used for connector match speedup */
	uint8_t uc_length;   /* uc part length */
	uint8_t uc_start;    /* uc start position */
} condesc_t;

typedef struct length_limit_def
{
	const char *defword;
	const Exp *defexp;
	struct length_limit_def *next;
	int length_limit;
} length_limit_def_t;

typedef struct
{
	condesc_t **hdesc;    /* Hashed connector descriptors table */
	condesc_t **sdesc;    /* Alphabetically sorted descriptors */
	size_t size;          /* Allocated size */
	size_t num_con;       /* Number of connector types */
	size_t num_uc;        /* Number of connector types with different UC part */
	length_limit_def_t *length_limit_def;
	length_limit_def_t **length_limit_def_next;
} ConTable;

/* On a 64-bit machine, this struct should be exactly 4*8=32 bytes long.
 * Lets try to keep it that way.
 */
struct Connector_struct
{
	uint8_t length_limit; /* Can be different than in the descriptor */
	uint8_t nearest_word;
	                      /* The nearest word to my left (or right) that
	                         this could ever connect to.  Computed by
	                         setup_connectors() */
	bool multi;           /* TRUE if this is a multi-connector */
	const condesc_t *desc;
	Connector *next;
	const gword_set *originating_gword;
};

void sort_condesc_by_uc_constring(Dictionary);
void condesc_delete(Dictionary);

/* GET accessors for connector attributes.
 * Can be used for experimenting with Connector_struct internals in
 * non-trivial ways without the need to change most of the code that
 * accesses connectors.
 * FIXME: Maybe remove the _get part of the names, since we don't
 * need SET accessors. */
static inline const char * connector_string(const Connector *c)
{
	return c->desc->string;
}

static inline int connector_uc_start(const Connector *c)
{
	return c->desc->uc_start;
}

static inline const condesc_t *connector_desc(const Connector *c)
{
	return c->desc;
}

static inline int connector_uc_hash(const Connector * c)
{
	return c->desc->uc_hash;
}

static inline int connector_uc_num(const Connector * c)
{
	return c->desc->uc_num;
}


/* Connector utilities ... */
Connector * connector_new(const condesc_t *, Parse_Options);
void set_connector_length_limit(Connector *, Parse_Options);
void free_connectors(Connector *);

/* Length-limits for how far connectors can reach out. */
#define UNLIMITED_LEN 255

void set_all_condesc_length_limit(Dictionary);

/**
 * Returns TRUE if s and t match according to the connector matching
 * rules.  The connector strings must be properly formed, starting with
 * zero or one lower case letters, followed by one or more upper case
 * letters, followed by some other letters.
 *
 * The algorithm is symmetric with respect to a and b.
 *
 * Connectors starting with lower-case letters match ONLY if the initial
 * letters are DIFFERENT.  Otherwise, connectors only match if the
 * upper-case letters are the same, and the trailing lower case letters
 * are the same (or have wildcards).
 *
 * The initial lower-case letters allow an initial 'h' (denoting 'head
 * word') to match an initial 'd' (denoting 'dependent word'), while
 * rejecting a match 'h' to 'h' or 'd' to 'd'.  This allows the parser
 * to work with catena, instead of just links.
 */
static inline bool easy_match(const char * s, const char * t)
{
	char is = 0, it = 0;
	if (islower((int) *s)) { is = *s; s++; }
	if (islower((int) *t)) { it = *t; t++; }

	if (is != 0 && it != 0 && is == it) return false;

	while (isupper((int)*s) || isupper((int)*t))
	{
		if (*s != *t) return false;
		s++;
		t++;
	}

	while ((*s!='\0') && (*t!='\0'))
	{
		if ((*s == '*') || (*t == '*') || (*s == *t))
		{
			s++;
			t++;
		}
		else
			return false;
	}
	return true;
}

/**
 * Compare the lower-case and head/dependent parts of two connector descriptors.
 * When this function is called, it is assumed that the upper-case
 * parts are equal, and thus do not need to be checked again.
 */
static bool lc_easy_match(const condesc_t *c1, const condesc_t *c2)
{
	if ((c1->lc_letters ^ c2->lc_letters) & c1->lc_mask & c2->lc_mask)
		return false;
	if (('\0' != c1->head_dependent) && (c1->head_dependent == c2->head_dependent))
		return false;

	return true;
}

/**
 * This function is like easy_match(), but with connector descriptors.
 * It uses a shortcut comparison of the upper-case parts.
 */
static inline bool easy_match_desc(const condesc_t *c1, const condesc_t *c2)
{
	if (c1->uc_num != c2->uc_num) return false;
	return lc_easy_match(c1, c2);
}

static inline int string_hash(const char *s)
{
	unsigned int i;

	/* djb2 hash */
	i = 5381;
	while (*s)
	{
		i = ((i << 5) + i) + *s;
		s++;
	}
	return i;
}

bool calculate_connector_info(condesc_t *);

static inline int connector_str_hash(const char *s)
{
	uint32_t i;

	/* For most situations, all three hashes are very nearly equal;
	 * as to which is faster depends on the parsed text.
	 * For both English and Russian, there are about 100 pre-defined
	 * connectors, and another 2K-4K autogen'ed ones (the IDxxx idiom
	 * connectors, and the LLxxx suffix connectors for Russian).
	 * Turns out the cost of setting up the hash table dominates the
	 * cost of collisions. */
#ifdef USE_DJB2
	/* djb2 hash */
	i = 5381;
	while (*s)
	{
		i = ((i << 5) + i) + *s;
		s++;
	}
	i += i>>14;
#endif /* USE_DJB2 */

#define USE_JENKINS
#ifdef USE_JENKINS
	/* Jenkins one-at-a-time hash */
	i = 0;
	while (*s)
	{
		i += *s;
		i += (i<<10);
		i ^= (i>>6);
		s++;
	}
	i += (i << 3);
	i ^= (i >> 11);
	i += (i << 15);
#endif /* USE_JENKINS */

	return i;
}

/**
 * hash function. Based on some tests, this seems to be an almost
 * "perfect" hash, in that almost all hash buckets have the same size!
 */
static inline unsigned int pair_hash(unsigned int table_size,
                            int lw, int rw,
                            const Connector *le, const Connector *re,
                            unsigned int cost)
{
	unsigned int i;

#if 0
	/* hash function. Based on some tests, this seems to be
	 * an almost "perfect" hash, in that almost all hash buckets
	 * have the same size! */
	i = 1 << cost;
	i += 1 << (lw % (log2_table_size-1));
	i += 1 << (rw % (log2_table_size-1));
	i += ((unsigned int) le) >> 2;
	i += ((unsigned int) le) >> log2_table_size;
	i += ((unsigned int) re) >> 2;
	i += ((unsigned int) re) >> log2_table_size;
	i += i >> log2_table_size;
#else
	/* sdbm-based hash */
	i = cost;
	i = lw + (i << 6) + (i << 16) - i;
	i = rw + (i << 6) + (i << 16) - i;
	i = ((int)(intptr_t)le) + (i << 6) + (i << 16) - i;
	i = ((int)(intptr_t)re) + (i << 6) + (i << 16) - i;
#endif

	return i & (table_size-1);
}

static inline condesc_t **condesc_find(ConTable *ct, const char *constring, int hash)
{
	size_t i = hash & (ct->size-1);

	while ((NULL != ct->hdesc[i]) &&
	       !string_set_cmp(constring, ct->hdesc[i]->string))
	{
		i = (i + 1) & (ct->size-1);
	}

	return &ct->hdesc[i];
}

static inline void condesc_table_alloc(ConTable *ct, size_t size)
{
	ct->hdesc = (condesc_t **)malloc(size * sizeof(condesc_t *));
	memset(ct->hdesc, 0, size * sizeof(condesc_t *));
	ct->size = size;
}

static inline bool condesc_insert(ConTable *ct, condesc_t **h,
                                  const char *constring, int hash)
{
	*h = (condesc_t *)malloc(sizeof(condesc_t));
	memset(*h, 0, sizeof(condesc_t));
	(*h)->str_hash = hash;
	(*h)->string = constring;
	ct->num_con++;

	return calculate_connector_info(*h);
}

#define CONDESC_TABLE_GROW_FACTOR 2

static inline bool condesc_grow(ConTable *ct)
{
	size_t old_size = ct->size;
	condesc_t **old_hdesc = ct->hdesc;

	lgdebug(+11, "Growing ConTable from %zu\n", old_size);
	condesc_table_alloc(ct, ct->size * CONDESC_TABLE_GROW_FACTOR);

	for (size_t i = 0; i < old_size; i++)
	{
		condesc_t *old_h = old_hdesc[i];
		if (NULL == old_h) continue;
		condesc_t **new_h = condesc_find(ct, old_h->string, old_h->str_hash);

		if (NULL != *new_h)
		{
			prt_error("Fatal Error: condesc_grow(): Internal error\n");
			free(old_hdesc);
			return false;
		}
		*new_h = old_h;
	}

	free(old_hdesc);
	return true;
}

static inline condesc_t *condesc_add(ConTable *ct, const char *constring)
{
	if (0 == ct->size)
	{
		condesc_table_alloc(ct, ct->num_con);
		ct->num_con = 0;
	}

	int hash = connector_str_hash(constring);
	condesc_t **h = condesc_find(ct, constring, hash);

	if (NULL == *h)
	{
		lgdebug(+11, "Creating connector '%s'\n", constring);
		if (!condesc_insert(ct, h, constring, hash)) return NULL;

		if ((8 * ct->num_con) > (3 * ct->size))
		{
			if (!condesc_grow(ct)) return NULL;
			h = condesc_find(ct, constring, hash);
		}
	}

	return *h;
}

#endif /* _LINK_GRAMMAR_CONNECTORS_H_ */
