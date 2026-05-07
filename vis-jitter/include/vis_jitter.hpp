#pragma once

/**
 * vis_jitter.hpp
 *
 * VİS (Virtual Intelligent Scheduler) — Jitter Measurement Core Header
 *
 * This file defines all fundamental types, constants, enums, and public
 * API signatures used by the vis-jitter tool. Every translation unit
 * in the project includes this header to share common definitions.
 *
 * Dependencies: <cstdint> (standard fixed-width integer types)
 * License: MIT
 */

#include <cstdint>

// ===========================================================================
// C O N S T A N T S
// ===========================================================================

/** Semantic version of the tool, reported in JSON output */
#define VIS_JITTER_VERSION "1.0.0"

/** Number of samples collected per measurement window (1-second window) */
#define VIS_WINDOW_SIZE 1'000'000

/** Default total measurement duration in seconds (can be overridden by CLI) */
#define VIS_DEFAULT_DURATION_SEC 60

/** Default P99 threshold in nanoseconds for the determinism verdict */
#define VIS_DEFAULT_THRESHOLD_NS 100.0

/** Number of histogram buckets. Each bucket spans VIS_BUCKET_WIDTH_NS ns */
#define VIS_HISTOGRAM_BUCKETS 500

/** Width of a single histogram bucket in nanoseconds */
#define VIS_BUCKET_WIDTH_NS 10

// ===========================================================================
// S T A T U S   C O D E S
// ===========================================================================

/**
 * Return codes for vis_jitter_run().
 * Uses an `enum class` to provide type safety and prevent implicit int conversions.
 */
enum class vis_status_t : int {
    VIS_OK              =  0,  /**< Measurement completed successfully */
    VIS_ERR_AFFINITY    = -1,  /**< Failed to pin thread to requested core */
    VIS_ERR_MSR         = -2,  /**< Could not open MSR device (need root or CAP_SYS_RAWIO) */
    VIS_ERR_NO_SAMPLES  = -3,  /**< All measurement windows were rejected (excessive SMI) */
    VIS_ERR_INVALID_ARG = -4,  /**< Invalid argument (e.g., negative duration) */
};

// ===========================================================================
// D A T A   S T R U C T U R E S
// ===========================================================================

/**
 * System properties DETECTED at runtime by the tool.
 * These values are measured and verified — they are NOT user claims.
 * Reported in `system.detected` block of the JSON report.
 */
struct vis_detected_t {
    uint32_t cpu_core;              /**< Core index the measurement ran on */
    double   frequency_ghz;         /**< Actual CPU frequency read from MSR (GHz) */
    uint32_t numa_node;             /**< NUMA node of the measuring core */
    bool     smt_active;            /**< Is Hyper-Threading enabled? */
    bool     tsc_invariant;         /**< Does TSC remain constant across C-state transitions? */
    bool     rdtscp_supported;      /**< CPUID indicates RDTSCP instruction support */
};
/**
 * System properties DETECTED at runtime by the tool.
 * These values are measured and verified — they are NOT user claims.
 * Reported in `system.detected` block of the JSON report.
 * 
 * NOTE: This distinction (detected vs asserted) is the architectural core of VIS.
 * It turns a measurement tool into a certification authority.
 */
/**
 * System properties ASSERTED by the user (e.g., via CLI flags).
 * These values are NOT verified by vis-jitter; they represent the user's claim
 * about the environment. The commercial certification layer verifies them
 * through a kernel module and elevates them to detected status.
 * Reported in `system.asserted` block of the JSON report.
 */
struct vis_asserted_t {
    char p_state[32];               /**< e.g. "P0_locked" */
    char c_states_disabled[128];    /**< e.g. "C1E,C3,C6" */
    bool hugepages_1gb;             /**< Are 1 GB HugePages active? */
    char egress_memory[16];         /**< "UC" or "WC" – memory type for egress buffer */
    char rx_buffer_memory[16];      /**< "UC" or "WC" – memory type for receive buffer */
    bool ddio_enabled;              /**< Is Intel DDIO active? */
};

/**
 * SMI (System Management Interrupt) audit data.
 *
 * Policy: full_window
 *   - At the start and end of each measurement window, IA32_SMI_COUNT MSR is read.
 *   - If the counter increased, the ENTIRE window is discarded.
 *   - This conservative approach produces a provably clean dataset suitable
 *     for regulatory certification.
 * 
 * DESIGN DECISION: I chose full-window rejection over partial rejection.
 * Losing 1M samples is better than lying with statistics. Regulatory compliance demands this.
 */
struct vis_smi_audit_t {
    uint64_t msr_start;             /**< SMI count at window start */
    uint64_t msr_end;               /**< SMI count at window end */
    uint32_t events_detected;       /**< Total SMI events across all windows */
    uint64_t samples_rejected;      /**< Samples discarded due to SMI events */
    char     rejection_policy[32];  /**< Always "full_window" in V1 */
};

/**
 * Computed latency statistics.
 * All values are in nanoseconds and are derived from the histogram.
 */
struct vis_latency_t {
    double min_ns;      /**< Minimum observed latency */
    double p50_ns;      /**< Median (50th percentile) */
    double p99_ns;      /**< 99th percentile – determines the determinism verdict */
    double p99_9_ns;    /**< 99.9th percentile */
    double p99_99_ns;   /**< 99.99th percentile */
    double max_ns;      /**< Maximum observed latency */
};

