/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_security.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <command.h>
#include <command_registry.h>
#include <sandbox.h>
#include <fileperm.h>
#include <process.h>
#include <fs/vfs.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>

// Command: Show sandbox info for current or specified process
void cmd_sandbox(const char* args) {
    int pid;
    
    if (args && *args) {
        pid = atoi(args);
    } else {
        pid = process_getpid();
    }
    
    sandbox_t sb;
    if (sandbox_get(pid, &sb) != 0) {
        vga_puts("Error: Could not get sandbox info for PID ");
        char buf[12];
        itoa(pid, buf, 10);
        vga_puts(buf);
        vga_puts("\n");
        return;
    }
    
    vga_puts("Sandbox Info for PID ");
    char buf[12];
    itoa(pid, buf, 10);
    vga_puts(buf);
    vga_puts(":\n");
    
    // Cage level
    vga_puts("  Cage Level: ");
    switch (sb.cage_level) {
        case CAGE_NONE:     vga_puts("NONE (system)\n"); break;
        case CAGE_LIGHT:    vga_puts("LIGHT\n"); break;
        case CAGE_STANDARD: vga_puts("STANDARD\n"); break;
        case CAGE_STRICT:   vga_puts("STRICT\n"); break;
        case CAGE_LOCKED:   vga_puts("LOCKED\n"); break;
        default:            vga_puts("UNKNOWN\n"); break;
    }
    
    // Cage root
    if (sb.cageroot[0]) {
        vga_puts("  Cage Root: ");
        vga_puts(sb.cageroot);
        vga_puts("\n");
    }
    
    // Permissions
    vga_puts("  Permissions: ");
    if (sb.syscall_filter & ALLOW_IO_READ)   vga_puts("READ ");
    if (sb.syscall_filter & ALLOW_IO_WRITE)  vga_puts("WRITE ");
    if (sb.syscall_filter & ALLOW_IO_EXEC)   vga_puts("EXEC ");
    if (sb.syscall_filter & ALLOW_PROCESS)   vga_puts("PROCESS ");
    if (sb.syscall_filter & ALLOW_MEMORY)    vga_puts("MEMORY ");
    if (sb.syscall_filter & ALLOW_DEVICE)    vga_puts("DEVICE ");
    if (sb.syscall_filter & ALLOW_TIME)      vga_puts("TIME ");
    if (sb.syscall_filter & ALLOW_IPC)       vga_puts("IPC ");
    vga_puts("\n");
    
    // Resource limits
    vga_puts("  Resource Limits:\n");
    if (sb.limits.max_memory > 0) {
        vga_puts("    Memory: ");
        itoa(sb.limits.max_memory / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" KB\n");
    } else {
        vga_puts("    Memory: unlimited\n");
    }
    
    if (sb.limits.max_files > 0) {
        vga_puts("    Files: ");
        itoa(sb.limits.max_files, buf, 10);
        vga_puts(buf);
        vga_puts("\n");
    } else {
        vga_puts("    Files: unlimited\n");
    }
    
    if (sb.limits.max_processes > 0) {
        vga_puts("    Processes: ");
        itoa(sb.limits.max_processes, buf, 10);
        vga_puts(buf);
        vga_puts("\n");
    } else {
        vga_puts("    Processes: unlimited\n");
    }
    
    // Flags
    if (sb.flags) {
        vga_puts("  Flags: ");
        if (sb.flags & SANDBOX_READONLY)  vga_puts("READONLY ");
        if (sb.flags & SANDBOX_NOEXEC)    vga_puts("NOEXEC ");
        if (sb.flags & SANDBOX_NONET)     vga_puts("NONET ");
        if (sb.flags & SANDBOX_IMMUTABLE) vga_puts("IMMUTABLE ");
        vga_puts("\n");
    }
}

