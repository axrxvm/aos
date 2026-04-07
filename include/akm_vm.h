/*
 * === AOS HEADER BEGIN ===
 * include/akm_vm.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/*
 * DEVELOPER_NOTE_BLOCK
 * Module Overview:
 * - This file is part of the aOS production kernel/userspace codebase.
 * - Review public symbols in this unit to understand contracts with adjacent modules.
 * - Keep behavior-focused comments near non-obvious invariants, state transitions, and safety checks.
 * - Avoid changing ABI/data-layout assumptions without updating dependent modules.
 */

#ifndef AKM_VM_H
#define AKM_VM_H

/**
 * AKM Virtual Machine
 * Simple stack-based bytecode interpreter for executing akmcc-compiled
 * kernel modules (.akm files).
 */

#include <stdint.h>
#include <stddef.h>
#include <kmodule_api.h>

// VM OPCODES

// Stack operations
#define AKM_OP_NOP          0x00
#define AKM_OP_PUSH         0x01    // Push 32-bit value
#define AKM_OP_PUSH_STR     0x02    // Push string table offset
#define AKM_OP_PUSH_ARG     0x03    // Push argument
#define AKM_OP_POP          0x04    // Pop top
#define AKM_OP_DUP          0x05    // Duplicate top
#define AKM_OP_SWAP         0x06    // Swap top two

// Load/Store
#define AKM_OP_LOAD_LOCAL   0x10    // Load local variable
#define AKM_OP_STORE_LOCAL  0x11    // Store local variable
#define AKM_OP_LOAD_GLOBAL  0x12    // Load global variable
#define AKM_OP_STORE_GLOBAL 0x13    // Store global variable

// Arithmetic
#define AKM_OP_ADD          0x20
#define AKM_OP_SUB          0x21
#define AKM_OP_MUL          0x22
#define AKM_OP_DIV          0x23
#define AKM_OP_MOD          0x24
#define AKM_OP_NEG          0x25
#define AKM_OP_INC          0x26
#define AKM_OP_DEC          0x27

// Bitwise
#define AKM_OP_AND          0x30
#define AKM_OP_OR           0x31
#define AKM_OP_XOR          0x32
#define AKM_OP_NOT          0x33
#define AKM_OP_SHL          0x34
#define AKM_OP_SHR          0x35

// Comparison (push 1 or 0)
#define AKM_OP_EQ           0x40
#define AKM_OP_NE           0x41
#define AKM_OP_LT           0x42
#define AKM_OP_LE           0x43
#define AKM_OP_GT           0x44
#define AKM_OP_GE           0x45

// Control flow
#define AKM_OP_JMP          0x50    // Unconditional jump
#define AKM_OP_JZ           0x51    // Jump if zero
#define AKM_OP_JNZ          0x52    // Jump if not zero
#define AKM_OP_CALL         0x53    // Call function
#define AKM_OP_CALL_API     0x54    // Call kernel API
#define AKM_OP_RET          0x55    // Return

// Memory
#define AKM_OP_LOAD8        0x60
#define AKM_OP_LOAD16       0x61
#define AKM_OP_LOAD32       0x62
#define AKM_OP_STORE8       0x63
#define AKM_OP_STORE16      0x64
#define AKM_OP_STORE32      0x65

// Special
#define AKM_OP_SYSCALL      0x70
#define AKM_OP_BREAKPOINT   0x71
#define AKM_OP_HALT         0x7F

//                           API INDICES
// Must match akmcc/src/constants.js API_FUNCTIONS order

