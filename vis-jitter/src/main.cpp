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

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static bool parse_uint32(const char* text, uint32_t* out) {
    if (text == nullptr || text[0] == '\0' || out == nullptr) {
        return false;
    }
    if (text[0] == '-') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return false;
    }

    *out = static_cast<uint32_t>(value);
    return true;
}

static bool parse_positive_uint32(const char* text, uint32_t* out) {
    if (!parse_uint32(text, out)) {
        return false;
    }
    return *out > 0;
}

static bool parse_nonnegative_double(const char* text, double* out) {
    if (text == nullptr || text[0] == '\0' || out == nullptr) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' ||
        !std::isfinite(value) || value < 0.0) {
        return false;
    }

    *out = value;
    return true;
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
            if (!parse_uint32(argv[++i], &core_id)) {
                fprintf(stderr, "[vis-jitter] Invalid --cpu value.\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            if (!parse_positive_uint32(argv[++i], &duration_sec)) {
                fprintf(stderr, "[vis-jitter] Invalid --duration value.\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) {
            if (!parse_nonnegative_double(argv[++i], &threshold_ns)) {
                fprintf(stderr, "[vis-jitter] Invalid --threshold value.\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else {
            fprintf(stderr, "[vis-jitter] Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    printf("[vis-jitter] Starting measurement on core %u "
           "for %u seconds (threshold: %.1f ns)\n",
           core_id, duration_sec, threshold_ns);

    vis_report_t report;
    printf("[vis-jitter] Running measurement...\n");

    vis_status_t status = vis_jitter_run(
        core_id,
        duration_sec,
        threshold_ns,
        nullptr,        // workload: NULL = empty loop (baseline)
        nullptr,        // context
        &report
    );
    if (status != vis_status_t::VIS_OK) {
        fprintf(stderr, "[vis-jitter] ERROR: Measurement failed (code %d).\n",
                static_cast<int>(status));
        return 1;
    }

    vis_report_print_summary(&report);

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
