/* nvme-cli-inspired demonstration. No SSD commands are actually sent. */
#include "argparse.h"
#include <inttypes.h>
#include <stdio.h>

typedef struct format_config {
    uint32_t nsid;
    uint8_t lbaf;
    uint8_t ses;
    bool dry_run;
    unsigned int verbose;
} format_config;

typedef struct identify_config {
    uint32_t nsid;
    const char *output;
} identify_config;

int main(int argc, char **argv)
{
    format_config fmt = { 1, 0, 0, false, 0 };
    identify_config id = { 1, "normal" };
    const ap_option format_opts[] = {
        { "namespace-id", 'n', AP_U32, &fmt.nsid, "nsid",
          "Namespace ID (decimal or hex)", 0, NULL },
        { "lbaf", 'l', AP_U8, &fmt.lbaf, "lbaf",
          "LBA format index", 0, NULL },
        { "ses", 's', AP_U8, &fmt.ses, "ses",
          "Secure erase settings", 0, NULL },
        { "dry-run", 0, AP_FLAG, &fmt.dry_run, NULL,
          "Print the request without executing it", 0, NULL },
        { "verbose", 'v', AP_COUNT_FLAG, &fmt.verbose, NULL,
          "Increase verbosity (repeatable)", 0, NULL }
    };
    const ap_option id_opts[] = {
        { "namespace-id", 'n', AP_U32, &id.nsid, "nsid",
          "Namespace ID", 0, NULL },
        { "output-format", 'o', AP_CHOICE, &id.output, "format",
          "Display format", 0, "normal|json|binary" }
    };
    const ap_command commands[] = {
        { "format", "Prepare Format NVM parameters (demo only)",
          "<device>", format_opts, AP_COUNT(format_opts),
          NULL, 0, 1, 1 },
        { "id-ctrl", "Identify controller (demo only)",
          "<device>", id_opts, AP_COUNT(id_opts),
          NULL, 0, 1, 1 }
    };
    const ap_command root = {
        "ssdctl", "Portable C command-line parser example",
        NULL, NULL, 0, commands, AP_COUNT(commands), 0, 0
    };
    const char *positionals[8];
    unsigned char seen[16];
    ap_result result = { 0 };
    ap_status st;
    result.positionals = positionals;
    result.positional_capacity = AP_COUNT(positionals);
    result.seen = seen;
    result.seen_capacity = AP_COUNT(seen);

    st = ap_parse(&root, argc, (const char *const *)argv, &result);
    if (st == AP_HELP) {
        ap_print_help(stdout, &root, result.command);
        return 0;
    }
    if (st != AP_OK) {
        fprintf(stderr, "error: %s\n", result.error[0] ? result.error
                                                         : ap_status_name(st));
        ap_print_help(stderr, &root, result.command);
        return 2;
    }

    if (result.command == &commands[0]) {
        printf("FORMAT (demo; nothing sent)\n");
        printf("  device=%s nsid=%" PRIu32 " lbaf=%u ses=%u dry_run=%s verbose=%u\n",
               result.positionals[0], fmt.nsid, (unsigned)fmt.lbaf,
               (unsigned)fmt.ses, fmt.dry_run ? "true" : "false", fmt.verbose);
        printf("  lbaf_explicit=%s\n",
               ap_was_provided(&result, "lbaf") ? "true" : "false");
    } else if (result.command == &commands[1]) {
        printf("IDENTIFY (demo; nothing sent)\n");
        printf("  device=%s nsid=%" PRIu32 " output=%s\n",
               result.positionals[0], id.nsid, id.output);
    }
    return 0;
}
