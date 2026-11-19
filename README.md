# portable_argparse

A small **ISO C99**, **platform-independent**, `nvme-cli`-style command-line parser.
The parser library uses only the C standard library. It has no dependencies on
`getopt`, `getopt_long`, `unistd.h`, `windows.h`, dynamic allocation, or global state.

This project is independently written; it does not copy nvme-cli source code.
The example **does not issue NVMe commands**.

## Supported syntax

```text
ssdctl --help
ssdctl format --help
ssdctl format disk0 --namespace-id=1 --lbaf=5 --ses=1
ssdctl format -n 1 -l5 -s1 -vv --dry-run disk0
ssdctl id-ctrl disk0 --output-format=json
```

- Command dispatch (`root subcommand`; nested commands are supported)
- `--long=value` and `--long value`; exact long option matching
- `-n value`, `-nvalue`, `-n=value` and grouped flags (`-vvh`)
- `--` to stop option parsing
- `bool`, counter, `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`,
  `int32_t`, `int64_t`, string, and `|`-separated choice types
- Strict decimal and `0x` hexadecimal integer parsing; overflow detection
- Required option validation; min/max positional arguments
- Built-in `-h` / `--help`, generated help, structured diagnostics
- Caller-owned option destinations and scratch space; no malloc

## Build and test (Linux, Windows, macOS, embedded toolchains)

With a standalone compiler:

```sh
cc -std=c99 -Wall -Wextra -Wpedantic -Iinclude src/argparse.c examples/ssdctl.c -o ssdctl
./ssdctl format disk0 -n1 -l5 -s1 --dry-run
```

On Windows MSVC Developer Command Prompt (VS2022):

```bat
cl /nologo /W4 /Iinclude src\argparse.c examples\ssdctl.c /Fe:ssdctl.exe
ssdctl.exe format disk0 -n1 -l5
```

With CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Integration

Copy `include/argparse.h` and `src/argparse.c` into your project, or link the
`portable_argparse` static library. Declare options and subcommands using the
structures in `argparse.h`. See `examples/ssdctl.c` for a complete program.

Typical code:

```c
uint32_t nsid = 1;
const ap_option options[] = {
    { "namespace-id", 'n', AP_U32, &nsid, "nsid",
      "Namespace ID", 0, NULL },
};
/* Build an ap_command for these options, then call ap_parse(). */
```

Prepare an `ap_result` with two buffers:

```c
const char *positionals[8];
unsigned char seen[16];
ap_result result = {0};
result.positionals = positionals;
result.positional_capacity = AP_COUNT(positionals);
result.seen = seen;
result.seen_capacity = AP_COUNT(seen);
```

`seen` needs to be at least as large as the **largest number of options on any
single command level**. Positionals are stored as pointers to the original
`argv` strings; the parser does not copy or modify those strings. Strings and
choices likewise point into `argv`, so keep the original `argv` alive as long
as the values are needed.

## Design choices / limits

- `-h` / `--help` are reserved and cannot be declared by callers.
- Options for a command level are parsed **before** entering a child command;
  parent options are not inherited after the child name. This keeps memory
  requirements low and behavior deterministic.
- For commands containing subcommands, ordinary positional arguments are not
  supported before selecting a subcommand.
- A repeated value option is **last value wins**. A repeated flag stays true;
  a counter increments with each occurrence.
- Numeric syntax is strictly decimal or hexadecimal (`0x`), not octal, not
  suffixes (K/M/G), and not expressions.
- An option that takes a value consumes the following argv token even if that
  token begins with `-`. Hence `-d -10` works; `--foo --bar` produces an invalid
  value or sets a string to `--bar` (by design).
- Help only writes when you explicitly call `ap_print_help(FILE*,...)`. If you
  do not use help output, the parse interface does not perform I/O.
- On errors, some destinations may already have changed. Initialize config
  defaults before each parse; do not assume transactional rollback.
- Not thread-safe **if the same configuration destination or result/scratch
  buffers are concurrently shared**; independent parser instances are safe.

## License

MIT. See LICENSE.
