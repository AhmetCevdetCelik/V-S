/**
 * measurement.cpp
 *
 * Core latency measurement loop for vis-jitter.
 * Pins thread to a given core, runs windowed RDTSCP measurements,
 * checks for SMI events per window, and populates histogram.
 *
 * License: MIT
 */

#include "../include/measurement.hpp"
#include "../include/smi_audit.hpp"
#include "../include/histogram.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cinttypes>
#include <cpuid.h>
#include <pthread.h>
#include <sched.h>
#include <numa.h>

// ---------------------------------------------------------------------------
// Internal: RDTSCP inline
// ---------------------------------------------------------------------------

static inline uint64_t rdtscp(uint32_t* aux) {
    uint64_t rax, rdx;
    __asm__ volatile (
        "rdtscp"
        : "=a"(rax), "=d"(rdx), "=c"(*aux)
        :
        : "memory"
    );
    // TSC is a 64-bit counter. Upper 32 in RDX, lower 32 in RAX.
    // RDTSCP also returns the core ID in RCX — we use this to
    // detect thread migration mid-measurement.
    return (rdx << 32) | rax;
}

static inline void serialize() {
    uint32_t eax, ebx, ecx, edx;
    __get_cpuid(0, &eax, &ebx, &ecx, &edx);
}

// ---------------------------------------------------------------------------
// Internal: thread pinning
// ---------------------------------------------------------------------------

static int pin_thread_to_core(uint32_t core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int ret = pthread_setaffinity_np(
        pthread_self(), sizeof(cpu_set_t), &cpuset
    );
    if (ret != 0) {
        fprintf(stderr, "[vis-jitter] ERROR: Failed to pin thread to core %u\n",
                core_id);
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Internal: CPU frequency
// ---------------------------------------------------------------------------

static double read_cpu_frequency_ghz(uint32_t core_id) {
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%u/cpufreq/cpuinfo_max_freq",
             core_id);

    FILE* f = fopen(path, "r");
    if (f == nullptr) {
        fprintf(stderr, "[vis-jitter] WARNING: Cannot read CPU frequency "
                        "from %s.\n", path);
        return 0.0;
    }

    uint64_t khz = 0;
    fscanf(f, "%" SCNu64, &khz);
    fclose(f);

    return static_cast<double>(khz) / 1e6;
}

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

int vis_detect_system(uint32_t core_id, vis_detected_t* detected) {
    if (detected == nullptr) return -1;

    memset(detected, 0, sizeof(vis_detected_t));

    detected->cpu_core      = core_id;
    detected->frequency_ghz = read_cpu_frequency_ghz(core_id);

    int node = numa_node_of_cpu(static_cast<int>(core_id));
    detected->numa_node = (node >= 0) ? static_cast<uint32_t>(node) : 0;

    // SMT detection
    char smt_path[128];
    snprintf(smt_path, sizeof(smt_path),
             "/sys/devices/system/cpu/cpu%u/topology/thread_siblings_list",
             core_id);
    FILE* f = fopen(smt_path, "r");
    if (f != nullptr) {
        char buf[64];
        if (fgets(buf, sizeof(buf), f) != nullptr) {
            detected->smt_active = (strchr(buf, ',') != nullptr);
        }
        fclose(f);
    }

    // TSC features via CPUID
    uint32_t eax, ebx, ecx, edx;

    if (__get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx)) {
        detected->tsc_invariant = (edx >> 8) & 1;
    }
    if (__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx)) {
        detected->rdtscp_supported = (edx >> 27) & 1;
    }

    return 0;
}

