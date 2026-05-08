/**
 * test_vis_run.cpp
 *
 * Rootless tests for VIS Run policy parsing.
 *
 * License: MIT
 */

#include "../include/vis_run.hpp"

#include <cstdio>

static const char* kPolicyJson =
    "{\n"
    "  \"vis_doctor_report\": {\n"
    "    \"recommended_runtime_policy\": {\n"
    "      \"available\": true,\n"
    "      \"profile\": \"strict\",\n"
    "      \"cpu_policy\": \"sibling_aware_primary\",\n"
    "      \"primary_cpus\": [0, 2, 4],\n"
    "      \"secondary_cpus\": [0, 1, 2, 3, 4, 5],\n"
    "      \"avoid_cpus\": [6, 7],\n"
    "      \"contaminated_cpus\": [],\n"
    "      \"smt_policy\": \"avoid_sibling_sharing_for_first_profile\",\n"
    "      \"lower_throughput_policy\": \"avoid_for_first_profile\",\n"
    "      \"requires_longer_validation\": true,\n"
    "      \"warnings\": [\"smt_active\", \"short_scan_requires_longer_validation\"]\n"
    "    }\n"
    "  }\n"
    "}\n";

static const char* kContaminatedJson =
    "{\n"
    "  \"recommended_runtime_policy\": {\n"
    "    \"available\": true,\n"
    "    \"profile\": \"strict\",\n"
    "    \"cpu_policy\": \"sibling_aware_primary\",\n"
    "    \"primary_cpus\": [0, 2],\n"
    "    \"secondary_cpus\": [0, 1, 2],\n"
    "    \"avoid_cpus\": [],\n"
    "    \"contaminated_cpus\": [2],\n"
    "    \"smt_policy\": \"avoid_sibling_sharing_for_first_profile\",\n"
    "    \"lower_throughput_policy\": \"avoid_for_first_profile\",\n"
    "    \"requires_longer_validation\": false,\n"
    "    \"warnings\": []\n"
    "  }\n"
    "}\n";

static bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

int main() {
    vis_run_policy_t policy;
    std::string error;
    if (!vis_run_parse_policy_text(kPolicyJson, &policy, &error)) {
        std::printf("[test] FAILED: parse valid policy: %s\n", error.c_str());
        return 1;
    }
    if (!policy.available || policy.profile != "strict" ||
        policy.primary_cpus.size() != 3 ||
        policy.secondary_cpus.size() != 6 ||
        !policy.requires_longer_validation ||
        policy.warnings.size() != 2) {
        std::printf("[test] FAILED: parsed policy has wrong fields.\n");
        return 1;
    }

    std::vector<uint32_t> cpus;
    if (!vis_run_select_cpus(policy, "primary", &cpus, &error) ||
        vis_run_join_cpus(cpus) != "0,2,4") {
        std::printf("[test] FAILED: primary CPU selection failed.\n");
        return 1;
    }
    if (!vis_run_select_cpus(policy, "secondary", &cpus, &error) ||
        vis_run_join_cpus(cpus) != "0,1,2,3,4,5") {
        std::printf("[test] FAILED: secondary CPU selection failed.\n");
        return 1;
    }

    if (!vis_run_parse_cpu_list("0,2,4-6", &cpus, &error) ||
        vis_run_join_cpus(cpus) != "0,2,4,5,6") {
        std::printf("[test] FAILED: CPU list parser failed: %s\n",
                    error.c_str());
        return 1;
    }

    std::vector<uint32_t> outside;
    std::vector<uint32_t> assigned = {0, 2, 4};
    std::vector<uint32_t> observed = {0, 2, 5};
    if (!vis_run_cpu_list_has_outside(observed, assigned, &outside) ||
        vis_run_join_cpus(outside) != "5") {
        std::printf("[test] FAILED: outside CPU detection failed.\n");
        return 1;
    }
    observed = {0, 2, 4};
    if (vis_run_cpu_list_has_outside(observed, assigned, &outside)) {
        std::printf("[test] FAILED: matching CPU list should not escape.\n");
        return 1;
    }

    if (vis_run_parse_policy_text("{\"vis_doctor_report\":{}}",
                                  &policy, &error)) {
        std::printf("[test] FAILED: missing runtime policy should fail.\n");
        return 1;
    }

    if (!vis_run_parse_policy_text(kContaminatedJson, &policy, &error)) {
        std::printf("[test] FAILED: parse contaminated policy.\n");
        return 1;
    }
    if (vis_run_select_cpus(policy, "primary", &cpus, &error)) {
        std::printf("[test] FAILED: contaminated CPU selection should fail.\n");
        return 1;
    }

    vis_run_result_t result{};
    result.applied = true;
    result.exit_code = 0;
    vis_run_add_policy_warning_events(policy, &result);
    if (vis_run_result_verdict(result) != "CONTROLLED") {
        std::printf("[test] FAILED: clean result verdict should be CONTROLLED.\n");
        return 1;
    }

    if (!vis_run_parse_policy_text(kPolicyJson, &policy, &error)) {
        std::printf("[test] FAILED: reparse warning policy.\n");
        return 1;
    }
    result = vis_run_result_t{};
    result.applied = true;
    result.exit_code = 0;
    vis_run_add_policy_warning_events(policy, &result);
    if (result.events.empty() ||
        vis_run_result_verdict(result) != "CONTROLLED_WITH_WARNINGS") {
        std::printf("[test] FAILED: warning policy should strengthen recommendations.\n");
        return 1;
    }

    vis_run_add_event(&result, "error", "workload", "run workload",
                      "nonzero_exit", "exit code 1",
                      "Inspect workload failure.");
    result.exit_code = 1;
    if (vis_run_result_verdict(result) != "WORKLOAD_FAILED") {
        std::printf("[test] FAILED: error event should fail workload verdict.\n");
        return 1;
    }

    result.verdict = vis_run_result_verdict(result);
    result.observation.mode = "process_tree_best_effort";
    result.observation.samples = 2;
    result.observation.observed_process_samples = 2;
    result.observation.observed_thread_samples = 2;
    result.observation.affinity_escapes.push_back({
        123,
        124,
        "0,2,5",
        {5}
    });
    std::vector<std::string> argv = {"/bin/false"};
    std::string json = vis_run_attestation_to_json(
        "doctor.json",
        policy,
        "primary",
        cpus,
        result,
        argv
    );
    if (!contains(json, "\"vis_run_attestation\"") ||
        !contains(json, "\"workload\"") ||
        !contains(json, "\"observation\"") ||
        !contains(json, "\"affinity_escapes\"") ||
        !contains(json, "\"escaped_cpus\": [5]") ||
        !contains(json, "\"events\"") ||
        !contains(json, "\"failed_actions\"") ||
        !contains(json, "\"strengthened_recommendations\"") ||
        !contains(json, "\"verdict\": \"WORKLOAD_FAILED\"")) {
        std::printf("[test] FAILED: run attestation JSON missing fields.\n");
        return 1;
    }

    std::printf("[test] PASS: VIS Run policy parser works.\n");
    return 0;
}
