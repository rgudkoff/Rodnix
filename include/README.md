# include - Header Files

This directory contains public header files for the kernel.

## Structure

- types.h: basic types
- kernel.h: main kernel header (includes everything)
- console.h: console output interface
- debug.h: debugging utilities
- common.h: common utilities
- README.md: this file

## Usage

For kernel development, include kernel.h which includes all necessary headers:

```c
#include "kernel.h"
```

For specific functionality, include individual headers:

```c
#include "console.h"
#include "debug.h"
```

## Headers

- types.h: basic data types (uint8_t, uint64_t, etc.)
- kernel.h: main header that includes all kernel headers
- console.h: console output functions
- debug.h: debugging macros and functions
- common.h: common utility functions (string, memory, etc.)
