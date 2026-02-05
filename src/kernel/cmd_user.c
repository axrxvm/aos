/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/cmd_user.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <user.h>
#include <fs_layout.h>
#include <serial.h>
#include <shell.h>

extern void kprint(const char *str);

static void cmd_adduser(const char* args) {
    if (!args || !*args) {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        vga_puts("Usage: ");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_puts("adduser <username> [--admin]");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        return;
    }
    
    session_t* session = user_get_session();
    if (session && session->user) {
        char buf[32];
        serial_puts("Debug: Current user: ");
        serial_puts(session->user->username);
        serial_puts(", UID: ");
        itoa(session->user->uid, buf, 10);
        serial_puts(buf);
        serial_puts(", flags: 0x");
        itoa(session->user->flags, buf, 16);
        serial_puts(buf);
        serial_puts(", is_root: ");
        itoa(user_is_root(), buf, 10);
        serial_puts(buf);
        serial_puts(", is_admin: ");
        itoa(user_is_admin(), buf, 10);
        serial_puts(buf);
        serial_puts("\n");
    }
    
    if (!user_is_root() && !user_is_admin()) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("Permission denied: Only root/admin can add users");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    
    char username[32];
    int is_admin = 0;
    
    const char* ptr = args;
    uint32_t i = 0;
    
    while (*ptr && *ptr != ' ' && i < sizeof(username) - 1) {
        username[i++] = *ptr++;
    }
    username[i] = '\0';
    
    while (*ptr == ' ') ptr++;
    if (*ptr && strcmp(ptr, "--admin") == 0) {
        is_admin = 1;
    }
    
    char home_dir[128];
    fs_layout_get_user_home(username, home_dir, sizeof(home_dir));
    
    int ret = user_create(username, username, 0, GID_USERS, home_dir, "/bin/shell");
    if (ret == 0) {
        user_t* new_user = user_find_by_name(username);
        if (new_user) {
            if (is_admin) {
                new_user->flags |= USER_FLAG_ADMIN;
                vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
                vga_puts("[ADMIN] ");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            }
            
            new_user->flags |= USER_FLAG_MUST_CHANGE_PASS;
        }
        
        fs_layout_create_user_home(username);
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("User '");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_puts(username);
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("' created successfully.");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        kprint("Default password is the same as username.");
        kprint("User will be asked to change password on first login.");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        
        if (fs_layout_get_mode() == FS_MODE_LOCAL) {
            user_save_database(USER_DATABASE_PATH);
        }
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("Failed to create user '");
        vga_puts(username);
        vga_puts("'");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    }
}

static void cmd_deluser(const char* args) {
    if (!args || !*args) {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        vga_puts("Usage: ");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_puts("deluser <username>");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        return;
    }
    
    if (!user_is_root() && !user_is_admin()) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("Permission denied: Only root/admin can delete users");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    
    char username[32];
    strncpy(username, args, sizeof(username) - 1);
    username[sizeof(username) - 1] = '\0';
    
    uint32_t len = strlen(username);
    while (len > 0 && username[len - 1] == ' ') {
        username[--len] = '\0';
    }
    
    int ret = user_delete(username);
    if (ret == 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("User '");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_puts(username);
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("' deleted successfully.");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("Failed to delete user '");
        vga_puts(username);
        vga_puts("'");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    }
}

static void list_user_callback(user_t* user, void* user_data) {
    (void)user_data;
    
    char buf[16];
    
    vga_puts("  UID ");
    itoa(user->uid, buf, 10);
    vga_puts(buf);
    vga_puts(": ");
    
    vga_puts(user->username);
    
    vga_puts(" (GID ");
    itoa(user->gid, buf, 10);
    vga_puts(buf);
    vga_puts(")");
    
    if (user->flags & USER_FLAG_LOCKED) {
        vga_puts(" [LOCKED]");
    } else if (!(user->flags & USER_FLAG_ACTIVE)) {
        vga_puts(" [INACTIVE]");
    }
    if (user->flags & USER_FLAG_ADMIN) {
        vga_puts(" [ADMIN]");
    }
    
    kprint("");
}

static void cmd_listusers(const char* args) {
    (void)args;
    
    char buf[16];
    uint32_t count = user_get_count();
    
    vga_puts("Total users: ");
    itoa(count, buf, 10);
    kprint(buf);
    
    if (count > 0) {
        user_list_all(list_user_callback, NULL);
    }
}

static void cmd_passwd(const char* args) {
    (void)args;
    
    session_t* session = user_get_session();
    if (!session || !session->user) {
        kprint("Error: Not logged in");
        return;
    }
    
    char old_password[128];
    char new_password[128];
    char confirm_password[128];
    
    kprint("Changing password for user: ");
    kprint(session->user->username);
    kprint("");
    vga_puts("Old password: ");
    if (read_password(old_password, sizeof(old_password)) <= 0) {
        kprint("\nPassword change cancelled.");
        return;
    }
    
    if (!user_authenticate(session->user->username, old_password)) {
        kprint("\nError: Incorrect password");
        return;
    }
    
    vga_puts("New password: ");
    if (read_password(new_password, sizeof(new_password)) <= 0) {
        kprint("\nPassword change cancelled.");
        return;
    }
    
    if (strlen(new_password) < 4) {
        kprint("\nError: Password must be at least 4 characters");
        return;
    }
    
    vga_puts("Retype new password: ");
    if (read_password(confirm_password, sizeof(confirm_password)) <= 0) {
        kprint("\nPassword change cancelled.");
        return;
    }
    
    if (strcmp(new_password, confirm_password) != 0) {
        kprint("\nError: Passwords do not match");
        return;
    }
    
    if (user_change_password(session->user->username, old_password, new_password) == 0) {
        kprint("\nPassword changed successfully!");
        
        if (fs_layout_get_mode() == FS_MODE_LOCAL) {
            if (user_save_database("/sys/config/users.db") == 0) {
                serial_puts("User database saved after password change\n");
            } else {
                kprint("Warning: Failed to save user database to disk");
            }
        }
    } else {
        kprint("\nError: Failed to change password");
    }
}

static void cmd_fsmode(const char* args) {
    (void)args;
    
    int mode = fs_layout_get_mode();
    vga_puts("Filesystem mode: ");
    if (mode == FS_MODE_LOCAL) {
        kprint("LOCAL (disk filesystem)");
        kprint("  User data will persist across reboots");
    } else {
        kprint("ISO (ramfs in memory)");
        kprint("  User data will NOT persist across reboots");
    }
}

void cmd_module_user_register(void) {
    command_register_with_category("adduser", "<username> [--admin]", "Create user account", "User", cmd_adduser);
    command_register_with_category("deluser", "<username>", "Delete user account", "User", cmd_deluser);
    command_register_with_category("listusers", "", "List user accounts", "User", cmd_listusers);
    command_register_with_category("passwd", "", "Change password", "User", cmd_passwd);
    command_register_with_category("fsmode", "", "Display filesystem mode", "User", cmd_fsmode);
}
