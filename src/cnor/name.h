/** @file name.h @brief Arena-owned qualified names shared by parsing and compilation. */
#ifndef CNOR_SRC_NAME_H
#define CNOR_SRC_NAME_H
#include <cnor/arena.h>
#include <cnor/ast.h>
#include <lh/null.h>
#include <string.h>

static inline lh_bool_t
cnor_name_join(cnor_arena_t *arena, lh_str_view_t left, lh_str_view_t right, lh_str_view_t *out)
{
    lh_char_t *text = cnor_arena_alloc(arena, left.size + right.size + 1U);
    if (text == lh_null) return lh_bool_false;
    memcpy(text, left.begin, left.size);
    text[left.size] = '.';
    memcpy(text + left.size + 1U, right.begin, right.size);
    lh_str_view_init_by_size(out, text, left.size + right.size + 1U);
    return lh_bool_true;
}

/* Flatten an identifier/member chain without recursion or modifying the tree. */
static inline lh_bool_t
cnor_node_name(cnor_arena_t *arena, const cnor_node_t *node, lh_str_view_t *out)
{
    const cnor_node_t *part = node;
    lh_usize_t size = 0U;
    lh_usize_t pos;
    lh_char_t *text;
    while (part->kind == cnor_node_member)
    {
        size += part->name.size + 1U;
        part = part->data.index.target;
    }
    if (part->kind != cnor_node_ident) return lh_bool_false;
    if (part == node) { *out = part->name; return lh_bool_true; }
    size += part->name.size;
    text = cnor_arena_alloc(arena, size);
    if (text == lh_null) return lh_bool_false;
    pos = size;
    for (part = node; part->kind == cnor_node_member; part = part->data.index.target)
    {
        pos -= part->name.size;
        memcpy(text + pos, part->name.begin, part->name.size);
        text[--pos] = '.';
    }
    memcpy(text, part->name.begin, part->name.size);
    lh_str_view_init_by_size(out, text, size);
    return lh_bool_true;
}
#endif
