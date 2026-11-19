#include "argparse.h"

#include <limits.h>
#include <string.h>

static int ap_value_type(ap_type type)
{
    return type != AP_TYPE_FLAG && type != AP_TYPE_INCR;
}

static int ap_unsigned_type(ap_type type)
{
    return type == AP_TYPE_UINT || type == AP_TYPE_U8 || type == AP_TYPE_U16 ||
           type == AP_TYPE_U32 || type == AP_TYPE_U64;
}

static int ap_valid_type(ap_type type)
{
    return type >= AP_TYPE_FLAG && type <= AP_TYPE_CSTR;
}

/* Locale-independent unsigned parser; no errno, strtoull, or allocations. */
static int ap_uint64(const char *value, uint64_t limit, uint64_t *result)
{
    const unsigned char *p = (const unsigned char *)value;
    unsigned base = 10;
    uint64_t number = 0;
    int any = 0;

    if (!value) return 0;
    if (*p == '+') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    }
    for (; *p; ++p) {
        unsigned digit;
        if (*p >= '0' && *p <= '9') digit = (unsigned)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = (unsigned)(*p - 'a') + 10u;
        else if (*p >= 'A' && *p <= 'F') digit = (unsigned)(*p - 'A') + 10u;
        else return 0;
        if (digit >= base || digit > limit ||
            number > (limit - digit) / base) return 0;
        number = number * base + digit;
        any = 1;
    }
    if (!any) return 0;
    *result = number;
    return 1;
}

static int ap_int64(const char *value, uint64_t pos_limit,
                    uint64_t neg_limit, int64_t *result)
{
    int negative;
    uint64_t number;
    if (!value) return 0;
    negative = value[0] == '-';
    if (!ap_uint64(negative ? value + 1 : value,
                   negative ? neg_limit : pos_limit, &number)) return 0;
    if (negative && number == (uint64_t)INT64_MAX + UINT64_C(1))
        *result = INT64_MIN;
    else
        *result = negative ? -(int64_t)number : (int64_t)number;
    return 1;
}

static int ap_enum_value(const ap_named_value *values,
                         const char *word, uint64_t *result)
{
    size_t i;
    if (!values) return 0;
    /* Arrays supplied here must have AP_VALUE_END() as final element. */
    for (i = 0; values[i].name; ++i) {
        if (strcmp(values[i].name, word) == 0) {
            *result = values[i].value;
            return 1;
        }
    }
    return 0;
}

static int ap_assign(ap_option *opt, const char *arg)
{
    uint64_t u = 0;
    int64_t s = 0;
    switch (opt->type) {
    case AP_TYPE_FLAG:
        *(bool *)opt->destination = true;
        break;
    case AP_TYPE_INCR:
        if (*(int *)opt->destination == INT_MAX) return 0;
        ++*(int *)opt->destination;
        break;
    case AP_TYPE_STR:
        *(char **)opt->destination = (char *)arg;
        break;
    case AP_TYPE_CSTR:
        *(const char **)opt->destination = arg;
        break;
    case AP_TYPE_UINT:
    case AP_TYPE_U8:
    case AP_TYPE_U16:
    case AP_TYPE_U32:
    case AP_TYPE_U64: {
        uint64_t limit = UINT64_MAX;
        if (opt->type == AP_TYPE_UINT) limit = UINT_MAX;
        if (opt->type == AP_TYPE_U8) limit = UINT8_MAX;
        if (opt->type == AP_TYPE_U16) limit = UINT16_MAX;
        if (opt->type == AP_TYPE_U32) limit = UINT32_MAX;
        if (!ap_enum_value(opt->values, arg, &u) &&
            !ap_uint64(arg, limit, &u)) return 0;
        if (u > limit) return 0;
        switch (opt->type) {
        case AP_TYPE_UINT: *(unsigned int *)opt->destination = (unsigned int)u; break;
        case AP_TYPE_U8: *(uint8_t *)opt->destination = (uint8_t)u; break;
        case AP_TYPE_U16: *(uint16_t *)opt->destination = (uint16_t)u; break;
        case AP_TYPE_U32: *(uint32_t *)opt->destination = (uint32_t)u; break;
        default: *(uint64_t *)opt->destination = u; break;
        }
        break;
    }
    case AP_TYPE_INT:
    case AP_TYPE_I32:
    case AP_TYPE_I64: {
        const uint64_t positive = opt->type == AP_TYPE_INT ? INT_MAX :
                                  opt->type == AP_TYPE_I32 ? INT32_MAX : INT64_MAX;
        const uint64_t negative = positive + UINT64_C(1);
        if (!ap_int64(arg, positive, negative, &s)) return 0;
        if (opt->type == AP_TYPE_INT) *(int *)opt->destination = (int)s;
        else if (opt->type == AP_TYPE_I32) *(int32_t *)opt->destination = (int32_t)s;
        else *(int64_t *)opt->destination = s;
        break;
    }
    default:
        return 0;
    }
    opt->seen++;
    return 1;
}

