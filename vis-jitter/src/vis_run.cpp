/**
 * vis_run.cpp
 *
 * VIS Run policy parser and helpers.
 *
 * License: MIT
 */

#include "../include/vis_run.hpp"

#include <cerrno>
#include <climits>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/types.h>

static void set_error(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
}

static std::string json_escape(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

static void write_json_u32_array(std::ostringstream& out,
                                 const std::vector<uint32_t>& values) {
    out << "[";
    for (size_t i = 0; i < values.size(); i++) {
        if (i != 0) out << ", ";
        out << values[i];
    }
    out << "]";
}

static void write_json_string_array(std::ostringstream& out,
                                    const std::vector<std::string>& values) {
    out << "[";
    for (size_t i = 0; i < values.size(); i++) {
        if (i != 0) out << ", ";
        out << "\"" << json_escape(values[i]) << "\"";
    }
    out << "]";
}

static bool parse_u32_token(const std::string& text,
                            size_t start,
                            size_t end,
                            uint32_t* out,
                            std::string* error) {
    if (start >= end) {
        set_error(error, "empty CPU token");
        return false;
    }
    for (size_t i = start; i < end; i++) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            set_error(error, "invalid CPU token: " + text.substr(start, end - start));
            return false;
        }
    }

    errno = 0;
    char* parsed_end = nullptr;
    unsigned long value = strtoul(text.c_str() + start, &parsed_end, 10);
    if (errno != 0 || parsed_end != text.c_str() + end ||
        value > UINT32_MAX) {
        set_error(error, "CPU id is out of range");
        return false;
    }
    if (out != nullptr) *out = static_cast<uint32_t>(value);
    return true;
}

static bool append_unique_u32(std::vector<uint32_t>* values, uint32_t value) {
    if (values == nullptr) return false;
    for (uint32_t existing : *values) {
        if (existing == value) return false;
    }
    values->push_back(value);
    return true;
}

static void skip_ws(const std::string& text, size_t* pos) {
    while (pos != nullptr && *pos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[*pos]))) {
        (*pos)++;
    }
}

static bool find_json_object(const std::string& text,
                             const std::string& key,
                             std::string* object,
                             std::string* error) {
    std::string needle = "\"" + key + "\"";
    size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        set_error(error, "missing JSON object: " + key);
        return false;
    }

    size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        set_error(error, "missing ':' after JSON object key: " + key);
        return false;
    }

    size_t start = text.find('{', colon + 1);
    if (start == std::string::npos) {
        set_error(error, "missing object body for key: " + key);
        return false;
    }

    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    for (size_t i = start; i < text.size(); i++) {
        char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                if (object != nullptr) {
                    *object = text.substr(start, i - start + 1);
                }
                return true;
            }
        }
    }

    set_error(error, "unterminated JSON object: " + key);
    return false;
}

static bool find_value_start(const std::string& object,
                             const std::string& key,
                             size_t* pos,
                             std::string* error) {
    std::string needle = "\"" + key + "\"";
    size_t key_pos = object.find(needle);
    if (key_pos == std::string::npos) {
        set_error(error, "missing policy field: " + key);
        return false;
    }

    size_t colon = object.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        set_error(error, "missing ':' after policy field: " + key);
        return false;
    }

    *pos = colon + 1;
    skip_ws(object, pos);
    return true;
}

