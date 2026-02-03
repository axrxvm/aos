/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/panic.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <panic.h> // This now brings in <debug.h>

// The actual panic implementation (panic_screen, panic_msg_loc) is in src/kernel/debug.c.
// This file (panic.c) can be kept minimal or even empty if all panic calls
// correctly use the new panic() macro defined in debug.h.
// If there were any old-style `void panic(const char* message)` function definitions here,
// they are superseded by the new system.

// It's good practice to ensure this file still exists and compiles,
// in case any part of the build system or older code still expects a panic.o object file.
// However, its functional role is now taken over by debug.c.

// Add a dummy function or comment to ensure the file is not completely empty
// if that causes issues with any build tools (though typically it shouldn't).
void _panic_c_dummy_func() {
    // This function serves no purpose other than ensuring panic.o is created
    // if absolutely necessary. Ideally, this file would be removed if unneeded.
}
