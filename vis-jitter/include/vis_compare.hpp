#pragma once

/**
 * vis_compare.hpp
 *
 * VIS Compare Lite — profile comparison over VIS Run attestations.
 *
 * License: MIT
 */

#include <cstdint>
#include <string>
#include <vector>

#define VIS_COMPARE_VERSION "0.1.0"

struct vis_compare_profile_result_t {
    std::string profile_source;
    std::string attestation_path;
    std::vector<uint32_t> assigned_cpus;
    int exit_code;
    uint64_t duration_ms;
    std::string verdict;
    size_t affinity_escape_count;
    size_t warning_count;
};

struct vis_compare_report_t {
    std::string policy_path;
    std::string workload;
    std::vector<vis_compare_profile_result_t> profiles;
    std::string recommendation;
};

bool vis_compare_parse_run_attestation(
    const std::string& text,
    vis_compare_profile_result_t* result,
    std::string* error
);
std::string vis_compare_to_json(const vis_compare_report_t& report);
std::string vis_compare_to_markdown(const vis_compare_report_t& report);
bool vis_compare_write_file(const char* path,
                            const std::string& content,
                            std::string* error);
std::string vis_compare_join_cpus(const std::vector<uint32_t>& cpus);
std::string vis_compare_join_argv(const std::vector<std::string>& argv);
