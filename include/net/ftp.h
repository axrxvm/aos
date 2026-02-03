/*
 * === AOS HEADER BEGIN ===
 * ./include/net/ftp.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef FTP_H
#define FTP_H

#include <stdint.h>
#include <net/net.h>

// FTP default ports
#define FTP_CONTROL_PORT    21
#define FTP_DATA_PORT       20

// FTP response codes
#define FTP_REPLY_READY         220     // Service ready
#define FTP_REPLY_GOODBYE       221     // Service closing
#define FTP_REPLY_TRANSFER_OK   226     // Transfer complete
#define FTP_REPLY_PASSIVE_OK    227     // Entering passive mode
#define FTP_REPLY_LOGIN_OK      230     // User logged in
#define FTP_REPLY_FILE_OK       250     // File action completed
#define FTP_REPLY_PATHNAME      257     // Pathname created
#define FTP_REPLY_NEED_PASS     331     // Need password
#define FTP_REPLY_NEED_ACCT     332     // Need account
#define FTP_REPLY_PENDING       350     // Pending further info
#define FTP_REPLY_UNAVAIL       421     // Service unavailable
#define FTP_REPLY_CONN_FAIL     425     // Can't open data connection
#define FTP_REPLY_ABORTED       426     // Connection closed, transfer aborted
#define FTP_REPLY_NOT_FOUND     450     // File unavailable
#define FTP_REPLY_LOCAL_ERR     451     // Local error
#define FTP_REPLY_STORAGE_ERR   452     // Insufficient storage
#define FTP_REPLY_SYNTAX_ERR    500     // Syntax error
#define FTP_REPLY_PARAM_ERR     501     // Syntax error in params
#define FTP_REPLY_NOT_IMPL      502     // Command not implemented
#define FTP_REPLY_BAD_SEQ       503     // Bad sequence of commands
#define FTP_REPLY_NOT_IMPL_PARAM 504    // Command not implemented for param
#define FTP_REPLY_NOT_LOGGED    530     // Not logged in
#define FTP_REPLY_FILE_FAIL     550     // File unavailable
#define FTP_REPLY_PAGE_ERR      551     // Page type unknown
#define FTP_REPLY_EXCEED_QUOTA  552     // Exceeded storage allocation
#define FTP_REPLY_NAME_ERR      553     // File name not allowed

// FTP transfer modes
#define FTP_MODE_ASCII      'A'
#define FTP_MODE_BINARY     'I'
#define FTP_MODE_EBCDIC     'E'

// FTP transfer types
#define FTP_TYPE_STREAM     'S'
#define FTP_TYPE_BLOCK      'B'
#define FTP_TYPE_COMPRESSED 'C'

// FTP buffer sizes
#define FTP_BUFFER_SIZE     4096
#define FTP_MAX_PATH        256

// FTP session structure
typedef struct {
    char host[128];
    uint16_t port;
    uint32_t ip_addr;
    void* control_socket;   // TCP socket for control connection
    void* data_socket;      // TCP socket for data connection
    int connected;
    int logged_in;
    char username[64];
    char current_dir[FTP_MAX_PATH];
    char transfer_mode;
    int passive_mode;
    uint32_t data_ip;       // IP for data connection (passive)
    uint16_t data_port;     // Port for data connection (passive)
    char last_reply[512];
    int last_code;
} ftp_session_t;

// FTP file info
typedef struct {
    char name[256];
    uint32_t size;
    uint8_t is_directory;
    char permissions[16];
    char date[32];
} ftp_file_info_t;

// FTP initialization
void ftp_init(void);

// FTP session management
ftp_session_t* ftp_session_create(void);
void ftp_session_free(ftp_session_t* session);

// FTP connection
int ftp_connect(ftp_session_t* session, const char* host, uint16_t port);
int ftp_login(ftp_session_t* session, const char* username, const char* password);
int ftp_disconnect(ftp_session_t* session);

// FTP commands
int ftp_pwd(ftp_session_t* session, char* path, int max_len);
int ftp_cwd(ftp_session_t* session, const char* path);
int ftp_cdup(ftp_session_t* session);
int ftp_mkdir(ftp_session_t* session, const char* path);
int ftp_rmdir(ftp_session_t* session, const char* path);
int ftp_delete(ftp_session_t* session, const char* path);
int ftp_rename(ftp_session_t* session, const char* from, const char* to);
int ftp_size(ftp_session_t* session, const char* path, uint32_t* size);

// FTP transfer operations
int ftp_set_mode(ftp_session_t* session, char mode);
int ftp_set_passive(ftp_session_t* session, int passive);
int ftp_list(ftp_session_t* session, const char* path, char* buffer, int max_len);
int ftp_download(ftp_session_t* session, const char* remote_path, const char* local_path);
int ftp_upload(ftp_session_t* session, const char* local_path, const char* remote_path);
int ftp_get(ftp_session_t* session, const char* path, uint8_t** data, uint32_t* len);
int ftp_put(ftp_session_t* session, const char* path, const uint8_t* data, uint32_t len);

// FTP utilities
int ftp_send_command(ftp_session_t* session, const char* cmd, const char* arg);
int ftp_read_reply(ftp_session_t* session);

#endif // FTP_H
