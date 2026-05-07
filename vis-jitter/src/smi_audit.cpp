/**
 * smi_audit.cpp
 *
 * Implementation of SMI monitoring via IA32_SMI_COUNT MSR.
 * Uses Linux MSR device driver (/dev/cpu/N/msr) to read
 * the SMI counter directly from hardware.
 *
 * Requires root or CAP_SYS_RAWIO capability.
 *
 * License: MIT
 */

#include "../include/smi_audit.hpp"

#include <fcntl.h>      // open()
#include <unistd.h>     // close(), pread()
#include <cstdio>       // fprintf()

// ---------------------------------------------------------------------------
// Internal helper
// ---------------------------------------------------------------------------

/**
 * Build the MSR device path for a given CPU core.
 * e.g. core 2 → "/dev/cpu/2/msr"
 */
static void build_msr_path(uint32_t core_id, char* buf, size_t buf_size) {
    snprintf(buf, buf_size, "/dev/cpu/%u/msr", core_id);
}

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

int vis_msr_open(uint32_t core_id) {
    char path[64];
    build_msr_path(core_id, path, sizeof(path));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[vis-jitter] ERROR: Cannot open %s. "
                        "Run as root or load msr kernel module: "
                        "sudo modprobe msr\n", path);
    }
    return fd;
}

void vis_msr_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

int vis_smi_read(int fd, uint64_t* out) {
    if (fd < 0 || out == nullptr) {
        return -1;
    }

    /**
     * pread() reads exactly 8 bytes from the MSR file at the
     * offset corresponding to IA32_SMI_COUNT (0x34).
     * The offset IS the MSR address on Linux's /dev/cpu/N/msr.
     */
    ssize_t bytes_read = pread(fd, out, sizeof(uint64_t), IA32_SMI_COUNT);

    if (bytes_read != sizeof(uint64_t)) {
        fprintf(stderr, "[vis-jitter] ERROR: Failed to read IA32_SMI_COUNT MSR. "
                        "bytes_read=%zd\n", bytes_read);
        return -1;
    }

    return 0;
}