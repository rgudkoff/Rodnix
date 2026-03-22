/* Linker script for Rust userland utilities.
 *
 * Extends the userland link.ld layout and discards Rust/LLVM metadata
 * sections that are meaningless in a bare-metal, no-unwind environment.
 *
 * Important: include the userland linker script, not the repository-root
 * kernel linker script.
 */

INCLUDE "link.ld"

SECTIONS
{
    /DISCARD/ : {
        /* Unwind tables — panic=abort makes these empty, but discard anyway. */
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.gcc_except_table*)
        /* LLVM/compiler metadata. */
        *(.note.GNU-stack)
        *(.note*)
        *(.comment)
        /* Rust compiler internal metadata. */
        *(.rustc*)
    }
}
