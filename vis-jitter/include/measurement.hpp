#pragma once

/**
 * measurement.hpp
 *
 * Core latency measurement loop for vis-jitter.
 * Uses RDTSCP + CPUID serialization to measure cycle-accurate latency.
 * Integrates SMI audit and histogram collection per measurement window.
 *
 * License: MIT
 */

#include "vis_jitter.hpp"

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * Detect CPU properties and populate vis_detected_t.
 * Reads frequency from MSR, NUMA node from sched_getcpu(),
 * TSC and RDTSCP support from CPUID.
 *
 * @param core_id   Core the measurement will run on.
 * @param detected  Output: populated system properties.
 * @return          0 on success, -1 on failure.
 */
int vis_detect_system(uint32_t core_id, vis_detected_t* detected);

/**
 * Run the core measurement loop.
 * Pins the calling thread to core_id, then runs measurement windows
 * until duration_sec elapses. Each window is checked for SMI events.
 *
 * @param core_id       CPU core to pin to.
 * @param duration_sec  Total measurement duration in seconds.
 * @param workload      Function to measure. NULL = empty loop.
 * @param context       Passed as-is to workload().
 * @param histogram     Output: populated histogram.
 * @param smi_audit     Output: populated SMI audit data.
 * @param results       Output: populated results (excluding latency stats).
 * @return              0 on success, negative on error.
 */
vis_status_t vis_measure(
    uint32_t              core_id,
    uint32_t              duration_sec,
    const vis_detected_t* detected,
    vis_workload_fn       workload,
    void*                 context,
    vis_histogram_t*      histogram,
    vis_smi_audit_t*      smi_audit,
    vis_results_t*        results
);