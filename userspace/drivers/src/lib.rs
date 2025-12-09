#![no_std]
#![no_main]
#![feature(lang_items)]

extern crate alloc;

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

#[lang = "eh_personality"]
extern "C" fn eh_personality() {}

// Placeholder for driver implementations
// Drivers will be implemented here

