#include "key_def.h"
/**
 * Returns hash function appropriate to key_def
 * @param key_def - key_def for field description
 * @return function
 */
tuple_hash_t
tuple_hash_create(const struct key_def *def);

/**
 * Returns hash function appropriate to key_def
 * @param key_def - key_def for field description
 * @details Returned function:
 * 	Calculate a common hash value for a tuple
 * 	-param key - full key (msgpack fields w/o array marker)
 * 	-param key_def - key_def for field description
 * 	-return - hash value
 * @return function
 */
key_hash_t
key_hash_create(const struct key_def *def);
