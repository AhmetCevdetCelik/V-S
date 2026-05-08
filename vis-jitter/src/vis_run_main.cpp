/**
 * vis_run_main.cpp
 *
 * VIS Run CLI entry point.
 *
 * License: MIT
 */

#include "../include/vis_run.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

static void print_usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s [OPTIONS] -- <program> [args...]\n"
        "\n"
        "Options:\n"
        "  --policy <file.json>       Doctor JSON policy (default: doctor.json)\n"
        "  --profile <name>           Expected profile name (default: strict)\n"
        "  --cpu-source <source>      primary or secondary (default: primary)\n"
        "  --output <file.json>       Write VIS Run JSON attestation\n"
        "  --dry-run                  Print policy without starting workload\n"
        "  --help                     Show this message\n"
        "\n"
        "Example:\n"
        "  ./vis-run --policy doctor.json --profile strict -- ./app\n",
        argv0
    );
}

static bool build_cpu_set(const std::vector<uint32_t>& cpus,
                          cpu_set_t* set,
                          std::string* error) {
    if (set == nullptr) return false;
    CPU_ZERO(set);
    for (uint32_t cpu : cpus) {
        if (cpu >= CPU_SETSIZE) {
            if (error != nullptr) {
                *error = "CPU id exceeds CPU_SETSIZE: " + std::to_string(cpu);
            }
            return false;
        }
        CPU_SET(cpu, set);
    }
    return true;
}

static const char* exit_description(int status) {
    if (WIFEXITED(status)) return "exited";
    if (WIFSIGNALED(status)) return "signaled";
    return "unknown";
}