static ap_option *ap_long(ap_option *options, size_t count,
                          const char *key, size_t len)
{
    size_t k;
    for (k = 0; k < count; ++k)
        if (options[k].name && strlen(options[k].name) == len &&
            strncmp(options[k].name, key, len) == 0) return &options[k];
    return NULL;
}

static ap_option *ap_short(ap_option *options, size_t count, char ch)
{
    size_t k;
    for (k = 0; k < count; ++k)
        if (options[k].short_name && options[k].short_name == ch)
            return &options[k];
    return NULL;
}

static ap_option *ap_next_positional(ap_option *options, size_t count)
{
    size_t k;
    for (k = 0; k < count; ++k)
        if ((options[k].flags & AP_OPT_POSITIONAL) && !options[k].seen)
            return &options[k];
    return NULL;
}

static int ap_schema_valid(ap_option options[], size_t count)
{
    size_t i, j;
    /* OPT_END is required, checked without scanning beyond the actual array. */
    if (!count || options[count - 1u].type != AP_TYPE_END ||
        options[count - 1u].name || options[count - 1u].short_name) return 0;
    for (i = 0; i + 1u < count; ++i) {
        ap_option *o = &options[i];
        const int positional = (o->flags & AP_OPT_POSITIONAL) != 0;
        if (!ap_valid_type(o->type) || !o->destination ||
            (o->flags & ~(AP_OPT_REQUIRED | AP_OPT_POSITIONAL))) return 0;
        if (positional) {
            if (o->name || o->short_name || !o->metavar ||
                o->type != AP_TYPE_STR) return 0;
        } else if ((!o->name && !o->short_name) ||
                   (o->name && (!o->name[0] || strchr(o->name, '=') ||
                                strcmp(o->name, "help") == 0)) ||
                   o->short_name == '-' || o->short_name == 'h') return 0;
        if (o->values && !ap_unsigned_type(o->type)) return 0;
        for (j = 0; j < i; ++j) {
            if ((o->name && options[j].name &&
                 strcmp(o->name, options[j].name) == 0) ||
                (o->short_name && options[j].short_name == o->short_name))
                return 0;
        }
        o->seen = 0;
    }
    return 1;
}

void ap_print_args_help(FILE *out, const char *program,
                        const char *description,
                        const ap_option options[], size_t count)
{
    size_t k;
    fprintf(out, "Usage: %s [options]", program ? program : "command");
    for (k = 0; k < count; ++k) {
        if ((options[k].flags & AP_OPT_POSITIONAL) && options[k].metavar) {
            fprintf(out, (options[k].flags & AP_OPT_REQUIRED) ? " <%s>" : " [%s]",
                    options[k].metavar);
        }
    }
    fputc('\n', out);
    if (description && description[0]) fprintf(out, "\n%s\n", description);
    fputs("\nOptions:\n  -h, --help                 Show this help\n", out);
    for (k = 0; k < count; ++k) {
        const ap_option *o = &options[k];
        if (o->type == AP_TYPE_END || (o->flags & AP_OPT_POSITIONAL)) continue;
        fputs("  ", out);
        if (o->short_name) fprintf(out, "-%c, ", o->short_name);
        else fputs("    ", out);
        if (o->name) fprintf(out, "--%s", o->name);
        if (ap_value_type(o->type)) fputs(" <value>", out);
        if (o->flags & AP_OPT_REQUIRED) fputs(" [required]", out);
        if (o->description) fprintf(out, "\n      %s", o->description);
        fputc('\n', out);
    }
}

static int ap_error(const char *program, const char *message, const char *detail)
{
    fprintf(stderr, "error: %s", message);
    if (detail) fprintf(stderr, ": %s", detail);
    fprintf(stderr, "\nTry '%s --help' for usage.\n", program ? program : "command");
    return AP_PARSE_ERROR;
}

