/* Linker script for Rust userland utilities.
 *
 * Extends the common link.ld layout and discards Rust/LLVM metadata sections
 * that are meaningless in a bare-metal, no-unwind environment.
 */

INCLUDE "../link.ld"

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