static bool parse_json_string_at(const std::string& text,
                                 size_t* pos,
                                 std::string* out,
                                 std::string* error) {
    if (pos == nullptr || *pos >= text.size() || text[*pos] != '"') {
        set_error(error, "expected JSON string");
        return false;
    }
    (*pos)++;

    std::string value;
    bool escaped = false;
    for (; *pos < text.size(); (*pos)++) {
        char c = text[*pos];
        if (escaped) {
            switch (c) {
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '/': value += '/'; break;
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                default:
                    set_error(error, "unsupported JSON string escape");
                    return false;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            (*pos)++;
            if (out != nullptr) *out = value;
            return true;
        } else {
            value += c;
        }
    }

    set_error(error, "unterminated JSON string");
    return false;
}

static bool parse_string_field(const std::string& object,
                               const std::string& key,
                               std::string* out,
                               std::string* error) {
    size_t pos = 0;
    if (!find_value_start(object, key, &pos, error)) return false;
    return parse_json_string_at(object, &pos, out, error);
}

static bool parse_bool_field(const std::string& object,
                             const std::string& key,
                             bool* out,
                             std::string* error) {
    size_t pos = 0;
    if (!find_value_start(object, key, &pos, error)) return false;

    if (object.compare(pos, 4, "true") == 0) {
        if (out != nullptr) *out = true;
        return true;
    }
    if (object.compare(pos, 5, "false") == 0) {
        if (out != nullptr) *out = false;
        return true;
    }

    set_error(error, "expected boolean field: " + key);
    return false;
}

static bool parse_u32_array_field(const std::string& object,
                                  const std::string& key,
                                  std::vector<uint32_t>* out,
                                  std::string* error) {
    size_t pos = 0;
    if (!find_value_start(object, key, &pos, error)) return false;
    if (pos >= object.size() || object[pos] != '[') {
        set_error(error, "expected integer array field: " + key);
        return false;
    }
    pos++;

    std::vector<uint32_t> values;
    while (pos < object.size()) {
        skip_ws(object, &pos);
        if (pos < object.size() && object[pos] == ']') {
            if (out != nullptr) *out = values;
            return true;
        }
        if (pos >= object.size() ||
            !std::isdigit(static_cast<unsigned char>(object[pos]))) {
            set_error(error, "expected unsigned integer in field: " + key);
            return false;
        }

        errno = 0;
        char* end = nullptr;
        unsigned long value = strtoul(object.c_str() + pos, &end, 10);
        if (errno != 0 || end == object.c_str() + pos ||
            value > UINT32_MAX) {
            set_error(error, "invalid unsigned integer in field: " + key);
            return false;
        }
        values.push_back(static_cast<uint32_t>(value));
        pos = static_cast<size_t>(end - object.c_str());
        skip_ws(object, &pos);

        if (pos < object.size() && object[pos] == ',') {
            pos++;
            continue;
        }
        if (pos < object.size() && object[pos] == ']') {
            continue;
        }
        set_error(error, "expected ',' or ']' in field: " + key);
        return false;
    }

    set_error(error, "unterminated integer array field: " + key);
    return false;
}

static bool parse_string_array_field(const std::string& object,
                                     const std::string& key,
                                     std::vector<std::string>* out,
                                     std::string* error) {
    size_t pos = 0;
    if (!find_value_start(object, key, &pos, error)) return false;
    if (pos >= object.size() || object[pos] != '[') {
        set_error(error, "expected string array field: " + key);
        return false;
    }
    pos++;

    std::vector<std::string> values;
    while (pos < object.size()) {
        skip_ws(object, &pos);
        if (pos < object.size() && object[pos] == ']') {
            if (out != nullptr) *out = values;
            return true;
        }

        std::string value;
        if (!parse_json_string_at(object, &pos, &value, error)) return false;
        values.push_back(value);
        skip_ws(object, &pos);

        if (pos < object.size() && object[pos] == ',') {
            pos++;
            continue;
        }
        if (pos < object.size() && object[pos] == ']') {
            continue;
        }
        set_error(error, "expected ',' or ']' in field: " + key);
        return false;
    }

    set_error(error, "unterminated string array field: " + key);
    return false;
}

bool vis_run_parse_policy_text(const std::string& text,
                               vis_run_policy_t* policy,
                               std::string* error) {
    if (policy == nullptr) {
        set_error(error, "policy output is null");
        return false;
    }
    *policy = vis_run_policy_t{};

    std::string object;
    if (!find_json_object(text, "recommended_runtime_policy",
                          &object, error)) {
        return false;
    }

    return parse_bool_field(object, "available", &policy->available, error) &&
        parse_string_field(object, "profile", &policy->profile, error) &&
        parse_string_field(object, "cpu_policy", &policy->cpu_policy, error) &&
        parse_u32_array_field(object, "primary_cpus",
                              &policy->primary_cpus, error) &&
        parse_u32_array_field(object, "secondary_cpus",
                              &policy->secondary_cpus, error) &&
        parse_u32_array_field(object, "avoid_cpus",
                              &policy->avoid_cpus, error) &&
        parse_u32_array_field(object, "contaminated_cpus",
                              &policy->contaminated_cpus, error) &&
        parse_string_field(object, "smt_policy", &policy->smt_policy, error) &&
        parse_string_field(object, "lower_throughput_policy",
                           &policy->lower_throughput_policy, error) &&
        parse_bool_field(object, "requires_longer_validation",
                         &policy->requires_longer_validation, error) &&
        parse_string_array_field(object, "warnings",
                                 &policy->warnings, error);
}

bool vis_run_parse_policy_file(const char* path,
                               vis_run_policy_t* policy,
                               std::string* error) {
    if (path == nullptr || path[0] == '\0') {
        set_error(error, "policy path is empty");
        return false;
    }

    std::ifstream in(path);
    if (!in) {
        set_error(error, std::string("cannot open policy file: ") + path);
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return vis_run_parse_policy_text(ss.str(), policy, error);
}

static bool contains_cpu(const std::vector<uint32_t>& values, uint32_t cpu) {
    for (uint32_t value : values) {
        if (value == cpu) return true;
    }
    return false;
}

bool vis_run_select_cpus(const vis_run_policy_t& policy,
                         const std::string& cpu_source,
                         std::vector<uint32_t>* cpus,
                         std::string* error) {
    if (cpus == nullptr) {
        set_error(error, "CPU output is null");
        return false;
    }

    if (!policy.available) {
        set_error(error, "runtime policy is not available");
        return false;
    }

    if (cpu_source == "primary") {
        *cpus = policy.primary_cpus;
    } else if (cpu_source == "secondary") {
        *cpus = policy.secondary_cpus;
    } else {
        set_error(error, "invalid --cpu-source value: " + cpu_source);
        return false;
    }

    if (cpus->empty()) {
        set_error(error, "selected CPU list is empty");
        return false;
    }

    for (uint32_t cpu : *cpus) {
        if (contains_cpu(policy.contaminated_cpus, cpu)) {
            set_error(error, "selected CPU list includes contaminated CPU: " +
                std::to_string(cpu));
            return false;
        }
    }

    return true;
}

std::string vis_run_join_cpus(const std::vector<uint32_t>& cpus) {
    std::string out;
    for (size_t i = 0; i < cpus.size(); i++) {
        if (i != 0) out += ",";
        out += std::to_string(cpus[i]);
    }
    return out.empty() ? "none" : out;
}

std::string vis_run_join_warnings(const std::vector<std::string>& warnings) {
    std::string out;
    for (size_t i = 0; i < warnings.size(); i++) {
        if (i != 0) out += ", ";
        out += warnings[i];
    }
    return out.empty() ? "none" : out;
}

bool vis_run_parse_cpu_list(const std::string& text,
                            std::vector<uint32_t>* cpus,
                            std::string* error) {
    if (cpus == nullptr) {
        set_error(error, "CPU list output is null");
        return false;
    }
    cpus->clear();

    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() &&
               std::isspace(static_cast<unsigned char>(text[pos]))) {
            pos++;
        }
        if (pos >= text.size()) break;

        size_t token_start = pos;
        while (pos < text.size() && text[pos] != ',') pos++;
        size_t token_end = pos;
        while (token_end > token_start &&
               std::isspace(static_cast<unsigned char>(text[token_end - 1]))) {
            token_end--;
        }

        size_t dash = text.find('-', token_start);
        if (dash != std::string::npos && dash < token_end) {
            uint32_t first = 0;
            uint32_t last = 0;
            if (!parse_u32_token(text, token_start, dash, &first, error) ||
                !parse_u32_token(text, dash + 1, token_end, &last, error)) {
                return false;
            }
            if (last < first) {
                set_error(error, "CPU range is reversed");
                return false;
            }
            for (uint32_t cpu = first; cpu <= last; cpu++) {
                append_unique_u32(cpus, cpu);
                if (cpu == UINT32_MAX) break;
            }
        } else {
            uint32_t cpu = 0;
            if (!parse_u32_token(text, token_start, token_end, &cpu, error)) {
                return false;
            }
            append_unique_u32(cpus, cpu);
        }

        if (pos < text.size() && text[pos] == ',') pos++;
    }

    return true;
}

