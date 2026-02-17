/*
 * === AOS HEADER BEGIN ===
 * src/system/time_subsystem.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <time_subsystem.h>
#include <net/http.h>
#include <fs/vfs.h>
#include <envars.h>
#include <arch.h>
#include <io.h>
#include <acpi.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <vmm.h>

#define TIME_API_BASE_URL "http://api.aosproject.workers.dev/time"
#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT 0x71
#define CMOS_NMI_DISABLE 0x80
#define RTC_REG_SECONDS 0x00
#define RTC_REG_MINUTES 0x02
#define RTC_REG_HOURS 0x04
#define RTC_REG_DAY 0x07
#define RTC_REG_MONTH 0x08
#define RTC_REG_YEAR 0x09
#define RTC_REG_STATUS_A 0x0A
#define RTC_REG_STATUS_B 0x0B
#define RTC_REG_CENTURY_FALLBACK 0x32
#define RTC_STATUS_A_UIP 0x80
#define RTC_STATUS_B_24H 0x02
#define RTC_STATUS_B_BINARY 0x04
#define RTC_STATUS_B_SET 0x80

static char current_timezone[TIME_MAX_TIMEZONE_LEN];
static aos_datetime_t base_datetime = {1970, 1, 1, 0, 0, 0};
static uint32_t base_ticks = 0;
static bool wall_clock_synced = false;

static int save_timezone_config(void);
static int load_timezone_config(void);
static int bios_rtc_read_datetime(aos_datetime_t* out);
static int bios_rtc_write_datetime(const aos_datetime_t* in);
static int days_in_month(int year, int month);

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
    uint8_t reg_b;
} rtc_snapshot_t;

static uint8_t cmos_read_register(uint8_t reg) {
    outb(CMOS_ADDRESS_PORT, (uint8_t)((reg & 0x7F) | CMOS_NMI_DISABLE));
    io_wait();
    return inb(CMOS_DATA_PORT);
}

static void cmos_write_register(uint8_t reg, uint8_t value) {
    outb(CMOS_ADDRESS_PORT, (uint8_t)((reg & 0x7F) | CMOS_NMI_DISABLE));
    io_wait();
    outb(CMOS_DATA_PORT, value);
    io_wait();
}

static int rtc_wait_until_ready(void) {
    for (int i = 0; i < 100000; i++) {
        if ((cmos_read_register(RTC_REG_STATUS_A) & RTC_STATUS_A_UIP) == 0) {
            return 0;
        }
    }
    return -1;
}

static uint8_t rtc_get_century_register(void) {
    const acpi_state_t* acpi = acpi_get_state();
    if (acpi && acpi->fadt && acpi->fadt->century != 0) {
        return acpi->fadt->century;
    }
    return RTC_REG_CENTURY_FALLBACK;
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

static uint8_t binary_to_bcd(uint8_t value) {
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static void rtc_take_snapshot(rtc_snapshot_t* out) {
    uint8_t century_reg = rtc_get_century_register();

    out->seconds = cmos_read_register(RTC_REG_SECONDS);
    out->minutes = cmos_read_register(RTC_REG_MINUTES);
    out->hours = cmos_read_register(RTC_REG_HOURS);
    out->day = cmos_read_register(RTC_REG_DAY);
    out->month = cmos_read_register(RTC_REG_MONTH);
    out->year = cmos_read_register(RTC_REG_YEAR);
    out->century = cmos_read_register(century_reg);
    out->reg_b = cmos_read_register(RTC_REG_STATUS_B);
}

static int rtc_snapshot_equal(const rtc_snapshot_t* a, const rtc_snapshot_t* b) {
    if (a->seconds != b->seconds) return 0;
    if (a->minutes != b->minutes) return 0;
    if (a->hours != b->hours) return 0;
    if (a->day != b->day) return 0;
    if (a->month != b->month) return 0;
    if (a->year != b->year) return 0;
    if (a->century != b->century) return 0;
    return a->reg_b == b->reg_b;
}

static int rtc_year_from_snapshot(const rtc_snapshot_t* snap) {
    int year_low;
    int century;

    if (!snap) {
        return -1;
    }

    if (snap->reg_b & RTC_STATUS_B_BINARY) {
        year_low = snap->year;
        century = snap->century;
    } else {
        year_low = (int)bcd_to_binary(snap->year);
        century = (int)bcd_to_binary(snap->century);
    }

    if (century >= 19 && century <= 99) {
        return (century * 100) + year_low;
    }

    if (year_low < 70) {
        return 2000 + year_low;
    }
    return 1900 + year_low;
}

static int bios_rtc_read_datetime(aos_datetime_t* out) {
    rtc_snapshot_t a;
    rtc_snapshot_t b;
    int hour;
    int minute;
    int second;
    int day;
    int month;
    int year;
    int is_binary;
    int is_24h;
    int pm_flag;

    if (!out) {
        return -1;
    }

    if (rtc_wait_until_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 5; i++) {
        rtc_take_snapshot(&a);
        if (rtc_wait_until_ready() != 0) {
            return -1;
        }
        rtc_take_snapshot(&b);

        if (rtc_snapshot_equal(&a, &b)) {
            break;
        }

        if (i == 4) {
            return -1;
        }
    }

    is_binary = (b.reg_b & RTC_STATUS_B_BINARY) ? 1 : 0;
    is_24h = (b.reg_b & RTC_STATUS_B_24H) ? 1 : 0;

    second = is_binary ? b.seconds : (int)bcd_to_binary(b.seconds);
    minute = is_binary ? b.minutes : (int)bcd_to_binary(b.minutes);
    hour = is_binary ? (b.hours & 0x7F) : (int)bcd_to_binary((uint8_t)(b.hours & 0x7F));
    day = is_binary ? b.day : (int)bcd_to_binary(b.day);
    month = is_binary ? b.month : (int)bcd_to_binary(b.month);
    year = rtc_year_from_snapshot(&b);
    pm_flag = (b.hours & 0x80) ? 1 : 0;

    if (!is_24h) {
        if (hour == 12) {
            hour = 0;
        }
        if (pm_flag) {
            hour += 12;
        }
    }

    if (year < 1970 || year > 9999) return -1;
    if (month < 1 || month > 12) return -1;
    if (day < 1 || day > days_in_month(year, month)) return -1;
    if (hour < 0 || hour > 23) return -1;
    if (minute < 0 || minute > 59) return -1;
    if (second < 0 || second > 59) return -1;

    out->year = (uint16_t)year;
    out->month = (uint8_t)month;
    out->day = (uint8_t)day;
    out->hour = (uint8_t)hour;
    out->minute = (uint8_t)minute;
    out->second = (uint8_t)second;
    return 0;
}

static int bios_rtc_write_datetime(const aos_datetime_t* in) {
    uint8_t reg_b;
    uint8_t century_reg = rtc_get_century_register();
    int is_binary;
    int is_24h;
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;

    if (!in) {
        return -1;
    }

    if (in->month < 1 || in->month > 12) return -1;
    if (in->day < 1 || in->day > days_in_month((int)in->year, (int)in->month)) return -1;
    if (in->hour > 23 || in->minute > 59 || in->second > 59) return -1;

    if (rtc_wait_until_ready() != 0) {
        return -1;
    }

    reg_b = cmos_read_register(RTC_REG_STATUS_B);
    is_binary = (reg_b & RTC_STATUS_B_BINARY) ? 1 : 0;
    is_24h = (reg_b & RTC_STATUS_B_24H) ? 1 : 0;

    seconds = in->second;
    minutes = in->minute;
    day = in->day;
    month = in->month;
    year = (uint8_t)(in->year % 100);
    century = (uint8_t)(in->year / 100);

    if (is_24h) {
        hours = in->hour;
    } else {
        uint8_t hour12 = (uint8_t)(in->hour % 12);
        if (hour12 == 0) {
            hour12 = 12;
        }
        hours = hour12;
        if (in->hour >= 12) {
            hours |= 0x80;  // PM bit
        }
    }

    if (!is_binary) {
        seconds = binary_to_bcd(seconds);
        minutes = binary_to_bcd(minutes);
        day = binary_to_bcd(day);
        month = binary_to_bcd(month);
        year = binary_to_bcd(year);
        if (is_24h) {
            hours = binary_to_bcd(hours);
        } else {
            hours = (uint8_t)((hours & 0x80) | binary_to_bcd((uint8_t)(hours & 0x7F)));
        }
        century = binary_to_bcd(century);
    }

    cmos_write_register(RTC_REG_STATUS_B, (uint8_t)(reg_b | RTC_STATUS_B_SET));
    cmos_write_register(RTC_REG_SECONDS, seconds);
    cmos_write_register(RTC_REG_MINUTES, minutes);
    cmos_write_register(RTC_REG_HOURS, hours);
    cmos_write_register(RTC_REG_DAY, day);
    cmos_write_register(RTC_REG_MONTH, month);
    cmos_write_register(RTC_REG_YEAR, year);
    cmos_write_register(century_reg, century);
    cmos_write_register(RTC_REG_STATUS_B, reg_b);

    return 0;
}

static int is_digit_char(char c) {
    return (c >= '0' && c <= '9');
}

static int parse_n_digits(const char* ptr, int digits) {
    int value = 0;
    if (!ptr || digits <= 0) {
        return -1;
    }

    for (int i = 0; i < digits; i++) {
        if (!is_digit_char(ptr[i])) {
            return -1;
        }
        value = (value * 10) + (ptr[i] - '0');
    }

    return value;
}

static int is_leap_year(int year) {
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    return (year % 4 == 0) ? 1 : 0;
}

static int days_in_month(int year, int month) {
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return dim[month - 1];
}

static void increment_day(aos_datetime_t* dt) {
    int dim;
    if (!dt) {
        return;
    }

    dim = days_in_month((int)dt->year, (int)dt->month);
    dt->day++;
    if (dt->day <= dim) {
        return;
    }

    dt->day = 1;
    dt->month++;
    if (dt->month <= 12) {
        return;
    }

    dt->month = 1;
    dt->year++;
}

static void datetime_add_seconds(aos_datetime_t* dt, uint32_t seconds) {
    while (dt && seconds > 0) {
        uint32_t seconds_of_day;
        uint32_t remaining_today;

        seconds_of_day = ((uint32_t)dt->hour * 3600U) + ((uint32_t)dt->minute * 60U) + (uint32_t)dt->second;
        remaining_today = 86400U - seconds_of_day;

        if (seconds < remaining_today) {
            seconds_of_day += seconds;
            dt->hour = (uint8_t)(seconds_of_day / 3600U);
            dt->minute = (uint8_t)((seconds_of_day % 3600U) / 60U);
            dt->second = (uint8_t)(seconds_of_day % 60U);
            return;
        }

        seconds -= remaining_today;
        dt->hour = 0;
        dt->minute = 0;
        dt->second = 0;
        increment_day(dt);
    }
}

static int parse_iso_datetime(const char* iso, aos_datetime_t* out) {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;

    if (!iso || !out || strlen(iso) < 19) {
        return -1;
    }

    if (iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' || iso[13] != ':' || iso[16] != ':') {
        return -1;
    }

    year = parse_n_digits(iso, 4);
    month = parse_n_digits(iso + 5, 2);
    day = parse_n_digits(iso + 8, 2);
    hour = parse_n_digits(iso + 11, 2);
    minute = parse_n_digits(iso + 14, 2);
    second = parse_n_digits(iso + 17, 2);

    if (year < 1970 || month < 1 || month > 12) return -1;
    if (day < 1 || day > days_in_month(year, month)) return -1;
    if (hour < 0 || hour > 23) return -1;
    if (minute < 0 || minute > 59) return -1;
    if (second < 0 || second > 59) return -1;

    out->year = (uint16_t)year;
    out->month = (uint8_t)month;
    out->day = (uint8_t)day;
    out->hour = (uint8_t)hour;
    out->minute = (uint8_t)minute;
    out->second = (uint8_t)second;
    return 0;
}

static int validate_timezone(const char* timezone) {
    size_t len;

    if (!timezone) {
        return 0;
    }

    len = strlen(timezone);
    if (len == 0 || len >= TIME_MAX_TIMEZONE_LEN) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        char c = timezone[i];
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '/' || c == '_' || c == '-' || c == '+') {
            continue;
        }
        return 0;
    }

    return 1;
}

static char* trim_whitespace(char* str) {
    char* end;

    if (!str) {
        return str;
    }

    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
        str++;
    }

    end = str + strlen(str);
    while (end > str) {
        char c = end[-1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            end--;
            *end = '\0';
            continue;
        }
        break;
    }

    return str;
}

static int is_unreserved_query_char(char c) {
    if ((c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
        return 1;
    }
    return 0;
}

static char hex_digit(uint8_t v) {
    return (v < 10) ? (char)('0' + v) : (char)('A' + (v - 10));
}

static void url_encode_component(const char* input, char* output, uint32_t out_size) {
    uint32_t out = 0;
    uint32_t i = 0;

    if (!output || out_size == 0) {
        return;
    }

    output[0] = '\0';
    if (!input) {
        return;
    }

    while (input[i] && out + 1 < out_size) {
        uint8_t c = (uint8_t)input[i];
        if (is_unreserved_query_char((char)c)) {
            output[out++] = (char)c;
            i++;
            continue;
        }

        if (out + 3 >= out_size) {
            break;
        }

        output[out++] = '%';
        output[out++] = hex_digit((uint8_t)((c >> 4) & 0x0F));
        output[out++] = hex_digit((uint8_t)(c & 0x0F));
        i++;
    }

    output[out] = '\0';
}

static int json_extract_string(const char* json, const char* key, char* out, uint32_t out_size) {
    char needle[64];
    const char* ptr;
    uint32_t idx = 0;

    if (!json || !key || !out || out_size == 0) {
        return -1;
    }

    needle[0] = '\0';
    strncat(needle, "\"", sizeof(needle) - strlen(needle) - 1);
    strncat(needle, key, sizeof(needle) - strlen(needle) - 1);
    strncat(needle, "\":\"", sizeof(needle) - strlen(needle) - 1);

    ptr = strstr(json, needle);
    if (!ptr) {
        return -1;
    }
    ptr += strlen(needle);

    while (*ptr && *ptr != '"' && idx + 1 < out_size) {
        if (*ptr == '\\' && *(ptr + 1)) {
            ptr++;
        }
        out[idx++] = *ptr++;
    }

    out[idx] = '\0';
    return (*ptr == '"') ? 0 : -1;
}

const char* time_get_timezone(void) {
    return current_timezone;
}

bool time_is_synced(void) {
    return wall_clock_synced;
}

int time_set_timezone(const char* timezone, bool persist) {
    if (!validate_timezone(timezone)) {
        return -1;
    }

    strncpy(current_timezone, timezone, sizeof(current_timezone) - 1);
    current_timezone[sizeof(current_timezone) - 1] = '\0';

    envar_set("TZ", current_timezone);

    if (persist) {
        return save_timezone_config();
    }

    return 0;
}

int time_get_datetime(aos_datetime_t* out) {
    uint32_t now_ticks;
    uint32_t tick_frequency;
    uint32_t elapsed_seconds;

    if (!out) {
        return -1;
    }

    if (!wall_clock_synced) {
        return -1;
    }

    *out = base_datetime;
    now_ticks = arch_timer_get_ticks();
    tick_frequency = arch_timer_get_frequency();
    if (tick_frequency == 0) {
        tick_frequency = 100;
    }

    elapsed_seconds = (now_ticks - base_ticks) / tick_frequency;
    datetime_add_seconds(out, elapsed_seconds);
    return 0;
}

static int append_char(char* buffer, uint32_t size, uint32_t* offset, char ch) {
    if (!buffer || !offset || *offset + 1 >= size) {
        return -1;
    }

    buffer[*offset] = ch;
    (*offset)++;
    buffer[*offset] = '\0';
    return 0;
}

static int append_text(char* buffer, uint32_t size, uint32_t* offset, const char* text) {
    uint32_t i = 0;

    if (!text) {
        return -1;
    }

    while (text[i]) {
        if (append_char(buffer, size, offset, text[i]) != 0) {
            return -1;
        }
        i++;
    }

    return 0;
}

static int append_padded_u32(char* buffer, uint32_t size, uint32_t* offset, uint32_t value, uint32_t width) {
    char digits[10];

    if (width == 0 || width > sizeof(digits)) {
        return -1;
    }

    for (int i = (int)width - 1; i >= 0; i--) {
        digits[i] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    for (uint32_t i = 0; i < width; i++) {
        if (append_char(buffer, size, offset, digits[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

int time_format_now(char* buffer, uint32_t size) {
    aos_datetime_t current;
    uint32_t offset = 0;

    if (!buffer || size == 0) {
        return -1;
    }

    buffer[0] = '\0';

    if (time_get_datetime(&current) != 0) {
        return -1;
    }

    if (append_padded_u32(buffer, size, &offset, (uint32_t)current.year, 4) != 0) return -1;
    if (append_char(buffer, size, &offset, '-') != 0) return -1;
    if (append_padded_u32(buffer, size, &offset, (uint32_t)current.month, 2) != 0) return -1;
    if (append_char(buffer, size, &offset, '-') != 0) return -1;
    if (append_padded_u32(buffer, size, &offset, (uint32_t)current.day, 2) != 0) return -1;
    if (append_char(buffer, size, &offset, ' ') != 0) return -1;
    if (append_padded_u32(buffer, size, &offset, (uint32_t)current.hour, 2) != 0) return -1;
    if (append_char(buffer, size, &offset, ':') != 0) return -1;
    if (append_padded_u32(buffer, size, &offset, (uint32_t)current.minute, 2) != 0) return -1;
    if (append_char(buffer, size, &offset, ':') != 0) return -1;
    if (append_padded_u32(buffer, size, &offset, (uint32_t)current.second, 2) != 0) return -1;
    if (append_char(buffer, size, &offset, ' ') != 0) return -1;
    if (append_text(buffer, size, &offset, current_timezone) != 0) return -1;

    return 0;
}

int time_sync_now(void) {
    http_response_t* response;
    aos_datetime_t parsed;
    char encoded_tz[TIME_MAX_TIMEZONE_LEN * 3];
    char url[256];
    char api_timezone[TIME_MAX_TIMEZONE_LEN];
    char api_datetime[48];
    int result = -1;

    response = http_response_create();
    if (!response) {
        return -1;
    }

    url_encode_component(current_timezone, encoded_tz, sizeof(encoded_tz));
    if (encoded_tz[0] == '\0') {
        strncpy(encoded_tz, "UTC", sizeof(encoded_tz) - 1);
        encoded_tz[sizeof(encoded_tz) - 1] = '\0';
    }

    url[0] = '\0';
    strncat(url, TIME_API_BASE_URL, sizeof(url) - strlen(url) - 1);
    strncat(url, "?tz=", sizeof(url) - strlen(url) - 1);
    strncat(url, encoded_tz, sizeof(url) - strlen(url) - 1);

    if (http_get(url, response) != 0) {
        goto done;
    }
    if (response->status_code != HTTP_STATUS_OK || !response->body) {
        goto done;
    }

    if (json_extract_string((const char*)response->body, "timezone", api_timezone, sizeof(api_timezone)) != 0) {
        goto done;
    }
    if (json_extract_string((const char*)response->body, "datetime", api_datetime, sizeof(api_datetime)) != 0) {
        goto done;
    }
    if (parse_iso_datetime(api_datetime, &parsed) != 0) {
        goto done;
    }

    base_datetime = parsed;
    base_ticks = arch_timer_get_ticks();
    wall_clock_synced = true;

    if (validate_timezone(api_timezone)) {
        strncpy(current_timezone, api_timezone, sizeof(current_timezone) - 1);
        current_timezone[sizeof(current_timezone) - 1] = '\0';
        envar_set("TZ", current_timezone);
    }

    serial_puts("[TIME] Synchronized via API (");
    serial_puts(current_timezone);
    serial_puts(")\n");

    if (bios_rtc_write_datetime(&parsed) == 0) {
        serial_puts("[TIME] BIOS RTC updated\n");
    } else {
        serial_puts("[TIME] BIOS RTC update failed\n");
    }

    result = 0;

done:
    http_response_free(response);
    return result;
}

static int save_timezone_config(void) {
    int fd;
    char line[128];
    int len;

    fd = vfs_open(TIME_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return -1;
    }

    line[0] = '\0';
    strncat(line, "timezone=", sizeof(line) - strlen(line) - 1);
    strncat(line, current_timezone, sizeof(line) - strlen(line) - 1);
    strncat(line, "\n", sizeof(line) - strlen(line) - 1);
    len = (int)strlen(line);
    vfs_write(fd, line, (uint32_t)len);
    vfs_close(fd);
    return 0;
}

static int load_timezone_config(void) {
    int fd;
    char buffer[256];
    int bytes_read;
    char* line;

    fd = vfs_open(TIME_CONFIG_PATH, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    bytes_read = vfs_read(fd, buffer, sizeof(buffer) - 1);
    vfs_close(fd);

    if (bytes_read <= 0) {
        return -1;
    }

    buffer[bytes_read] = '\0';
    line = buffer;

    while (*line) {
        char* line_end = line;
        char* value;
        char saved;

        while (*line_end && *line_end != '\n') line_end++;
        saved = *line_end;
        *line_end = '\0';

        value = trim_whitespace(line);
        if (*value && *value != '#' && *value != ';') {
            if (strncmp(value, "timezone=", 9) == 0) {
                value += 9;
            }
            value = trim_whitespace(value);
            if (validate_timezone(value)) {
                time_set_timezone(value, false);
                *line_end = saved;
                return 0;
            }
        }

        *line_end = saved;
        if (*line_end == '\0') break;
        line = line_end + 1;
    }

    return -1;
}

void time_subsystem_init(void) {
    aos_datetime_t bios_time;

    serial_puts("Initializing time subsystem...\n");

    strncpy(current_timezone, TIME_DEFAULT_TIMEZONE, sizeof(current_timezone) - 1);
    current_timezone[sizeof(current_timezone) - 1] = '\0';
    base_ticks = arch_timer_get_ticks();
    wall_clock_synced = false;
    envar_set("TZ", current_timezone);

    if (load_timezone_config() == 0) {
        serial_puts("[TIME] Loaded timezone from ");
        serial_puts(TIME_CONFIG_PATH);
        serial_puts(": ");
        serial_puts(current_timezone);
        serial_puts("\n");
    }

    if (bios_rtc_read_datetime(&bios_time) == 0) {
        base_datetime = bios_time;
        base_ticks = arch_timer_get_ticks();
        wall_clock_synced = true;
        serial_puts("[TIME] Loaded wall clock from BIOS RTC\n");
    }

    serial_puts("Time subsystem initialized.\n");
}
