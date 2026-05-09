/**
 * test_vis_compare.cpp
 *
 * Rootless tests for VIS Compare report parsing and serialization.
 *
 * License: MIT
 */

#include "../include/vis_compare.hpp"

#include <cstdio>

static const char* kRunAttestation =
    "{\n"
    "  \"vis_run_attestation\": {\n"
    "    \"assigned_cpus\": [0, 2, 4],\n"
    "    \"workload\": {\"exit_code\": 0},\n"
    "    \"observation\": {\n"
    "      \"affinity_escapes\": [\n"
    "        {\"pid\": 1, \"tid\": 1, \"escaped_cpus\": [9]}\n"
    "      ]\n"
    "    },\n"
    "    \"events\": [\n"
    "      {\"severity\": \"warning\", \"category\": \"validation\"}\n"
    "    ],\n"
    "    \"verdict\": \"CONTROLLED_WITH_WARNINGS\"\n"
    "  }\n"
    "}\n";

static bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

int main() {
    vis_compare_profile_result_t result;
    std::string error;
    if (!vis_compare_parse_run_attestation(kRunAttestation,
                                           &result,
                                           &error)) {
        std::printf("[test] FAILED: parse attestation: %s\n",
                    error.c_str());
        return 1;
    }
    if (vis_compare_join_cpus(result.assigned_cpus) != "0,2,4" ||
        result.exit_code != 0 ||
        result.verdict != "CONTROLLED_WITH_WARNINGS" ||
        result.affinity_escape_count != 1 ||
        result.warning_count != 1) {
        std::printf("[test] FAILED: parsed attestation fields are wrong.\n");
        return 1;
    }

    vis_compare_metric_spec_t spec;
    if (!vis_compare_parse_metric_spec("score=score: ([0-9.]+)",
                                       &spec,
                                       &error)) {
        std::printf("[test] FAILED: parse metric spec: %s\n",
                    error.c_str());
        return 1;
    }
    std::vector<vis_compare_metric_result_t> metrics;
    vis_compare_capture_metrics("score: 42.5\n", {spec}, &metrics);
    if (metrics.size() != 1 ||
        !metrics[0].matched ||
        metrics[0].value != 42.5 ||
        metrics[0].raw_value != "42.5") {
        std::printf("[test] FAILED: metric capture did not parse value.\n");
        return 1;
    }
    vis_compare_capture_metrics("no score here\n", {spec}, &metrics);
    if (metrics.size() != 1 || metrics[0].matched) {
        std::printf("[test] FAILED: unmatched metric should be reported.\n");
        return 1;
    }

    result.profile_source = "primary";
    result.attestation_path = "vis-compare-runs/primary.json";
    result.output_path = "vis-compare-runs/primary.output.txt";
    result.duration_ms = 42;
    vis_compare_capture_metrics("score: 42.5\n", {spec}, &result.metrics);

    vis_compare_report_t report;
    report.policy_path = "doctor.json";
    report.workload = "/bin/true";
    report.profiles.push_back(result);
    report.recommendation = "Use primary as the first latency-sensitive profile.";

    std::string json = vis_compare_to_json(report);
    if (!contains(json, "\"vis_compare_report\"") ||
        !contains(json, "\"profiles\"") ||
        !contains(json, "\"profile_source\": \"primary\"") ||
        !contains(json, "\"affinity_escape_count\": 1") ||
        !contains(json, "\"captured_output_path\"") ||
        !contains(json, "\"metrics\"") ||
        !contains(json, "\"value\": 42.5") ||
        !contains(json, "\"recommendation\"")) {
        std::printf("[test] FAILED: compare JSON missing fields.\n");
        return 1;
    }

    std::string md = vis_compare_to_markdown(report);
    if (!contains(md, "# VIS Compare AI Context") ||
        !contains(md, "## Runtime Control Evidence") ||
        !contains(md, "## Application Metrics") ||
        !contains(md, "## Recommendation")) {
        std::printf("[test] FAILED: compare Markdown missing sections.\n");
        return 1;
    }

    std::printf("[test] PASS: VIS Compare parser and serializers work.\n");
    return 0;
}
