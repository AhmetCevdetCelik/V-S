/**
 * doctor_main.cpp
 *
 * VIS Doctor CLI entry point.
 *
 * License: MIT
 */

#include "../include/doctor.hpp"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void print_usage(const char* argv0) {
    printf(
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --inspect              Inspect machine only (default, rootless)\n"
        "  --scan                 Run all-core SMI-aware jitter scan (requires root/MSR)\n"
        "  --duration <seconds>   Scan duration per CPU (default: 30)\n"
        "  --threshold <ns>       P99 threshold in ns (default: 100.0)\n"
        "  --output <file.json>   Write AI/tool-readable JSON report\n"
        "  --llm <file.md>        Write AI-pasteable Markdown context\n"
        "  --help                 Show this message\n",
        argv0
    );
}

static bool parse_u32(const char* text, uint32_t* out) {
    if (text == nullptr || text[0] == '\0' || text[0] == '-' || out == nullptr) {
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

static bool parse_double(const char* text, double* out) {
    if (text == nullptr || text[0] == '\0' || out == nullptr) return false;
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

int main(int argc, char* argv[]) {
    bool scan = false;
    uint32_t duration_sec = 30;
    double threshold_ns = VIS_DEFAULT_THRESHOLD_NS;
    const char* output_path = nullptr;
    const char* llm_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--inspect") == 0) {
            scan = false;
        } else if (strcmp(argv[i], "--scan") == 0) {
            scan = true;
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &duration_sec) || duration_sec == 0) {
                fprintf(stderr, "[vis-doctor] Invalid --duration value.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) {
            if (!parse_double(argv[++i], &threshold_ns)) {
                fprintf(stderr, "[vis-doctor] Invalid --threshold value.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--llm") == 0 && i + 1 < argc) {
            llm_path = argv[++i];
        } else {
            fprintf(stderr, "[vis-doctor] Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    vis_doctor_report_t report;
    int ret = scan
        ? vis_doctor_scan_all(duration_sec, threshold_ns, &report)
        : vis_doctor_inspect(&report);
    if (ret < 0) {
        fprintf(stderr, "[vis-doctor] ERROR: diagnosis failed.\n");
        return 1;
    }

    vis_doctor_print_summary(&report);

    if (output_path != nullptr &&
        !vis_doctor_write_file(output_path, vis_doctor_to_json(&report))) {
        fprintf(stderr, "[vis-doctor] ERROR: cannot write %s\n", output_path);
        return 1;
    }
    if (llm_path != nullptr &&
        !vis_doctor_write_file(llm_path, vis_doctor_to_markdown(&report))) {
        fprintf(stderr, "[vis-doctor] ERROR: cannot write %s\n", llm_path);
        return 1;
    }

    if (output_path != nullptr) {
        printf("[vis-doctor] JSON report saved to: %s\n", output_path);
    }
    if (llm_path != nullptr) {
        printf("[vis-doctor] AI context saved to: %s\n", llm_path);
    }

    return 0;
}