static int workload_exit_code(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static void print_attestation(const char* policy_path,
                              const vis_run_policy_t& policy,
                              const std::string& cpu_source,
                              const std::vector<uint32_t>& assigned_cpus,
                              const vis_run_result_t& result) {
    printf("\nVIS Run Attestation\n");
    printf("Policy source: %s\n", policy_path);
    printf("Profile: %s\n", policy.profile.c_str());
    printf("CPU policy: %s\n", policy.cpu_policy.c_str());
    printf("CPU source: %s\n", cpu_source.c_str());
    printf("Assigned CPUs: %s\n", vis_run_join_cpus(assigned_cpus).c_str());
    printf("Avoided CPUs: %s\n", vis_run_join_cpus(policy.avoid_cpus).c_str());
    printf("Contaminated CPUs: %s\n",
           vis_run_join_cpus(policy.contaminated_cpus).c_str());
    printf("SMT policy: %s\n", policy.smt_policy.c_str());
    printf("Lower-throughput policy: %s\n",
           policy.lower_throughput_policy.c_str());
    printf("Requires longer validation: %s\n",
           policy.requires_longer_validation ? "yes" : "no");
    printf("Warnings: %s\n", vis_run_join_warnings(policy.warnings).c_str());
    if (result.dry_run) {
        printf("Workload: not started (dry-run)\n");
    } else {
        printf("Affinity applied: %s\n", result.applied ? "yes" : "no");
        printf("Workload status: %s\n", result.workload_status.c_str());
        printf("Workload exit code: %d\n", result.exit_code);
    }
    printf("Observation mode: %s\n",
           (result.observation.mode.empty() ?
            "process_tree_best_effort" :
            result.observation.mode.c_str()));
    printf("Observation samples: %llu\n",
           static_cast<unsigned long long>(result.observation.samples));
    printf("Observed process samples: %llu\n",
           static_cast<unsigned long long>(
               result.observation.observed_process_samples));
    printf("Observed thread samples: %llu\n",
           static_cast<unsigned long long>(
               result.observation.observed_thread_samples));
    printf("Affinity escapes: %zu\n",
           result.observation.affinity_escapes.size());
    for (size_t i = 0; i < result.observation.affinity_escapes.size() &&
         i < 3; i++) {
        const auto& escape = result.observation.affinity_escapes[i];
        printf("  pid=%d tid=%d allowed=%s escaped=%s\n",
               escape.pid,
               escape.tid,
               escape.cpus_allowed_list.c_str(),
               vis_run_join_cpus(escape.escaped_cpus).c_str());
    }

    printf("\nFailed actions:\n");
    bool printed_failed = false;
    for (const auto& event : result.events) {
        if (event.severity == "error") {
            printf("  %s: %s -> %s\n", event.category.c_str(),
                   event.action.c_str(), event.result.c_str());
            printf("    Evidence: %s\n", event.evidence.c_str());
            printed_failed = true;
        }
    }
    if (!printed_failed) printf("  none\n");

    printf("\nStrengthened recommendations:\n");
    bool printed_recommendation = false;
    for (const auto& event : result.events) {
        if (!event.recommendation.empty()) {
            printf("  %s: %s\n", event.category.c_str(),
                   event.recommendation.c_str());
            printed_recommendation = true;
        }
    }
    if (!printed_recommendation) printf("  none\n");

    printf("\nVerdict: %s\n", result.verdict.c_str());
    printf("\n");
}

static bool write_attestation_if_requested(
    const char* output_path,
    const char* policy_path,
    const vis_run_policy_t& policy,
    const std::string& cpu_source,
    const std::vector<uint32_t>& assigned_cpus,
    const vis_run_result_t& result,
    const std::vector<std::string>& workload_argv
) {
    if (output_path == nullptr) return true;
    std::string error;
    std::string json = vis_run_attestation_to_json(
        policy_path,
        policy,
        cpu_source,
        assigned_cpus,
        result,
        workload_argv
    );
    if (!vis_run_write_file(output_path, json, &error)) {
        fprintf(stderr, "[vis-run] ERROR: %s\n", error.c_str());
        return false;
    }
    printf("[vis-run] JSON attestation saved to: %s\n", output_path);
    return true;
}

static void print_failure_attestation(const char* policy_path,
                                      const std::string& category,
                                      const std::string& action,
                                      const std::string& evidence,
                                      const std::string& recommendation,
                                      const std::string& verdict) {
    printf("\nVIS Run Attestation\n");
    printf("Policy source: %s\n", policy_path);
    printf("Profile: unknown\n");
    printf("CPU policy: unknown\n");
    printf("Assigned CPUs: none\n");
    printf("Workload: not started\n");
    printf("\nFailed actions:\n");
    printf("  %s: %s -> failed\n", category.c_str(), action.c_str());
    printf("    Evidence: %s\n", evidence.c_str());
    printf("\nStrengthened recommendations:\n");
    printf("  %s: %s\n", category.c_str(), recommendation.c_str());
    printf("\nVerdict: %s\n\n", verdict.c_str());
}

int main(int argc, char* argv[]) {
    const char* policy_path = "doctor.json";
    const char* expected_profile = "strict";
    const char* output_path = nullptr;
    std::string cpu_source = "primary";
    bool dry_run = false;
    int command_index = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            policy_path = argv[++i];
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            expected_profile = argv[++i];
        } else if (strcmp(argv[i], "--cpu-source") == 0 && i + 1 < argc) {
            cpu_source = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "--") == 0) {
            command_index = i + 1;
            break;
        } else {
            fprintf(stderr, "[vis-run] Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!dry_run && (command_index < 0 || command_index >= argc)) {
        fprintf(stderr, "[vis-run] Missing workload command after --.\n");
        print_usage(argv[0]);
        return 1;
    }

    vis_run_policy_t policy;
    std::string error;
    if (!vis_run_parse_policy_file(policy_path, &policy, &error)) {
        fprintf(stderr, "[vis-run] ERROR: %s\n", error.c_str());
        print_failure_attestation(policy_path,
            "policy",
            "parse Doctor runtime policy",
            error,
            "Regenerate doctor.json with a current vis-doctor scan.",
            "POLICY_REJECTED");
        return 1;
    }

    if (policy.profile != expected_profile) {
        fprintf(stderr,
                "[vis-run] ERROR: policy profile '%s' does not match '%s'.\n",
                policy.profile.c_str(), expected_profile);
        print_failure_attestation(policy_path,
            "profile",
            "validate requested profile",
            "Policy profile does not match --profile.",
            "Use the profile from doctor.json or regenerate the runtime policy.",
            "POLICY_REJECTED");
        return 1;
    }

    vis_run_result_t result{};
    result.dry_run = dry_run;
    result.applied = false;
    result.exit_code = 0;
    result.workload_status = dry_run ? "not_started" : "unknown";
    result.observation.mode = "process_tree_best_effort";
    vis_run_add_policy_warning_events(policy, &result);

    std::vector<std::string> workload_argv;
    if (command_index >= 0) {
        for (int i = command_index; i < argc; i++) {
            workload_argv.push_back(argv[i]);
        }
    }

    std::vector<uint32_t> assigned_cpus;
    if (!vis_run_select_cpus(policy, cpu_source, &assigned_cpus, &error)) {
        fprintf(stderr, "[vis-run] ERROR: %s\n", error.c_str());
        vis_run_add_event(&result, "error", "policy",
            "select CPU list",
            "failed",
            error,
            "Rerun vis-doctor and choose a clean primary or secondary CPU source.");
        result.verdict = vis_run_result_verdict(result);
        print_attestation(policy_path, policy, cpu_source, assigned_cpus, result);
        write_attestation_if_requested(output_path, policy_path, policy,
                                       cpu_source, assigned_cpus, result,
                                       workload_argv);
        return 1;
    }

    cpu_set_t set;
    if (!build_cpu_set(assigned_cpus, &set, &error)) {
        fprintf(stderr, "[vis-run] ERROR: %s\n", error.c_str());
        vis_run_add_event(&result, "error", "affinity",
            "build CPU affinity mask",
            "failed",
            error,
            "Rerun vis-doctor and verify selected CPUs are online and within CPU_SETSIZE.");
        result.verdict = vis_run_result_verdict(result);
        print_attestation(policy_path, policy, cpu_source, assigned_cpus, result);
        write_attestation_if_requested(output_path, policy_path, policy,
                                       cpu_source, assigned_cpus, result,
                                       workload_argv);
        return 1;
    }

    if (dry_run) {
        result.verdict = vis_run_result_verdict(result);
        print_attestation(policy_path, policy, cpu_source, assigned_cpus,
                          result);
        if (!write_attestation_if_requested(output_path, policy_path, policy,
                                            cpu_source, assigned_cpus, result,
                                            workload_argv)) {
            return 1;
        }
        return 0;
    }

    int ready_pipe[2] = {-1, -1};
    if (pipe(ready_pipe) != 0) {
        fprintf(stderr, "[vis-run] ERROR: pipe failed: %s\n", strerror(errno));
        vis_run_add_event(&result, "error", "process",
            "create workload readiness pipe",
            "failed",
            strerror(errno),
            "Check process limits and system pressure before retrying.");
        result.verdict = vis_run_result_verdict(result);
        print_attestation(policy_path, policy, cpu_source, assigned_cpus, result);
        write_attestation_if_requested(output_path, policy_path, policy,
                                       cpu_source, assigned_cpus, result,
                                       workload_argv);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[vis-run] ERROR: fork failed: %s\n", strerror(errno));
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        vis_run_add_event(&result, "error", "process",
            "fork workload",
            "failed",
            strerror(errno),
            "Check process limits and system pressure before retrying.");
        result.verdict = vis_run_result_verdict(result);
        print_attestation(policy_path, policy, cpu_source, assigned_cpus, result);
        write_attestation_if_requested(output_path, policy_path, policy,
                                       cpu_source, assigned_cpus, result,
                                       workload_argv);
        return 1;
    }

    if (pid == 0) {
        close(ready_pipe[0]);
        setpgid(0, 0);
        if (sched_setaffinity(0, sizeof(set), &set) != 0) {
            fprintf(stderr, "[vis-run] ERROR: sched_setaffinity failed: %s\n",
                    strerror(errno));
            close(ready_pipe[1]);
            _exit(126);
        }
        char ready = '1';
        ssize_t ignored = write(ready_pipe[1], &ready, 1);
        (void)ignored;
        close(ready_pipe[1]);
        execvp(argv[command_index], &argv[command_index]);
        fprintf(stderr, "[vis-run] ERROR: execvp failed: %s\n",
                strerror(errno));
        _exit(127);
    }

    close(ready_pipe[1]);
    setpgid(pid, pid);

    int status = 0;
    bool wait_failed = false;
    char ready = '\0';
    ssize_t readiness = 0;
    do {
        readiness = read(ready_pipe[0], &ready, 1);
    } while (readiness < 0 && errno == EINTR);
    close(ready_pipe[0]);
    if (readiness < 0) {
        fprintf(stderr, "[vis-run] ERROR: readiness read failed: %s\n",
                strerror(errno));
        vis_run_add_event(&result, "warning", "observation",
            "wait for affinity readiness",
            "read_failed",
            strerror(errno),
            "Rerun vis-run; if this repeats, inspect process limits and pipe behavior.");
    }
    while (true) {
        vis_run_observe_process_group(static_cast<int>(pid),
                                      assigned_cpus,
                                      &result);
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited == 0) {
            usleep(250000);
            continue;
        }
        if (waited < 0 && errno == EINTR) continue;
        wait_failed = true;
        break;
    }

    if (wait_failed) {
        fprintf(stderr, "[vis-run] ERROR: waitpid failed: %s\n",
                strerror(errno));
        vis_run_add_event(&result, "error", "process",
            "wait for workload",
            "failed",
            strerror(errno),
            "Inspect process state and rerun vis-run.");
        result.verdict = vis_run_result_verdict(result);
        print_attestation(policy_path, policy, cpu_source, assigned_cpus, result);
        write_attestation_if_requested(output_path, policy_path, policy,
                                       cpu_source, assigned_cpus, result,
                                       workload_argv);
        return 1;
    }

    result.applied = !(WIFEXITED(status) && WEXITSTATUS(status) == 126);
    result.workload_status = exit_description(status);
    result.exit_code = workload_exit_code(status);
    if (!result.applied) {
        vis_run_add_event(&result, "error", "affinity",
            "apply CPU affinity",
            "failed",
            "Child process reported sched_setaffinity failure.",
            "Verify selected CPUs are online and not blocked by cgroup/cpuset constraints.");
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        vis_run_add_event(&result, "error", "workload",
            "exec workload",
            "failed",
            "Child process reported execvp failure.",
            "Check workload path, permissions, and command arguments.");
    } else if (WIFSIGNALED(status)) {
        vis_run_add_event(&result, "error", "workload",
            "run workload",
            "signaled",
            "Workload terminated by signal " + std::to_string(WTERMSIG(status)) + ".",
            "Inspect workload logs and rerun with the same VIS policy for reproduction.");
    } else if (result.exit_code != 0) {
        vis_run_add_event(&result, "error", "workload",
            "run workload",
            "nonzero_exit",
            "Workload exited with code " + std::to_string(result.exit_code) + ".",
            "Inspect workload failure before treating runtime policy as successful.");
    }
    if (!result.observation.affinity_escapes.empty()) {
        vis_run_add_event(&result, "warning", "runtime_affinity",
            "observe workload CPU affinity",
            "escape_detected",
            "Observed workload process or thread CPU allowance outside the assigned VIS CPU set.",
            "Use VIS cgroups v2 containment in the next CPU profile step, then rerun vis-run.");
    }
    result.verdict = vis_run_result_verdict(result);
    print_attestation(policy_path, policy, cpu_source, assigned_cpus, result);
    if (!write_attestation_if_requested(output_path, policy_path, policy,
                                        cpu_source, assigned_cpus, result,
                                        workload_argv)) {
        return 1;
    }
    return workload_exit_code(status);
}
