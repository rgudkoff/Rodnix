# IDL generator (minimal)

This tool parses a very small `.defs` format and generates C headers and
minimal client/server C stubs that use the IPC runtime.

## Grammar (subset)

```
interface <name> {
  rpc <name>(arg: type, ...) -> (ret: type, ...);
}
```

Types supported: `u32`, `u64`, `string`, `port`.

## Usage

```sh
python3 scripts/idl/idlgen.py <input.defs> <out_dir>
```

## Example

```sh
python3 scripts/idl/idlgen.py scripts/idl/example.defs build/idl
```

The generator emits:

- `<name>_ipc.h` (message ids + request/reply structs)
- `<name>_client.h/.c` (client stubs)
- `<name>_server.h/.c` (server stubs + dispatch)
