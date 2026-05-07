/**
 * test_smi_audit.cpp
 *
 * Manual test for SMI monitoring functions.
 * Reads IA32_SMI_COUNT MSR twice with a short delay and reports
 * whether any SMI events occurred during that window.
 *
 * Run as root:
 *   sudo ./test_smi_audit
 *
 * License: MIT
 */

#include "../include/smi_audit.hpp"

#include <cstdio>
#include <unistd.h>     // sleep()
#include <cinttypes>    // PRIu64 for portable uint64_t printing

int main() {
    const uint32_t core_id = 2;

    printf("[test] Opening MSR for core %u...\n", core_id);

    int fd = vis_msr_open(core_id);
    if (fd < 0) {
        printf("[test] FAILED: Could not open MSR. Try: sudo modprobe msr\n");
        return 1;
    }

    printf("[test] MSR opened successfully.\n");

    // Read SMI counter at window start
    uint64_t smi_start = 0;
    if (vis_smi_read(fd, &smi_start) < 0) {
        printf("[test] FAILED: Could not read SMI counter.\n");
        vis_msr_close(fd);
        return 1;
    }

    printf("[test] SMI counter at start: %lu\n", smi_start);
    printf("[test] Waiting 3 seconds...\n");

    sleep(3);

    // Read SMI counter at window end
    uint64_t smi_end = 0;
    if (vis_smi_read(fd, &smi_end) < 0) {
        printf("[test] FAILED: Could not read SMI counter.\n");
        vis_msr_close(fd);
        return 1;
    }

    printf("[test] SMI counter at start: %" PRIu64 "\n", smi_start);
    printf("[test] SMI counter at end:   %" PRIu64 "\n", smi_end);
    printf("[test] SMI events detected:  %" PRIu64 "\n", smi_end - smi_start);

    if (vis_smi_detected(smi_start, smi_end)) {
        printf("[test] RESULT: SMI event occurred during window — window would be REJECTED.\n");
    } else {
        printf("[test] RESULT: No SMI events — window is CLEAN.\n");
    }

    vis_msr_close(fd);
    return 0;
}