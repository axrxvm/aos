/*
 * === AOS HEADER BEGIN ===
 * include/time_subsystem.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef TIME_SUBSYSTEM_H
#define TIME_SUBSYSTEM_H

#include <stdint.h>
#include <stdbool.h>

#define TIME_MAX_TIMEZONE_LEN 64
#define TIME_DEFAULT_TIMEZONE "UTC"
#define TIME_CONFIG_PATH "/etc/timezone.conf"

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} aos_datetime_t;

// Initialize time subsystem state and load persisted timezone
void time_subsystem_init(void);

// Synchronize wall clock with the aOS time API
int time_sync_now(void);

// Get/set timezone (IANA identifier format, e.g. "Asia/Kolkata")
const char* time_get_timezone(void);
int time_set_timezone(const char* timezone, bool persist);

// Query current wall clock time and status
int time_get_datetime(aos_datetime_t* out);
int time_format_now(char* buffer, uint32_t size);
bool time_is_synced(void);

#endif // TIME_SUBSYSTEM_H
