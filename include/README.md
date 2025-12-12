# include - Header Files

This directory contains public header files for the kernel.

## Structure

```
include/
├── types.h          # Basic types
├── kernel.h         # Main kernel header (includes everything)
├── console.h        # Console output interface
├── debug.h          # Debugging utilities
├── common.h         # Common utilities
└── README.md        # This file
```

## Usage

For kernel development, include `kernel.h` which includes all necessary headers:

```c
#include "kernel.h"
```

For specific functionality, include individual headers:

```c
#include "console.h"
#include "debug.h"
```

## Headers

- **types.h**: Basic data types (uint8_t, uint64_t, etc.)
- **kernel.h**: Main header that includes all kernel headers
- **console.h**: Console output functions
- **debug.h**: Debugging macros and functions
- **common.h**: Common utility functions (string, memory, etc.)