bool vis_run_cpu_list_has_outside(const std::vector<uint32_t>& observed,
                                  const std::vector<uint32_t>& assigned,
                                  std::vector<uint32_t>* outside) {
    if (outside != nullptr) outside->clear();
    bool found = false;
    for (uint32_t cpu : observed) {
        if (!contains_cpu(assigned, cpu)) {
            found = true;
            append_unique_u32(outside, cpu);
        }
    }
    return found;
}

static bool read_file_to_string(const std::string& path, std::string* out) {
    if (out == nullptr) return false;
    std::ifstream in(path);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

static bool read_process_group(int pid, int* process_group) {
    if (process_group == nullptr) return false;

    std::string stat;
    if (!read_file_to_string("/proc/" + std::to_string(pid) + "/stat",
                             &stat)) {
        return false;
    }

    size_t close_paren = stat.rfind(')');
    if (close_paren == std::string::npos || close_paren + 2 >= stat.size()) {
        return false;
    }

    std::istringstream ss(stat.substr(close_paren + 2));
    char state = '\0';
    long parent = 0;
    long pgrp = 0;
    ss >> state >> parent >> pgrp;
    if (!ss) return false;
    *process_group = static_cast<int>(pgrp);
    return true;
}

static bool read_cpus_allowed_list(const std::string& status_path,
                                   std::string* value) {
    if (value == nullptr) return false;

    std::ifstream in(status_path);
    if (!in) return false;

    std::string line;
    const std::string key = "Cpus_allowed_list:";
    while (std::getline(in, line)) {
        if (line.compare(0, key.size(), key) == 0) {
            size_t start = key.size();
            while (start < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[start]))) {
                start++;
            }
            *value = line.substr(start);
            return true;
        }
    }

    return false;
}

