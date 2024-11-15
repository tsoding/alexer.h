#ifndef ALEXER_H_
#define ALEXER_H_

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define ALEXER_ARRAY_LEN(xs) (sizeof(xs)/(sizeof((xs)[0])))
#define alexer_return_defer(value) do { result = (value); goto defer; } while(0)

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} Alexer_String_Builder;

#define ALEXER_ASSERT assert
#define ALEXER_REALLOC realloc
#define ALEXER_FREE free

// Initial capacity of a dynamic array
#ifndef ALEXER_DA_INIT_CAP
#define ALEXER_DA_INIT_CAP 256
#endif

// Append several items to a dynamic array
#define alexer_da_append_many(da, new_items, new_items_count)                                  \
    do {                                                                                    \
        if ((da)->count + (new_items_count) > (da)->capacity) {                               \
            if ((da)->capacity == 0) {                                                      \
                (da)->capacity = ALEXER_DA_INIT_CAP;                                           \
            }                                                                               \
            while ((da)->count + (new_items_count) > (da)->capacity) {                        \
                (da)->capacity *= 2;                                                        \
            }                                                                               \
            (da)->items = ALEXER_REALLOC((da)->items, (da)->capacity*sizeof(*(da)->items)); \
            ALEXER_ASSERT((da)->items != NULL && "Buy more RAM lol");                          \
        }                                                                                   \
        memcpy((da)->items + (da)->count, (new_items), (new_items_count)*sizeof(*(da)->items)); \
        (da)->count += (new_items_count);                                                     \
    } while (0)

// Append a NULL-terminated string to a string builder
#define alexer_sb_append_cstr(sb, cstr)  \
    do {                              \
        const char *s = (cstr);       \
        size_t n = strlen(s);         \
        alexer_da_append_many(sb, s, n); \
    } while (0)

// Append a single NULL character at the end of a string builder. So then you can
// use it a NULL-terminated C string
#define alexer_sb_append_null(sb) alexer_da_append_many(sb, "", 1)

// TODO: support for utf-8

typedef struct {
    const char *file_path;
    size_t row;
    size_t col;
} Alexer_Loc;

#define Alexer_Loc_Fmt "%s:%zu:%zu"
#define Alexer_Loc_Arg(loc) (loc).file_path, (loc).row, (loc).col

typedef enum {
    ALEXER_INVALID,
    ALEXER_END,
    ALEXER_INT,
    ALEXER_SYMBOL,
    ALEXER_KEYWORD,
    ALEXER_PUNCT,
    ALEXER_STRING,
    ALEXER_COUNT_KINDS,
} Alexer_Kind;

static_assert(ALEXER_COUNT_KINDS == 7, "Amount of kinds have changed");
const char *alexer_kind_names[ALEXER_COUNT_KINDS] = {
    [ALEXER_INVALID] = "INVALID",
    [ALEXER_END]     = "END",
    [ALEXER_INT]     = "INT",
    [ALEXER_SYMBOL]  = "SYMBOL",
    [ALEXER_KEYWORD] = "KEYWORD",
    [ALEXER_PUNCT]   = "PUNCT",
    [ALEXER_STRING]  = "STRING",
};
#define alexer_kind_name(kind) (assert(0 <= kind && kind < ALEXER_COUNT_KINDS), alexer_kind_names[kind])

typedef struct {
    long kind;
    Alexer_Loc loc;
    const char *begin;
    const char *end;
    long int_value;
    size_t punct_index;
    size_t keyword_index;
} Alexer_Token;

typedef struct {
    const char *file_path;
    const char *content;
    size_t size;

    size_t cur;
    size_t bol;
    size_t row;

    const char **puncts;
    size_t puncts_count;
    const char **keywords;
    size_t keywords_count;
    void (*diagf)(Alexer_Loc loc, const char *level, const char *fmt, ...);
} Alexer;

