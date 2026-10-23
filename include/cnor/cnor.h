/**
 * @file cnor.h
 * @brief The library's own interface: which cnor this binary is.
 *
 * Not an umbrella. Each part of the language gets its own header, so a program
 * includes what it uses and nothing else.
 */

#ifndef CNOR_H
#define CNOR_H

#include <lh/version.h>

LH_COMPILER_EXTERN_C_BEGIN

/**
 * @brief Return a pointer to the compiled library version.
 *        The returned pointer refers to a static object; do not free it.
 *
 * @return Pointer to the ::lh_version_t describing this build.
 *
 * Example usage:
 * @code{.c}
 * const lh_version_t *v = cnor_get_version();
 * lh_version_major_t major = lh_version_get_major(v);
 * @endcode
 */
const lh_version_t *
cnor_get_version(void);

LH_COMPILER_EXTERN_C_END

#endif /* CNOR_H */
