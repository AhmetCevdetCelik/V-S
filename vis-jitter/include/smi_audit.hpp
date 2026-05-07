#pragma once

/**
 * smi_audit.hpp
 *
 * SMI (System Management Interrupt) monitoring via IA32_SMI_COUNT MSR.
 * Provides functions to read the SMI counter and detect SMI events
 * during a measurement window.
 *
 * Requires: /dev/cpu/N/msr to be accessible (run as root or with CAP_SYS_RAWIO)
 *
 * License: MIT
 */

#include <cstdint>
/** MSR address for SMI event counter */
#define IA32_SMI_COUNT 0x34

/**
 * Open the MSR file descriptor for the given CPU core.
 * Must be called before vis_smi_read().
 *
 * @param core_id  CPU core index (e.g. 2 for /dev/cpu/2/msr)
 * @return         File descriptor on success, -1 on failure.
 */
int vis_msr_open(uint32_t core_id);

/**
 * Close the MSR file descriptor returned by vis_msr_open().
 */
void vis_msr_close(int fd);

/**
 * Read the current value of IA32_SMI_COUNT MSR (address 0x34).
 *
 * @param fd    File descriptor from vis_msr_open().
 * @param out   Output: the current SMI counter value.
 * @return      0 on success, -1 on failure.
 */
int vis_smi_read(int fd, uint64_t* out);

/**
 * Check whether an SMI event occurred between two counter readings.
 *
 * @param start  Counter value at window start.
 * @param end    Counter value at window end.
 * @return       true if at least one SMI event occurred.
 */
inline bool vis_smi_detected(uint64_t start, uint64_t end) {
    return end > start;
}