#define AKM_API_LOG             0
#define AKM_API_INFO            1
#define AKM_API_WARN            2
#define AKM_API_ERROR           3
#define AKM_API_DEBUG           4
#define AKM_API_HEXDUMP         5
#define AKM_API_MALLOC          6
#define AKM_API_CALLOC          7
#define AKM_API_REALLOC         8
#define AKM_API_FREE            9
#define AKM_API_ALLOC_PAGE      10
#define AKM_API_FREE_PAGE       11
#define AKM_API_REGISTER_CMD    12
#define AKM_API_UNREGISTER_CMD  13
#define AKM_API_GETENV          14
#define AKM_API_SETENV          15
#define AKM_API_UNSETENV        16
#define AKM_API_REGISTER_DRV    17
#define AKM_API_UNREGISTER_DRV  18
#define AKM_API_REGISTER_FS     19
#define AKM_API_UNREGISTER_FS   20
#define AKM_API_VFS_OPEN        21
#define AKM_API_VFS_CLOSE       22
#define AKM_API_VFS_READ        23
#define AKM_API_VFS_WRITE       24
#define AKM_API_VFS_SEEK        25
#define AKM_API_REGISTER_NETIF  26
#define AKM_API_UNREGISTER_NETIF 27
#define AKM_API_NETIF_RECEIVE   28
#define AKM_API_REGISTER_IRQ    29
#define AKM_API_UNREGISTER_IRQ  30
#define AKM_API_ENABLE_IRQ      31
#define AKM_API_DISABLE_IRQ     32
#define AKM_API_OUTB            33
#define AKM_API_OUTW            34
#define AKM_API_OUTL            35
#define AKM_API_INB             36
#define AKM_API_INW             37
#define AKM_API_INL             38
#define AKM_API_IO_WAIT         39
#define AKM_API_PCI_FIND_DEV    40
#define AKM_API_PCI_FIND_CLASS  41
#define AKM_API_PCI_READ_CFG    42
#define AKM_API_PCI_WRITE_CFG   43
#define AKM_API_PCI_BUSMASTER   44
#define AKM_API_CREATE_TIMER    45
#define AKM_API_START_TIMER     46
#define AKM_API_STOP_TIMER      47
#define AKM_API_DESTROY_TIMER   48
#define AKM_API_GET_TICKS       49
#define AKM_API_SLEEP           50
#define AKM_API_SPAWN           51
#define AKM_API_KILL            52
#define AKM_API_GETPID          53
#define AKM_API_YIELD           54
#define AKM_API_GET_SYSINFO     55
#define AKM_API_GET_KERNEL_VER  56
#define AKM_API_IPC_SEND        57
#define AKM_API_IPC_RECV        58
#define AKM_API_IPC_CREATE_CH   59
#define AKM_API_IPC_DESTROY_CH  60
#define AKM_API_SHA256          61
#define AKM_API_RANDOM_BYTES    62
#define AKM_API_GET_UID         63
#define AKM_API_GET_USERNAME    64
#define AKM_API_CHECK_PERM      65
#define AKM_API_GET_ARGS        66
#define AKM_API_PRINT           67
#define AKM_API_STRCAT          68
#define AKM_API_ITOA            69
#define AKM_API_STRLEN          70
#define AKM_API_SOCKET_CREATE   71
#define AKM_API_SOCKET_BIND     72
#define AKM_API_SOCKET_LISTEN   73
#define AKM_API_SOCKET_ACCEPT   74
#define AKM_API_SOCKET_CONNECT  75
#define AKM_API_SOCKET_SEND     76
#define AKM_API_SOCKET_RECV     77
#define AKM_API_SOCKET_CLOSE    78
#define AKM_API_DNS_RESOLVE     79
#define AKM_API_NET_POLL        80
#define AKM_API_NET_INIT        81
#define AKM_API_NET_IF_COUNT    82
#define AKM_API_NET_IF_GET      83
#define AKM_API_NET_IF_GET_IDX  84
#define AKM_API_NET_IF_UP       85
#define AKM_API_NET_IF_DOWN     86
#define AKM_API_NET_IF_SET_IP   87
#define AKM_API_NET_PKT_ALLOC   88
#define AKM_API_NET_PKT_FREE    89
#define AKM_API_NET_TX_PKT      90
#define AKM_API_NET_RX_PKT      91
#define AKM_API_IP_TO_STRING    92
#define AKM_API_STRING_TO_IP    93
#define AKM_API_ETH_RECEIVE     94
#define AKM_API_ETH_TRANSMIT    95
#define AKM_API_MAC_TO_STRING   96
#define AKM_API_MAC_COMPARE     97
#define AKM_API_ARP_INIT        98
#define AKM_API_ARP_SEND_REQ    99
#define AKM_API_ARP_SEND_REP    100
#define AKM_API_ARP_CACHE_LOOKUP 101
#define AKM_API_ARP_CACHE_ADD   102
#define AKM_API_ARP_CACHE_CLEAR 103
#define AKM_API_ARP_CACHE_ENTRIES 104
#define AKM_API_IPV4_INIT       105
#define AKM_API_IPV4_SEND       106
#define AKM_API_IPV4_ROUTE      107
#define AKM_API_IPV4_PENDING    108
#define AKM_API_IPV4_RESOLVE_ARP 109
#define AKM_API_ICMP_INIT       110
#define AKM_API_ICMP_ECHO_REQ   111
#define AKM_API_DNS_INIT        112
#define AKM_API_DNS_SET_SERVER  113
#define AKM_API_DNS_GET_SERVER  114
#define AKM_API_DNS_SET_TIMEOUT 115
#define AKM_API_DNS_SET_RETRY   116
#define AKM_API_DNS_CACHE_CLEAR 117
#define AKM_API_DNS_CACHE_LOOKUP 118
#define AKM_API_DNS_CACHE_ADD   119
#define AKM_API_DNS_CACHE_ENTRIES 120
#define AKM_API_DHCP_INIT       121
#define AKM_API_DHCP_DISCOVER   122
#define AKM_API_DHCP_DISCOVER_TIMED 123
#define AKM_API_DHCP_CONFIG_IF  124
#define AKM_API_DHCP_GET_CONFIG 125
#define AKM_API_SOCKET_SENDTO   126
#define AKM_API_SOCKET_RECVFROM 127
#define AKM_API_TCP_INIT        128
#define AKM_API_TCP_SOCK_CREATE 129
#define AKM_API_TCP_SOCK_BIND   130
#define AKM_API_TCP_SOCK_LISTEN 131
#define AKM_API_TCP_SOCK_ACCEPT 132
#define AKM_API_TCP_SOCK_CONNECT 133
#define AKM_API_TCP_SOCK_SEND   134
#define AKM_API_TCP_SOCK_RECV   135
#define AKM_API_TCP_SOCK_CLOSE  136
#define AKM_API_TCP_SOCK_CONN_BLK 137
#define AKM_API_TCP_SOCK_RECV_BLK 138
#define AKM_API_TCP_STATE_TO_STR 139
#define AKM_API_UDP_INIT        140
#define AKM_API_UDP_SOCK_CREATE 141
#define AKM_API_UDP_SOCK_BIND   142
#define AKM_API_UDP_SOCK_CONNECT 143
#define AKM_API_UDP_SOCK_SENDTO 144
#define AKM_API_UDP_SOCK_RECVFROM 145
#define AKM_API_UDP_SOCK_CLOSE  146
#define AKM_API_UDP_SEND        147
#define AKM_API_HTTP_INIT       148
#define AKM_API_HTTP_GET        149
#define AKM_API_HTTP_POST       150
#define AKM_API_HTTP_SEND       151
#define AKM_API_HTTP_DOWNLOAD   152
#define AKM_API_HTTP_STATUS_TEXT 153
#define AKM_API_HTTP_REQ_CREATE 154
#define AKM_API_HTTP_REQ_FREE   155
#define AKM_API_HTTP_REQ_ADD_HEADER 156
#define AKM_API_HTTP_REQ_SET_BODY 157
#define AKM_API_HTTP_RESP_CREATE 158
#define AKM_API_HTTP_RESP_FREE  159
#define AKM_API_HTTP_RESP_GET_HEADER 160
#define AKM_API_TLS_INIT        161
#define AKM_API_TLS_SESSION_CREATE 162
#define AKM_API_TLS_HANDSHAKE   163
#define AKM_API_TLS_SEND        164
#define AKM_API_TLS_RECV        165
#define AKM_API_TLS_SESSION_CLOSE 166
#define AKM_API_TLS_SESSION_FREE 167
#define AKM_API_TLS_SET_VERIFY  168
#define AKM_API_NAT_INIT        169
#define AKM_API_NAT_ENABLE      170
#define AKM_API_NAT_DISABLE     171
#define AKM_API_NAT_IS_ENABLED  172
#define AKM_API_NAT_SET_INTERNAL_IF 173
#define AKM_API_NAT_SET_EXTERNAL_IF 174
#define AKM_API_NAT_SET_EXTERNAL_IP 175
#define AKM_API_NAT_PROCESS_OUT 176
#define AKM_API_NAT_PROCESS_IN  177
#define AKM_API_NAT_ADD_PORT_FWD 178
#define AKM_API_NAT_REMOVE_PORT_FWD 179
#define AKM_API_NAT_LIST_PORT_FWD 180
#define AKM_API_NAT_GET_STATS   181
#define AKM_API_NAT_GET_CONFIG  182
#define AKM_API_NAT_DUMP_TABLE  183
#define AKM_API_NAT_TIMER_TICK  184
#define AKM_API_LOOPBACK_INIT   185
#define AKM_API_LOOPBACK_GET_IF 186
#define AKM_API_NETCONFIG_INIT  187
#define AKM_API_NETCONFIG_GET   188
#define AKM_API_NETCONFIG_SET   189
#define AKM_API_NETCONFIG_SET_STATIC 190
#define AKM_API_NETCONFIG_SET_DHCP 191
#define AKM_API_NETCONFIG_APPLY 192
#define AKM_API_NETCONFIG_SAVE  193
#define AKM_API_NETCONFIG_LOAD  194
#define AKM_API_NETCONFIG_GET_HOSTNAME 195
#define AKM_API_NETCONFIG_SET_HOSTNAME 196
#define AKM_API_NETCONFIG_SET_DNS 197
#define AKM_API_NETCONFIG_GET_DNS 198
#define AKM_API_HTTP_RESP_GET_STATUS 199
#define AKM_API_HTTP_RESP_GET_BODY 200

