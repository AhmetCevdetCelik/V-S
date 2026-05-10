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

#include <cerrno>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <atomic>
#include <cpuid.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <numa.h>
#ifdef __linux__
#include <sys/random.h>
#endif

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

static bool is_valid_core_id(uint32_t core_id) {
    if (core_id >= CPU_SETSIZE) {
        return false;
    }

    long configured_cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (configured_cpus <= 0 ||
        core_id >= static_cast<uint32_t>(configured_cpus)) {
        return false;
    }

    char online_path[128];
    snprintf(online_path, sizeof(online_path),
             "/sys/devices/system/cpu/cpu%u/online", core_id);

    FILE* f = fopen(online_path, "r");
    if (f == nullptr) {
        // cpu0 commonly has no "online" file and is always online.
        return core_id == 0;
    }

    int online = 0;
    int read_count = fscanf(f, "%d", &online);
    fclose(f);

    return read_count == 1 && online == 1;
}

static int pin_thread_to_core(uint32_t core_id) {
    if (!is_valid_core_id(core_id)) {
        fprintf(stderr, "[vis-jitter] ERROR: Invalid or offline CPU core %u\n",
                core_id);
        return -1;
    }

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
// Internal: TSC frequency
// ---------------------------------------------------------------------------

static double read_tsc_frequency_ghz_from_cpuid() {
    uint32_t eax, ebx, ecx, edx;

    if (__get_cpuid(0x15, &eax, &ebx, &ecx, &edx) &&
        eax != 0 && ebx != 0 && ecx != 0) {
        double tsc_hz = static_cast<double>(ecx) *
                        static_cast<double>(ebx) /
                        static_cast<double>(eax);
        return tsc_hz / 1e9;
    }

    return 0.0;
}

static double calibrate_tsc_frequency_ghz() {
    struct timespec start, end;
    uint32_t aux;

    serialize();
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    uint64_t tsc_start = rdtscp(&aux);

    usleep(100000);

    uint64_t tsc_end = rdtscp(&aux);
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    serialize();

    double elapsed = (end.tv_sec  - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    if (elapsed <= 0.0 || tsc_end <= tsc_start) {
        return 0.0;
    }

    return (static_cast<double>(tsc_end - tsc_start) / elapsed) / 1e9;
}

static double read_tsc_frequency_ghz() {
    double frequency_ghz = read_tsc_frequency_ghz_from_cpuid();
    if (frequency_ghz != 0.0) {
        return frequency_ghz;
    }

    fprintf(stderr, "[vis-jitter] WARNING: Cannot derive TSC frequency "
                    "from CPUID; calibrating against CLOCK_MONOTONIC_RAW.\n");
    return calibrate_tsc_frequency_ghz();
}

static bool parse_u32_token(const char* start,
                            const char* end,
                            uint32_t* value) {
    if (start == nullptr || end == nullptr || value == nullptr ||
        start >= end) {
        return false;
    }
    for (const char* p = start; p < end; p++) {
        if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    }

    errno = 0;
    char* parsed_end = nullptr;
    unsigned long parsed = strtoul(start, &parsed_end, 10);
    if (errno != 0 || parsed_end != end || parsed > UINT32_MAX) {
        return false;
    }

    *value = static_cast<uint32_t>(parsed);
    return true;
}

static bool sibling_list_has_multiple_cpus(const char* text) {
    if (text == nullptr) return false;

    const char* token_start = text;
    while (*token_start != '\0') {
        while (*token_start == ' ' || *token_start == '\t' ||
               *token_start == '\n' || *token_start == '\r' ||
               *token_start == ',') {
            token_start++;
        }
        if (*token_start == '\0') break;

        const char* token_end = token_start;
        while (*token_end != '\0' && *token_end != ',' &&
               *token_end != '\n' && *token_end != '\r') {
            token_end++;
        }
        while (token_end > token_start &&
               (*(token_end - 1) == ' ' || *(token_end - 1) == '\t')) {
            token_end--;
        }

        const char* dash = nullptr;
        for (const char* p = token_start; p < token_end; p++) {
            if (*p == '-') {
                dash = p;
                break;
            }
        }

        if (dash != nullptr) {
            uint32_t first = 0;
            uint32_t last = 0;
            if (parse_u32_token(token_start, dash, &first) &&
                parse_u32_token(dash + 1, token_end, &last) &&
                last > first) {
                return true;
            }
        } else {
            // More than one comma-separated singleton means multiple siblings.
            const char* next = token_end;
            while (*next == ' ' || *next == '\t' ||
                   *next == '\n' || *next == '\r') {
                next++;
            }
            if (*next == ',') return true;
        }

        token_start = token_end;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

int vis_detect_system(uint32_t core_id, vis_detected_t* detected) {
    if (detected == nullptr) return -1;
    if (!is_valid_core_id(core_id)) return -1;

    memset(detected, 0, sizeof(vis_detected_t));

    detected->cpu_core      = core_id;
    detected->frequency_ghz = read_tsc_frequency_ghz();

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
            detected->smt_active = sibling_list_has_multiple_cpus(buf);
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
    if (!is_valid_core_id(core_id)) {
        return vis_status_t::VIS_ERR_INVALID_ARG;
    }

    if (pin_thread_to_core(core_id) < 0) {
        return vis_status_t::VIS_ERR_AFFINITY;
    }

    int msr_fd = vis_msr_open(core_id);
    if (msr_fd < 0) {
        return vis_status_t::VIS_ERR_MSR;
    }

    double tsc_frequency_ghz = detected->frequency_ghz;
    if (tsc_frequency_ghz == 0.0) {
        fprintf(stderr, "[vis-jitter] ERROR: TSC frequency is 0, "
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

    thread_local double window_deltas[VIS_WINDOW_SIZE];

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
            double   ns     = static_cast<double>(cycles) / tsc_frequency_ghz;
            window_deltas[window_count++] = ns;
        }

        uint64_t smi_end = 0;
        if (vis_smi_read(msr_fd, &smi_end) < 0) {
            vis_msr_close(msr_fd);
            return vis_status_t::VIS_ERR_MSR;
        }

        if (vis_smi_detected(smi_start, smi_end)) {
            smi_audit->contaminated_windows++;
            smi_audit->events_detected = smi_audit->contaminated_windows;
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
    smi_audit->msr_delta = smi_audit->msr_end - smi_audit->msr_start;

    vis_msr_close(msr_fd);

    if (results->samples_accepted == 0) {
        fprintf(stderr, "[vis-jitter] ERROR: All windows rejected.\n");
        return vis_status_t::VIS_ERR_NO_SAMPLES;
    }

    return vis_status_t::VIS_OK;
}

static bool fill_random_bytes(uint8_t* bytes, size_t count) {
    if (bytes == nullptr) {
        return false;
    }

#ifdef __linux__
    size_t offset = 0;
    while (offset < count) {
        ssize_t n = getrandom(bytes + offset, count - offset, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        offset += static_cast<size_t>(n);
    }
    if (offset == count) {
        return true;
    }
#endif

    static std::atomic<uint64_t> fallback_counter{0};
    uint32_t aux = 0;
    uint64_t tsc = rdtscp(&aux);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t counter = fallback_counter.fetch_add(1, std::memory_order_relaxed);

    uint64_t sec_bits = static_cast<uint64_t>(ts.tv_sec) << 32;
    uint64_t nsec_bits = static_cast<uint64_t>(ts.tv_nsec);
    uint64_t pid_bits = static_cast<uint64_t>(getpid()) << 32;
    uint64_t aux_bits = static_cast<uint64_t>(aux) << 16;
    uint64_t seed_a = tsc ^ sec_bits ^ nsec_bits;
    uint64_t seed_b = pid_bits ^ aux_bits ^ counter;

    memcpy(bytes, &seed_a, count < sizeof(seed_a) ? count : sizeof(seed_a));
    if (count > sizeof(seed_a)) {
        size_t remaining = count - sizeof(seed_a);
        memcpy(bytes + sizeof(seed_a), &seed_b,
               remaining < sizeof(seed_b) ? remaining : sizeof(seed_b));
    }
    return true;
}

static void generate_uuid(char* buf, size_t buf_size) {
    uint8_t bytes[16] = {};
    fill_random_bytes(bytes, sizeof(bytes));

    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);

    uint32_t time_low =
        (static_cast<uint32_t>(bytes[0]) << 24) |
        (static_cast<uint32_t>(bytes[1]) << 16) |
        (static_cast<uint32_t>(bytes[2]) << 8) |
        static_cast<uint32_t>(bytes[3]);
    uint16_t time_mid =
        static_cast<uint16_t>((bytes[4] << 8) | bytes[5]);
    uint16_t time_hi =
        static_cast<uint16_t>((bytes[6] << 8) | bytes[7]);
    uint16_t clock_seq =
        static_cast<uint16_t>((bytes[8] << 8) | bytes[9]);
    uint64_t node =
        (static_cast<uint64_t>(bytes[10]) << 40) |
        (static_cast<uint64_t>(bytes[11]) << 32) |
        (static_cast<uint64_t>(bytes[12]) << 24) |
        (static_cast<uint64_t>(bytes[13]) << 16) |
        (static_cast<uint64_t>(bytes[14]) << 8) |
        static_cast<uint64_t>(bytes[15]);

    snprintf(buf, buf_size, "%08x-%04x-%04x-%04x-%012llx",
             time_low, time_mid, time_hi, clock_seq,
             static_cast<unsigned long long>(node));
}

static void generate_timestamp(char* buf, size_t buf_size) {
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    if (utc == nullptr) {
        snprintf(buf, buf_size, "unknown");
        return;
    }
    strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", utc);
}

vis_status_t vis_jitter_run(
    uint32_t        core_id,
    uint32_t        duration_sec,
    double          threshold_ns,
    vis_workload_fn workload,
    void*           context,
    vis_report_t*   report
) {
    if (report == nullptr || duration_sec == 0 ||
        !std::isfinite(threshold_ns) || threshold_ns < 0.0) {
        return vis_status_t::VIS_ERR_INVALID_ARG;
    }
    if (!is_valid_core_id(core_id)) {
        return vis_status_t::VIS_ERR_INVALID_ARG;
    }

    memset(report, 0, sizeof(vis_report_t));

    strncpy(report->schema_version, "1.0",
            sizeof(report->schema_version) - 1);
    strncpy(report->generator, "vis-jitter " VIS_JITTER_VERSION,
            sizeof(report->generator) - 1);

    generate_uuid(report->report_id, sizeof(report->report_id));
    generate_timestamp(report->generated_at, sizeof(report->generated_at));

    if (vis_detect_system(core_id, &report->detected) < 0) {
        return vis_status_t::VIS_ERR_INVALID_ARG;
    }

    vis_histogram_t histogram;
    vis_status_t status = vis_measure(
        core_id,
        duration_sec,
        &report->detected,
        workload,
        context,
        &histogram,
        &report->smi_audit,
        &report->results
    );
    if (status != vis_status_t::VIS_OK) {
        return status;
    }

    vis_histogram_compute(&histogram, &report->results.latency_ns);
    report->results.histogram = histogram;
    report->results.threshold_ns = threshold_ns;
    report->results.determinism_pass =
        (report->results.latency_ns.p99_ns <= threshold_ns);

    return vis_status_t::VIS_OK;
}
// ---------------------------------------------------------------------------
// D E S I G N   N O T E S
// ---------------------------------------------------------------------------
// 1. Why thread_local window_deltas instead of stack allocation?
//    A 1M-element double array is ~8 MB. On the stack, this would risk
//    exhausting the default Linux thread stack. thread_local keeps the public
//    API safe for concurrent calls while avoiding per-window heap churn.
//
// 2. Why serialization before AND after the workload?
//    Without the second CPUID, the next iteration's RDTSCP could be
//    hoisted before the current iteration's workload completes.
//    Double-fencing is the only portable way to prevent this.
