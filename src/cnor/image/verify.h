/**
 * @file verify.h
 * @brief Library-private: the load-time verifier.
 *
 * Proves, before a single instruction runs, that:
 *
 * - functions tile the code section in table order, starting at 0;
 * - every instruction decodes inside its own function and the last one never
 *   falls through;
 * - every operand indexes inside its table (constants, slots, globals,
 *   functions, natives) and every call passes the callee's parameter count;
 * - every jump lands on an instruction start inside the same function;
 * - the operand stack never underflows, agrees at every join, and never exceeds
 *   the function's declared depth, which itself fits the header's frame bound.
 *
 * The machine relies on this: it does not re-check operands while running.
 */

#ifndef CNOR_SRC_IMAGE_VERIFY_H
#define CNOR_SRC_IMAGE_VERIFY_H

#include <cnor/diag.h>
#include <cnor/image/view.h>

/**
 * @brief Verify @p view. On failure @p diag holds the reason and, as its
 *        offset, the code address at fault.
 */
lh_bool_t
cnor_image_verify(const cnor_image_view_t *view, cnor_diag_t *diag);

#endif /* CNOR_SRC_IMAGE_VERIFY_H */
