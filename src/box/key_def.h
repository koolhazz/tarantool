#ifndef TARANTOOL_BOX_KEY_DEF_H_INCLUDED
#define TARANTOOL_BOX_KEY_DEF_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "trivia/util.h"
#include "small/rlist.h"
#include "error.h"
#include "diag.h"
#include <msgpuck.h>
#define RB_COMPACT 1
#include "small/rb.h"
#include <limits.h>
#include <wchar.h>
#include <wctype.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	BOX_ENGINE_MAX = 3, /* + 1 to the actual number of engines */
	BOX_SPACE_MAX = INT32_MAX,
	BOX_FUNCTION_MAX = 32000,
	BOX_INDEX_MAX = 128,
	BOX_NAME_MAX = 64,
	BOX_ENGINE_NAME_MAX = 7,
	BOX_FIELD_MAX = INT32_MAX,
	BOX_USER_MAX = 32,
	/**
	 * A fairly arbitrary limit which is still necessary
	 * to keep tuple_format object small.
	 */
	BOX_INDEX_FIELD_MAX = INT16_MAX,
	/** Yet another arbitrary limit which simply needs to
	 * exist.
	 */
	BOX_INDEX_PART_MAX = UINT8_MAX
};

/*
 * Different objects which can be subject to access
 * control.
 *
 * Use 0 for unknown to use the same index consistently
 * even when there are more object types in the future.
 */
enum schema_object_type {
	SC_UNKNOWN = 0, SC_UNIVERSE = 1, SC_SPACE = 2, SC_FUNCTION = 3,
	SC_USER = 4, SC_ROLE = 5
};

enum schema_object_type
schema_object_type(const char *name);

const char *
schema_object_name(enum schema_object_type type);

/*
 * Possible field data types. Can't use STRS/ENUM macros for them,
 * since there is a mismatch between enum name (STRING) and type
 * name literal ("STR"). STR is already used as Objective C type.
 */
enum field_type {
	FIELD_TYPE_ANY = 0,
	FIELD_TYPE_UNSIGNED,
	FIELD_TYPE_STRING,
	FIELD_TYPE_ARRAY,
	FIELD_TYPE_NUMBER,
	FIELD_TYPE_INTEGER,
	FIELD_TYPE_SCALAR,
	field_type_MAX
};
extern const char *field_type_strs[];

/* MsgPack type names */
extern const char *mp_type_strs[];

/**
 * The supported language of the stored function.
 */
enum func_language {
	FUNC_LANGUAGE_LUA,
	FUNC_LANGUAGE_C,
	func_language_MAX,
};
extern const char *func_language_strs[];

static inline uint32_t
field_type_maxlen(enum field_type type)
{
	static const uint32_t maxlen[] =
		{ UINT32_MAX, 8, UINT32_MAX, UINT32_MAX, UINT32_MAX };
	return maxlen[type];
}

enum field_type
field_type_by_name(const char *name);

enum index_type {
	HASH = 0, /* HASH Index */
	TREE,     /* TREE Index */
	BITSET,   /* BITSET Index */
	RTREE,    /* R-Tree Index */
	index_type_MAX,
};

extern const char *index_type_strs[];

enum opt_type {
	OPT_BOOL,	/* bool */
	OPT_INT,	/* int64_t */
	OPT_FLOAT,	/* double */
	OPT_STR,	/* char[] */
	OPT_STRP,	/* char* */
	opt_type_MAX,
};

extern const char *opt_type_strs[];

struct opt_def {
	const char *name;
	enum opt_type type;
	ptrdiff_t offset;
	uint32_t len;
};

#define OPT_DEF(key, type, opts, field) \
	{ key, type, offsetof(opts, field), sizeof(((opts *)0)->field) }

enum rtree_index_distance_type {
	 /* Euclid distance, sqrt(dx*dx + dy*dy) */
	RTREE_INDEX_DISTANCE_TYPE_EUCLID,
	/* Manhattan distance, fabs(dx) + fabs(dy) */
	RTREE_INDEX_DISTANCE_TYPE_MANHATTAN,
	rtree_index_distance_type_MAX
};
extern const char *rtree_index_distance_type_strs[];

/** Descriptor of a single part in a multipart key. */
struct key_part {
	uint32_t fieldno;
	enum field_type type;
};

/** Index options */
struct key_opts {
	/**
	 * Is this index unique or not - relevant to HASH/TREE
	 * index
	 */
	bool is_unique;
	/**
	 * RTREE index dimension.
	 */
	int64_t dimension;
	/**
	 * RTREE distance type.
	 */
	char distancebuf[16];
	enum rtree_index_distance_type distance;
	/**
	 * Vinyl index options.
	 */
	const char *path;
	int64_t range_size;
	int64_t page_size;
	/**
	 * Maximal number of runs that can be created in a level
	 * of the LSM tree before triggering compaction.
	 */
	int64_t run_count_per_level;
	/**
	 * The LSM tree multiplier. Each subsequent level of
	 * the LSM tree is run_size_ratio times larger than
	 * previous one.
	 */
	double run_size_ratio;
	/**
	 * LSN from the time of index creation.
	 */
	int64_t lsn;
};

