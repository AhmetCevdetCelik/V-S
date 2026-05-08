/**
 * test_report_json.cpp
 *
 * Regression test for JSON report serialization.
 *
 * Run:
 *   ./test_report_json
 *
 * License: MIT
 */

#include "../include/report.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool contains(const char* text, const char* needle) {
    return std::strstr(text, needle) != nullptr;
}

static bool has_trailing_comma_before_close(const char* json) {
    for (const char* p = json; *p != '\0'; p++) {
        if (*p != ',') {
            continue;
        }

        const char* q = p + 1;
        while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t') {
            q++;
        }

        if (*q == '}' || *q == ']') {
            return true;
        }
    }

    return false;
}

static void populate_report(vis_report_t* report) {
    std::memset(report, 0, sizeof(vis_report_t));

    std::strncpy(report->schema_version, "1.0",
                 sizeof(report->schema_version) - 1);
    std::strncpy(report->report_id,
                 "00000000-0000-4000-8000-000000000000",
                 sizeof(report->report_id) - 1);
    std::strncpy(report->generated_at, "2026-05-07T00:00:00Z",
                 sizeof(report->generated_at) - 1);
    std::strncpy(report->generator, "vis-jitter test",
                 sizeof(report->generator) - 1);

    report->detected.cpu_core = 2;
    report->detected.frequency_ghz = 4.800;
    report->detected.numa_node = 0;
    report->detected.smt_active = false;
    report->detected.tsc_invariant = true;
    report->detected.rdtscp_supported = true;

    std::strncpy(report->asserted.p_state, "P0\"locked\\test",
                 sizeof(report->asserted.p_state) - 1);
    std::strncpy(report->asserted.c_states_disabled, "C1E\nC3\tC6",
                 sizeof(report->asserted.c_states_disabled) - 1);
    report->asserted.hugepages_1gb = true;
    std::strncpy(report->asserted.egress_memory, "UC",
                 sizeof(report->asserted.egress_memory) - 1);
    std::strncpy(report->asserted.rx_buffer_memory, "WC",
                 sizeof(report->asserted.rx_buffer_memory) - 1);
    report->asserted.ddio_enabled = false;

    report->smi_audit.msr_start = 100;
    report->smi_audit.msr_end = 104;
    report->smi_audit.msr_delta = 4;
    report->smi_audit.events_detected = 2;
    report->smi_audit.contaminated_windows = 2;
    report->smi_audit.samples_rejected = 2'000'000;
    std::strncpy(report->smi_audit.rejection_policy, "full_window",
                 sizeof(report->smi_audit.rejection_policy) - 1);

    report->results.samples_accepted = 10'000'000;
    report->results.core_migration_rejected = 3;
    report->results.latency_ns.min_ns = 0.0;
    report->results.latency_ns.p50_ns = 5.0;
    report->results.latency_ns.p99_ns = 15.0;
    report->results.latency_ns.p99_9_ns = 25.0;
    report->results.latency_ns.p99_99_ns = 35.0;
    report->results.latency_ns.max_ns = 45.0;
    report->results.determinism_pass = true;
    report->results.threshold_ns = 100.0;
}

int main() {
    if (vis_report_to_json(nullptr) != nullptr) {
        std::printf("[test] FAILED: null report should return nullptr.\n");
        return 1;
    }

    vis_report_t report;
    populate_report(&report);

    char* json = vis_report_to_json(&report);
    if (json == nullptr) {
        std::printf("[test] FAILED: serializer returned nullptr.\n");
        return 1;
    }

    const char* required_fields[] = {
        "\"schema_version\": \"1.0\"",
        "\"report_id\": \"00000000-0000-4000-8000-000000000000\"",
        "\"cpu_core\": 2",
        "\"frequency_ghz\": 4.800",
        "\"msr_delta\": 4",
        "\"events_detected\": 2",
        "\"contaminated_windows\": 2",
        "\"samples_rejected\": 2000000",
        "\"samples_accepted\": 10000000",
        "\"p99\": 15.0",
        "\"determinism_verdict\": \"PASS\"",
        "\"threshold_ns\": 100.0",
        "\"p_state\": \"P0\\\"locked\\\\test\"",
        "\"c_states_disabled\": \"C1E\\nC3\\tC6\""
    };

    for (size_t i = 0; i < sizeof(required_fields) / sizeof(required_fields[0]); i++) {
        if (!contains(json, required_fields[i])) {
            std::printf("[test] FAILED: missing JSON field: %s\n",
                        required_fields[i]);
            std::free(json);
            return 1;
        }
    }

    if (has_trailing_comma_before_close(json)) {
        std::printf("[test] FAILED: JSON has a trailing comma before a close token.\n");
        std::free(json);
        return 1;
    }

    const char* output_path = "/tmp/vis_report_json_test.json";
    FILE* output = std::fopen(output_path, "w");
    if (output == nullptr) {
        std::printf("[test] FAILED: cannot open %s for writing.\n", output_path);
        std::free(json);
        return 1;
    }

    std::fputs(json, output);
    std::fclose(output);
    std::free(json);

    std::printf("[test] PASS: JSON report serialization is structurally valid.\n");
    std::printf("[test] Wrote parser fixture to %s\n", output_path);
    return 0;
}
