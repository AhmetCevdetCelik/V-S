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
    if (fd < 0) 
        // Most common cause: msr kernel module not loaded.
        // sudo modprobe msr fixes this. The tool could auto-load it,
        // but I chose to let the user decide — loading kernel modules
        // silently is a trust violation.
    {
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
     * 
     * This is Linux's elegant design: every MSR is just a file
     * at a known offset. No ioctl, no kernel module needed.
     */
    ssize_t bytes_read = pread(fd, out, sizeof(uint64_t), IA32_SMI_COUNT);

    if (bytes_read != sizeof(uint64_t)) {
        fprintf(stderr, "[vis-jitter] ERROR: Failed to read IA32_SMI_COUNT MSR. "
                        "bytes_read=%zd\n", bytes_read);
        return -1;
    }

    return 0;
}
// ---------------------------------------------------------------------------
// D E S I G N   N O T E S
// ---------------------------------------------------------------------------
// Why /dev/cpu/N/msr instead of a kernel module? (V1 only)
// The MSR device driver gives userspace direct access to hardware counters
// without writing a single line of kernel code. This is perfect for V1:
// zero maintenance burden, zero kernel ABI dependency.
//
// V3 will add a kernel module for UC memory mapping and CAT management.
// At that point, SMI reads will move into the module for tighter integration.