extern const struct key_opts key_opts_default;
extern const struct opt_def key_opts_reg[];

static inline int
key_opts_cmp(const struct key_opts *o1, const struct key_opts *o2)
{
	if (o1->is_unique != o2->is_unique)
		return o1->is_unique < o2->is_unique ? -1 : 1;
	if (o1->dimension != o2->dimension)
		return o1->dimension < o2->dimension ? -1 : 1;
	if (o1->distance != o2->distance)
		return o1->distance < o2->distance ? -1 : 1;
	return 0;
}

struct key_def;
struct tuple;

typedef int (*tuple_compare_with_key_t)(const struct tuple *tuple_a,
			      const char *key,
			      uint32_t part_count,
			      const struct key_def *key_def);
typedef int (*tuple_compare_t)(const struct tuple *tuple_a,
			   const struct tuple *tuple_b,
			   const struct key_def *key_def);

/* Descriptor of a multipart key. */
struct key_def {
	/* A link in key list. */
	struct rlist link;
	/** Index name. */
	const char *name;
	/** Ordinal index number in the index array. */
	uint32_t iid;
	/* Space id. */
	uint32_t space_id;
	/** Index type. */
	enum index_type type;
	struct key_opts opts;
	/** tuple <-> tuple comparison function */
	tuple_compare_t tuple_compare;
	/** tuple <-> key comparison function */
	tuple_compare_with_key_t tuple_compare_with_key;
	/** The size of the 'parts' array. */
	uint32_t part_count;
	/** Description of parts of a multipart index. */
	struct key_part parts[];
};

struct key_def *
key_def_dup(const struct key_def *def);

/* Destroy and free a key_def. */
void
key_def_delete(struct key_def *def);

/**
 * Encapsulates privileges of a user on an object.
 * I.e. "space" object has an instance of this
 * structure for each user.
 */
struct access {
	/**
	 * Granted access has been given to a user explicitly
	 * via some form of a grant.
	 */
	uint8_t granted;
	/**
	 * Effective access is a sum of granted access and
	 * all privileges inherited by a user on this object
	 * via some role. Since roles may be granted to other
	 * roles, this may include indirect grants.
	 */
	uint8_t effective;
};

/**
 * Effective session user. A cache of user data
 * and access stored in session and fiber local storage.
 * Differs from the authenticated user when executing
 * setuid functions.
 */
struct credentials {
	/** A look up key to quickly find session user. */
	uint8_t auth_token;
	/**
	 * Cached global grants, to avoid an extra look up
	 * when checking global grants.
	 */
	uint8_t universal_access;
	/** User id of the authenticated user. */
	uint32_t uid;
};

/**
 * Definition of a function. Function body is not stored
 * or replicated (yet).
 */
struct func_def {
	/** Function id. */
	uint32_t fid;
	/** Owner of the function. */
	uint32_t uid;
	/**
	 * True if the function requires change of user id before
	 * invocation.
	 */
	bool setuid;
	/**
	 * The language of the stored function.
	 */
	enum func_language language;
	/** Function name. */
	char name[BOX_NAME_MAX + 1];
};

/**
 * Definition of a privilege
 */
struct priv_def {
	/** Who grants the privilege. */
	uint32_t grantor_id;
	/** Whom the privilege is granted. */
	uint32_t grantee_id;
	/* Object id - is only defined for object type */
	uint32_t object_id;
	/* Object type - function, space, universe */
	enum schema_object_type object_type;
	/**
	 * What is being granted, has been granted, or is being
	 * revoked.
	 */
	uint8_t access;
	/** To maintain a set of effective privileges. */
	rb_node(struct priv_def) link;
};

/** Space options */
struct space_opts {
        /**
	 * The space is a temporary:
	 * - it is empty at server start
	 * - changes are not written to WAL
	 * - changes are not part of a snapshot
	 */
	bool temporary;
};

extern const struct space_opts space_opts_default;
extern const struct opt_def space_opts_reg[];

/** Space metadata. */
struct space_def {
	/** Space id. */
	uint32_t id;
	/** User id of the creator of the space */
	uint32_t uid;
	/**
	 * If not set (is 0), any tuple in the
	 * space can have any number of fields.
	 * If set, each tuple
	 * must have exactly this many fields.
	 */
	uint32_t exact_field_count;
	struct space_opts opts;
	const char *name;
	char engine_name[BOX_ENGINE_NAME_MAX + 1];
};

/**
 * API of C stored function.
 */
typedef struct box_function_ctx box_function_ctx_t;
typedef int (*box_function_f)(box_function_ctx_t *ctx,
	     const char *args, const char *args_end);

/**
 * Allocate a new key definition.
 * @retval not NULL Success.
 * @retval NULL     Memory error.
 */
struct key_def *
key_def_new(uint32_t space_id, uint32_t iid, size_t name_len,
	    enum index_type type, const struct key_opts *opts,
	    uint32_t part_count);