vis_status_t vis_measure(
    uint32_t              core_id,
    uint32_t              duration_sec,
    const vis_detected_t* detected,
    vis_workload_fn       workload,
    void*                 context,
    vis_histogram_t*      histogram,
    vis_smi_audit_t*      smi_audit,
    vis_results_t*        results
) {
    if (histogram == nullptr || smi_audit == nullptr ||
        results   == nullptr || detected  == nullptr) {
        return vis_status_t::VIS_ERR_INVALID_ARG;
    }

    if (pin_thread_to_core(core_id) < 0) {
        return vis_status_t::VIS_ERR_AFFINITY;
    }

    int msr_fd = vis_msr_open(core_id);
    if (msr_fd < 0) {
        return vis_status_t::VIS_ERR_MSR;
    }

      double frequency_ghz = detected->frequency_ghz;
    if (frequency_ghz == 0.0) {
        fprintf(stderr, "[vis-jitter] ERROR: frequency_ghz is 0, "
                        "call vis_detect_system() first.\n");
        vis_msr_close(msr_fd);
        return vis_status_t::VIS_ERR_INVALID_ARG;
    }

    vis_histogram_init(histogram);
    memset(smi_audit, 0, sizeof(vis_smi_audit_t));
    memset(results,   0, sizeof(vis_results_t));
    strncpy(smi_audit->rejection_policy, "full_window",
            sizeof(smi_audit->rejection_policy) - 1);

    // Record initial SMI counter
    if (vis_smi_read(msr_fd, &smi_audit->msr_start) < 0) {
        vis_msr_close(msr_fd);
        return vis_status_t::VIS_ERR_MSR;
    }

    static double window_deltas[VIS_WINDOW_SIZE];

    struct timespec ts_start, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    while (true) {
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec  - ts_start.tv_sec) +
                         (ts_now.tv_nsec - ts_start.tv_nsec) / 1e9;
        if (elapsed >= static_cast<double>(duration_sec)) break;

        uint64_t smi_start = 0;
        if (vis_smi_read(msr_fd, &smi_start) < 0) {
            vis_msr_close(msr_fd);
            // DESIGN CHOICE: We discard the ENTIRE window.
            // Partial rejection would save data but compromise auditability.
            // A regulator doesn't want "mostly clean" — they want provably clean.
            return vis_status_t::VIS_ERR_MSR;
        }

        uint64_t window_count = 0;

        for (uint64_t i = 0; i < VIS_WINDOW_SIZE; i++) {
            uint32_t core0, core1;
        /**
        * Serialize instruction stream using CPUID.
        * Without this, the CPU's out-of-order engine can move RDTSCP
        * across the workload boundary, making the delta meaningless.
        * CPUID is the heaviest hammer for serialization — expensive,
        * but correct. For measurement, correctness > speed.
        */
            serialize();
            uint64_t t0 = rdtscp(&core0);

            if (workload != nullptr) {
                workload(context);
            }

            uint64_t t1 = rdtscp(&core1);
            serialize();

            if (core0 != core1) {
                results->core_migration_rejected++;
                continue;
            }

            uint64_t cycles = t1 - t0;
            double   ns     = static_cast<double>(cycles) / frequency_ghz;
            window_deltas[window_count++] = ns;
        }

        uint64_t smi_end = 0;
        if (vis_smi_read(msr_fd, &smi_end) < 0) {
            vis_msr_close(msr_fd);
            return vis_status_t::VIS_ERR_MSR;
        }

        if (vis_smi_detected(smi_start, smi_end)) {
            smi_audit->events_detected++;
            smi_audit->samples_rejected += window_count;
        } else {
            for (uint64_t i = 0; i < window_count; i++) {
                vis_histogram_add(histogram, window_deltas[i]);
            }
            results->samples_accepted += window_count;
        }
    }

    if (vis_smi_read(msr_fd, &smi_audit->msr_end) < 0) {
        vis_msr_close(msr_fd);
        return vis_status_t::VIS_ERR_MSR;
    }

    vis_msr_close(msr_fd);

    if (results->samples_accepted == 0) {
        fprintf(stderr, "[vis-jitter] ERROR: All windows rejected.\n");
        return vis_status_t::VIS_ERR_NO_SAMPLES;
    }

    return vis_status_t::VIS_OK;
}
// ---------------------------------------------------------------------------
// D E S I G N   N O T E S
// ---------------------------------------------------------------------------
// 1. Why static window_deltas instead of dynamic allocation?
//    A 1M-element double array is ~8 MB. On the stack, this would blow
//    the default Linux stack (8 MB). Static moves it to the data segment.
//    It's a deliberate trade-off: not thread-safe, but vis-jitter is
//    single-threaded by design.
//
// 2. Why serialization before AND after the workload?
//    Without the second CPUID, the next iteration's RDTSCP could be
//    hoisted before the current iteration's workload completes.
//    Double-fencing is the only portable way to prevent this.