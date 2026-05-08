#pragma once

/**
 * vis_run.hpp
 *
 * VIS Run — workload-scoped runtime policy application.
 *
 * License: MIT
 */

#include <cstdint>
#include <string>
#include <vector>

#define VIS_RUN_VERSION "0.1.0"

struct vis_run_policy_t {
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

struct vis_run_event_t {
    std::string severity;
    std::string category;
    std::string action;
    std::string result;
    std::string evidence;
    std::string recommendation;
};

struct vis_run_affinity_escape_t {
    int pid;
    int tid;
    std::string cpus_allowed_list;
    std::vector<uint32_t> escaped_cpus;
};

struct vis_run_observation_t {
    std::string mode;
    uint64_t samples;
    uint64_t observed_process_samples;
    uint64_t observed_thread_samples;
    std::vector<vis_run_affinity_escape_t> affinity_escapes;
};

struct vis_run_result_t {
    bool applied;
    bool dry_run;
    std::string verdict;
    std::string workload_status;
    int exit_code;
    vis_run_observation_t observation;
    std::vector<vis_run_event_t> events;
};

bool vis_run_parse_policy_text(const std::string& text,
                               vis_run_policy_t* policy,
                               std::string* error);
bool vis_run_parse_policy_file(const char* path,
                               vis_run_policy_t* policy,
                               std::string* error);
bool vis_run_select_cpus(const vis_run_policy_t& policy,
                         const std::string& cpu_source,
                         std::vector<uint32_t>* cpus,
                         std::string* error);
std::string vis_run_join_cpus(const std::vector<uint32_t>& cpus);
std::string vis_run_join_warnings(const std::vector<std::string>& warnings);
bool vis_run_parse_cpu_list(const std::string& text,
                            std::vector<uint32_t>* cpus,
                            std::string* error);
bool vis_run_cpu_list_has_outside(const std::vector<uint32_t>& observed,
                                  const std::vector<uint32_t>& assigned,
                                  std::vector<uint32_t>* outside);
void vis_run_observe_process_group(int process_group,
                                   const std::vector<uint32_t>& assigned_cpus,
                                   vis_run_result_t* result);
std::string vis_run_attestation_to_json(const char* policy_path,
                                        const vis_run_policy_t& policy,
                                        const std::string& cpu_source,
                                        const std::vector<uint32_t>& assigned_cpus,
                                        const vis_run_result_t& result,
                                        const std::vector<std::string>& workload_argv);
bool vis_run_write_file(const char* path,
                        const std::string& content,
                        std::string* error);
void vis_run_add_event(vis_run_result_t* result,
                       const std::string& severity,
                       const std::string& category,
                       const std::string& action,
                       const std::string& event_result,
                       const std::string& evidence,
                       const std::string& recommendation);
void vis_run_add_policy_warning_events(const vis_run_policy_t& policy,
                                       vis_run_result_t* result);
std::string vis_run_result_verdict(const vis_run_result_t& result);
