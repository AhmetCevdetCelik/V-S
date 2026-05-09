/**
 * vis_compare_main.cpp
 *
 * VIS Compare Lite CLI entry point.
 *
 * License: MIT
 */

#include "../include/vis_compare.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static void print_usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s [OPTIONS] -- <program> [args...]\n"
        "\n"
        "Options:\n"
        "  --policy <file.json>       Doctor JSON policy (default: doctor.json)\n"
        "  --profile <name>           Expected profile name (default: strict)\n"
        "  --vis-run <path>           vis-run binary path (default: ./vis-run)\n"
        "  --runs-dir <dir>           Per-profile attestations (default: vis-compare-runs)\n"
        "  --output <file.json>       Write VIS Compare JSON report\n"
        "  --llm <file.md>            Write AI-readable Markdown report\n"
        "  --help                     Show this message\n"
        "\n"
        "Example:\n"
        "  ./vis-compare --policy doctor.json --output compare.json -- ./app\n",
        argv0
    );
}

static bool read_file(const std::string& path, std::string* out) {
    if (out == nullptr) return false;
    std::ifstream in(path);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

static bool ensure_dir(const std::string& path) {
    if (path.empty()) return false;
    struct stat st {};
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    return mkdir(path.c_str(), 0755) == 0;
}

static int command_exit_code(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static bool run_vis_run_profile(
    const std::string& vis_run_path,
    const std::string& policy_path,
    const std::string& profile_name,
    const std::string& cpu_source,
    const std::string& attestation_path,
    const std::vector<std::string>& workload_argv,
    vis_compare_profile_result_t* result
) {
    if (result == nullptr || workload_argv.empty()) return false;

    std::vector<std::string> args = {
        vis_run_path,
        "--policy", policy_path,
        "--profile", profile_name,
        "--cpu-source", cpu_source,
        "--output", attestation_path,
        "--"
    };
    args.insert(args.end(), workload_argv.begin(), workload_argv.end());

    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    auto start = std::chrono::steady_clock::now();
    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        execvp(vis_run_path.c_str(), argv.data());
        fprintf(stderr, "[vis-compare] ERROR: execvp vis-run failed: %s\n",
                strerror(errno));
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return false;
    }
    auto end = std::chrono::steady_clock::now();

    std::string json;
    if (!read_file(attestation_path, &json)) return false;

    std::string error;
    if (!vis_compare_parse_run_attestation(json, result, &error)) {
        fprintf(stderr, "[vis-compare] ERROR: cannot parse %s: %s\n",
                attestation_path.c_str(), error.c_str());
        return false;
    }

    result->profile_source = cpu_source;
    result->attestation_path = attestation_path;
    result->duration_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
    );
    if (result->exit_code != command_exit_code(status)) {
        result->exit_code = command_exit_code(status);
    }
    return true;
}

static std::string build_recommendation(
    const std::vector<vis_compare_profile_result_t>& profiles
) {
    bool primary_ok = false;
    bool secondary_ok = false;
    for (const auto& p : profiles) {
        if (p.profile_source == "primary" && p.exit_code == 0 &&
            p.affinity_escape_count == 0) {
            primary_ok = true;
        }
        if (p.profile_source == "secondary" && p.exit_code == 0 &&
            p.affinity_escape_count == 0) {
            secondary_ok = true;
        }
    }

    if (primary_ok && secondary_ok) {
        return "Use primary as the first latency-sensitive profile and "
               "secondary as the first throughput comparison profile.";
    }
    if (primary_ok) {
        return "Primary completed cleanly; inspect secondary before using it "
               "as a throughput comparison profile.";
    }
    if (secondary_ok) {
        return "Secondary completed cleanly, but primary should be inspected "
               "before treating it as the latency-sensitive profile.";
    }
    return "No compared profile completed cleanly; inspect per-profile VIS Run "
           "attestations before making a placement decision.";
}

static void print_summary(const vis_compare_report_t& report) {
    printf("\nVIS Compare\n");
    printf("Workload: %s\n\n", report.workload.c_str());
    printf("%-10s %-6s %-8s %-10s %s\n",
           "Profile", "Exit", "Escapes", "Duration", "Verdict");
    for (const auto& p : report.profiles) {
        printf("%-10s %-6d %-8zu %-10llu %s\n",
               p.profile_source.c_str(),
               p.exit_code,
               p.affinity_escape_count,
               static_cast<unsigned long long>(p.duration_ms),
               p.verdict.c_str());
        printf("  CPUs: %s\n",
               vis_compare_join_cpus(p.assigned_cpus).c_str());
    }
    printf("\nRecommendation:\n%s\n\n", report.recommendation.c_str());
}

int main(int argc, char* argv[]) {
    const char* policy_path = "doctor.json";
    const char* profile_name = "strict";
    const char* vis_run_path = "./vis-run";
    const char* runs_dir = "vis-compare-runs";
    const char* output_path = nullptr;
    const char* llm_path = nullptr;
    int command_index = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            policy_path = argv[++i];
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            profile_name = argv[++i];
        } else if (strcmp(argv[i], "--vis-run") == 0 && i + 1 < argc) {
            vis_run_path = argv[++i];
        } else if (strcmp(argv[i], "--runs-dir") == 0 && i + 1 < argc) {
            runs_dir = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--llm") == 0 && i + 1 < argc) {
            llm_path = argv[++i];
        } else if (strcmp(argv[i], "--") == 0) {
            command_index = i + 1;
            break;
        } else {
            fprintf(stderr, "[vis-compare] Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (command_index < 0 || command_index >= argc) {
        fprintf(stderr, "[vis-compare] Missing workload command after --.\n");
        print_usage(argv[0]);
        return 1;
    }
    if (!ensure_dir(runs_dir)) {
        fprintf(stderr, "[vis-compare] ERROR: cannot create runs dir: %s\n",
                runs_dir);
        return 1;
    }

    std::vector<std::string> workload_argv;
    for (int i = command_index; i < argc; i++) {
        workload_argv.push_back(argv[i]);
    }

    vis_compare_report_t report;
    report.policy_path = policy_path;
    report.workload = vis_compare_join_argv(workload_argv);

    const char* sources[] = {"primary", "secondary"};
    bool all_invocations_started = true;
    for (const char* source : sources) {
        std::string attestation_path =
            std::string(runs_dir) + "/" + source + ".json";
        vis_compare_profile_result_t profile_result;
        if (!run_vis_run_profile(vis_run_path, policy_path, profile_name,
                                 source, attestation_path, workload_argv,
                                 &profile_result)) {
            all_invocations_started = false;
            profile_result.profile_source = source;
            profile_result.attestation_path = attestation_path;
            profile_result.exit_code = 127;
            profile_result.verdict = "VIS_RUN_FAILED";
        }
        report.profiles.push_back(profile_result);
    }

    report.recommendation = build_recommendation(report.profiles);
    print_summary(report);

    std::string error;
    if (output_path != nullptr &&
        !vis_compare_write_file(output_path,
                                vis_compare_to_json(report),
                                &error)) {
        fprintf(stderr, "[vis-compare] ERROR: %s\n", error.c_str());
        return 1;
    }
    if (llm_path != nullptr &&
        !vis_compare_write_file(llm_path,
                                vis_compare_to_markdown(report),
                                &error)) {
        fprintf(stderr, "[vis-compare] ERROR: %s\n", error.c_str());
        return 1;
    }

    return all_invocations_started ? 0 : 1;
}
