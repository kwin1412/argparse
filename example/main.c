/* Full nvme-cli-like usage, without platform APIs or manual parser state. */
#include "argparse.h"
#include <stdio.h>

static int cmd_security_send(int argc, char *argv[])
{
    const char *desc = "Send a TCG Security Protocol payload (demo only).";
    struct config {
        unsigned int protocol;
        uint16_t comid;
        char *data;
        bool dry_run;
        int verbose;
    } cfg = { 1u, 0x1001u, NULL, false, 0 };

    OPT_ARGS(opts) = {
        OPT_UINT("protocol", 'p', &cfg.protocol, "Security protocol number"),
        OPT_U16("comid", 'c', &cfg.comid, "TCG ComID"),
        OPT_STR("data", 'd', &cfg.data, "Payload file name"),
        OPT_FLAG("dry-run", 0, &cfg.dry_run, "Do not send anything"),
        OPT_INCR("verbose", 'v', &cfg.verbose, "More verbose output"),
        OPT_END()
    };

    int rc = parse_args(argc, argv, desc, opts);
    if (rc) return rc;
    printf("security-send: protocol=%u comid=0x%04x data=%s dry-run=%s verbose=%d\n",
           cfg.protocol, (unsigned)cfg.comid,
           cfg.data ? cfg.data : "(none)", cfg.dry_run ? "true" : "false", cfg.verbose);
    return 0;
}

static int cmd_identify(int argc, char *argv[])
{
    const char *desc = "Identify namespace in a portable CLI demonstration.";
    struct config {
        unsigned int namespace_id;
        bool raw_binary;
        char *device;
    } cfg = { 0u, false, NULL };
    static const ap_named_value nvme_nsid_values[] = {
        AP_VALUE("all", UINT32_MAX, "All namespaces"),
        AP_VALUE_END()
    };
    const char *namespace_id_desired = "Namespace identifier";
    const char *raw_identify = "Print raw binary data";

    OPT_ARGS(opts) = {
        /* Exactly the same 4/5 argument macro style as the posted nvme-cli code. */
        OPT_UINT("namespace-id", 'n', &cfg.namespace_id,
                 namespace_id_desired, nvme_nsid_values),
        OPT_FLAG("raw-binary", 'b', &cfg.raw_binary, raw_identify),
        OPT_POSITIONAL_OPT("device", &cfg.device, "Optional device path"),
        OPT_END()
    };

    int rc = parse_args(argc, argv, desc, opts);
    if (rc) return rc;
    printf("identify: nsid=%u raw=%s device=%s provided=%s\n",
           cfg.namespace_id, cfg.raw_binary ? "true" : "false",
           cfg.device ? cfg.device : "(none)",
           argconfig_given(opts, &cfg.namespace_id) ? "yes" : "no");
    return 0;
}

static const ap_command sim_commands[] = {
    AP_COMMAND("security-send", cmd_security_send, "Security Protocol Send"),
    AP_COMMAND("identify", cmd_identify, "Identify namespace")
};
static const ap_command ssd_commands[] = {
    AP_GROUP("sim", "TCG SSD simulator", sim_commands)
};
static const ap_command root_commands[] = {
    AP_GROUP("ssd", "SSD subcommands", ssd_commands)
};

int main(int argc, char *argv[])
{
    int rc = dispatch_commands(argc, argv, "Portable TCG SSD command-line example",
                               root_commands);
    /* --help is not an error for the process. */
    return rc == AP_PARSE_HELP ? 0 : rc == AP_PARSE_OK ? 0 : 1;
}
