/**
 * vis_compare.cpp
 *
 * VIS Compare Lite helpers.
 *
 * License: MIT
 */

#include "../include/vis_compare.hpp"

#include <cerrno>
#include <climits>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

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

static void skip_ws(const std::string& text, size_t* pos) {
    while (pos != nullptr && *pos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[*pos]))) {
        (*pos)++;
    }
}

static bool find_value_start(const std::string& text,
                             const std::string& key,
                             size_t* pos,
                             std::string* error) {
    std::string needle = "\"" + key + "\"";
    size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        set_error(error, "missing JSON field: " + key);
        return false;
    }

    size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        set_error(error, "missing ':' after JSON field: " + key);
        return false;
    }

    *pos = colon + 1;
    skip_ws(text, pos);
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

static bool parse_string_field(const std::string& text,
                               const std::string& key,
                               std::string* out,
                               std::string* error) {
    size_t pos = 0;
    if (!find_value_start(text, key, &pos, error)) return false;
    return parse_json_string_at(text, &pos, out, error);
}

static bool parse_int_field(const std::string& text,
                            const std::string& key,
                            int* out,
                            std::string* error) {
    size_t pos = 0;
    if (!find_value_start(text, key, &pos, error)) return false;

    errno = 0;
    char* end = nullptr;
    long value = strtol(text.c_str() + pos, &end, 10);
    if (errno != 0 || end == text.c_str() + pos ||
        value < INT_MIN || value > INT_MAX) {
        set_error(error, "invalid integer field: " + key);
        return false;
    }
    if (out != nullptr) *out = static_cast<int>(value);
    return true;
}

static bool parse_u32_array_field(const std::string& text,
                                  const std::string& key,
                                  std::vector<uint32_t>* out,
                                  std::string* error) {
    size_t pos = 0;
    if (!find_value_start(text, key, &pos, error)) return false;
    if (pos >= text.size() || text[pos] != '[') {
        set_error(error, "expected integer array field: " + key);
        return false;
    }
    pos++;

    std::vector<uint32_t> values;
    while (pos < text.size()) {
        skip_ws(text, &pos);
        if (pos < text.size() && text[pos] == ']') {
            if (out != nullptr) *out = values;
            return true;
        }
        if (pos >= text.size() ||
            !std::isdigit(static_cast<unsigned char>(text[pos]))) {
            set_error(error, "expected unsigned integer in field: " + key);
            return false;
        }

        errno = 0;
        char* end = nullptr;
        unsigned long value = strtoul(text.c_str() + pos, &end, 10);
        if (errno != 0 || end == text.c_str() + pos ||
            value > UINT32_MAX) {
            set_error(error, "invalid unsigned integer in field: " + key);
            return false;
        }
        values.push_back(static_cast<uint32_t>(value));
        pos = static_cast<size_t>(end - text.c_str());
        skip_ws(text, &pos);

        if (pos < text.size() && text[pos] == ',') {
            pos++;
            continue;
        }
        if (pos < text.size() && text[pos] == ']') {
            continue;
        }
        set_error(error, "expected ',' or ']' in field: " + key);
        return false;
    }

    set_error(error, "unterminated integer array field: " + key);
    return false;
}

static size_t count_occurrences(const std::string& text,
                                const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        count++;
        pos += needle.size();
    }
    return count;
}

bool vis_compare_parse_run_attestation(
    const std::string& text,
    vis_compare_profile_result_t* result,
    std::string* error
) {
    if (result == nullptr) {
        set_error(error, "result output is null");
        return false;
    }
    *result = vis_compare_profile_result_t{};

    if (!parse_u32_array_field(text, "assigned_cpus",
                               &result->assigned_cpus, error) ||
        !parse_int_field(text, "exit_code", &result->exit_code, error) ||
        !parse_string_field(text, "verdict", &result->verdict, error)) {
        return false;
    }

    result->affinity_escape_count =
        count_occurrences(text, "\"escaped_cpus\"");
    result->warning_count =
        count_occurrences(text, "\"severity\": \"warning\"");
    return true;
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

std::string vis_compare_to_json(const vis_compare_report_t& report) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"vis_compare_report\": {\n";
    out << "    \"schema_version\": \"0.1\",\n";
    out << "    \"generator\": \"vis-compare " << VIS_COMPARE_VERSION << "\",\n";
    out << "    \"policy_source\": \"" << json_escape(report.policy_path) << "\",\n";
    out << "    \"workload\": \"" << json_escape(report.workload) << "\",\n";
    out << "    \"profiles\": [\n";
    for (size_t i = 0; i < report.profiles.size(); i++) {
        const auto& p = report.profiles[i];
        out << "      {\"profile_source\": \""
            << json_escape(p.profile_source)
            << "\", \"attestation_path\": \""
            << json_escape(p.attestation_path)
            << "\", \"assigned_cpus\": ";
        write_json_u32_array(out, p.assigned_cpus);
        out << ", \"exit_code\": " << p.exit_code
            << ", \"duration_ms\": " << p.duration_ms
            << ", \"verdict\": \"" << json_escape(p.verdict)
            << "\", \"affinity_escape_count\": "
            << p.affinity_escape_count
            << ", \"warning_count\": " << p.warning_count
            << "}";
        out << (i + 1 == report.profiles.size() ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"recommendation\": \""
        << json_escape(report.recommendation) << "\"\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

std::string vis_compare_to_markdown(const vis_compare_report_t& report) {
    std::ostringstream out;
    out << "# VIS Compare AI Context\n\n";
    out << "Policy source: `" << report.policy_path << "`\n\n";
    out << "Workload: `" << report.workload << "`\n\n";
    out << "## Profile Comparison\n\n";
    out << "| Profile | CPUs | Exit | Escapes | Duration | Verdict |\n";
    out << "|---|---|---:|---:|---:|---|\n";
    for (const auto& p : report.profiles) {
        out << "| " << p.profile_source
            << " | " << vis_compare_join_cpus(p.assigned_cpus)
            << " | " << p.exit_code
            << " | " << p.affinity_escape_count
            << " | " << p.duration_ms << " ms"
            << " | " << p.verdict << " |\n";
    }
    out << "\n## Recommendation\n\n";
    out << report.recommendation << "\n\n";
    out << "Interpret this as runtime-control evidence. VIS Compare Lite "
        << "does not yet parse application-level latency, FPS, tok/s, or "
        << "domain-specific benchmark metrics.\n";
    return out.str();
}

bool vis_compare_write_file(const char* path,
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

std::string vis_compare_join_cpus(const std::vector<uint32_t>& cpus) {
    std::string out;
    for (size_t i = 0; i < cpus.size(); i++) {
        if (i != 0) out += ",";
        out += std::to_string(cpus[i]);
    }
    return out.empty() ? "none" : out;
}

std::string vis_compare_join_argv(const std::vector<std::string>& argv) {
    std::string out;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i != 0) out += " ";
        out += argv[i];
    }
    return out;
}
