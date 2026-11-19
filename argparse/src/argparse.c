#include "argparse.h"

#include <limits.h>
#include <string.h>

static ap_status ap_fail(ap_result *r, ap_status code, int index,
                         const char *message, const char *detail)
{
    r->error_index = index;
    if (detail)
        (void)snprintf(r->error, sizeof(r->error), "%s: %s", message, detail);
    else
        (void)snprintf(r->error, sizeof(r->error), "%s", message);
    return code;
}

static int ap_is_value_option(ap_type t)
{
    return t != AP_FLAG && t != AP_COUNT_FLAG;
}

static const ap_option *ap_find_long(const ap_command *c, const char *s,
                                     size_t len, size_t *index)
{
    size_t k;
    for (k = 0; k < c->option_count; ++k) {
        const char *name = c->options[k].long_name;
        if (name && strlen(name) == len && strncmp(name, s, len) == 0) {
            *index = k;
            return &c->options[k];
        }
    }
    return NULL;
}

static const ap_option *ap_find_short(const ap_command *c, char ch,
                                      size_t *index)
{
    size_t k;
    for (k = 0; k < c->option_count; ++k) {
        if (c->options[k].short_name == ch) {
            *index = k;
            return &c->options[k];
        }
    }
    return NULL;
}

static const ap_command *ap_find_command(const ap_command *c, const char *name)
{
    size_t k;
    for (k = 0; k < c->subcommand_count; ++k) {
        if (c->subcommands[k].name && strcmp(c->subcommands[k].name, name) == 0)
            return &c->subcommands[k];
    }
    return NULL;
}

/* Strict ASCII numeric parsing, without locale, errno, strto*, or overflow. */
static int ap_parse_magnitude(const char *s, uint64_t limit, uint64_t *out)
{
    uint64_t n = 0;
    unsigned base = 10;
    int digit_seen = 0;
    const unsigned char *p = (const unsigned char *)s;
    if (*p == '+') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    for (; *p; ++p) {
        unsigned d;
        if (*p >= '0' && *p <= '9') d = (unsigned)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') d = (unsigned)(*p - 'a') + 10;
        else if (*p >= 'A' && *p <= 'F') d = (unsigned)(*p - 'A') + 10;
        else return 0;
        if (d >= base || n > (limit - d) / base) return 0;
        n = n * base + d;
        digit_seen = 1;
    }
    if (!digit_seen) return 0;
    *out = n;
    return 1;
}

static int ap_parse_signed(const char *s, uint64_t positive_max,
                           uint64_t negative_magnitude_max, int64_t *out)
{
    int negative = (*s == '-');
    uint64_t m;
    if (negative) ++s;
    if (!ap_parse_magnitude(s,
            negative ? negative_magnitude_max : positive_max, &m)) return 0;
    /* Avoid implementation-defined uint64_t -> int64_t for INT64_MIN. */
    if (negative && m == ((uint64_t)INT64_MAX + 1u))
        *out = INT64_MIN;
    else
        *out = negative ? -(int64_t)m : (int64_t)m;
    return 1;
}

static int ap_choice_valid(const char *choices, const char *value)
{
    const char *p;
    size_t n;
    if (!choices) return 0;
    n = strlen(value);
    p = choices;
    for (;;) {
        const char *end = strchr(p, '|');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, value, n) == 0) return 1;
        if (!end) return 0;
        p = end + 1;
    }
}

