/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_cpu.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <command_registry.h>
#include <cpu.h>
#include <stdlib.h>
#include <string.h>

extern void kprint(const char *str);

static void print_flag(const char* name, bool enabled) {
    char line[64];
    strcpy(line, "  ");
    strcat(line, name);
    strcat(line, ": ");
    strcat(line, enabled ? "yes" : "no");
    kprint(line);
}

static void cmd_cpuinfo(const char* args) {
    (void)args;

    const cpu_info_t* info = cpu_get_info();
    if (!info || !info->valid) {
        kprint("CPU information unavailable");
        return;
    }

    char line[192];
    char num[24];

    strcpy(line, "Vendor: ");
    strcat(line, info->vendor);
    kprint(line);

    if (info->brand[0]) {
        strcpy(line, "Model: ");
        strcat(line, info->brand);
        kprint(line);
    }

    strcpy(line, "Family/Model/Stepping: ");
    itoa(info->family, num, 10);
    strcat(line, num);
    strcat(line, "/");
    itoa(info->model, num, 10);
    strcat(line, num);
    strcat(line, "/");
    itoa(info->stepping, num, 10);
    strcat(line, num);
    kprint(line);

    strcpy(line, "Detected CPUs: ");
    itoa(info->detected_cpus, num, 10);
    strcat(line, num);
    strcat(line, " (online ");
    itoa(info->online_cpus, num, 10);
    strcat(line, num);
    strcat(line, ")");
    kprint(line);

    strcpy(line, "Topology CPUID logical/cores/threads: ");
    itoa(info->logical_cpus_cpuid, num, 10);
    strcat(line, num);
    strcat(line, "/");
    itoa(info->physical_cores_cpuid, num, 10);
    strcat(line, num);
    strcat(line, "/");
    itoa(info->threads_per_core, num, 10);
    strcat(line, num);
    kprint(line);

    strcpy(line, "Topology ACPI enabled/total: ");
    itoa(info->acpi_enabled_cpus, num, 10);
    strcat(line, num);
    strcat(line, "/");
    itoa(info->acpi_total_cpus, num, 10);
    strcat(line, num);
    kprint(line);

    if (info->lapic_mmio_base != 0) {
        strcpy(line, "LAPIC MMIO base: 0x");
        itoa(info->lapic_mmio_base, num, 16);
        strcat(line, num);
        kprint(line);
    }

    if (info->apic_ids_enabled.count > 0) {
        char ids_line[256];
        strcpy(ids_line, "Enabled LAPIC IDs: ");
        for (uint32_t i = 0; i < info->apic_ids_enabled.count; i++) {
            itoa(info->apic_ids_enabled.ids[i], num, 10);
            strcat(ids_line, num);
            if (i + 1 < info->apic_ids_enabled.count) {
                strcat(ids_line, ",");
            }
            if (strlen(ids_line) > 220 && i + 1 < info->apic_ids_enabled.count) {
                strcat(ids_line, "...");
                break;
            }
        }
        kprint(ids_line);
    }

    if (info->base_mhz > 0) {
        strcpy(line, "Frequency base/max MHz: ");
        itoa(info->base_mhz, num, 10);
        strcat(line, num);
        strcat(line, "/");
        itoa(info->max_mhz, num, 10);
        strcat(line, num);
        kprint(line);
    }

    kprint(info->smp_active ? "SMP state: active" : (info->smp_possible ? "SMP state: capable (AP startup not active)" : "SMP state: single-core mode"));
}

static void cmd_cpufeatures(const char* args) {
    (void)args;

    const cpu_info_t* info = cpu_get_info();
    if (!info || !info->valid) {
        kprint("CPU information unavailable");
        return;
    }

    kprint("CPU feature flags:");
    print_flag("fpu", info->features.fpu);
    print_flag("apic", info->features.apic);
    print_flag("x2apic", info->features.x2apic);
    print_flag("tsc", info->features.tsc);
    print_flag("invariant_tsc", info->features.invariant_tsc);
    print_flag("msr", info->features.msr);
    print_flag("mtrr", info->features.mtrr);
    print_flag("pat", info->features.pat);
    print_flag("mmx", info->features.mmx);
    print_flag("sse", info->features.sse);
    print_flag("sse2", info->features.sse2);
    print_flag("sse3", info->features.sse3);
    print_flag("ssse3", info->features.ssse3);
    print_flag("sse4.1", info->features.sse41);
    print_flag("sse4.2", info->features.sse42);
    print_flag("avx", info->features.avx);
    print_flag("avx2", info->features.avx2);
    print_flag("aes", info->features.aes);
    print_flag("fma", info->features.fma);
    print_flag("htt", info->features.htt);
}

void cmd_module_cpu_register(void) {
    command_register_with_category("cpuinfo", "", "Show CPU model/topology detection", "System", cmd_cpuinfo);
    command_register_with_category("cpufeatures", "", "Show CPU instruction and platform flags", "System", cmd_cpufeatures);
}
