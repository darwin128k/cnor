#include "args.h"

#include <lh/null.h>
#include <lh/runtime/allocator.h>
#include <lh/size.h>

#if defined(_WIN32)
#    include <windows.h>
#    include <shellapi.h>
#endif

#if defined(_WIN32)

/* -- windows --------------------------------------------------------------- */

/* Bytes of @p wide as UTF-8, its terminator included; 0 on a bad sequence. */
static lh_usize_t
cnor_cli_args_utf8_size(const wchar_t *wide)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, lh_null, 0, lh_null, lh_null);

    return size > 0 ? (lh_usize_t) size : 0U;
}

/* One block: the pointer array, then every string. */
static lh_bool_t
cnor_cli_args_convert(cnor_cli_args_t *self, wchar_t **wide, int count)
{
    lh_usize_t total = (lh_usize_t) count * sizeof(lh_char_t *);
    lh_char_t **values;
    lh_char_t *text;
    int i;

    for (i = 1; i < count; ++i)
    {
        const lh_usize_t size = cnor_cli_args_utf8_size(wide[i]);

        if (size == 0U)
        {
            return lh_bool_false;
        }
        total += size;
    }
    values = (lh_char_t **) lh_runtime_allocator_alloc(total);
    if (values == lh_null)
    {
        return lh_bool_false;
    }
    text = (lh_char_t *) (values + count);
    for (i = 1; i < count; ++i)
    {
        const lh_usize_t size = cnor_cli_args_utf8_size(wide[i]);

        (void) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text, (int) size, lh_null, lh_null);
        values[i - 1] = text;
        text += size;
    }
    self->count = count - 1;
    self->values = values;
    self->allocation = values;
    return lh_bool_true;
}

lh_bool_t
cnor_cli_args_init(cnor_cli_args_t *self, int argc, lh_char_t **argv)
{
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    lh_bool_t ok;

    (void) argc;
    (void) argv;
    self->count = 0;
    self->values = lh_null;
    self->allocation = lh_null;
    if (wide == lh_null)
    {
        return lh_bool_false;
    }
    ok = count > 0 ? cnor_cli_args_convert(self, wide, count) : lh_bool_true;
    (void) LocalFree(wide);
    return ok;
}

#else

/* -- elsewhere ------------------------------------------------------------- */

lh_bool_t
cnor_cli_args_init(cnor_cli_args_t *self, int argc, lh_char_t **argv)
{
    self->count = argc > 1 ? argc - 1 : 0;
    self->values = argc > 1 ? argv + 1 : lh_null;
    self->allocation = lh_null;
    return lh_bool_true;
}

#endif

void
cnor_cli_args_deinit(cnor_cli_args_t *self)
{
    if (self->allocation != lh_null)
    {
        lh_runtime_allocator_free(self->allocation);
    }
    self->count = 0;
    self->values = lh_null;
    self->allocation = lh_null;
}