static ap_status ap_assign(const ap_option *o, const char *v,
                            ap_result *r, int arg_index)
{
    uint64_t u = 0;
    int64_t s = 0;
    if (!o->value) return ap_fail(r, AP_ERR_SCHEMA, arg_index,
                                   "option has a NULL destination", o->long_name);
    switch (o->type) {
    case AP_FLAG:
        *(bool *)o->value = true;
        return AP_OK;
    case AP_COUNT_FLAG:
        if (*(unsigned int *)o->value == UINT_MAX)
            return ap_fail(r, AP_ERR_INVALID_VALUE, arg_index,
                           "counter overflow", o->long_name);
        ++*(unsigned int *)o->value;
        return AP_OK;
    case AP_STRING:
        *(const char **)o->value = v;
        return AP_OK;
    case AP_CHOICE:
        if (!ap_choice_valid(o->choices, v))
            return ap_fail(r, AP_ERR_INVALID_VALUE, arg_index,
                           "value not in choices", v);
        *(const char **)o->value = v;
        return AP_OK;
    case AP_U8:
        if (!ap_parse_magnitude(v, UINT8_MAX, &u)) break;
        *(uint8_t *)o->value = (uint8_t)u;
        return AP_OK;
    case AP_U16:
        if (!ap_parse_magnitude(v, UINT16_MAX, &u)) break;
        *(uint16_t *)o->value = (uint16_t)u;
        return AP_OK;
    case AP_U32:
        if (!ap_parse_magnitude(v, UINT32_MAX, &u)) break;
        *(uint32_t *)o->value = (uint32_t)u;
        return AP_OK;
    case AP_U64:
        if (!ap_parse_magnitude(v, UINT64_MAX, &u)) break;
        *(uint64_t *)o->value = u;
        return AP_OK;
    case AP_I32:
        if (!ap_parse_signed(v, INT32_MAX, (uint64_t)INT32_MAX + 1u, &s)) break;
        *(int32_t *)o->value = (int32_t)s;
        return AP_OK;
    case AP_I64:
        if (!ap_parse_signed(v, INT64_MAX, (uint64_t)INT64_MAX + 1u, &s)) break;
        *(int64_t *)o->value = s;
        return AP_OK;
    default:
        return ap_fail(r, AP_ERR_SCHEMA, arg_index, "unknown option type",
                       o->long_name);
    }
    return ap_fail(r, AP_ERR_INVALID_VALUE, arg_index,
                   "invalid or out-of-range numeric value", v);
}

static ap_status ap_check_required(const ap_command *c, ap_result *r, int i)
{
    size_t k;
    for (k = 0; k < c->option_count; ++k) {
        if ((c->options[k].flags & AP_OPT_REQUIRED) && !r->seen[k])
            return ap_fail(r, AP_ERR_REQUIRED, i, "required option missing",
                           c->options[k].long_name);
    }
    return AP_OK;
}

static ap_status ap_enter(const ap_command *c, ap_result *r, int i)
{
    size_t k, j;
    if (c->option_count > r->seen_capacity || (c->option_count && !r->seen))
        return ap_fail(r, AP_ERR_CAPACITY, i, "option scratch buffer too small", c->name);
    if (c->option_count && !c->options)
        return ap_fail(r, AP_ERR_SCHEMA, i, "missing options array", c->name);
    if (c->subcommand_count && !c->subcommands)
        return ap_fail(r, AP_ERR_SCHEMA, i, "missing subcommands array", c->name);
    if (c->min_args > c->max_args)
        return ap_fail(r, AP_ERR_SCHEMA, i, "invalid positional bounds", c->name);
    for (k = 0; k < c->option_count; ++k) {
        const ap_option *o = &c->options[k];
        if (!o->value || (!o->long_name && !o->short_name) ||
            o->short_name == '-' || o->short_name == 'h' ||
            (o->long_name && (o->long_name[0] == '\0' ||
                              strcmp(o->long_name, "help") == 0 ||
                              strchr(o->long_name, '=') != NULL)) ||
            (o->type == AP_CHOICE && !o->choices))
            return ap_fail(r, AP_ERR_SCHEMA, i, "invalid option declaration", c->name);
        for (j = 0; j < k; ++j) {
            const ap_option *prev = &c->options[j];
            if ((o->short_name && prev->short_name == o->short_name) ||
                (o->long_name && prev->long_name &&
                 strcmp(o->long_name, prev->long_name) == 0))
                return ap_fail(r, AP_ERR_SCHEMA, i, "duplicate option name", c->name);
        }
    }
    if (c->option_count) memset(r->seen, 0, c->option_count);
    r->command = c;
    return AP_OK;
}