// Command: Create sandbox with specified level
void cmd_cage(const char* args) {
    if (!args || !*args) {
        vga_puts("Usage: cage <level>\n");
        vga_puts("Levels: none, light, standard, strict, locked\n");
        return;
    }
    
    cage_level_t level;
    if (strcmp(args, "none") == 0) {
        level = CAGE_NONE;
    } else if (strcmp(args, "light") == 0) {
        level = CAGE_LIGHT;
    } else if (strcmp(args, "standard") == 0) {
        level = CAGE_STANDARD;
    } else if (strcmp(args, "strict") == 0) {
        level = CAGE_STRICT;
    } else if (strcmp(args, "locked") == 0) {
        level = CAGE_LOCKED;
    } else {
        vga_puts("Invalid cage level\n");
        return;
    }
    
    sandbox_t sb;
    if (sandbox_create(&sb, level) != 0) {
        vga_puts("Failed to create sandbox\n");
        return;
    }
    
    int pid = process_getpid();
    if (sandbox_apply(pid, &sb) != 0) {
        vga_puts("Failed to apply sandbox\n");
        return;
    }
    
    vga_puts("Sandbox applied to current process\n");
}

// Command: Set cage root (aOS's chroot)
void cmd_cageroot(const char* args) {
    if (!args || !*args) {
        vga_puts("Usage: cageroot <path>\n");
        return;
    }
    
    int pid = process_getpid();
    if (cage_set_root(pid, args) != 0) {
        vga_puts("Failed to set cage root\n");
        return;
    }
    
    vga_puts("Cage root set to: ");
    vga_puts(args);
    vga_puts("\n");
    vga_puts("Note: Cage root is active but not yet enforced by VFS\n");
}

// Command: Show file permissions
void cmd_perms(const char* args) {
    if (!args || !*args) {
        vga_puts("Usage: perms <path>\n");
        return;
    }
    
    file_access_t access;
    if (vfs_get_access(args, &access) != 0) {
        vga_puts("Error: Could not get permissions for ");
        vga_puts(args);
        vga_puts("\n");
        return;
    }
    
    vga_puts("Permissions for: ");
    vga_puts(args);
    vga_puts("\n");
    
    // Owner info
    vga_puts("  Owner ID: ");
    char buf[12];
    itoa(access.owner_id, buf, 10);
    vga_puts(buf);
    vga_puts(" (");
    switch (access.owner_type) {
        case OWNER_SYSTEM:  
            vga_puts("SYSTEM"); 
            break;
        case OWNER_ROOT:    
            if (access.owner_id == 0) {
                vga_puts("ROOT USER");
            } else {
                vga_puts("ROOT");
            }
            break;
        case OWNER_ADMIN:   vga_puts("ADMIN"); break;
        case OWNER_PRGMS:   vga_puts("PRGMS"); break;
        case OWNER_USR:     vga_puts("USR"); break;
        case OWNER_BASIC:   vga_puts("BASIC"); break;
        default:            vga_puts("UNKNOWN"); break;
    }
    vga_puts(")\n");
    
    // Owner access
    vga_puts("  Owner Access: ");
    if (access.owner_access & ACCESS_VIEW)   vga_puts("VIEW ");
    if (access.owner_access & ACCESS_MODIFY) vga_puts("MODIFY ");
    if (access.owner_access & ACCESS_RUN)    vga_puts("RUN ");
    if (access.owner_access & ACCESS_DELETE) vga_puts("DELETE ");
    vga_puts("\n");
    
    // Other access
    vga_puts("  Other Access: ");
    if (access.other_access & ACCESS_VIEW)   vga_puts("VIEW ");
    if (access.other_access & ACCESS_MODIFY) vga_puts("MODIFY ");
    if (access.other_access & ACCESS_RUN)    vga_puts("RUN ");
    if (access.other_access & ACCESS_DELETE) vga_puts("DELETE ");
    vga_puts("\n");
    
    // Flags
    if (access.flags) {
        vga_puts("  Flags: ");
        if (access.flags & ACCESS_SYSTEM) vga_puts("SYSTEM ");
        if (access.flags & ACCESS_HIDDEN) vga_puts("HIDDEN ");
        if (access.flags & ACCESS_LOCK)   vga_puts("LOCKED ");
        vga_puts("\n");
    }
}

// Register security commands
void register_security_commands(void) {
    command_register_with_category("sandbox", "[pid]", 
                     "Show sandbox info", "Security", cmd_sandbox);
    command_register_with_category("cage", "<level>", 
                     "Apply sandbox to process", "Security", cmd_cage);
    command_register_with_category("cageroot", "<path>", 
                     "Set cage root", "Security", cmd_cageroot);
    command_register_with_category("perms", "<path>", 
                     "Show file permissions", "Security", cmd_perms);
}
