#include <cnor/compiler/internal.h>
#include <lh/null.h>

/*
 * Import paths, resolved by text alone: the compiler never asks a file system
 * what a path means. `/` and `\` both separate segments, `.` is dropped and
 * `..` drops the segment before it, so one file has one spelling and a cycle
 * is found by comparing paths. Symbolic links and case-insensitive file
 * systems can still give one file two spellings; it is then compiled twice.
 */

/* -- characters ------------------------------------------------------------ */

static lh_bool_t
cnor_path_is_separator(lh_char_t c)
{
    return (c == '/' || c == '\\') ? lh_bool_true : lh_bool_false;
}

static lh_bool_t
cnor_path_is_letter(lh_char_t c)
{
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ? lh_bool_true : lh_bool_false;
}

/* Bytes of the drive prefix (`C:`) @p path starts with, or 0. */
static lh_usize_t
cnor_path_drive_size(const lh_char_t *path, lh_usize_t size)
{
    return (size >= 2U && cnor_path_is_letter(path[0]) && path[1] == ':') ? 2U : 0U;
}

/* The first @p size bytes of @p text; lh views of no bytes have no data. */
static lh_str_view_t
cnor_path_view(const lh_char_t *text, lh_usize_t size)
{
    lh_str_view_t view;

    if (size == 0U)
    {
        return lh_str_view_empty();
    }
    lh_str_view_init_by_size(&view, text, size);
    return view;
}

/* -- queries --------------------------------------------------------------- */

lh_bool_t
cnor_path_is_absolute(lh_str_view_t path)
{
    const lh_char_t *text = lh_str_view_get_data(&path);
    const lh_usize_t size = lh_str_view_get_size(&path);

    return (size > 0U && (cnor_path_is_separator(text[0]) || cnor_path_drive_size(text, size) > 0U))
               ? lh_bool_true
               : lh_bool_false;
}

lh_str_view_t
cnor_path_dir(lh_str_view_t path)
{
    const lh_char_t *text = lh_str_view_get_data(&path);
    lh_usize_t size = lh_str_view_get_size(&path);

    while (size > 0U && !cnor_path_is_separator(text[size - 1U]))
    {
        --size;
    }
    if (size == 0U)
    {
        size = cnor_path_drive_size(text, lh_str_view_get_size(&path));
    }
    return cnor_path_view(text, size);
}

/* -- joining --------------------------------------------------------------- */

/**
 * @struct cnor_path_builder
 * @brief A path being written segment by segment into a buffer large enough
 *        for the joined input, which folding only shortens.
 */
struct cnor_path_builder
{
    lh_char_t *text;   /**< The buffer. */
    lh_usize_t size;   /**< Bytes written. */
    lh_usize_t root;   /**< Bytes of the prefix `..` never removes (`/`, `C:/`, `//`). */
    lh_usize_t ups;    /**< Leading `..` segments of a relative path, kept. */
    lh_usize_t count;  /**< Segments after the root, `..` ones included. */
};
/** @copybrief cnor_path_builder */
typedef struct cnor_path_builder cnor_path_builder_t;

/* Drop the last segment and the separator before it. */
static void
cnor_path_builder_pop(cnor_path_builder_t *self)
{
    while (self->size > self->root && self->text[self->size - 1U] != '/')
    {
        --self->size;
    }
    if (self->size > self->root)
    {
        --self->size;
    }
    self->count--;
}

static void
cnor_path_builder_put(cnor_path_builder_t *self, const lh_char_t *segment, lh_usize_t size)
{
    lh_usize_t i;

    if (self->count > 0U)
    {
        self->text[self->size++] = '/';
    }
    for (i = 0U; i < size; ++i)
    {
        self->text[self->size++] = segment[i];
    }
    self->count++;
}

static void
cnor_path_builder_segment(cnor_path_builder_t *self, const lh_char_t *segment, lh_usize_t size)
{
    if (size == 0U || (size == 1U && segment[0] == '.'))
    {
        return;
    }
    if (size == 2U && segment[0] == '.' && segment[1] == '.')
    {
        if (self->count > self->ups)
        {
            cnor_path_builder_pop(self);
            return;
        }
        if (self->root > 0U)
        {
            /* Above the root is the root. */
            return;
        }
        self->ups++;
    }
    cnor_path_builder_put(self, segment, size);
}

/* Every segment of @p text. */
static void
cnor_path_builder_segments(cnor_path_builder_t *self, const lh_char_t *text, lh_usize_t size)
{
    lh_usize_t begin = 0U;
    lh_usize_t i;

    for (i = 0U; i <= size; ++i)
    {
        if (i == size || cnor_path_is_separator(text[i]))
        {
            cnor_path_builder_segment(self, text + begin, i - begin);
            begin = i + 1U;
        }
    }
}

/* The root of an absolute @p text: `C:`, then `/` (`//` for a network path). */
static lh_usize_t
cnor_path_builder_root(cnor_path_builder_t *self, const lh_char_t *text, lh_usize_t size)
{
    lh_usize_t at = cnor_path_drive_size(text, size);
    lh_usize_t i;

    for (i = 0U; i < at; ++i)
    {
        self->text[self->size++] = text[i];
    }
    if (at < size && cnor_path_is_separator(text[at]))
    {
        self->text[self->size++] = '/';
        ++at;
        if (at == 1U && at < size && cnor_path_is_separator(text[at]))
        {
            self->text[self->size++] = '/';
            ++at;
        }
    }
    self->root = self->size;
    return at;
}

lh_bool_t
cnor_path_join(cnor_arena_t *arena, lh_str_view_t dir, lh_str_view_t path, lh_str_view_t *out)
{
    const lh_str_view_t first = cnor_path_is_absolute(path) ? path : dir;
    const lh_char_t *head = lh_str_view_get_data(&first);
    const lh_usize_t head_size = lh_str_view_get_size(&first);
    cnor_path_builder_t builder;
    lh_usize_t skip;

    builder.text = (lh_char_t *) cnor_arena_alloc(
        arena, lh_str_view_get_size(&dir) + lh_str_view_get_size(&path) + 2U);
    if (builder.text == lh_null)
    {
        return lh_bool_false;
    }
    builder.size = 0U;
    builder.ups = 0U;
    builder.count = 0U;
    skip = cnor_path_builder_root(&builder, head, head_size);
    cnor_path_builder_segments(&builder, head + skip, head_size - skip);
    if (!cnor_path_is_absolute(path))
    {
        cnor_path_builder_segments(&builder, lh_str_view_get_data(&path),
                                   lh_str_view_get_size(&path));
    }
    if (builder.size == 0U)
    {
        /* Everything folded away: the directory itself. */
        builder.text[builder.size++] = '.';
    }
    *out = cnor_path_view(builder.text, builder.size);
    return lh_bool_true;
}