ap_status ap_parse(const ap_command *root, int argc,
                   const char *const argv[], ap_result *r)
{
    const ap_command *cmd;
    ap_status status;
    int i, end_options = 0;
    if (!r) return AP_ERR_ARGUMENT;
    r->error_index = -1;
    r->error[0] = '\0';
    r->positional_count = 0;
    r->command = root;
    if (!root || argc < 0 || (argc > 0 && !argv) ||
        (r->positional_capacity && !r->positionals))
        return ap_fail(r, AP_ERR_ARGUMENT, -1, "invalid parser arguments", NULL);
    cmd = root;
    status = ap_enter(cmd, r, 0);
    if (status != AP_OK) return status;

    for (i = 1; i < argc; ++i) {
        const char *token = argv[i];
        if (!token)
            return ap_fail(r, AP_ERR_ARGUMENT, i, "NULL argument", NULL);
        if (!end_options && strcmp(token, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && token[0] == '-' && token[1] == '-' && token[2]) {
            const char *key = token + 2;
            const char *equal = strchr(key, '=');
            size_t key_len = equal ? (size_t)(equal - key) : strlen(key);
            const ap_option *opt;
            size_t k;
            const char *v = NULL;
            if (key_len == 4 && strncmp(key, "help", 4) == 0 && !equal)
                return AP_HELP;
            opt = ap_find_long(cmd, key, key_len, &k);
            if (!opt)
                return ap_fail(r, AP_ERR_UNKNOWN_OPTION, i, "unknown option", token);
            if (ap_is_value_option(opt->type)) {
                if (equal) v = equal + 1;
                else if (i + 1 < argc) v = argv[++i];
                else return ap_fail(r, AP_ERR_MISSING_VALUE, i, "value missing for", token);
                if (!v) return ap_fail(r, AP_ERR_ARGUMENT, i, "NULL value", token);
            } else if (equal) {
                return ap_fail(r, AP_ERR_INVALID_VALUE, i,
                               "flag does not take a value", token);
            }
            status = ap_assign(opt, v, r, i);
            if (status != AP_OK) return status;
            r->seen[k] = 1;
            continue;
        }
        if (!end_options && token[0] == '-' && token[1] != '\0') {
            const char *p = token + 1;
            while (*p) {
                size_t k;
                const ap_option *opt;
                const char *v = NULL;
                if (*p == 'h') return AP_HELP;
                opt = ap_find_short(cmd, *p, &k);
                if (!opt)
                    return ap_fail(r, AP_ERR_UNKNOWN_OPTION, i, "unknown short option", token);
                ++p;
                if (ap_is_value_option(opt->type)) {
                    if (*p) v = (*p == '=') ? p + 1 : p;
                    else if (i + 1 < argc) v = argv[++i];
                    else return ap_fail(r, AP_ERR_MISSING_VALUE, i,
                                        "value missing for", token);
                    if (!v) return ap_fail(r, AP_ERR_ARGUMENT, i, "NULL value", token);
                    status = ap_assign(opt, v, r, i);
                    if (status != AP_OK) return status;
                    r->seen[k] = 1;
                    break; /* value-taking option consumes remainder of group */
                }
                status = ap_assign(opt, NULL, r, i);
                if (status != AP_OK) return status;
                r->seen[k] = 1;
            }
            continue;
        }
        if (cmd->subcommand_count) {
            const ap_command *next = !end_options ? ap_find_command(cmd, token) : NULL;
            if (!next)
                return ap_fail(r, AP_ERR_UNKNOWN_COMMAND, i,
                               "unknown or missing subcommand", token);
            status = ap_check_required(cmd, r, i);
            if (status != AP_OK) return status;
            status = ap_enter(next, r, i);
            if (status != AP_OK) return status;
            cmd = next;
            end_options = 0;
            continue;
        }
        if (r->positional_count >= r->positional_capacity)
            return ap_fail(r, AP_ERR_CAPACITY, i,
                           "positional scratch buffer too small", token);
        r->positionals[r->positional_count++] = token;
    }
    if (cmd->subcommand_count)
        return AP_HELP; /* no subcommand: display command overview */
    status = ap_check_required(cmd, r, argc);
    if (status != AP_OK) return status;
    if (r->positional_count < cmd->min_args || r->positional_count > cmd->max_args)
        return ap_fail(r, AP_ERR_ARG_COUNT, argc,
                       "invalid number of positional arguments", cmd->name);
    return AP_OK;
}

bool ap_was_provided(const ap_result *r, const char *name)
{
    size_t k;
    if (!r || !r->command || !r->seen || !name) return false;
    for (k = 0; k < r->command->option_count && k < r->seen_capacity; ++k) {
        const char *n = r->command->options[k].long_name;
        if (n && strcmp(n, name) == 0) return r->seen[k] != 0;
    }
    return false;
}

static int ap_contains(const ap_command *node, const ap_command *target)
{
    size_t i;
    if (node == target) return 1;
    for (i = 0; i < node->subcommand_count; ++i)
        if (ap_contains(&node->subcommands[i], target)) return 1;
    return 0;
}

static void ap_print_path(FILE *out, const ap_command *node,
                          const ap_command *target)
{
    size_t i;
    if (node == target) return;
    for (i = 0; i < node->subcommand_count; ++i) {
        const ap_command *child = &node->subcommands[i];
        if (ap_contains(child, target)) {
            fprintf(out, " %s", child->name);
            ap_print_path(out, child, target);
            return;
        }
    }
}

void ap_print_help(FILE *out, const ap_command *root, const ap_command *selected)
{
    size_t i;
    const ap_command *c = selected ? selected : root;
    if (!out || !root || !c) return;
    fprintf(out, "Usage: %s", root->name ? root->name : "program");
    if (c != root && ap_contains(root, c)) ap_print_path(out, root, c);
    fprintf(out, " [options]"); /* --help is always available */
    if (c->subcommand_count) fprintf(out, " <command>");
    if (c->args_usage) fprintf(out, " %s", c->args_usage);
    else if (c->max_args) fprintf(out, " [args...]");
    fprintf(out, "\n");
    if (c->description) fprintf(out, "\n%s\n", c->description);
    if (c->subcommand_count) {
        fprintf(out, "\nCommands:\n");
        for (i = 0; i < c->subcommand_count; ++i) {
            const ap_command *child = &c->subcommands[i];
            fprintf(out, "  %-20s %s\n", child->name,
                    child->description ? child->description : "");
        }
    }
    fprintf(out, "\nOptions:\n  -h, --help             Show this help\n");
    for (i = 0; i < c->option_count; ++i) {
        const ap_option *o = &c->options[i];
        char spec[100];
        size_t used = 0;
        spec[0] = '\0';
        if (o->short_name)
            used += (size_t)snprintf(spec + used, sizeof(spec) - used,
                                     "-%c%s", o->short_name,
                                     o->long_name ? ", " : "");
        if (o->long_name && used < sizeof(spec))
            used += (size_t)snprintf(spec + used, sizeof(spec) - used,
                                     "--%s", o->long_name);
        if (ap_is_value_option(o->type) && used < sizeof(spec))
            (void)snprintf(spec + used, sizeof(spec) - used, " <%s>",
                           o->metavar ? o->metavar : "value");
        fprintf(out, "  %-33s %s%s", spec,
                o->help ? o->help : "",
                (o->flags & AP_OPT_REQUIRED) ? " (required)" : "");
        if (o->type == AP_CHOICE && o->choices)
            fprintf(out, " [%s]", o->choices);
        fprintf(out, "\n");
    }
}

const char *ap_status_name(ap_status status)
{
    switch (status) {
    case AP_OK: return "ok";
    case AP_HELP: return "help requested";
    case AP_ERR_ARGUMENT: return "invalid parser arguments";
    case AP_ERR_SCHEMA: return "invalid command declaration";
    case AP_ERR_UNKNOWN_COMMAND: return "unknown command";
    case AP_ERR_UNKNOWN_OPTION: return "unknown option";
    case AP_ERR_MISSING_VALUE: return "missing option value";
    case AP_ERR_INVALID_VALUE: return "invalid option value";
    case AP_ERR_REQUIRED: return "required option missing";
    case AP_ERR_ARG_COUNT: return "wrong positional count";
    case AP_ERR_CAPACITY: return "scratch buffer too small";
    default: return "unknown error";
    }
}
