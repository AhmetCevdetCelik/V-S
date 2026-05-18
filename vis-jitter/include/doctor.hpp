#pragma once

/**
 * doctor.hpp
 *
 * VIS Doctor — machine inspection and AI-readable diagnostics.
 *
 * License: MIT
 */

#include "vis_jitter.hpp"

#include <cstdint>
#include <string>
#include <vector>

#define VIS_DOCTOR_VERSION "0.1.0"

struct vis_doctor_cpu_t {
    uint32_t id;
    bool online;
    uint32_t core_id;
    uint32_t numa_node;
    std::string siblings;
    std::string governor;
    std::string scaling_driver;
    uint64_t current_freq_khz;
    uint64_t max_freq_khz;
};

struct vis_doctor_machine_t {
    std::string hostname;
    std::string kernel;
    std::string generated_at;
    bool smt_active;
    std::string isolated_cpus;
    std::string nohz_full_cpus;
    uint64_t hugepages_total;
    uint64_t hugepages_free;
    uint64_t mlocked_kb;
    std::vector<vis_doctor_cpu_t> cpus;
};

struct vis_doctor_environment_t {
    std::string mode;
    std::string evidence_quality;
    bool root_user;
    bool msr_device_available;
    bool rdtscp_supported;
    bool hypervisor_detected;
    bool container_detected;
    std::vector<std::string> limitations;
    std::vector<std::string> reasons;
};

struct vis_doctor_sensor_t {
    std::string name;
    bool available;
    std::string quality;
    std::string source;
    std::vector<std::string> capabilities;
    std::vector<std::string> limitations;
};

struct vis_doctor_scan_t {
    uint32_t cpu_id;
    bool scanned;
    vis_status_t status;
    uint64_t accepted_samples;
    uint64_t contaminated_windows;
    uint64_t msr_delta;
    uint64_t samples_rejected;
    uint64_t core_migration_rejected;
    double p50_ns;
    double p99_ns;
    double p99_9_ns;
    double p99_99_ns;
    double max_ns;
    bool pass;
    bool clean_candidate;
    double accepted_per_sec;
    std::string throughput_class;
};

struct vis_doctor_baseline_cpu_t {
    uint32_t cpu_id;
    double baseline_accepted_per_sec;
    double current_accepted_per_sec;
    double drop_ratio;
};

struct vis_doctor_baseline_t {
    std::string path;
    bool available;
    uint32_t compared_cpus;
    double global_accepted_per_sec_drop_ratio;
    bool pressure_detected;
    std::vector<uint32_t> affected_cpus;
    std::vector<vis_doctor_baseline_cpu_t> cpus;
};

struct vis_doctor_finding_t {
    std::string severity;
    std::string category;
    std::string message;
    std::string evidence;
    std::vector<uint32_t> affected_cpus;
};

struct vis_doctor_recommendation_t {
    std::string risk;
    std::string action;
    std::string reason;
    std::string why_it_matters;
    std::string safe_suggestion;
    std::string advanced_suggestion;
    std::string advanced_risk;
    std::string expected_effect;
    std::string validation_command;
};

struct vis_doctor_runtime_policy_t {
    bool available;
    std::string profile;
    std::string cpu_policy;
    std::vector<uint32_t> primary_cpus;
    std::vector<uint32_t> secondary_cpus;
    std::vector<uint32_t> avoid_cpus;
    std::vector<uint32_t> contaminated_cpus;
    std::string smt_policy;
    std::string lower_throughput_policy;
    bool requires_longer_validation;
    std::vector<std::string> warnings;
};

struct vis_doctor_report_t {
    vis_doctor_machine_t machine;
    vis_doctor_environment_t environment;
    std::vector<vis_doctor_sensor_t> sensors;
    vis_doctor_baseline_t baseline;
    std::vector<vis_doctor_scan_t> scans;
    std::vector<vis_doctor_finding_t> findings;
    std::vector<vis_doctor_recommendation_t> recommendations;
    vis_doctor_runtime_policy_t runtime_policy;
    uint32_t duration_sec;
    double threshold_ns;
    bool scan_ran;
};

int vis_doctor_inspect(vis_doctor_report_t* report);
int vis_doctor_scan_all(uint32_t duration_sec, double threshold_ns,
                        vis_doctor_report_t* report);
int vis_doctor_load_baseline(const char* path, vis_doctor_report_t* report);
void vis_doctor_analyze(vis_doctor_report_t* report);
void vis_doctor_print_summary(const vis_doctor_report_t* report);
std::string vis_doctor_to_json(const vis_doctor_report_t* report);
std::string vis_doctor_to_markdown(const vis_doctor_report_t* report);
bool vis_doctor_write_file(const char* path, const std::string& content);