void alexer_default_diagf(Alexer_Loc loc, const char *level, const char *fmt, ...);
void alexer_ignore_diagf(Alexer_Loc loc, const char *level, const char *fmt, ...);
Alexer alexer_create(const char *file_path, const char *content, size_t size);
bool alexer_chop_char(Alexer *l);
void alexer_chop_chars(Alexer *l, size_t n);
void alexer_trim_left_ws(Alexer *l);
Alexer_Loc alexer_loc(Alexer *l);
bool alexer_is_symbol(char x); // TODO: Configurable alexer_is_symbol()
bool alexer_is_symbol_start(char x); // TODO: Configurable alexer_is_symbol_start()
bool alexer_starts_with(Alexer *l, const char *prefix, size_t len);
// alexer_get_token()
//   Gets the next token. Returns false on END or INVALID. Returns true on any other kind of token.
bool alexer_get_token(Alexer *l, Alexer_Token *t);
bool alexer_expect_kind(Alexer *l, Alexer_Token t, Alexer_Kind kind);
bool alexer_expect_one_of_kinds(Alexer *l, Alexer_Token t, Alexer_Kind *kinds, size_t kinds_size);
bool alexer_expect_punct(Alexer *l, Alexer_Token t, size_t punct_index);
bool alexer_expect_one_of_puncts(Alexer *l, Alexer_Token t, size_t *punct_indices, size_t punct_indices_count);
bool alexer_expect_keyword(Alexer *l, Alexer_Token t, size_t keyword_index);
bool alexer_expect_one_of_keywords(Alexer *l, Alexer_Token t, size_t *keyword_indices, size_t keyword_indices_count);

#endif // ALEXER_H_

#ifdef ALEXER_IMPLEMENTATION

Alexer alexer_create(const char *file_path, const char *content, size_t size)
{
    return (Alexer) {
        .file_path = file_path,
        .content = content,
        .size = size,
        .diagf = alexer_default_diagf,
    };
}

bool alexer_chop_char(Alexer *l)
{
    if (l->cur < l->size) {
        char x = l->content[l->cur];
        l->cur++;
        if (x == '\n') {
            l->bol = l->cur;
            l->row += 1;
        }
        return true;
    }
    return false;
}

void alexer_chop_chars(Alexer *l, size_t n)
{
    while (n --> 0 && alexer_chop_char(l));
}

void alexer_trim_left_ws(Alexer *l)
{
    // TODO: configurable isspace()
    while (l->cur < l->size && isspace(l->content[l->cur])) {
        alexer_chop_char(l);
    }
}

Alexer_Loc alexer_loc(Alexer *l)
{
    return (Alexer_Loc) {
        .file_path = l->file_path,
        .row = l->row + 1,
        .col = l->cur - l->bol + 1,
    };
}

bool alexer_is_symbol(char x)
{
    return isalnum(x) || x == '_';
}

bool alexer_is_symbol_start(char x)
{
    return isalpha(x) || x == '_';
}

