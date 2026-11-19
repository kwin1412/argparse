/* Portable argparse: nvme-cli-style API, ISO C99 only. SPDX-License-Identifier: MIT */
#ifndef PORTABLE_ARGPARSE_H
#define PORTABLE_ARGPARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AP_COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef enum ap_type {
    AP_TYPE_END = 0,
    AP_TYPE_FLAG, AP_TYPE_INCR,
    AP_TYPE_UINT, AP_TYPE_INT,
    AP_TYPE_U8, AP_TYPE_U16, AP_TYPE_U32, AP_TYPE_U64,
    AP_TYPE_I32, AP_TYPE_I64,
    AP_TYPE_STR, AP_TYPE_CSTR
} ap_type;

/* Optional symbolic aliases for unsigned numeric options. End with AP_VALUE_END().
 * Numeric values not in this list remain valid; aliases are additional names. */
typedef struct ap_named_value {
    const char *name;
    uint64_t value;
    const char *description;
} ap_named_value;

#define AP_VALUE(name, number, description) { (name), (number), (description) }
#define AP_VALUE_END() { NULL, 0u, NULL }

enum {
    AP_OPT_REQUIRED = 1u,
    AP_OPT_POSITIONAL = 2u
};

typedef struct ap_option {
    const char *name;
    char short_name;
    ap_type type;
    void *destination;
    const char *description;
    const ap_named_value *values;
    const char *metavar;
    unsigned flags;
    unsigned seen;   /* Internal. Each parse resets this field. */
} ap_option;

/* Keep declarations in the command handler, as in nvme-cli. */
#define OPT_ARGS(name) ap_option name[]
#define AP_OPT(n, s, t, p, d) \
    { (n), (s), (t), (void *)(p), (d), NULL, NULL, 0u, 0u }
#define AP_OPT_VAL(n, s, t, p, d, v) \
    { (n), (s), (t), (void *)(p), (d), (v), NULL, 0u, 0u }
#define AP_OPT_REQ(n, s, t, p, d) \
    { (n), (s), (t), (void *)(p), (d), NULL, NULL, AP_OPT_REQUIRED, 0u }

#define OPT_FLAG(n, s, p, d) AP_OPT(n, s, AP_TYPE_FLAG, p, d)
#define OPT_INCR(n, s, p, d) AP_OPT(n, s, AP_TYPE_INCR, p, d)
#define OPT_INT(n, s, p, d) AP_OPT(n, s, AP_TYPE_INT, p, d)
#define OPT_U8(n, s, p, d) AP_OPT(n, s, AP_TYPE_U8, p, d)
#define OPT_U16(n, s, p, d) AP_OPT(n, s, AP_TYPE_U16, p, d)
#define OPT_U32(n, s, p, d) AP_OPT(n, s, AP_TYPE_U32, p, d)
#define OPT_U64(n, s, p, d) AP_OPT(n, s, AP_TYPE_U64, p, d)
#define OPT_I32(n, s, p, d) AP_OPT(n, s, AP_TYPE_I32, p, d)
#define OPT_I64(n, s, p, d) AP_OPT(n, s, AP_TYPE_I64, p, d)
/* OPT_STR takes char **, OPT_CSTR takes const char **. No allocation/copy. */
#define OPT_STR(n, s, p, d) AP_OPT(n, s, AP_TYPE_STR, p, d)
#define OPT_CSTR(n, s, p, d) AP_OPT(n, s, AP_TYPE_CSTR, p, d)
#define OPT_FMT(n, s, p, d) OPT_STR(n, s, p, d)

/* OPT_UINT takes unsigned int *, like the corresponding nvme-cli option.
 * Both OPT_UINT(name, short, ptr, help) and (..., help, ap_named_value[]) work. */
#define AP_OPT_UINT_4(n, s, p, d) AP_OPT(n, s, AP_TYPE_UINT, p, d)
#define AP_OPT_UINT_5(n, s, p, d, v) AP_OPT_VAL(n, s, AP_TYPE_UINT, p, d, v)
#define AP_SELECT_5(_1, _2, _3, _4, _5, name, ...) name
#define OPT_UINT(...) AP_SELECT_5(__VA_ARGS__, AP_OPT_UINT_5, AP_OPT_UINT_4, AP_OPT_UINT_4)(__VA_ARGS__)

#define OPT_REQ_UINT(n, s, p, d) AP_OPT_REQ(n, s, AP_TYPE_UINT, p, d)
#define OPT_REQ_U32(n, s, p, d) AP_OPT_REQ(n, s, AP_TYPE_U32, p, d)
#define OPT_REQ_STR(n, s, p, d) AP_OPT_REQ(n, s, AP_TYPE_STR, p, d)
#define OPT_REQ_CSTR(n, s, p, d) AP_OPT_REQ(n, s, AP_TYPE_CSTR, p, d)

/* Positionals are opt-in, and their requiredness is encoded in the macro:
 * no min_args/max_args and no positional scratch array. */
#define OPT_POSITIONAL(n, p, d) \
    { NULL, 0, AP_TYPE_STR, (void *)(p), (d), NULL, (n), \
      AP_OPT_REQUIRED | AP_OPT_POSITIONAL, 0u }
#define OPT_POSITIONAL_OPT(n, p, d) \
    { NULL, 0, AP_TYPE_STR, (void *)(p), (d), NULL, (n), \
      AP_OPT_POSITIONAL, 0u }
#define OPT_END() { NULL, 0, AP_TYPE_END, NULL, NULL, NULL, NULL, 0u, 0u }

/* 0 = parsed successfully, 1 = help printed, negative = parse error. */
enum { AP_PARSE_OK = 0, AP_PARSE_HELP = 1, AP_PARSE_ERROR = -1 };

/* Internal entry points. Prefer the four-argument parse_args() macro. */
int ap_parse_args_impl(int argc, char *argv[], const char *description,
                       ap_option options[], size_t count);
bool ap_given_impl(const ap_option options[], size_t count,
                   const void *destination);
bool ap_seen_impl(const ap_option options[], size_t count,
                  const char *long_name);
void ap_print_args_help(FILE *out, const char *program,
                        const char *description,
                        const ap_option options[], size_t count);

/* sizeof() determines count at compile time. No buffer/capacity bookkeeping. */
#define parse_args(argc, argv, description, options) \
    ap_parse_args_impl((argc), (argv), (description), (options), AP_COUNT(options))
#define argconfig_given(options, destination) \
    ap_given_impl((options), AP_COUNT(options), (destination))
#define argconfig_parse_seen(options, name) \
    ap_seen_impl((options), AP_COUNT(options), (name))

typedef int (*ap_command_handler)(int argc, char *argv[]);
typedef struct ap_command {
    const char *name;
    const char *description;
    ap_command_handler handler;
    const struct ap_command *children;
    size_t child_count;
} ap_command;

#define AP_COMMAND(n, fn, desc) { (n), (desc), (fn), NULL, 0u }
#define AP_GROUP(n, desc, commands) \
    { (n), (desc), NULL, (commands), AP_COUNT(commands) }
#define AP_COMMAND_END() { NULL, NULL, NULL, NULL, 0u }

int ap_dispatch_impl(int argc, char *argv[], const char *description,
                     const ap_command commands[], size_t count);
#define dispatch_commands(argc, argv, description, commands) \
    ap_dispatch_impl((argc), (argv), (description), (commands), AP_COUNT(commands))

#ifdef __cplusplus
}
#endif
#endif