int ap_parse_args_impl(int argc, char *argv[], const char *description,
                       ap_option options[], size_t count)
{
    int i, only_positionals = 0;
    const char *prog = (argc > 0 && argv && argv[0]) ? argv[0] : "command";
    size_t k;
    if (!argv || argc < 1 || !options || !ap_schema_valid(options, count))
        return ap_error(prog, "invalid arguments or option declaration", NULL);
    for (i = 1; i < argc; ++i) {
        char *token = argv[i];
        if (!token) return ap_error(prog, "NULL argument", NULL);
        if (!only_positionals && strcmp(token, "--") == 0) {
            only_positionals = 1;
            continue;
        }
        if (!only_positionals && strcmp(token, "--help") == 0) {
            ap_print_args_help(stdout, prog, description, options, count);
            return AP_PARSE_HELP;
        }
        if (!only_positionals && token[0] == '-' && token[1] == '-' && token[2]) {
            const char *key = token + 2;
            const char *equal = strchr(key, '=');
            const size_t keylen = equal ? (size_t)(equal - key) : strlen(key);
            const char *value = NULL;
            ap_option *o = ap_long(options, count - 1u, key, keylen);
            if (!o) return ap_error(prog, "unknown option", token);
            if (ap_value_type(o->type)) {
                if (equal) value = equal + 1;
                else if (i + 1 < argc) value = argv[++i];
                else return ap_error(prog, "missing option value", token);
                if (!value) return ap_error(prog, "NULL option value", token);
            } else if (equal) return ap_error(prog, "flag takes no value", token);
            if (!ap_assign(o, value)) return ap_error(prog, "invalid option value", token);
            continue;
        }
        if (!only_positionals && token[0] == '-' && token[1]) {
            const char *p = token + 1;
            while (*p) {
                ap_option *o;
                const char *value = NULL;
                if (*p == 'h') {
                    ap_print_args_help(stdout, prog, description, options, count);
                    return AP_PARSE_HELP;
                }
                o = ap_short(options, count - 1u, *p);
                if (!o) return ap_error(prog, "unknown option", token);
                ++p;
                if (ap_value_type(o->type)) {
                    if (*p) value = *p == '=' ? p + 1 : p;
                    else if (i + 1 < argc) value = argv[++i];
                    else return ap_error(prog, "missing option value", token);
                    if (!value) return ap_error(prog, "NULL option value", token);
                    if (!ap_assign(o, value))
                        return ap_error(prog, "invalid option value", token);
                    break;
                }
                if (!ap_assign(o, NULL))
                    return ap_error(prog, "invalid flag", token);
            }
            continue;
        }
        {
            ap_option *o = ap_next_positional(options, count - 1u);
            if (!o) return ap_error(prog, "unexpected positional argument", token);
            if (!ap_assign(o, token))
                return ap_error(prog, "invalid positional argument", token);
        }
    }
    for (k = 0; k + 1u < count; ++k) {
        if ((options[k].flags & AP_OPT_REQUIRED) && !options[k].seen)
            return ap_error(prog, "required argument missing",
                            options[k].name ? options[k].name : options[k].metavar);
    }
    return AP_PARSE_OK;
}

bool ap_given_impl(const ap_option options[], size_t count,
                   const void *destination)
{
    size_t k;
    if (!options || !destination) return false;
    for (k = 0; k < count; ++k)
        if (options[k].type != AP_TYPE_END &&
            options[k].destination == destination && options[k].seen)
            return true;
    return false;
}

bool ap_seen_impl(const ap_option options[], size_t count,
                  const char *long_name)
{
    size_t k;
    if (!options || !long_name) return false;
    for (k = 0; k < count; ++k)
        if (options[k].name && strcmp(options[k].name, long_name) == 0)
            return options[k].seen != 0;
    return false;
}

static void ap_print_commands(FILE *out, const char *name, const char *desc,
                              const ap_command cmds[], size_t count)
{
    size_t k;
    fprintf(out, "Usage: %s <command> [options]\n", name ? name : "program");
    if (desc && desc[0]) fprintf(out, "\n%s\n", desc);
    fputs("\nCommands:\n", out);
    for (k = 0; k < count; ++k)
        if (cmds[k].name) fprintf(out, "  %-22s %s\n", cmds[k].name,
                                  cmds[k].description ? cmds[k].description : "");
    fputs("\nUse <command> --help for command-specific options.\n", out);
}

int ap_dispatch_impl(int argc, char *argv[], const char *description,
                     const ap_command cmds[], size_t count)
{
    size_t k;
    const char *program = (argc > 0 && argv && argv[0]) ? argv[0] : "program";
    if (!cmds || !argv || argc < 1) return AP_PARSE_ERROR;
    if (argc == 1 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        ap_print_commands(stdout, program, description, cmds, count);
        return argc == 1 ? AP_PARSE_ERROR : AP_PARSE_HELP;
    }
    for (k = 0; k < count; ++k) {
        if (!cmds[k].name || strcmp(cmds[k].name, argv[1]) != 0) continue;
        if (cmds[k].handler) return cmds[k].handler(argc - 1, argv + 1);
        if (cmds[k].children && cmds[k].child_count)
            return ap_dispatch_impl(argc - 1, argv + 1,
                                    cmds[k].description,
                                    cmds[k].children, cmds[k].child_count);
        return ap_error(program, "command has no handler", cmds[k].name);
    }
    return ap_error(program, "unknown command", argv[1]);
}
