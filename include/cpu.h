/*
 * === AOS HEADER BEGIN ===
 * include/cpu.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>
#include <acpi.h>

typedef struct {
    bool fpu;
    bool apic;
    bool tsc;
    bool msr;
    bool mtrr;
    bool pat;
    bool cmov;
    bool mmx;
    bool sse;
    bool sse2;
    bool sse3;
    bool ssse3;
    bool sse41;
    bool sse42;
    bool avx;
    bool avx2;
    bool aes;
    bool fma;
    bool htt;
    bool x2apic;
    bool xsave;
    bool tsc_deadline;
    bool invariant_tsc;
} cpu_features_t;

typedef struct {
    bool valid;
    char vendor[13];
    char brand[49];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t apic_id;
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    uint32_t logical_cpus_cpuid;
    uint32_t physical_cores_cpuid;
    uint32_t threads_per_core;
    uint32_t package_count;
    uint32_t acpi_enabled_cpus;
    uint32_t acpi_total_cpus;
    uint32_t lapic_mmio_base;
    uint32_t bsp_lapic_id;
    acpi_cpu_id_list_t apic_ids_enabled;
    uint32_t detected_cpus;
    uint32_t online_cpus;
    uint32_t base_mhz;
    uint32_t max_mhz;
    bool smp_possible;
    bool smp_active;
    cpu_features_t features;
} cpu_info_t;

void cpu_detect_topology(void);
void cpu_log_summary(void);

const cpu_info_t* cpu_get_info(void);
uint32_t cpu_get_detected_count(void);
uint32_t cpu_get_online_count(void);

#endif // CPU_H
