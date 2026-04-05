/*
 * === AOS HEADER BEGIN ===
 * src/system/cpu.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <cpu.h>
#include <acpi.h>
#include <serial.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} cpuid_regs_t;

static cpu_info_t g_cpu_info;

static void cpuid_exec(uint32_t leaf, uint32_t subleaf, cpuid_regs_t* out) {
    if (!out) {
        return;
    }

    __asm__ volatile(
        "cpuid"
        : "=a"(out->eax), "=b"(out->ebx), "=c"(out->ecx), "=d"(out->edx)
        : "a"(leaf), "c"(subleaf)
        : "memory"
    );
}

static void trim_brand(char* text) {
    if (!text) {
        return;
    }

    uint32_t len = (uint32_t)strlen(text);
    while (len > 0 && text[len - 1] == ' ') {
        text[len - 1] = '\0';
        len--;
    }
}

static void parse_family_model(uint32_t eax, uint32_t* family, uint32_t* model, uint32_t* stepping) {
    uint32_t base_stepping = eax & 0xF;
    uint32_t base_model = (eax >> 4) & 0xF;
    uint32_t base_family = (eax >> 8) & 0xF;
    uint32_t ext_model = (eax >> 16) & 0xF;
    uint32_t ext_family = (eax >> 20) & 0xFF;

    uint32_t effective_family = base_family;
    if (base_family == 0xF) {
        effective_family = base_family + ext_family;
    }

    uint32_t effective_model = base_model;
    if (base_family == 0x6 || base_family == 0xF) {
        effective_model = (ext_model << 4) | base_model;
    }

    *family = effective_family;
    *model = effective_model;
    *stepping = base_stepping;
}

static uint32_t max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

void cpu_detect_topology(void) {
    cpuid_regs_t r0;
    cpuid_regs_t r1;
    cpuid_regs_t rext;

    memset(&g_cpu_info, 0, sizeof(g_cpu_info));

    cpuid_exec(0, 0, &r0);
    g_cpu_info.max_basic_leaf = r0.eax;
    memcpy(&g_cpu_info.vendor[0], &r0.ebx, sizeof(uint32_t));
    memcpy(&g_cpu_info.vendor[4], &r0.edx, sizeof(uint32_t));
    memcpy(&g_cpu_info.vendor[8], &r0.ecx, sizeof(uint32_t));
    g_cpu_info.vendor[12] = '\0';

    cpuid_exec(0x80000000u, 0, &rext);
    g_cpu_info.max_extended_leaf = rext.eax;

    cpuid_exec(1, 0, &r1);
    parse_family_model(r1.eax, &g_cpu_info.family, &g_cpu_info.model, &g_cpu_info.stepping);
    g_cpu_info.apic_id = (r1.ebx >> 24) & 0xFF;
    g_cpu_info.logical_cpus_cpuid = (r1.ebx >> 16) & 0xFF;
    if (g_cpu_info.logical_cpus_cpuid == 0) {
        g_cpu_info.logical_cpus_cpuid = 1;
    }

    g_cpu_info.features.fpu = (r1.edx & (1u << 0)) != 0;
    g_cpu_info.features.tsc = (r1.edx & (1u << 4)) != 0;
    g_cpu_info.features.msr = (r1.edx & (1u << 5)) != 0;
    g_cpu_info.features.apic = (r1.edx & (1u << 9)) != 0;
    g_cpu_info.features.mtrr = (r1.edx & (1u << 12)) != 0;
    g_cpu_info.features.pat = (r1.edx & (1u << 16)) != 0;
    g_cpu_info.features.cmov = (r1.edx & (1u << 15)) != 0;
    g_cpu_info.features.mmx = (r1.edx & (1u << 23)) != 0;
    g_cpu_info.features.sse = (r1.edx & (1u << 25)) != 0;
    g_cpu_info.features.sse2 = (r1.edx & (1u << 26)) != 0;
    g_cpu_info.features.htt = (r1.edx & (1u << 28)) != 0;

    g_cpu_info.features.sse3 = (r1.ecx & (1u << 0)) != 0;
    g_cpu_info.features.ssse3 = (r1.ecx & (1u << 9)) != 0;
    g_cpu_info.features.fma = (r1.ecx & (1u << 12)) != 0;
    g_cpu_info.features.sse41 = (r1.ecx & (1u << 19)) != 0;
    g_cpu_info.features.sse42 = (r1.ecx & (1u << 20)) != 0;
    g_cpu_info.features.x2apic = (r1.ecx & (1u << 21)) != 0;
    g_cpu_info.features.tsc_deadline = (r1.ecx & (1u << 24)) != 0;
    g_cpu_info.features.xsave = (r1.ecx & (1u << 26)) != 0;
    g_cpu_info.features.avx = (r1.ecx & (1u << 28)) != 0;
    g_cpu_info.features.aes = (r1.ecx & (1u << 25)) != 0;

    g_cpu_info.physical_cores_cpuid = 1;
    g_cpu_info.threads_per_core = 1;

    if (g_cpu_info.max_basic_leaf >= 0xB) {
        uint32_t smt_width = 0;
        uint32_t logical_per_pkg = g_cpu_info.logical_cpus_cpuid;

        for (uint32_t level = 0; level < 8; level++) {
            cpuid_regs_t rt;
            cpuid_exec(0xB, level, &rt);

            uint32_t level_type = (rt.ecx >> 8) & 0xFF;
            if (level_type == 0 || rt.ebx == 0) {
                break;
            }

            if (level_type == 1) {
                smt_width = rt.ebx & 0xFFFF;
                if (smt_width == 0) {
                    smt_width = 1;
                }
            } else if (level_type == 2) {
                logical_per_pkg = rt.ebx & 0xFFFF;
            }
        }

        if (smt_width == 0) {
            smt_width = 1;
        }
        if (logical_per_pkg == 0) {
            logical_per_pkg = g_cpu_info.logical_cpus_cpuid;
            if (logical_per_pkg == 0) {
                logical_per_pkg = 1;
            }
        }

        g_cpu_info.threads_per_core = smt_width;
        g_cpu_info.physical_cores_cpuid = logical_per_pkg / smt_width;
        if (g_cpu_info.physical_cores_cpuid == 0) {
            g_cpu_info.physical_cores_cpuid = 1;
        }
    } else if (g_cpu_info.max_basic_leaf >= 4) {
        cpuid_regs_t rc4;
        cpuid_exec(4, 0, &rc4);
        g_cpu_info.physical_cores_cpuid = ((rc4.eax >> 26) & 0x3F) + 1;
        if (g_cpu_info.physical_cores_cpuid == 0) {
            g_cpu_info.physical_cores_cpuid = 1;
        }
        g_cpu_info.threads_per_core = g_cpu_info.logical_cpus_cpuid / g_cpu_info.physical_cores_cpuid;
        if (g_cpu_info.threads_per_core == 0) {
            g_cpu_info.threads_per_core = 1;
        }
    }

    if (g_cpu_info.max_basic_leaf >= 7) {
        cpuid_regs_t r7;
        cpuid_exec(7, 0, &r7);
        g_cpu_info.features.avx2 = (r7.ebx & (1u << 5)) != 0;
    }

    if (g_cpu_info.max_extended_leaf >= 0x80000004u) {
        uint32_t* brand_words = (uint32_t*)g_cpu_info.brand;
        cpuid_regs_t rb;
        cpuid_exec(0x80000002u, 0, &rb);
        brand_words[0] = rb.eax;
        brand_words[1] = rb.ebx;
        brand_words[2] = rb.ecx;
        brand_words[3] = rb.edx;

        cpuid_exec(0x80000003u, 0, &rb);
        brand_words[4] = rb.eax;
        brand_words[5] = rb.ebx;
        brand_words[6] = rb.ecx;
        brand_words[7] = rb.edx;

        cpuid_exec(0x80000004u, 0, &rb);
        brand_words[8] = rb.eax;
        brand_words[9] = rb.ebx;
        brand_words[10] = rb.ecx;
        brand_words[11] = rb.edx;
        g_cpu_info.brand[48] = '\0';
        trim_brand(g_cpu_info.brand);
    }

    if (g_cpu_info.max_basic_leaf >= 0x16) {
        cpuid_regs_t r16;
        cpuid_exec(0x16, 0, &r16);
        g_cpu_info.base_mhz = r16.eax & 0xFFFF;
        g_cpu_info.max_mhz = r16.ebx & 0xFFFF;
    }

    if (g_cpu_info.max_extended_leaf >= 0x80000007u) {
        cpuid_regs_t r87;
        cpuid_exec(0x80000007u, 0, &r87);
        g_cpu_info.features.invariant_tsc = (r87.edx & (1u << 8)) != 0;
    }

    {
        acpi_cpu_topology_t acpi_topo;
        if (acpi_get_cpu_topology(&acpi_topo) == 0) {
            g_cpu_info.acpi_enabled_cpus = acpi_topo.enabled_count;
            g_cpu_info.acpi_total_cpus = acpi_topo.total_count;
            g_cpu_info.lapic_mmio_base = acpi_topo.lapic_address;
            g_cpu_info.bsp_lapic_id = acpi_topo.bsp_lapic_id;
        }
    }

    {
        acpi_cpu_id_list_t ids;
        if (acpi_get_cpu_id_list(&ids, true) == 0) {
            g_cpu_info.apic_ids_enabled = ids;
        }
    }

    g_cpu_info.detected_cpus = max_u32(g_cpu_info.logical_cpus_cpuid, g_cpu_info.acpi_enabled_cpus);
    if (g_cpu_info.detected_cpus == 0) {
        g_cpu_info.detected_cpus = 1;
    }

    if (g_cpu_info.acpi_enabled_cpus > 0 && g_cpu_info.logical_cpus_cpuid > 0) {
        g_cpu_info.package_count = g_cpu_info.acpi_enabled_cpus / g_cpu_info.logical_cpus_cpuid;
        if (g_cpu_info.package_count == 0) {
            g_cpu_info.package_count = 1;
        }
    } else {
        g_cpu_info.package_count = 1;
    }

    g_cpu_info.smp_possible = (g_cpu_info.detected_cpus > 1) && g_cpu_info.features.apic;
    g_cpu_info.smp_active = false;
    g_cpu_info.online_cpus = 1;
    g_cpu_info.valid = true;
}

const cpu_info_t* cpu_get_info(void) {
    return &g_cpu_info;
}

uint32_t cpu_get_detected_count(void) {
    return g_cpu_info.detected_cpus > 0 ? g_cpu_info.detected_cpus : 1;
}

uint32_t cpu_get_online_count(void) {
    return g_cpu_info.online_cpus > 0 ? g_cpu_info.online_cpus : 1;
}

void cpu_log_summary(void) {
    if (!g_cpu_info.valid) {
        serial_puts("CPU: not detected\n");
        return;
    }

    char nbuf[32];

    serial_puts("CPU vendor: ");
    serial_puts(g_cpu_info.vendor);
    serial_puts("\n");

    if (g_cpu_info.brand[0] != '\0') {
        serial_puts("CPU model: ");
        serial_puts(g_cpu_info.brand);
        serial_puts("\n");
    }

    serial_puts("CPU family/model/stepping: ");
    itoa(g_cpu_info.family, nbuf, 10);
    serial_puts(nbuf);
    serial_puts("/");
    itoa(g_cpu_info.model, nbuf, 10);
    serial_puts(nbuf);
    serial_puts("/");
    itoa(g_cpu_info.stepping, nbuf, 10);
    serial_puts(nbuf);
    serial_puts("\n");

    serial_puts("CPU topology (cpuid/acpi): ");
    itoa(g_cpu_info.logical_cpus_cpuid, nbuf, 10);
    serial_puts(nbuf);
    serial_puts("/");
    itoa(g_cpu_info.acpi_enabled_cpus, nbuf, 10);
    serial_puts(nbuf);
    serial_puts(" detected, online ");
    itoa(g_cpu_info.online_cpus, nbuf, 10);
    serial_puts(nbuf);
    serial_puts("\n");

    if (g_cpu_info.lapic_mmio_base != 0) {
        serial_puts("LAPIC base/bsp-id: 0x");
        itoa(g_cpu_info.lapic_mmio_base, nbuf, 16);
        serial_puts(nbuf);
        serial_puts("/");
        itoa(g_cpu_info.bsp_lapic_id, nbuf, 10);
        serial_puts(nbuf);
        serial_puts("\n");
    }

    if (g_cpu_info.smp_possible && !g_cpu_info.smp_active) {
        serial_puts("SMP: hardware supports multiple CPUs; AP startup pending in this build\n");
    } else if (g_cpu_info.smp_active) {
        serial_puts("SMP: active\n");
    } else {
        serial_puts("SMP: single CPU mode\n");
    }
}
