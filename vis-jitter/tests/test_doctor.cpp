/**
 * test_doctor.cpp
 *
 * Rootless smoke test for VIS Doctor inspection and serializers.
 *
 * License: MIT
 */

#include "../include/doctor.hpp"

#include <cstdio>

static bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

int main() {
    vis_doctor_report_t report;
    if (vis_doctor_inspect(&report) < 0) {
        std::printf("[test] FAILED: vis_doctor_inspect failed.\n");
        return 1;
    }
    if (report.machine.cpus.empty()) {
        std::printf("[test] FAILED: expected at least one online CPU.\n");
        return 1;
    }
    if (report.sensors.size() < 6) {
        std::printf("[test] FAILED: expected baseline evidence sensors.\n");
        return 1;
    }

    std::string json = vis_doctor_to_json(&report);
    if (!contains(json, "\"vis_doctor_report\"") ||
        !contains(json, "\"machine\"") ||
        !contains(json, "\"thp_enabled\"") ||
        !contains(json, "\"mem_available_kb\"") ||
        !contains(json, "\"swap_total_kb\"") ||
        !contains(json, "\"anon_hugepages_kb\"") ||
        !contains(json, "\"environment\"") ||
        !contains(json, "\"evidence_quality\"") ||
        !contains(json, "\"limitations\"") ||
        !contains(json, "\"sensors\"") ||
        !contains(json, "\"msr\"") ||
        !contains(json, "\"sysfs\"") ||
        !contains(json, "\"procfs\"") ||
        !contains(json, "\"tracefs\"") ||
        !contains(json, "\"rtla\"") ||
        !contains(json, "\"perf\"") ||
        !contains(json, "\"capabilities\"") ||
        !contains(json, "\"recommendations\"") ||
        !contains(json, "\"why_it_matters\"") ||
        !contains(json, "\"safe_suggestion\"") ||
        !contains(json, "\"advanced_suggestion\"") ||
        !contains(json, "\"advanced_risk\"") ||
        !contains(json, "\"ai_context\"")) {
        std::printf("[test] FAILED: JSON missing required doctor fields.\n");
        return 1;
    }

    std::string md = vis_doctor_to_markdown(&report);
    if (!contains(md, "# VIS Doctor AI Context") ||
        !contains(md, "## Memory Evidence") ||
        !contains(md, "THP enabled") ||
        !contains(md, "MemAvailable") ||
        !contains(md, "## Environment Evidence") ||
        !contains(md, "Hardware evidence") ||
        !contains(md, "## Sensor Evidence") ||
        !contains(md, "passive availability signals") ||
        !contains(md, "capabilities=") ||
        !contains(md, "tracefs") ||
        !contains(md, "rtla") ||
        !contains(md, "perf") ||
        !contains(md, "## Recommendations") ||
        !contains(md, "Safe suggestion") ||
        !contains(md, "Advanced suggestion") ||
        !contains(md, "Advanced risk")) {
        std::printf("[test] FAILED: Markdown missing AI context sections.\n");
        return 1;
    }

    report.scans.clear();
    report.scan_ran = true;
    size_t synthetic_scans = report.machine.cpus.size() < 4
        ? report.machine.cpus.size()
        : 4;
    for (size_t i = 0; i < synthetic_scans; i++) {
        vis_doctor_scan_t scan{};
        scan.cpu_id = report.machine.cpus[i].id;
        scan.scanned = true;
        scan.status = vis_status_t::VIS_OK;
        scan.accepted_samples = 1000000;
        scan.accepted_per_sec = 1000000.0;
        scan.pass = true;
        scan.clean_candidate = true;
        scan.throughput_class = "higher_throughput_class";
        report.scans.push_back(scan);
    }
    vis_doctor_analyze(&report);

    json = vis_doctor_to_json(&report);
    if (!contains(json, "\"candidate_summary\"") ||
        !contains(json, "\"sibling_aware_primary\"") ||
        !contains(json, "\"all_clean_higher_throughput\"") ||
        !contains(json, "\"recommended_runtime_policy\"") ||
        !contains(json, "\"primary_cpus\"") ||
        !contains(json, "\"secondary_cpus\"") ||
        !contains(json, "\"avoid_cpus\"") ||
        !contains(json, "\"smt_policy\"") ||
        !contains(json, "\"lower_throughput_policy\"") ||
        !contains(json, "\"warnings\"")) {
        std::printf("[test] FAILED: JSON missing candidate summary fields.\n");
        return 1;
    }

    md = vis_doctor_to_markdown(&report);
    if (!contains(md, "## Candidate Summary") ||
        !contains(md, "Sibling-aware primary CPUs") ||
        !contains(md, "All clean higher-throughput CPUs") ||
        !contains(md, "## Recommended Runtime Policy") ||
        !contains(md, "Primary CPUs") ||
        !contains(md, "Secondary CPUs") ||
        !contains(md, "Avoid CPUs")) {
        std::printf("[test] FAILED: Markdown missing candidate summary.\n");
        return 1;
    }

    uint32_t contaminated_cpu = report.machine.cpus.front().id;
    report.scans.clear();
    report.baseline = vis_doctor_baseline_t{};
    report.scan_ran = true;
    {
        vis_doctor_scan_t scan{};
        scan.cpu_id = contaminated_cpu;
        scan.scanned = true;
        scan.status = vis_status_t::VIS_OK;
        scan.accepted_samples = 1000000;
        scan.accepted_per_sec = 1000000.0;
        scan.contaminated_windows = 1;
        scan.msr_delta = 2;
        scan.pass = true;
        scan.clean_candidate = false;
        scan.throughput_class = "higher_throughput_class";
        report.scans.push_back(scan);
    }
    vis_doctor_analyze(&report);
    json = vis_doctor_to_json(&report);
    std::string contaminated_array =
        "\"contaminated_cpus\": [" + std::to_string(contaminated_cpu) + "]";
    std::string avoid_array =
        "\"avoid_cpus\": [" + std::to_string(contaminated_cpu) + "]";
    if (!contains(json, contaminated_array.c_str()) ||
        !contains(json, avoid_array.c_str())) {
        std::printf("[test] FAILED: contaminated CPU must be explicit avoid policy.\n");
        return 1;
    }

    report.baseline = vis_doctor_baseline_t{};
    report.baseline.path = "baseline-doctor.json";
    report.baseline.available = true;
    report.baseline.compared_cpus = 1;
    report.baseline.global_accepted_per_sec_drop_ratio = 0.50;
    report.baseline.pressure_detected = true;
    report.baseline.affected_cpus.push_back(contaminated_cpu);
    report.baseline.cpus.push_back({
        contaminated_cpu,
        1000000.0,
        500000.0,
        0.50
    });
    report.scans.front().contaminated_windows = 0;
    report.scans.front().msr_delta = 0;
    report.scans.front().clean_candidate = true;
    report.scans.front().accepted_per_sec = 500000.0;
    vis_doctor_analyze(&report);
    json = vis_doctor_to_json(&report);
    md = vis_doctor_to_markdown(&report);
    if (!contains(json, "\"baseline_comparison\"") ||
        !contains(json, "\"baseline_cpu_pressure_detected\"") ||
        !contains(md, "## Baseline Comparison") ||
        !contains(md, "Pressure detected: yes")) {
        std::printf("[test] FAILED: baseline pressure evidence missing.\n");
        return 1;
    }

    std::printf("[test] PASS: VIS Doctor inspect and serializers work.\n");
    return 0;
}