//                           VM STATE

#define AKM_VM_STACK_SIZE   256     // Stack slots
#define AKM_VM_LOCALS_MAX   64      // Max local variables
#define AKM_VM_CALL_DEPTH   32      // Max call depth

// VM state flags
#define AKM_VM_RUNNING      0x01
#define AKM_VM_HALTED       0x02
#define AKM_VM_ERROR        0x04
#define AKM_VM_BREAKPOINT   0x08

// Error codes
#define AKM_VM_OK           0
#define AKM_VM_ERR_STACK    -1      // Stack overflow/underflow
#define AKM_VM_ERR_OPCODE   -2      // Invalid opcode
#define AKM_VM_ERR_ADDR     -3      // Invalid address
#define AKM_VM_ERR_DIV0     -4      // Division by zero
#define AKM_VM_ERR_API      -5      // API error
#define AKM_VM_ERR_CALL     -6      // Call depth exceeded

// VM instance
typedef struct akm_vm {
    // Code and data sections
    const uint8_t*  code;
    size_t          code_size;
    const uint8_t*  data;
    size_t          data_size;
    const char*     strtab;
    size_t          strtab_size;
    
    // Registers
    uint32_t        pc;             // Program counter (code offset)
    uint32_t        sp;             // Stack pointer
    uint32_t        fp;             // Frame pointer
    
    // Stack
    int32_t         stack[AKM_VM_STACK_SIZE];
    
    // Local variables (per call frame)
    int32_t         locals[AKM_VM_LOCALS_MAX];
    
    // Call stack
    uint32_t        call_stack[AKM_VM_CALL_DEPTH];
    uint32_t        call_fp[AKM_VM_CALL_DEPTH];
    int             call_depth;
    
    // Module context
    kmod_ctx_t*     ctx;
    
    // Command args (for command handlers)
    const char*     cmd_args;
    
    // State
    uint32_t        flags;
    int             error_code;
    int             return_value;
} akm_vm_t;

//                           VM API

/**
 * Initialize a VM instance for a module
 */
void akm_vm_init(akm_vm_t* vm, const void* code, size_t code_size,
                 const void* data, size_t data_size,
                 const char* strtab, size_t strtab_size,
                 kmod_ctx_t* ctx);

/**
 * Reset VM state (keep code/data pointers)
 */
void akm_vm_reset(akm_vm_t* vm);

/**
 * Execute bytecode starting at offset
 * Returns 0 on success, negative on error
 */
int akm_vm_execute(akm_vm_t* vm, uint32_t start_offset);

/**
 * Single-step one instruction
 * Returns 0 if still running, 1 if halted/returned, negative on error
 */
int akm_vm_step(akm_vm_t* vm);

/**
 * Get string from string table
 */
const char* akm_vm_get_string(akm_vm_t* vm, uint32_t offset);

/**
 * Push value onto stack
 */
int akm_vm_push(akm_vm_t* vm, int32_t value);

/**
 * Pop value from stack
 */
int32_t akm_vm_pop(akm_vm_t* vm);

/**
 * Cleanup resources registered by the VM module
 * Called when module is unloaded
 */
void akm_vm_cleanup_registry(akm_vm_t* vm);

#endif // AKM_VM_H