static bool is_numeric_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    for (const char* p = name; *p != '\0'; p++) {
        if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    }
    return true;
}

static bool has_escape_record(const vis_run_result_t& result,
                              int pid,
                              int tid,
                              const std::string& cpus_allowed_list) {
    for (const auto& escape : result.observation.affinity_escapes) {
        if (escape.pid == pid &&
            escape.tid == tid &&
            escape.cpus_allowed_list == cpus_allowed_list) {
            return true;
        }
    }
    return false;
}

static void observe_status_affinity(int pid,
                                    int tid,
                                    const std::string& status_path,
                                    const std::vector<uint32_t>& assigned_cpus,
                                    vis_run_result_t* result) {
    if (result == nullptr) return;

    std::string allowed_list;
    if (!read_cpus_allowed_list(status_path, &allowed_list)) return;

    std::vector<uint32_t> observed;
    std::string error;
    if (!vis_run_parse_cpu_list(allowed_list, &observed, &error)) return;

    std::vector<uint32_t> outside;
    if (!vis_run_cpu_list_has_outside(observed, assigned_cpus, &outside)) {
        return;
    }
    if (has_escape_record(*result, pid, tid, allowed_list)) return;

    result->observation.affinity_escapes.push_back({
        pid,
        tid,
        allowed_list,
        outside
    });
}

static void observe_process_affinity(int pid,
                                     const std::vector<uint32_t>& assigned_cpus,
                                     vis_run_result_t* result) {
    if (result == nullptr) return;
    result->observation.observed_process_samples++;

    observe_status_affinity(pid, pid,
        "/proc/" + std::to_string(pid) + "/status",
        assigned_cpus,
        result);

    std::string task_path = "/proc/" + std::to_string(pid) + "/task";
    DIR* task_dir = opendir(task_path.c_str());
    if (task_dir == nullptr) return;

    while (dirent* entry = readdir(task_dir)) {
        if (!is_numeric_name(entry->d_name)) continue;
        int tid = atoi(entry->d_name);
        result->observation.observed_thread_samples++;
        observe_status_affinity(pid, tid,
            task_path + "/" + std::to_string(tid) + "/status",
            assigned_cpus,
            result);
    }

    closedir(task_dir);
}

void vis_run_observe_process_group(int process_group,
                                   const std::vector<uint32_t>& assigned_cpus,
                                   vis_run_result_t* result) {
    if (result == nullptr || process_group <= 0) return;
    if (result->observation.mode.empty()) {
        result->observation.mode = "process_tree_best_effort";
    }
    result->observation.samples++;

    DIR* proc_dir = opendir("/proc");
    if (proc_dir == nullptr) return;

    while (dirent* entry = readdir(proc_dir)) {
        if (!is_numeric_name(entry->d_name)) continue;
        int pid = atoi(entry->d_name);
        int pgrp = -1;
        if (!read_process_group(pid, &pgrp)) continue;
        if (pgrp != process_group) continue;

        observe_process_affinity(pid, assigned_cpus, result);
    }

    closedir(proc_dir);
}

