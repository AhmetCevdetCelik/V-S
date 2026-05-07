/**
 * main.cpp
 *
 * vis-jitter CLI entry point.
 * Parses arguments, runs measurement, prints report and saves JSON.
 *
 * Usage:
 *   sudo ./vis-jitter --cpu 2 --duration 60 --threshold 100
 *
 * License: MIT
 */

#include "../include/vis_jitter.hpp"
#include "../include/measurement.hpp"
#include "../include/report.hpp"
#include "../include/histogram.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------------------
// Internal: UUID v4
// ---------------------------------------------------------------------------

static void generate_uuid(char* buf, size_t buf_size) {
    snprintf(buf, buf_size,
        "%08x-%04x-4%03x-%04x-%012x",
        rand() & 0xffffffff,
        rand() & 0xffff,
        rand() & 0x0fff,
        (rand() & 0x3fff) | 0x8000,
        (unsigned int)rand()
    );
}

// ---------------------------------------------------------------------------
// Internal: ISO 8601 timestamp
// ---------------------------------------------------------------------------

static void generate_timestamp(char* buf, size_t buf_size) {
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", utc);
}

// ---------------------------------------------------------------------------
// Internal: argument parsing
// ---------------------------------------------------------------------------

static void print_usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --cpu       <core_id>    CPU core to measure on (default: 0)\n"
        "  --duration  <seconds>    Measurement duration   (default: 60)\n"
        "  --threshold <ns>         P99 pass threshold ns  (default: 100.0)\n"
        "  --output    <file.json>  Save JSON report to file (optional)\n"
        "  --help                   Show this message\n"
        "\n"
        "Example:\n"
        "  sudo ./vis-jitter --cpu 2 --duration 60 --threshold 100\n",
        argv0
    );
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Defaults
    uint32_t core_id      = 0;
    uint32_t duration_sec = VIS_DEFAULT_DURATION_SEC;
    double   threshold_ns = VIS_DEFAULT_THRESHOLD_NS;
    const char* output_path = nullptr;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            core_id = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_sec = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) {
            threshold_ns = atof(argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else {
            fprintf(stderr, "[vis-jitter] Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Seed random for UUID generation
    srand(static_cast<unsigned>(time(nullptr)));

    printf("[vis-jitter] Starting measurement on core %u "
           "for %u seconds (threshold: %.1f ns)\n",
           core_id, duration_sec, threshold_ns);

    // Initialize report
    vis_report_t report;
    memset(&report, 0, sizeof(vis_report_t));

    strncpy(report.schema_version, "1.0",
            sizeof(report.schema_version) - 1);
    strncpy(report.generator, "vis-jitter " VIS_JITTER_VERSION,
            sizeof(report.generator) - 1);

    generate_uuid(report.report_id, sizeof(report.report_id));
    generate_timestamp(report.generated_at, sizeof(report.generated_at));

    // Step 1: detect system properties
    printf("[vis-jitter] Detecting system properties...\n");
    if (vis_detect_system(core_id, &report.detected) < 0) {
        fprintf(stderr, "[vis-jitter] ERROR: System detection failed.\n");
        return 1;
    }

    printf("[vis-jitter] Core %u | %.3f GHz | NUMA %u | SMT: %s | "
           "TSC invariant: %s\n",
           report.detected.cpu_core,
           report.detected.frequency_ghz,
           report.detected.numa_node,
           report.detected.smt_active      ? "yes" : "no",
           report.detected.tsc_invariant    ? "yes" : "no");

    // Step 2: run measurement
    printf("[vis-jitter] Running measurement...\n");

    vis_histogram_t histogram;
    vis_status_t status = vis_measure(
        core_id,
        duration_sec,
        &report.detected,
        nullptr,        // workload: NULL = empty loop (baseline)
        nullptr,        // context
        &histogram,
        &report.smi_audit,
        &report.results
    );

    if (status != vis_status_t::VIS_OK) {
        fprintf(stderr, "[vis-jitter] ERROR: Measurement failed (code %d).\n",
                static_cast<int>(status));
        return 1;
    }

    // Step 3: compute latency statistics from histogram
    vis_histogram_compute(&histogram, &report.results.latency_ns);

    // Copy histogram data to report
    report.results.histogram = histogram;

    // Step 4: apply threshold verdict
    report.results.threshold_ns    = threshold_ns;
    report.results.determinism_pass =
        (report.results.latency_ns.p99_ns <= threshold_ns);

    // Step 5: print terminal summary
    vis_report_print_summary(&report);

    // Step 6: save JSON if output path given
    if (output_path != nullptr) {
        char* json = vis_report_to_json(&report);
        if (json == nullptr) {
            fprintf(stderr, "[vis-jitter] ERROR: JSON serialization failed.\n");
            return 1;
        }

        FILE* f = fopen(output_path, "w");
        if (f == nullptr) {
            fprintf(stderr, "[vis-jitter] ERROR: Cannot open output file: %s\n",
                    output_path);
            free(json);
            return 1;
        }

        fputs(json, f);
        fclose(f);
        free(json);

        printf("[vis-jitter] Report saved to: %s\n", output_path);
    }

    return report.results.determinism_pass ? 0 : 1;
}