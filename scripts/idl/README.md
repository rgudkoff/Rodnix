# IDL generator (minimal)

This tool parses a very small `.defs` format and generates C header stubs
for client and server.

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