std::string vis_run_attestation_to_json(const char* policy_path,
                                        const vis_run_policy_t& policy,
                                        const std::string& cpu_source,
                                        const std::vector<uint32_t>& assigned_cpus,
                                        const vis_run_result_t& result,
                                        const std::vector<std::string>& workload_argv) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"vis_run_attestation\": {\n";
    out << "    \"schema_version\": \"0.1\",\n";
    out << "    \"generator\": \"vis-run " << VIS_RUN_VERSION << "\",\n";
    out << "    \"policy_source\": \""
        << json_escape(policy_path == nullptr ? "" : policy_path) << "\",\n";
    out << "    \"profile\": \"" << json_escape(policy.profile) << "\",\n";
    out << "    \"cpu_policy\": \"" << json_escape(policy.cpu_policy) << "\",\n";
    out << "    \"cpu_source\": \"" << json_escape(cpu_source) << "\",\n";
    out << "    \"assigned_cpus\": ";
    write_json_u32_array(out, assigned_cpus);
    out << ",\n";
    out << "    \"avoid_cpus\": ";
    write_json_u32_array(out, policy.avoid_cpus);
    out << ",\n";
    out << "    \"contaminated_cpus\": ";
    write_json_u32_array(out, policy.contaminated_cpus);
    out << ",\n";
    out << "    \"smt_policy\": \"" << json_escape(policy.smt_policy) << "\",\n";
    out << "    \"lower_throughput_policy\": \""
        << json_escape(policy.lower_throughput_policy) << "\",\n";
    out << "    \"requires_longer_validation\": "
        << (policy.requires_longer_validation ? "true" : "false") << ",\n";
    out << "    \"warnings\": ";
    write_json_string_array(out, policy.warnings);
    out << ",\n";
    out << "    \"workload\": {\n";
    out << "      \"argv\": ";
    write_json_string_array(out, workload_argv);
    out << ",\n";
    out << "      \"started\": "
        << (result.dry_run ? "false" : "true") << ",\n";
    out << "      \"dry_run\": "
        << (result.dry_run ? "true" : "false") << ",\n";
    out << "      \"affinity_applied\": "
        << (result.applied ? "true" : "false") << ",\n";
    out << "      \"status\": \"" << json_escape(result.workload_status) << "\",\n";
    out << "      \"exit_code\": " << result.exit_code << "\n";
    out << "    },\n";
    out << "    \"observation\": {\n";
    out << "      \"mode\": \""
        << json_escape(result.observation.mode.empty() ?
            "process_tree_best_effort" : result.observation.mode) << "\",\n";
    out << "      \"samples\": " << result.observation.samples << ",\n";
    out << "      \"observed_process_samples\": "
        << result.observation.observed_process_samples << ",\n";
    out << "      \"observed_thread_samples\": "
        << result.observation.observed_thread_samples << ",\n";
    out << "      \"affinity_escapes\": [\n";
    for (size_t i = 0; i < result.observation.affinity_escapes.size(); i++) {
        const auto& escape = result.observation.affinity_escapes[i];
        out << "        {\"pid\": " << escape.pid
            << ", \"tid\": " << escape.tid
            << ", \"cpus_allowed_list\": \""
            << json_escape(escape.cpus_allowed_list)
            << "\", \"escaped_cpus\": ";
        write_json_u32_array(out, escape.escaped_cpus);
        out << "}";
        out << (i + 1 == result.observation.affinity_escapes.size() ?
            "\n" : ",\n");
    }
    out << "      ]\n";
    out << "    },\n";
    out << "    \"events\": [\n";
    for (size_t i = 0; i < result.events.size(); i++) {
        const auto& event = result.events[i];
        out << "      {\"severity\": \"" << json_escape(event.severity)
            << "\", \"category\": \"" << json_escape(event.category)
            << "\", \"action\": \"" << json_escape(event.action)
            << "\", \"result\": \"" << json_escape(event.result)
            << "\", \"evidence\": \"" << json_escape(event.evidence)
            << "\", \"recommendation\": \""
            << json_escape(event.recommendation) << "\"}";
        out << (i + 1 == result.events.size() ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"failed_actions\": [\n";
    bool first_failed = true;
    for (const auto& event : result.events) {
        if (event.severity != "error") continue;
        if (!first_failed) out << ",\n";
        out << "      {\"category\": \"" << json_escape(event.category)
            << "\", \"action\": \"" << json_escape(event.action)
            << "\", \"result\": \"" << json_escape(event.result)
            << "\", \"evidence\": \"" << json_escape(event.evidence)
            << "\"}";
        first_failed = false;
    }
    out << "\n";
    out << "    ],\n";
    out << "    \"strengthened_recommendations\": [\n";
    bool first_recommendation = true;
    for (const auto& event : result.events) {
        if (event.recommendation.empty()) continue;
        if (!first_recommendation) out << ",\n";
        out << "      {\"category\": \"" << json_escape(event.category)
            << "\", \"recommendation\": \""
            << json_escape(event.recommendation) << "\"}";
        first_recommendation = false;
    }
    out << "\n";
    out << "    ],\n";
    out << "    \"verdict\": \"" << json_escape(result.verdict) << "\"\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

bool vis_run_write_file(const char* path,
                        const std::string& content,
                        std::string* error) {
    if (path == nullptr || path[0] == '\0') {
        set_error(error, "output path is empty");
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        set_error(error, std::string("cannot write output file: ") + path);
        return false;
    }
    out << content;
    return true;
}

void vis_run_add_event(vis_run_result_t* result,
                       const std::string& severity,
                       const std::string& category,
                       const std::string& action,
                       const std::string& event_result,
                       const std::string& evidence,
                       const std::string& recommendation) {
    if (result == nullptr) return;
    result->events.push_back({
        severity,
        category,
        action,
        event_result,
        evidence,
        recommendation
    });
}

static bool has_warning(const vis_run_policy_t& policy,
                        const std::string& warning) {
    for (const auto& item : policy.warnings) {
        if (item == warning) return true;
    }
    return false;
}

void vis_run_add_policy_warning_events(const vis_run_policy_t& policy,
                                       vis_run_result_t* result) {
    if (result == nullptr) return;

    if (policy.requires_longer_validation ||
        has_warning(policy, "short_scan_requires_longer_validation")) {
        vis_run_add_event(result, "warning", "validation",
            "accept runtime policy",
            "accepted_with_warning",
            "Doctor policy was produced from a short scan.",
            "Run vis-doctor with --duration 60 before trusting strict profiles.");
    }
    if (has_warning(policy, "powersave_governor")) {
        vis_run_add_event(result, "warning", "cpu_frequency",
            "accept runtime policy",
            "accepted_with_warning",
            "Doctor detected at least one CPU using the powersave governor.",
            "Use AC/performance mode or compare runs with the same governor state.");
    }
    if (has_warning(policy, "existing_isolation")) {
        vis_run_add_event(result, "warning", "isolation",
            "accept runtime policy",
            "accepted_with_warning",
            "Doctor detected existing isolated/nohz_full CPUs.",
            "Validate the existing isolation profile with workload-specific scans.");
    }
    if (has_warning(policy, "no_persistent_hugepages")) {
        vis_run_add_event(result, "info", "memory",
            "accept runtime policy",
            "not_applicable_to_cpu_run",
            "No persistent HugePages are configured.",
            "Treat HugePages as a later VIS-Mem/inference topic, not a CPU-run blocker.");
    }
    if (has_warning(policy, "lower_throughput_cpu_class_detected")) {
        vis_run_add_event(result, "info", "cpu_topology",
            "avoid lower-throughput CPUs",
            "policy_enforced",
            "Doctor detected a lower-throughput CPU class.",
            "Keep first strict workloads on primary CPUs; compare lower-throughput CPUs separately.");
    }
    if (has_warning(policy, "smi_contaminated_windows_detected")) {
        vis_run_add_event(result, "warning", "smi",
            "avoid contaminated CPUs",
            "policy_enforced",
            "Doctor detected SMI-contaminated measurement windows.",
            "Repeat vis-doctor and avoid contaminated CPUs until repeated scans are clean.");
    }
}

std::string vis_run_result_verdict(const vis_run_result_t& result) {
    bool has_warning_event = false;
    bool has_error_event = false;
    for (const auto& event : result.events) {
        if (event.severity == "error") has_error_event = true;
        if (event.severity == "warning") has_warning_event = true;
    }

    if (result.dry_run) return "DRY_RUN";
    if (has_error_event && !result.applied) return "FAILED_BEFORE_WORKLOAD";
    if (has_error_event) return "WORKLOAD_FAILED";
    if (!result.applied) return "POLICY_REJECTED";
    if (result.exit_code != 0) return "WORKLOAD_FAILED";
    if (has_warning_event) return "CONTROLLED_WITH_WARNINGS";
    return "CONTROLLED";
}