/**
 * Latency distribution histogram.
 *
 * Buckets are fixed-width (VIS_BUCKET_WIDTH_NS ns each).
 * bucket[i] covers the interval [i*10, (i+1)*10) nanoseconds.
 * Samples exceeding the last bucket are counted in the `overflow` field.
 * Total range: 0 – 4990 ns (500 buckets × 10 ns).
 */
struct vis_histogram_t {
    uint64_t buckets[VIS_HISTOGRAM_BUCKETS]; /**< Sample counts per bucket */
    uint64_t overflow;                       /**< Samples beyond the last bucket */
    uint64_t total_accepted;                 /**< Total samples inserted into the histogram */
};

/**
 * Final measurement results.
 *
 * Contains both the raw histogram and the percentile breakdown.
 * `determinism_pass` is true iff P99 <= threshold_ns.
 * In the open-source tier the threshold is user-defined;
 * in the commercial tier it is set and signed by the VİS authority.
 */
struct vis_results_t {
    uint64_t        samples_accepted;          /**< Accepted sample count */
    uint64_t        core_migration_rejected;   /**< Samples rejected due to core migration */
    vis_latency_t   latency_ns;                /**< Computed percentiles */
    vis_histogram_t histogram;                 /**< Raw latency histogram */
    bool            determinism_pass;          /**< true if P99 <= threshold_ns */
    double          threshold_ns;              /**< Threshold used for verdict */
};
// NOTE: In open-source tier, threshold is user-defined.
    // In commercial tier, it's set and signed by VIS authority.


/**
 * Top-level report structure.
 *
 * Populated entirely by vis_jitter_run() and serialized to JSON
 * by vis_report_to_json(). The schema mirrors the one designed
 * for the VİS determinism report.
 */
struct vis_report_t {
    char             schema_version[8];   /**< Report format version, e.g. "1.0" */
    char             report_id[40];       /**< UUID v4 string */
    char             generated_at[32];    /**< ISO 8601 timestamp */
    char             generator[64];       /**< e.g. "vis-jitter 1.0.0" */
    vis_detected_t   detected;            /**< Measured system properties */
    vis_asserted_t   asserted;            /**< User-claimed configuration */
    vis_smi_audit_t  smi_audit;           /**< SMI audit results */
    vis_results_t    results;             /**< Measurement results */
};

// ===========================================================================
// W O R K L O A D   C A L L B A C K
// ===========================================================================

/**
 * User-supplied function that represents the critical code path to measure.
 *
 * @param context  Opaque pointer passed through from vis_jitter_run().
 * @return         Any integer; ignored by the tool. May be used by the user
 *                 for custom error tracking.
 *
 * Pass NULL to measure the empty loop overhead (baseline calibration of
 * the RDTSCP+CPUID serialization sequence itself).
 */
using vis_workload_fn = int (*)(void* context);

// ===========================================================================
// P U B L I C   A P I
// ===========================================================================

/**
 * Run the jitter measurement and populate the report.
 *
 * This function:
 *   - Pins the calling thread to @p core_id.
 *   - Opens the MSR device to monitor SMI events.
 *   - Runs the measurement in 1-second windows for @p duration_sec seconds.
 *   - Calls @p workload repeatedly; if NULL, an empty loop is measured.
 *   - Rejects windows where an SMI event is detected (full_window policy).
 *   - Rejects samples where the thread migrated to a different core.
 *   - Computes latency percentiles and fills @p report.
 *
 * @param core_id       CPU core to pin the measurement thread to (0‑based).
 * @param duration_sec  Total measurement duration in seconds.
 * @param threshold_ns  P99 threshold for determinism verdict (nanoseconds).
 * @param workload      Function to measure. NULL = empty loop calibration.
 * @param context       Opaque pointer forwarded to workload().
 * @param report        [out] Fully populated report struct on success.
 * @return              vis_status_t::VIS_OK or an error code.
 */
vis_status_t vis_jitter_run(
    uint32_t        core_id,
    uint32_t        duration_sec,
    double          threshold_ns,
    vis_workload_fn workload,
    void*           context,
    vis_report_t*   report
);

/**
 * Serialize a report to a JSON string.
 *
 * @param report  Populated report struct.
 * @return        Owning pointer to a null-terminated JSON string.
 *                Caller must free() the returned pointer. nullptr on failure.
 */
char* vis_report_to_json(const vis_report_t* report);

/**
 * Load a report from a JSON file (for offline analysis and comparison).
 *
 * @param json_path  Path to the .json report file.
 * @return           Newly allocated vis_report_t. Caller must delete it.
 *                   Returns nullptr if parsing fails.
 */
vis_report_t* vis_report_from_json(const char* json_path);

/**
 * Print a human-readable summary of the report to stdout.
 *
 * Displays measurement conditions, SMI events, and latency percentiles.
 * All percentiles are shown to maintain transparency, even those exceeding
 * the threshold.
 */
void vis_report_print_summary(const vis_report_t* report);

/**
 * Sign a report with an Ed25519 private key.
 *
 * STUB in V1 – always returns nullptr.
 * Available only in the commercial certification tier.
 *
 * @param report           Report to sign.
 * @param private_key_path Path to an Ed25519 PEM private key file.
 * @return                 Base64-encoded signature; caller must free().
 *                         In V1, always returns nullptr.
 */
char* vis_report_sign(const vis_report_t* report, const char* private_key_path);
// ---------------------------------------------------------------------------
// D E S I G N   N O T E S
// ---------------------------------------------------------------------------
// VIS is not a measurement tool. It's an audit trail.
// Every architectural decision here was made with one question in mind:
// "Can this report be submitted to a regulator without explanation?"