/**
 * Set name within a key def.
 * Assumes the key def has enough capacity to store the name.
 */
void
key_def_set_name(struct key_def *def, const char *name, size_t name_len);

/**
 * Set a single key part in a key def.
 * @pre part_no < part_count
 */
void
key_def_set_part(struct key_def *def, uint32_t part_no,
		 uint32_t fieldno, enum field_type type);

/**
 * Returns the part in key_def->parts for the specified fieldno.
 * If fieldno is not in key_def->parts returns NULL.
 */
const struct key_part *
key_def_find(const struct key_def *key_def, uint32_t fieldno);

/**
 * Allocate a new key_def with a set union of key parts from
 * first and second key defs. Parts of the new key_def consist
 * of the first key_def's parts and those parts of the second
 * key_def that were not among the first parts.
 * @retval not NULL Ok.
 * @retval NULL     Memory error.
 */
struct key_def *
key_def_merge(const struct key_def *first, const struct key_def *second);

/*
 * Check that parts of the key match with the key definition.
 * @param key_def Key definition.
 * @param key MessagePack'ed data for matching.
 * @param part_count Field count in the key.
 *
 * @retval 0  The key is valid.
 * @retval -1 The key is invalid.
 */
int
key_validate_parts(struct key_def *key_def, const char *key,
		   uint32_t part_count);

/**
 * Return true if @a key_def defines a sequential key without
 * holes starting from the first field. In other words, for all
 * key parts key_def->parts[part_id].fieldno == part_id.
 * @param key_def key_def
 * @retval true key_def is sequential
 * @retval false otherwise
 */
static inline bool
key_def_is_sequential(const struct key_def *key_def)
{
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		if (key_def->parts[part_id].fieldno != part_id)
			return false;
	}
	return true;
}

/** A helper table for key_mp_type_validate */
extern const uint32_t key_mp_type[];

/**
 * @brief Checks if \a field_type (MsgPack) is compatible \a type (KeyDef).
 * @param type KeyDef type
 * @param field_type MsgPack type
 * @param field_no - a field number (is used to store an error message)
 *
 * @retval 0  mp_type is valid.
 * @retval -1 mp_type is invalid.
 */
static inline int
key_mp_type_validate(enum field_type key_type, enum mp_type mp_type,
	       int err, uint32_t field_no)
{
	assert(key_type < field_type_MAX);
	assert((size_t) mp_type < CHAR_BIT * sizeof(*key_mp_type));
	if (unlikely((key_mp_type[key_type] & (1U << mp_type)) == 0)) {
		diag_set(ClientError, err, field_no, field_type_strs[key_type]);
		return -1;
	}
	return 0;
}

#if defined(__cplusplus)
} /* extern "C" */

/** Compare two key part arrays.
 *
 * This function is used to find out whether alteration
 * of an index has changed it substantially enough to warrant
 * a rebuild or not. For example, change of index id is
 * not a substantial change, whereas change of index type
 * or key parts requires a rebuild.
 *
 * One key part is considered to be greater than the other if:
 * - its fieldno is greater
 * - given the same fieldno, NUM < STRING
 *   (coarsely speaking, based on field_type_maxlen()).
 *
 * A key part array is considered greater than the other if all
 * its key parts are greater, or, all common key parts are equal
 * but there are additional parts in the bigger array.
 */
int
key_part_cmp(const struct key_part *parts1, uint32_t part_count1,
	     const struct key_part *parts2, uint32_t part_count2);

/**
 * One key definition is greater than the other if it's id is
 * greater, it's name is greater,  it's index type is greater
 * (HASH < TREE < BITSET) or its key part array is greater.
 */
int
key_def_cmp(const struct key_def *key1, const struct key_def *key2);

/** Add a key to the list of keys. */
static inline  void
key_list_add_key(struct rlist *key_list, struct key_def *key)
{
	rlist_add_entry(key_list, key, link);
}

/** Remove a key from the list of keys. */
void
key_list_del_key(struct rlist *key_list, uint32_t id);

/**
 * Check a key definition for violation of various limits.
 *
 * @param key_def   key_def
 */
void
key_def_check(struct key_def *key_def);

/** Check space definition structure for errors. */
void
space_def_check(struct space_def *def, int32_t errcode);

/**
 * Given a tuple with an index definition, update the LSN stored
 * in the index options.
 *
 * @return a tuple with updated lsn in key def. The returned tuple
 *         is blessed (referenced by box_tuple_bless()).
 *
 * Throws an exception if error.
 *
 * @note Implemented in alter.cc
 */
extern struct tuple *
key_def_tuple_update_lsn(struct tuple *tuple, int64_t lsn);

/**
 * Check object identifier for invalid symbols.
 * The function checks \a str for matching [a-zA-Z_][a-zA-Z0-9_]* expression.
 * Result is locale-dependent.
 */
bool
identifier_is_valid(const char *str);

/**
 * Throw an error if identifier is not valid.
 */
void
identifier_check(const char *str);

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_KEY_DEF_H_INCLUDED */