bool alexer_starts_with(Alexer *l, const char *prefix, size_t len)
{
    for (size_t i = 0; l->cur + i < l->size && i < len; ++i) {
        if (l->content[l->cur + i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool alexer_get_token(Alexer *l, Alexer_Token *t)
{
    alexer_trim_left_ws(l);

    memset(t, 0, sizeof(*t));
    t->loc = alexer_loc(l);
    t->begin = &l->content[l->cur];
    t->end = &l->content[l->cur];

    if (l->cur >= l->size) {
        t->kind = ALEXER_END;
        return false;
    }

    // Puncts
    for (size_t i = 0; i < l->puncts_count; ++i) {
        size_t n = strlen(l->puncts[i]);
        if (alexer_starts_with(l, l->puncts[i], n)) {
            t->kind = ALEXER_PUNCT;
            t->punct_index = i;
            t->end += n;
            alexer_chop_chars(l, n);
            return true;
        }
    }

    // Int
    if (isdigit(l->content[l->cur])) {
        t->kind = ALEXER_INT;
        while (l->cur < l->size && isdigit(l->content[l->cur])) {
            t->int_value = t->int_value*10 + l->content[l->cur] - '0';
            t->end += 1;
            alexer_chop_char(l);
        }
        return true;
    }

    // Symbol
    if (alexer_is_symbol_start(l->content[l->cur])) {
        t->kind = ALEXER_SYMBOL;
        while (l->cur < l->size && alexer_is_symbol(l->content[l->cur])) {
            t->end += 1;
            alexer_chop_char(l);
        }

        // Keyword
        for (size_t i = 0; i < l->keywords_count; ++i) {
            size_t n = strlen(l->keywords[i]);
            if (n == (size_t)(t->end - t->begin) && memcmp(l->keywords[i], t->begin, n) == 0) {
                t->kind = ALEXER_KEYWORD;
                t->keyword_index = i;
                break;
            }
        }

        return true;
    }

    alexer_chop_char(l);
    t->end += 1;
    return false;
}

bool alexer_expect_punct(Alexer *l, Alexer_Token t, size_t punct_index)
{
    return alexer_expect_one_of_puncts(l, t, &punct_index, 1);
}

bool alexer_expect_one_of_puncts(Alexer *l, Alexer_Token t, size_t *punct_indices, size_t punct_indices_count)
{
    bool result = false;
    Alexer_String_Builder sb = {0};
    assert(punct_indices_count > 0);
    if (!alexer_expect_kind(l, t, ALEXER_PUNCT)) alexer_return_defer(false);
    for (size_t i = 0; i < punct_indices_count; ++i) {
        if (t.punct_index == punct_indices[i]) {
            alexer_return_defer(true);
        }
    }

    for (size_t i = 0; i < punct_indices_count; ++i) {
        if (i > 0) alexer_sb_append_cstr(&sb, ", ");
        assert(punct_indices[i] < l->puncts_count);
        alexer_sb_append_cstr(&sb, "`");
        alexer_sb_append_cstr(&sb, l->puncts[punct_indices[i]]);
        alexer_sb_append_cstr(&sb, "`");
    }
    alexer_sb_append_null(&sb);

    assert(t.punct_index < l->puncts_count);
    l->diagf(t.loc, "ERROR", "Expected %s but got `%s`", sb.items, l->puncts[t.punct_index]);

defer:
    free(sb.items);
    return result;
}

bool alexer_expect_keyword(Alexer *l, Alexer_Token t, size_t keyword_index)
{
    return alexer_expect_one_of_keywords(l, t, &keyword_index, 1);
}

bool alexer_expect_one_of_keywords(Alexer *l, Alexer_Token t, size_t *keyword_indices, size_t keyword_indices_count)
{
    bool result = false;
    Alexer_String_Builder sb = {0};
    assert(keyword_indices_count > 0);
    if (!alexer_expect_kind(l, t, ALEXER_KEYWORD)) return false;
    for (size_t i = 0; i < keyword_indices_count; ++i) {
        if (t.keyword_index == keyword_indices[i]) {
            alexer_return_defer(true);
        }
    }

    for (size_t i = 0; i < keyword_indices_count; ++i) {
        if (i > 0) alexer_sb_append_cstr(&sb, ", ");
        assert(keyword_indices[i] < l->keywords_count);
        alexer_sb_append_cstr(&sb, "`");
        alexer_sb_append_cstr(&sb, l->keywords[keyword_indices[i]]);
        alexer_sb_append_cstr(&sb, "`");
    }
    alexer_sb_append_null(&sb);

    assert(t.keyword_index < l->keywords_count);
    if (keyword_indices_count == 1) {
        l->diagf(t.loc, "ERROR", "Expected keyword %s but got keyword `%s`", sb.items, l->keywords[t.keyword_index]);
    } else {
        l->diagf(t.loc, "ERROR", "Expected keywords %s but got keyword `%s`", sb.items, l->keywords[t.keyword_index]);
    }

defer:
    free(sb.items);
    return result;
}

bool alexer_expect_kind(Alexer *l, Alexer_Token t, Alexer_Kind kind)
{
    return alexer_expect_one_of_kinds(l, t, &kind, 1);
}

bool alexer_expect_one_of_kinds(Alexer *l, Alexer_Token t, Alexer_Kind *kinds, size_t kinds_size)
{
    bool result = false;
    Alexer_String_Builder sb = {0};

    for (size_t i = 0; i < kinds_size; ++i) {
        if (t.kind == kinds[i]) {
            alexer_return_defer(true);
        }
    }

    for (size_t i = 0; i < kinds_size; ++i) {
        if (i > 0) alexer_sb_append_cstr(&sb, ", ");
        alexer_sb_append_cstr(&sb, alexer_kind_name(kinds[i]));
    }
    alexer_sb_append_null(&sb);

    if (t.kind == ALEXER_END) {
        l->diagf(t.loc, "ERROR", "Expected %s but got %s", sb.items, alexer_kind_name(t.kind));
    } else {
        l->diagf(t.loc, "ERROR", "Expected %s but got %s `%.*s`", sb.items, alexer_kind_name(t.kind), t.end - t.begin, t.begin);
    }

defer:
    free(sb.items);
    return result;
}

void alexer_default_diagf(Alexer_Loc loc, const char *level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, Alexer_Loc_Fmt": %s: ", Alexer_Loc_Arg(loc), level);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void alexer_ignore_diagf(Alexer_Loc loc, const char *level, const char *fmt, ...)
{
    (void) loc;
    (void) level;
    (void) fmt;
}

#endif // ALEXER_IMPLEMENTATION
