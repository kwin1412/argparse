/*
 * Portable argparse: a dependency-free, C99 command-line argument parser.
 * SPDX-License-Identifier: MIT
 */
#ifndef PORTABLE_ARGPARSE_H
#define PORTABLE_ARGPARSE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AP_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define AP_ANY_ARGS ((size_t)-1)
#define AP_OPT_REQUIRED (1u << 0)

typedef enum ap_type {
    AP_FLAG,     /* bool *          : set to true */
    AP_COUNT_FLAG, /* unsigned int *: increment on every occurrence */
    AP_U8,       /* uint8_t *       : decimal or 0x hexadecimal */
    AP_U16,      /* uint16_t * */
    AP_U32,      /* uint32_t * */
    AP_U64,      /* uint64_t * */
    AP_I32,      /* int32_t *       : signed decimal or 0x hexadecimal */
    AP_I64,      /* int64_t * */
    AP_STRING,   /* const char **   : points into argv, not copied */
    AP_CHOICE    /* const char **   : must match a '|' separated choice */
} ap_type;

typedef struct ap_option {
    const char *long_name;  /* without --; NULL for short-only option */
    char short_name;        /* 0 for long-only option */
    ap_type type;
    void *value;            /* caller-owned destination */
    const char *metavar;    /* shown in help; may be NULL */
    const char *help;       /* may be NULL */
    unsigned flags;         /* AP_OPT_REQUIRED, or 0 */
    const char *choices;    /* AP_CHOICE: e.g. "normal|json|binary" */
} ap_option;

typedef struct ap_command {
    const char *name;
    const char *description;
    const char *args_usage; /* e.g. "<device>"; display only */
    const ap_option *options;
    size_t option_count;
    const struct ap_command *subcommands;
    size_t subcommand_count;
    size_t min_args;
    size_t max_args;
} ap_command;

typedef enum ap_status {
    AP_OK = 0,
    AP_HELP,
    AP_ERR_ARGUMENT,
    AP_ERR_SCHEMA,
    AP_ERR_UNKNOWN_COMMAND,
    AP_ERR_UNKNOWN_OPTION,
    AP_ERR_MISSING_VALUE,
    AP_ERR_INVALID_VALUE,
    AP_ERR_REQUIRED,
    AP_ERR_ARG_COUNT,
    AP_ERR_CAPACITY
} ap_status;

/*
 * Caller supplies both scratch arrays. No allocations or globals are used.
 * The `seen` buffer must hold at least max(option_count) for all commands.
 * `positionals` holds each positional argument in its original order.
 */
typedef struct ap_result {
    const ap_command *command;
    const char **positionals;
    size_t positional_capacity;
    size_t positional_count;
    unsigned char *seen;
    size_t seen_capacity;
    int error_index;     /* argv index, -1 when inapplicable */
    char error[192];
} ap_result;

/*
 * Parses argv[1..argc-1] and sets result->command to the selected command.
 * Options can appear before, between, or after leaf positional arguments.
 * A parent command's options must appear before its subcommand name.
 * All option destinations retain their initial values when not provided.
 * On failure, already-parsed destination values may have been modified.
 */
ap_status ap_parse(const ap_command *root, int argc,
                   const char *const argv[], ap_result *result);

/* True iff the selected (leaf) command option was supplied in this parse. */
bool ap_was_provided(const ap_result *result, const char *long_name);

/* Platform-independent help output to a caller-selected standard C stream. */
void ap_print_help(FILE *stream, const ap_command *root,
                   const ap_command *selected);

const char *ap_status_name(ap_status status);

#ifdef __cplusplus
}
#endif
#endif /* PORTABLE_ARGPARSE_H */
