/**
 * doctor.cpp
 *
 * VIS Doctor implementation.
 *
 * License: MIT
 */

#include "../include/doctor.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cpuid.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

static std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                             value.back() == ' '  || value.back() == '\t')) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() &&
           (value[start] == ' ' || value[start] == '\t')) {
        start++;
    }
    return value.substr(start);
}

static std::string read_text(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return trim(ss.str());
}

static uint64_t read_u64(const std::string& path) {
    std::string text = read_text(path);
    if (text.empty()) return 0;
    char* end = nullptr;
    errno = 0;
    unsigned long long value = strtoull(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str()) return 0;
    return static_cast<uint64_t>(value);
}

static bool path_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static bool path_readable(const std::string& path) {
    return access(path.c_str(), R_OK) == 0;
}

static bool text_contains(const std::string& text,
                          const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

static bool parse_number_after_key(const std::string& text,
                                   size_t start,
                                   const std::string& key,
                                   double* out) {
    if (out == nullptr) return false;
    size_t key_pos = text.find(key, start);
    if (key_pos == std::string::npos) return false;
    size_t colon = text.find(':', key_pos + key.size());
    if (colon == std::string::npos) return false;
    const char* begin = text.c_str() + colon + 1;
    char* end = nullptr;
    errno = 0;
    double value = strtod(begin, &end);
    if (errno != 0 || end == begin) return false;
    *out = value;
    return true;
}

static bool parse_u32_after_key(const std::string& text,
                                size_t start,
                                const std::string& key,
                                uint32_t* out) {
    double value = 0.0;
    if (out == nullptr || !parse_number_after_key(text, start, key, &value)) {
        return false;
    }
    if (value < 0.0 || value > UINT32_MAX) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

static bool executable_in_path(const char* name) {
    if (name == nullptr || *name == '\0') return false;
    const char* path_env = getenv("PATH");
    if (path_env == nullptr) return false;

    std::string paths(path_env);
    size_t start = 0;
    while (start <= paths.size()) {
        size_t end = paths.find(':', start);
        std::string dir = paths.substr(start, end == std::string::npos
            ? std::string::npos
            : end - start);
        if (dir.empty()) dir = ".";
        std::string candidate = dir + "/" + name;
        if (access(candidate.c_str(), X_OK) == 0) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

static bool cpuid_rdtscp_supported() {
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    return __get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx) &&
        ((edx & (1u << 27)) != 0);
}

static bool msr_device_available_for_any_cpu() {
    return path_exists("/dev/cpu/0/msr");
}

static bool container_detected() {
    if (path_exists("/.dockerenv") || path_exists("/run/.containerenv")) {
        return true;
    }

    std::string cgroup = read_text("/proc/1/cgroup");
    return text_contains(cgroup, "docker") ||
        text_contains(cgroup, "kubepods") ||
        text_contains(cgroup, "containerd") ||
        text_contains(cgroup, "lxc");
}

static bool hypervisor_detected() {
    return text_contains(read_text("/proc/cpuinfo"), "hypervisor");
}

static vis_doctor_environment_t detect_environment() {
    vis_doctor_environment_t env{};
    env.root_user = geteuid() == 0;
    env.msr_device_available = msr_device_available_for_any_cpu();
    env.rdtscp_supported = cpuid_rdtscp_supported();
    env.hypervisor_detected = hypervisor_detected();
    env.container_detected = container_detected();

    if (env.container_detected) {
        env.mode = "container";
        env.limitations.push_back(
            "Containerized environments may hide host CPU, MSR, and scheduler evidence.");
    } else if (env.hypervisor_detected) {
        env.mode = "virtualized";
        env.limitations.push_back(
            "Virtualized environments may expose emulated or filtered hardware evidence.");
    } else {
        env.mode = "bare_metal";
    }

    if (!env.root_user) {
        env.limitations.push_back(
            "Current process is not root; MSR-backed SMI scans may require sudo or CAP_SYS_RAWIO.");
    }
    if (!env.msr_device_available) {
        env.limitations.push_back(
            "MSR device is not available; load the msr module and run privileged scans for SMI evidence.");
    }
    if (!env.rdtscp_supported) {
        env.limitations.push_back(
            "RDTSCP is not available; VIS Core jitter measurement cannot use its preferred timing path.");
    }

    if (env.mode == "bare_metal" && env.root_user &&
        env.msr_device_available && env.rdtscp_supported) {
        env.evidence_quality = "strong";
    } else if (env.mode == "bare_metal" && env.rdtscp_supported) {
        env.evidence_quality = "partial";
    } else if (env.rdtscp_supported) {
        env.evidence_quality = "limited";
    } else {
        env.evidence_quality = "unavailable";
    }

    env.reasons.push_back("mode=" + env.mode);
    env.reasons.push_back(std::string("root_user=") +
                          (env.root_user ? "yes" : "no"));
    env.reasons.push_back(std::string("msr_device_available=") +
                          (env.msr_device_available ? "yes" : "no"));
    env.reasons.push_back(std::string("rdtscp_supported=") +
                          (env.rdtscp_supported ? "yes" : "no"));
    return env;
}

static vis_doctor_sensor_t make_sensor(const std::string& name,
                                       bool available,
                                       const std::string& quality,
                                       const std::string& source,
                                       const std::vector<std::string>& limitations) {
    vis_doctor_sensor_t sensor{};
    sensor.name = name;
    sensor.available = available;
    sensor.quality = quality;
    sensor.source = source;
    sensor.limitations = limitations;
    return sensor;
}

static vis_doctor_sensor_t detect_msr_sensor(const vis_doctor_environment_t& env) {
    std::vector<std::string> limitations;
    if (!env.msr_device_available) {
        limitations.push_back("MSR device is not available at /dev/cpu/0/msr.");
    }
    if (!env.root_user) {
        limitations.push_back(
            "Current process is not root; MSR reads may require sudo or CAP_SYS_RAWIO.");
    }
    if (!env.rdtscp_supported) {
        limitations.push_back(
            "RDTSCP is unavailable, so VIS Core cannot use its preferred x86 timing path.");
    }

    std::string quality = "unavailable";
    if (env.root_user && env.msr_device_available && env.rdtscp_supported) {
        quality = "strong";
    } else if (env.msr_device_available || env.rdtscp_supported) {
        quality = "partial";
    }

    return make_sensor("msr",
                       env.msr_device_available,
                       quality,
                       "IA32_SMI_COUNT via /dev/cpu/*/msr",
                       limitations);
}

static vis_doctor_sensor_t detect_path_sensor(const std::string& name,
                                              const std::string& path,
                                              const std::string& source) {
    std::vector<std::string> limitations;
    bool exists = path_exists(path);
    bool readable = path_readable(path);
    if (!exists) {
        limitations.push_back(path + " is not present.");
    } else if (!readable) {
        limitations.push_back(path + " is present but not readable.");
    }

    std::string quality = "unavailable";
    if (exists && readable) {
        quality = "strong";
    } else if (exists) {
        quality = "limited";
    }

    return make_sensor(name, exists && readable, quality, source, limitations);
}

static vis_doctor_sensor_t detect_tracefs_sensor() {
    std::vector<std::string> limitations;
    const std::string primary = "/sys/kernel/tracing";
    const std::string fallback = "/sys/kernel/debug/tracing";
    std::string source;

    if (path_exists(primary)) {
        source = primary;
    } else if (path_exists(fallback)) {
        source = fallback;
    } else {
        source = "tracefs";
        limitations.push_back(
            "tracefs is not mounted at /sys/kernel/tracing or /sys/kernel/debug/tracing.");
        return make_sensor("tracefs", false, "unavailable", source, limitations);
    }

    bool readable = path_readable(source);
    if (!readable) {
        limitations.push_back(source + " is present but not readable by this process.");
    } else {
        limitations.push_back(
            "tracefs is visible, but VIS Doctor does not enable or read tracing data yet.");
    }

    return make_sensor("tracefs",
                       readable,
                       readable ? "partial" : "limited",
                       source,
                       limitations);
}

static vis_doctor_sensor_t detect_tool_sensor(const std::string& name) {
    bool available = executable_in_path(name.c_str());
    std::vector<std::string> limitations;
    if (!available) {
        limitations.push_back(name + " was not found in PATH.");
    } else {
        limitations.push_back(
            name + " was found in PATH, but VIS Doctor does not execute or import it yet.");
    }

    return make_sensor(name,
                       available,
                       available ? "partial" : "unavailable",
                       name,
                       limitations);
}

static std::vector<vis_doctor_sensor_t> detect_sensors(const vis_doctor_environment_t& env) {
    std::vector<vis_doctor_sensor_t> sensors;
    sensors.push_back(detect_msr_sensor(env));
    sensors.push_back(detect_path_sensor("sysfs",
        "/sys/devices/system/cpu",
        "/sys/devices/system/cpu"));
    sensors.push_back(detect_path_sensor("procfs", "/proc", "/proc"));
    sensors.push_back(detect_tracefs_sensor());
    sensors.push_back(detect_tool_sensor("rtla"));
    sensors.push_back(detect_tool_sensor("perf"));
    return sensors;
}

static bool cpu_dir_id(const std::string& name, uint32_t* out) {
    if (name.rfind("cpu", 0) != 0 || name.size() <= 3) return false;
    for (size_t i = 3; i < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9') return false;
    }
    char* end = nullptr;
    unsigned long value = strtoul(name.c_str() + 3, &end, 10);
    if (*end != '\0' || value > UINT32_MAX) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

static std::vector<uint32_t> online_cpus() {
    std::vector<uint32_t> cpus;
    FILE* pipe = popen("ls /sys/devices/system/cpu", "r");
    if (pipe == nullptr) return cpus;

    char buf[128];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        uint32_t id = 0;
        if (cpu_dir_id(trim(buf), &id)) {
            std::string online_path =
                "/sys/devices/system/cpu/cpu" + std::to_string(id) + "/online";
            std::string online = read_text(online_path);
            if (id == 0 || online == "1") {
                cpus.push_back(id);
            }
        }
    }
    pclose(pipe);
    std::sort(cpus.begin(), cpus.end());
    return cpus;
}

static uint32_t cpu_numa_node(uint32_t cpu) {
    FILE* pipe = popen("find /sys/devices/system/node -maxdepth 2 -name cpulist 2>/dev/null", "r");
    if (pipe == nullptr) return 0;

    char path[256];
    while (fgets(path, sizeof(path), pipe) != nullptr) {
        std::string p = trim(path);
        std::string list = read_text(p);
        std::string node_tag = "/node";
        size_t pos = p.find(node_tag);
        if (pos == std::string::npos) continue;
        uint32_t node = static_cast<uint32_t>(strtoul(p.c_str() + pos + node_tag.size(), nullptr, 10));

        std::stringstream ss(list);
        std::string part;
        while (std::getline(ss, part, ',')) {
            size_t dash = part.find('-');
            if (dash == std::string::npos) {
                if (static_cast<uint32_t>(strtoul(part.c_str(), nullptr, 10)) == cpu) {
                    pclose(pipe);
                    return node;
                }
            } else {
                uint32_t start = static_cast<uint32_t>(strtoul(part.substr(0, dash).c_str(), nullptr, 10));
                uint32_t end = static_cast<uint32_t>(strtoul(part.substr(dash + 1).c_str(), nullptr, 10));
                if (cpu >= start && cpu <= end) {
                    pclose(pipe);
                    return node;
                }
            }
        }
    }
    pclose(pipe);
    return 0;
}

static std::string now_iso8601() {
    char buf[32];
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    if (utc == nullptr) return "unknown";
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", utc);
    return buf;
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

static std::string join_cpus(const std::vector<uint32_t>& cpus) {
    std::string out;
    for (size_t i = 0; i < cpus.size(); i++) {
        if (i != 0) out += ",";
        out += std::to_string(cpus[i]);
    }
    return out.empty() ? "none" : out;
}

static std::vector<uint32_t> sorted_unique(std::vector<uint32_t> cpus) {
    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    return cpus;
}

static std::string join_strings(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); i++) {
        if (i != 0) out += ", ";
        out += values[i];
    }
    return out.empty() ? "none" : out;
}

static std::string join_sensor_status(const std::vector<vis_doctor_sensor_t>& sensors) {
    std::string out;
    for (size_t i = 0; i < sensors.size(); i++) {
        if (i != 0) out += " ";
        out += sensors[i].name;
        out += "=";
        out += sensors[i].available ? "yes" : "no";
    }
    return out.empty() ? "none" : out;
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

static std::vector<vis_doctor_scan_t> clean_higher_throughput_scans(
    const vis_doctor_report_t* report
) {
    std::vector<vis_doctor_scan_t> out;
    if (report == nullptr) return out;
    for (const auto& scan : report->scans) {
        if (scan.clean_candidate &&
            scan.throughput_class != "lower_throughput_class") {
            out.push_back(scan);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.cpu_id < b.cpu_id;
    });
    return out;
}

static std::vector<uint32_t> first_cpu_ids(
    const std::vector<vis_doctor_scan_t>& scans,
    size_t limit
) {
    std::vector<uint32_t> ids;
    for (size_t i = 0; i < scans.size() && i < limit; i++) {
        ids.push_back(scans[i].cpu_id);
    }
    return ids;
}

static const vis_doctor_cpu_t* find_cpu_info(
    const vis_doctor_report_t* report,
    uint32_t cpu_id
) {
    if (report == nullptr) return nullptr;
    for (const auto& cpu : report->machine.cpus) {
        if (cpu.id == cpu_id) return &cpu;
    }
    return nullptr;
}

static std::string physical_core_key(
    const vis_doctor_report_t* report,
    uint32_t cpu_id
) {
    const vis_doctor_cpu_t* cpu = find_cpu_info(report, cpu_id);
    if (cpu == nullptr) return "cpu:" + std::to_string(cpu_id);
    if (!cpu->siblings.empty()) {
        return "node:" + std::to_string(cpu->numa_node) +
            ":siblings:" + cpu->siblings;
    }
    return "node:" + std::to_string(cpu->numa_node) +
        ":core:" + std::to_string(cpu->core_id);
}

static std::vector<uint32_t> sibling_aware_primary_candidates(
    const vis_doctor_report_t* report,
    size_t limit
) {
    std::vector<uint32_t> ids;
    std::set<std::string> used_physical_cores;
    for (const auto& scan : clean_higher_throughput_scans(report)) {
        std::string key = physical_core_key(report, scan.cpu_id);
        if (used_physical_cores.insert(key).second) {
            ids.push_back(scan.cpu_id);
        }
        if (ids.size() >= limit) break;
    }
    return ids;
}

static void add_recommendation(
    vis_doctor_report_t* report,
    const std::string& risk,
    const std::string& action,
    const std::string& reason,
    const std::string& why_it_matters,
    const std::string& safe_suggestion,
    const std::string& advanced_suggestion,
    const std::string& advanced_risk,
    const std::string& expected_effect,
    const std::string& validation_command
) {
    if (report == nullptr) return;
    report->recommendations.push_back({
        risk,
        action,
        reason,
        why_it_matters,
        safe_suggestion,
        advanced_suggestion,
        advanced_risk,
        expected_effect,
        validation_command
    });
}

static bool has_governor(const vis_doctor_report_t* report,
                         const std::string& governor) {
    if (report == nullptr) return false;
    for (const auto& cpu : report->machine.cpus) {
        if (cpu.governor == governor) return true;
    }
    return false;
}

static void add_warning_once(std::vector<std::string>* warnings,
                             const std::string& warning) {
    if (warnings == nullptr) return;
    if (std::find(warnings->begin(), warnings->end(), warning) ==
        warnings->end()) {
        warnings->push_back(warning);
    }
}

static void add_environment_recommendations(vis_doctor_report_t* report) {
    if (report == nullptr) return;

    if (report->environment.evidence_quality == "limited" ||
        report->environment.evidence_quality == "unavailable") {
        add_recommendation(report, "safe",
            "Interpret hardware evidence carefully in this environment.",
            "VIS detected limited access to hardware-level evidence.",
            "Virtualized, containerized, or restricted environments can hide or filter MSR, SMI, and topology signals.",
            "Use VIS reports as runtime evidence for this environment, not as full bare-metal hardware proof.",
            "Repeat the same scan on bare metal or a trusted self-hosted runner when hardware proof matters.",
            "Assuming cloud/container evidence is equivalent to bare-metal evidence can lead to wrong placement decisions.",
            "Clearer separation between runtime behavior evidence and hardware-level evidence.",
            "cat /proc/cpuinfo | grep -i hypervisor; test -e /.dockerenv; test -e /dev/cpu/0/msr");
    }

    if (report->machine.smt_active) {
        add_recommendation(report, "advanced",
            "Account for SMT sibling noise before pinning critical work.",
            "SMT siblings share physical-core resources, so a noisy sibling can affect a clean-looking logical CPU.",
            "This matters most for latency-sensitive robotics, trading, audio, gaming, and inference loops.",
            "When testing a critical thread, keep its sibling idle or pin only non-critical work there.",
            "Disable SMT in firmware or isolate both sibling threads for a dedicated critical core profile.",
            "Disabling SMT or isolating siblings can reduce total throughput and may change power, heat, and responsiveness.",
            "Cleaner physical-core behavior for latency-sensitive validation.",
            "lscpu -e=CPU,CORE,SOCKET,NODE");
    }

    if (has_governor(report, "powersave")) {
        add_recommendation(report, "advanced",
            "Check CPU governor before comparing performance runs.",
            "The powersave governor may change frequency behavior during latency and throughput tests.",
            "Governor drift can make two otherwise identical VIS reports look different.",
            "Use AC power/performance mode from the desktop or vendor power profile, then rerun VIS Doctor.",
            "Temporarily test the performance governor for controlled benchmarking.",
            "Performance governor can increase power draw, heat, fan noise, and battery drain; avoid applying it blindly on laptops.",
            "More comparable benchmark runs and fewer frequency-policy surprises.",
            "cat /sys/devices/system/cpu/cpufreq/policy*/scaling_governor");
    }

    if (report->machine.hugepages_total == 0) {
        add_recommendation(report, "safe",
            "Treat HugePages as a future VIS-Mem/inference topic, not a CPU-jitter blocker.",
            "No persistent HugePages are configured, but VIS Core CPU jitter can still be valid.",
            "This separates CPU determinism evidence from later memory-placement optimization.",
            "Do not change HugePages just to fix CPU jitter; collect baseline reports first.",
            "For LLM/inference benchmarks, test a separate HugePages profile and compare before/after.",
            "HugePages reserve memory and can break or slow other workloads if sized badly.",
            "Keeps V1 CPU evidence clean while preserving a path for VIS-Mem experiments.",
            "grep -E 'HugePages_Total|HugePages_Free|Hugepagesize' /proc/meminfo");
    }

    if (!report->machine.isolated_cpus.empty()) {
        add_recommendation(report, "advanced",
            "Validate the existing isolated CPU profile with workload-specific scans.",
            "Isolation is already configured, but isolation alone does not prove the CPU is clean.",
            "VIS should verify that the configured isolated CPU is actually useful for the target workload.",
            "Run VIS Doctor and the real benchmark on the isolated CPU before changing boot parameters.",
            "Tune isolcpus/nohz_full/IRQ affinity as a named profile only after repeated evidence.",
            "Boot-parameter and IRQ-affinity mistakes can make the desktop less responsive or hide devices from normal scheduling.",
            "Turns existing isolation into measured evidence instead of an assumption.",
            "cat /sys/devices/system/cpu/isolated && cat /sys/devices/system/cpu/nohz_full");
    }
}

int vis_doctor_inspect(vis_doctor_report_t* report) {
    if (report == nullptr) return -1;
    *report = vis_doctor_report_t{};
    report->duration_sec = 0;
    report->threshold_ns = VIS_DEFAULT_THRESHOLD_NS;

    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        report->machine.hostname = hostname;
    }

    struct utsname uts;
    if (uname(&uts) == 0) {
        report->machine.kernel = std::string(uts.sysname) + " " + uts.release;
    }

    report->machine.generated_at = now_iso8601();
    report->environment = detect_environment();
    report->sensors = detect_sensors(report->environment);
    report->machine.smt_active =
        read_text("/sys/devices/system/cpu/smt/active") == "1";
    report->machine.isolated_cpus = read_text("/sys/devices/system/cpu/isolated");
    report->machine.nohz_full_cpus = read_text("/sys/devices/system/cpu/nohz_full");
    report->machine.hugepages_total = read_u64("/proc/sys/vm/nr_hugepages");

    std::ifstream meminfo("/proc/meminfo");
    std::string key;
    uint64_t value = 0;
    std::string unit;
    while (meminfo >> key >> value >> unit) {
        if (key == "HugePages_Free:") report->machine.hugepages_free = value;
        if (key == "Mlocked:") report->machine.mlocked_kb = value;
    }

    for (uint32_t cpu : online_cpus()) {
        vis_doctor_cpu_t item{};
        item.id = cpu;
        item.online = true;
        item.numa_node = cpu_numa_node(cpu);
        std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
        item.core_id = static_cast<uint32_t>(read_u64(base + "/topology/core_id"));
        item.siblings = read_text(base + "/topology/thread_siblings_list");
        std::string policy = "/sys/devices/system/cpu/cpufreq/policy" + std::to_string(cpu);
        item.governor = read_text(policy + "/scaling_governor");
        item.scaling_driver = read_text(policy + "/scaling_driver");
        item.current_freq_khz = read_u64(policy + "/scaling_cur_freq");
        item.max_freq_khz = read_u64(policy + "/cpuinfo_max_freq");
        report->machine.cpus.push_back(item);
    }

    vis_doctor_analyze(report);
    return 0;
}

int vis_doctor_scan_all(uint32_t duration_sec, double threshold_ns,
                        vis_doctor_report_t* report) {
    if (report == nullptr || duration_sec == 0 || threshold_ns < 0.0) return -1;
    if (vis_doctor_inspect(report) < 0) return -1;
    report->duration_sec = duration_sec;
    report->threshold_ns = threshold_ns;
    report->scan_ran = true;

    for (size_t i = 0; i < report->machine.cpus.size(); i++) {
        const auto& cpu = report->machine.cpus[i];
        printf("[vis-doctor] scanning CPU %u (%zu/%zu) for %u seconds...\n",
               cpu.id, i + 1, report->machine.cpus.size(), duration_sec);
        fflush(stdout);

        vis_doctor_scan_t scan{};
        scan.cpu_id = cpu.id;
        scan.scanned = true;
        vis_report_t jitter_report;
        scan.status = vis_jitter_run(cpu.id, duration_sec, threshold_ns,
                                     nullptr, nullptr, &jitter_report);
        if (scan.status == vis_status_t::VIS_OK) {
            const vis_results_t& r = jitter_report.results;
            const vis_smi_audit_t& s = jitter_report.smi_audit;
            const vis_latency_t& l = r.latency_ns;
            scan.accepted_samples = r.samples_accepted;
            scan.contaminated_windows = s.contaminated_windows;
            scan.msr_delta = s.msr_delta;
            scan.samples_rejected = s.samples_rejected;
            scan.core_migration_rejected = r.core_migration_rejected;
            scan.p50_ns = l.p50_ns;
            scan.p99_ns = l.p99_ns;
            scan.p99_9_ns = l.p99_9_ns;
            scan.p99_99_ns = l.p99_99_ns;
            scan.max_ns = l.max_ns;
            scan.pass = r.determinism_pass;
            scan.accepted_per_sec =
                static_cast<double>(r.samples_accepted) / duration_sec;
            scan.clean_candidate = scan.pass &&
                scan.contaminated_windows == 0 &&
                scan.core_migration_rejected == 0;
        }
        report->scans.push_back(scan);
    }

    vis_doctor_analyze(report);
    return 0;
}

int vis_doctor_load_baseline(const char* path, vis_doctor_report_t* report) {
    if (path == nullptr || path[0] == '\0' || report == nullptr) return -1;

    std::string text = read_text(path);
    if (text.empty()) return -1;

    std::map<uint32_t, double> current_rates;
    for (const auto& scan : report->scans) {
        current_rates[scan.cpu_id] = scan.accepted_per_sec;
    }

    vis_doctor_baseline_t baseline{};
    baseline.path = path;

    size_t pos = 0;
    while (true) {
        size_t cpu_pos = text.find("\"cpu\"", pos);
        if (cpu_pos == std::string::npos) break;

        size_t object_end = text.find('}', cpu_pos);
        if (object_end == std::string::npos) break;

        uint32_t cpu_id = 0;
        double baseline_rate = 0.0;
        if (parse_u32_after_key(text, cpu_pos, "\"cpu\"", &cpu_id) &&
            parse_number_after_key(text, cpu_pos,
                                   "\"accepted_per_sec\"",
                                   &baseline_rate) &&
            baseline_rate > 0.0) {
            auto current = current_rates.find(cpu_id);
            if (current != current_rates.end()) {
                vis_doctor_baseline_cpu_t item{};
                item.cpu_id = cpu_id;
                item.baseline_accepted_per_sec = baseline_rate;
                item.current_accepted_per_sec = current->second;
                item.drop_ratio = current->second >= baseline_rate
                    ? 0.0
                    : 1.0 - (current->second / baseline_rate);
                baseline.cpus.push_back(item);
            }
        }
        pos = object_end + 1;
    }

    if (baseline.cpus.empty()) return -1;

    double baseline_sum = 0.0;
    double current_sum = 0.0;
    for (const auto& item : baseline.cpus) {
        baseline_sum += item.baseline_accepted_per_sec;
        current_sum += item.current_accepted_per_sec;
        if (item.drop_ratio >= 0.40) {
            baseline.affected_cpus.push_back(item.cpu_id);
        }
    }

    baseline.available = true;
    baseline.compared_cpus = static_cast<uint32_t>(baseline.cpus.size());
    baseline.global_accepted_per_sec_drop_ratio =
        (baseline_sum > 0.0 && current_sum < baseline_sum)
            ? 1.0 - (current_sum / baseline_sum)
            : 0.0;
    baseline.pressure_detected =
        baseline.global_accepted_per_sec_drop_ratio >= 0.40 ||
        !baseline.affected_cpus.empty();
    baseline.affected_cpus = sorted_unique(baseline.affected_cpus);

    report->baseline = baseline;
    return 0;
}

void vis_doctor_analyze(vis_doctor_report_t* report) {
    if (report == nullptr) return;
    report->findings.clear();
    report->recommendations.clear();
    report->runtime_policy = vis_doctor_runtime_policy_t{};

    if (report->environment.mode != "bare_metal") {
        report->findings.push_back({"warning", "environment",
            "Hardware evidence is limited in this environment.",
            "mode=" + report->environment.mode +
                ", quality=" + report->environment.evidence_quality,
            {}});
        add_warning_once(&report->runtime_policy.warnings,
                         "limited_hardware_evidence");
    } else if (report->environment.evidence_quality != "strong") {
        report->findings.push_back({"info", "environment",
            "Hardware evidence is not fully available yet.",
            "quality=" + report->environment.evidence_quality +
                ", root=" + (report->environment.root_user ? std::string("yes") : std::string("no")) +
                ", msr=" + (report->environment.msr_device_available ? std::string("yes") : std::string("no")),
            {}});
        add_warning_once(&report->runtime_policy.warnings,
                         "partial_hardware_evidence");
    }

    if (report->machine.smt_active) {
        report->findings.push_back({"info", "cpu_topology",
            "SMT is active on this system.",
            "Thread sibling lists should be considered before choosing a critical CPU.",
            {}});
        add_warning_once(&report->runtime_policy.warnings, "smt_active");
    }
    if (has_governor(report, "powersave")) {
        report->findings.push_back({"info", "cpu_frequency",
            "At least one CPU is using the powersave governor.",
            "Governor drift can change latency and throughput between runs.",
            {}});
        add_warning_once(&report->runtime_policy.warnings, "powersave_governor");
    }
    if (!report->machine.isolated_cpus.empty()) {
        report->findings.push_back({"info", "isolation",
            "CPU isolation is configured.",
            "isolated=" + report->machine.isolated_cpus +
                ", nohz_full=" + report->machine.nohz_full_cpus,
            {}});
        add_warning_once(&report->runtime_policy.warnings, "existing_isolation");
    }
    if (report->machine.hugepages_total == 0) {
        report->findings.push_back({"info", "memory",
            "No persistent HugePages are configured.",
            "HugePages_Total is 0; VIS-Mem may recommend this later for memory-heavy workloads.",
            {}});
        add_warning_once(&report->runtime_policy.warnings, "no_persistent_hugepages");
    }
    add_environment_recommendations(report);

    if (!report->scan_ran) {
        add_recommendation(report, "safe",
            "Run an SMI-aware all-core scan.",
            "Inspection cannot rank clean CPUs without jitter measurements.",
            "VIS Doctor needs timing evidence before it can separate clean CPU choices from risky ones.",
            "Run the scan and share doctor.json or doctor.md with a reviewer/AI assistant.",
            "None before measurement; do not change BIOS, governor, or isolation settings yet.",
            "Changing system settings before a baseline can hide the original cause.",
            "Produces per-CPU latency, SMI, and throughput evidence.",
            "sudo ./vis-doctor --scan --duration 30 --threshold 100 --output doctor.json --llm doctor.md");
        return;
    }

    std::vector<uint32_t> contaminated;
    double max_rate = 0.0;
    for (const auto& s : report->scans) {
        if (s.accepted_per_sec > max_rate) max_rate = s.accepted_per_sec;
        if (s.contaminated_windows > 0) contaminated.push_back(s.cpu_id);
    }

    for (auto& s : report->scans) {
        if (max_rate > 0.0 && s.accepted_per_sec < max_rate * 0.50) {
            s.throughput_class = "lower_throughput_class";
        } else {
            s.throughput_class = "higher_throughput_class";
        }
    }

    if (!contaminated.empty()) {
        report->findings.push_back({"warning", "smi",
            "Some CPUs had SMI-contaminated measurement windows.",
            "Affected CPUs: " + join_cpus(contaminated),
            contaminated});
    }

    std::vector<uint32_t> lower;
    for (const auto& s : report->scans) {
        if (s.throughput_class == "lower_throughput_class") {
            lower.push_back(s.cpu_id);
        }
    }
    if (!lower.empty()) {
        report->findings.push_back({"info", "cpu_topology",
            "Some CPUs show lower accepted-sample throughput.",
            "This may indicate a lower-throughput core class. CPUs: " + join_cpus(lower),
            lower});
    }

    if (report->baseline.available && report->baseline.pressure_detected) {
        std::ostringstream evidence;
        evidence << "Baseline " << report->baseline.path
                 << ", compared_cpus=" << report->baseline.compared_cpus
                 << ", global_drop="
                 << static_cast<int>(report->baseline.global_accepted_per_sec_drop_ratio * 100.0)
                 << "%";
        if (!report->baseline.affected_cpus.empty()) {
            evidence << ", affected CPUs: "
                     << join_cpus(report->baseline.affected_cpus);
        }
        report->findings.push_back({"warning", "cpu_pressure",
            "Accepted sample throughput dropped compared to baseline.",
            evidence.str(),
            report->baseline.affected_cpus});
    }

    std::vector<uint32_t> primary =
        sibling_aware_primary_candidates(report, 8);
    std::vector<uint32_t> all_clean =
        first_cpu_ids(clean_higher_throughput_scans(report), 12);
    std::vector<uint32_t> avoid = lower;
    avoid.insert(avoid.end(), contaminated.begin(), contaminated.end());
    avoid = sorted_unique(avoid);

    report->runtime_policy.available = !primary.empty();
    report->runtime_policy.profile = "strict";
    report->runtime_policy.cpu_policy = "sibling_aware_primary";
    report->runtime_policy.primary_cpus = primary;
    report->runtime_policy.secondary_cpus = all_clean;
    report->runtime_policy.avoid_cpus = avoid;
    report->runtime_policy.contaminated_cpus = contaminated;
    report->runtime_policy.smt_policy = report->machine.smt_active
        ? "avoid_sibling_sharing_for_first_profile"
        : "not_applicable";
    report->runtime_policy.lower_throughput_policy = lower.empty()
        ? "no_lower_throughput_class_detected"
        : "avoid_for_first_profile";
    report->runtime_policy.requires_longer_validation =
        report->duration_sec < 60;
    if (!contaminated.empty()) {
        add_warning_once(&report->runtime_policy.warnings,
                         "smi_contaminated_windows_detected");
    }
    if (!lower.empty()) {
        add_warning_once(&report->runtime_policy.warnings,
                         "lower_throughput_cpu_class_detected");
    }
    if (report->runtime_policy.requires_longer_validation) {
        add_warning_once(&report->runtime_policy.warnings,
                         "short_scan_requires_longer_validation");
    }
    if (report->baseline.available && report->baseline.pressure_detected) {
        add_warning_once(&report->runtime_policy.warnings,
                         "baseline_cpu_pressure_detected");
    }

    add_recommendation(report, "safe",
        "Validate candidate CPUs with a longer scan.",
        "Short all-core scans identify candidates; longer runs improve confidence.",
        "A one-second scan is good for discovery, but SMI and scheduler noise are intermittent.",
        "Repeat the scan for 60 seconds before treating a CPU as a stable critical-workload target.",
        "Run repeated scans under workload, then compare p99/p99.9/SMI windows across reports.",
        "Longer stress runs can heat the machine and change fan/power behavior; interpret changes as part of the system profile.",
        "Confirms SMI cleanliness and tail latency stability.",
        "sudo ./vis-doctor --scan --duration 60 --threshold 100 --output doctor.json --llm doctor.md");

    if (!primary.empty()) {
        add_recommendation(report, "safe",
            "Prefer sibling-aware primary candidate CPUs: " + join_cpus(primary),
            "These CPUs are clean higher-throughput candidates with at most one logical CPU per physical core.",
            "This avoids accidentally treating SMT siblings as independent clean cores for the first critical-workload profile.",
            "Pin only the test process or benchmark to one primary candidate CPU first.",
            "Build a workload-specific affinity policy around primary candidates first, then decide whether sibling logical CPUs are allowed.",
            "Wrong affinity can reduce total throughput, fight the scheduler, or starve other work.",
            "Lower risk placement for latency-sensitive workload validation.",
            "sudo ./vis-jitter --cpu " + std::to_string(primary.front()) +
                " --duration 60 --threshold 100");
    }
    if (all_clean.size() > primary.size()) {
        add_recommendation(report, "safe",
            "Use the full clean logical CPU list as secondary evidence: " + join_cpus(all_clean),
            "The full clean list may include both logical siblings of the same physical core.",
            "Secondary candidates are useful evidence, but primary candidates are cleaner for first placement decisions.",
            "Use the primary list first; use the full clean list when testing sibling-sharing behavior deliberately.",
            "Create separate profiles for exclusive-core and sibling-sharing modes.",
            "Sharing a physical core can increase interference even when each logical CPU measured clean alone.",
            "Clearer distinction between clean logical CPUs and clean physical-core choices.",
            "lscpu -e=CPU,CORE,SOCKET,NODE");
    }
    if (!contaminated.empty()) {
        add_recommendation(report, "advanced",
            "Avoid contaminated CPUs until repeated scans are clean.",
            "SMI contamination invalidates full measurement windows.",
            "The accepted samples may still pass, but contaminated windows prove invisible firmware-level interruption happened nearby.",
            "Repeat the scan after stopping hot-plug, device attach/detach, and heavy background activity.",
            "Investigate BIOS/firmware/device triggers and compare reports before and after each change.",
            "Firmware and BIOS changes can affect boot stability, thermals, battery behavior, and warranty/support posture.",
            "Reduces risk of choosing a noisy critical CPU.",
            "sudo ./vis-jitter --cpu " + std::to_string(contaminated.front()) +
                " --duration 60 --threshold 100");
    }

    if (report->baseline.available && report->baseline.pressure_detected) {
        add_recommendation(report, "safe",
            "Investigate CPU pressure against the saved baseline.",
            "The current scan accepted fewer samples per second than the baseline.",
            "A global or per-CPU throughput drop can mean background load, thermal throttling, governor drift, or sibling contention.",
            "Stop intentional stress loads, keep power mode consistent, and rerun with the same duration before changing settings.",
            "Compare governor, thermals, and workload placement after collecting repeated baseline/current pairs.",
            "Changing CPU policy based on a single loaded scan can hide the actual workload or thermal cause.",
            "Separates real hardware class differences from temporary runtime pressure.",
            "sudo ./vis-doctor --scan --duration 60 --threshold 100 --baseline " +
                report->baseline.path + " --output doctor-pressure.json --llm doctor-pressure.md");
    }

    if (!lower.empty()) {
        add_recommendation(report, "safe",
            "Avoid lower-throughput CPUs for the first critical-workload profile.",
            "Some CPUs accepted far fewer samples per second in this scan.",
            "That class difference can be normal on hybrid or power-managed systems, but it changes timing evidence.",
            "Start validation on the higher-throughput clean candidates, then compare lower-throughput CPUs separately.",
            "Create separate profiles for performance cores, efficiency cores, or power-limited cores if repeated scans confirm the split.",
            "Assuming the wrong core class can hurt latency, throughput, thermals, or battery behavior.",
            "Clearer before/after measurements for workload placement.",
            "sudo ./vis-doctor --scan --duration 60 --threshold 100 --output doctor.json --llm doctor.md");
    }
}

void vis_doctor_print_summary(const vis_doctor_report_t* report) {
    if (report == nullptr) return;
    std::vector<uint32_t> primary =
        sibling_aware_primary_candidates(report, 12);
    std::vector<uint32_t> all_clean =
        first_cpu_ids(clean_higher_throughput_scans(report), 12);

    printf("\nVIS Doctor\n");
    printf("Status: %s\n\n", report->findings.empty() ? "CLEAN" : "CLEAN WITH NOTES");
    printf("Machine: %s | %s\n", report->machine.hostname.c_str(),
           report->machine.kernel.c_str());
    printf("CPUs: %zu online | SMT: %s | isolated: %s | nohz_full: %s\n",
           report->machine.cpus.size(),
           report->machine.smt_active ? "yes" : "no",
           report->machine.isolated_cpus.empty() ? "none" : report->machine.isolated_cpus.c_str(),
           report->machine.nohz_full_cpus.empty() ? "none" : report->machine.nohz_full_cpus.c_str());
    printf("Environment: %s | evidence: %s | MSR: %s | RDTSCP: %s\n",
           report->environment.mode.c_str(),
           report->environment.evidence_quality.c_str(),
           report->environment.msr_device_available ? "yes" : "no",
           report->environment.rdtscp_supported ? "yes" : "no");
    printf("Sensors: %s\n", join_sensor_status(report->sensors).c_str());

    if (report->scan_ran) {
        printf("Primary candidate CPUs: %s\n", join_cpus(primary).c_str());
        printf("All clean higher-throughput CPUs: %s\n\n",
               join_cpus(all_clean).c_str());
    } else {
        printf("Scan: not run. Use --scan with sudo/MSR access for ranking.\n\n");
    }

    printf("Findings:\n");
    for (const auto& f : report->findings) {
        printf("  %s %s: %s\n", f.severity.c_str(), f.category.c_str(),
               f.message.c_str());
    }
    printf("\nRecommendations:\n");
    for (const auto& r : report->recommendations) {
        printf("  %s: %s\n", r.risk.c_str(), r.action.c_str());
    }
    printf("\n");
}

std::string vis_doctor_to_json(const vis_doctor_report_t* report) {
    if (report == nullptr) return "";
    std::ostringstream out;
    out << "{\n  \"vis_doctor_report\": {\n";
    out << "    \"schema_version\": \"0.1\",\n";
    out << "    \"generator\": \"vis-doctor " << VIS_DOCTOR_VERSION << "\",\n";
    out << "    \"generated_at\": \"" << json_escape(report->machine.generated_at) << "\",\n";
    out << "    \"machine\": {\n";
    out << "      \"hostname\": \"" << json_escape(report->machine.hostname) << "\",\n";
    out << "      \"kernel\": \"" << json_escape(report->machine.kernel) << "\",\n";
    out << "      \"logical_cpus\": " << report->machine.cpus.size() << ",\n";
    out << "      \"smt_active\": " << (report->machine.smt_active ? "true" : "false") << ",\n";
    out << "      \"isolated_cpus\": \"" << json_escape(report->machine.isolated_cpus) << "\",\n";
    out << "      \"nohz_full_cpus\": \"" << json_escape(report->machine.nohz_full_cpus) << "\",\n";
    out << "      \"hugepages_total\": " << report->machine.hugepages_total << ",\n";
    out << "      \"hugepages_free\": " << report->machine.hugepages_free << "\n";
    out << "    },\n";
    out << "    \"environment\": {\n";
    out << "      \"mode\": \"" << json_escape(report->environment.mode) << "\",\n";
    out << "      \"evidence_quality\": \""
        << json_escape(report->environment.evidence_quality) << "\",\n";
    out << "      \"root_user\": "
        << (report->environment.root_user ? "true" : "false") << ",\n";
    out << "      \"msr_device_available\": "
        << (report->environment.msr_device_available ? "true" : "false")
        << ",\n";
    out << "      \"rdtscp_supported\": "
        << (report->environment.rdtscp_supported ? "true" : "false")
        << ",\n";
    out << "      \"hypervisor_detected\": "
        << (report->environment.hypervisor_detected ? "true" : "false")
        << ",\n";
    out << "      \"container_detected\": "
        << (report->environment.container_detected ? "true" : "false")
        << ",\n";
    out << "      \"limitations\": ";
    write_json_string_array(out, report->environment.limitations);
    out << ",\n";
    out << "      \"reasons\": ";
    write_json_string_array(out, report->environment.reasons);
    out << "\n";
    out << "    },\n";
    out << "    \"sensors\": {\n";
    for (size_t i = 0; i < report->sensors.size(); i++) {
        const auto& sensor = report->sensors[i];
        out << "      \"" << json_escape(sensor.name) << "\": {\n";
        out << "        \"available\": "
            << (sensor.available ? "true" : "false") << ",\n";
        out << "        \"quality\": \""
            << json_escape(sensor.quality) << "\",\n";
        out << "        \"source\": \""
            << json_escape(sensor.source) << "\",\n";
        out << "        \"limitations\": ";
        write_json_string_array(out, sensor.limitations);
        out << "\n";
        out << "      }";
        out << (i + 1 == report->sensors.size() ? "\n" : ",\n");
    }
    out << "    },\n";
    if (report->baseline.available) {
        out << "    \"baseline_comparison\": {\n";
        out << "      \"path\": \"" << json_escape(report->baseline.path) << "\",\n";
        out << "      \"available\": true,\n";
        out << "      \"compared_cpus\": " << report->baseline.compared_cpus << ",\n";
        out << "      \"global_drop_ratio\": "
            << report->baseline.global_accepted_per_sec_drop_ratio << ",\n";
        out << "      \"pressure_detected\": "
            << (report->baseline.pressure_detected ? "true" : "false") << ",\n";
        out << "      \"affected_cpus\": ";
        write_json_u32_array(out, report->baseline.affected_cpus);
        out << ",\n";
        out << "      \"cpus\": [\n";
        for (size_t i = 0; i < report->baseline.cpus.size(); i++) {
            const auto& c = report->baseline.cpus[i];
            out << "        {\"cpu\": " << c.cpu_id
                << ", \"baseline_accepted_per_sec\": "
                << c.baseline_accepted_per_sec
                << ", \"current_accepted_per_sec\": "
                << c.current_accepted_per_sec
                << ", \"drop_ratio\": " << c.drop_ratio << "}";
            out << (i + 1 == report->baseline.cpus.size() ? "\n" : ",\n");
        }
        out << "      ]\n";
        out << "    },\n";
    }
    if (report->scan_ran) {
        out << "    \"candidate_summary\": {\n";
        out << "      \"sibling_aware_primary\": \""
            << json_escape(join_cpus(sibling_aware_primary_candidates(report, 12)))
            << "\",\n";
        out << "      \"all_clean_higher_throughput\": \""
            << json_escape(join_cpus(first_cpu_ids(clean_higher_throughput_scans(report), 12)))
            << "\"\n";
        out << "    },\n";
        out << "    \"recommended_runtime_policy\": {\n";
        out << "      \"available\": "
            << (report->runtime_policy.available ? "true" : "false") << ",\n";
        out << "      \"profile\": \""
            << json_escape(report->runtime_policy.profile) << "\",\n";
        out << "      \"cpu_policy\": \""
            << json_escape(report->runtime_policy.cpu_policy) << "\",\n";
        out << "      \"primary_cpus\": ";
        write_json_u32_array(out, report->runtime_policy.primary_cpus);
        out << ",\n";
        out << "      \"secondary_cpus\": ";
        write_json_u32_array(out, report->runtime_policy.secondary_cpus);
        out << ",\n";
        out << "      \"avoid_cpus\": ";
        write_json_u32_array(out, report->runtime_policy.avoid_cpus);
        out << ",\n";
        out << "      \"contaminated_cpus\": ";
        write_json_u32_array(out, report->runtime_policy.contaminated_cpus);
        out << ",\n";
        out << "      \"smt_policy\": \""
            << json_escape(report->runtime_policy.smt_policy) << "\",\n";
        out << "      \"lower_throughput_policy\": \""
            << json_escape(report->runtime_policy.lower_throughput_policy) << "\",\n";
        out << "      \"requires_longer_validation\": "
            << (report->runtime_policy.requires_longer_validation ? "true" : "false")
            << ",\n";
        out << "      \"warnings\": ";
        write_json_string_array(out, report->runtime_policy.warnings);
        out << "\n";
        out << "    },\n";
    }
    out << "    \"cpus\": [\n";
    for (size_t i = 0; i < report->machine.cpus.size(); i++) {
        const auto& c = report->machine.cpus[i];
        out << "      {\"id\": " << c.id << ", \"core_id\": " << c.core_id
            << ", \"numa_node\": " << c.numa_node
            << ", \"siblings\": \"" << json_escape(c.siblings)
            << "\", \"governor\": \"" << json_escape(c.governor)
            << "\", \"driver\": \"" << json_escape(c.scaling_driver) << "\"}";
        out << (i + 1 == report->machine.cpus.size() ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"scan\": [\n";
    for (size_t i = 0; i < report->scans.size(); i++) {
        const auto& s = report->scans[i];
        out << "      {\"cpu\": " << s.cpu_id
            << ", \"status\": " << static_cast<int>(s.status)
            << ", \"accepted_samples\": " << s.accepted_samples
            << ", \"accepted_per_sec\": " << s.accepted_per_sec
            << ", \"contaminated_windows\": " << s.contaminated_windows
            << ", \"msr_delta\": " << s.msr_delta
            << ", \"p99\": " << s.p99_ns
            << ", \"p99_9\": " << s.p99_9_ns
            << ", \"p99_99\": " << s.p99_99_ns
            << ", \"max\": " << s.max_ns
            << ", \"clean_candidate\": " << (s.clean_candidate ? "true" : "false")
            << ", \"throughput_class\": \"" << json_escape(s.throughput_class) << "\"}";
        out << (i + 1 == report->scans.size() ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"findings\": [\n";
    for (size_t i = 0; i < report->findings.size(); i++) {
        const auto& f = report->findings[i];
        out << "      {\"severity\": \"" << json_escape(f.severity)
            << "\", \"category\": \"" << json_escape(f.category)
            << "\", \"message\": \"" << json_escape(f.message)
            << "\", \"evidence\": \"" << json_escape(f.evidence) << "\"}";
        out << (i + 1 == report->findings.size() ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"recommendations\": [\n";
    for (size_t i = 0; i < report->recommendations.size(); i++) {
        const auto& r = report->recommendations[i];
        out << "      {\"risk\": \"" << json_escape(r.risk)
            << "\", \"action\": \"" << json_escape(r.action)
            << "\", \"reason\": \"" << json_escape(r.reason)
            << "\", \"why_it_matters\": \"" << json_escape(r.why_it_matters)
            << "\", \"safe_suggestion\": \"" << json_escape(r.safe_suggestion)
            << "\", \"advanced_suggestion\": \"" << json_escape(r.advanced_suggestion)
            << "\", \"advanced_risk\": \"" << json_escape(r.advanced_risk)
            << "\", \"expected_effect\": \"" << json_escape(r.expected_effect)
            << "\", \"validation_command\": \"" << json_escape(r.validation_command) << "\"}";
        out << (i + 1 == report->recommendations.size() ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"ai_context\": \"VIS Doctor reports machine facts, evidence sensors, optional SMI-aware jitter scans, findings, and advisory recommendations. It does not mutate system settings. VIS should align with mature Linux RT/performance tools instead of blindly duplicating their sensors. Treat safe suggestions as low-risk validation steps; advanced suggestions require human review because they may affect power, thermals, boot behavior, scheduler behavior, or workload throughput.\"\n";
    out << "  }\n}\n";
    return out.str();
}

std::string vis_doctor_to_markdown(const vis_doctor_report_t* report) {
    if (report == nullptr) return "";
    std::ostringstream out;
    out << "# VIS Doctor AI Context\n\n";
    out << "Machine: " << report->machine.hostname << " (" << report->machine.kernel << ")\n\n";
    out << "## Summary\n";
    out << "- Online logical CPUs: " << report->machine.cpus.size() << "\n";
    out << "- SMT active: " << (report->machine.smt_active ? "yes" : "no") << "\n";
    out << "- Isolated CPUs: " << (report->machine.isolated_cpus.empty() ? "none" : report->machine.isolated_cpus) << "\n";
    out << "- nohz_full CPUs: " << (report->machine.nohz_full_cpus.empty() ? "none" : report->machine.nohz_full_cpus) << "\n\n";
    out << "## Environment Evidence\n";
    out << "- Mode: " << report->environment.mode << "\n";
    out << "- Hardware evidence: "
        << report->environment.evidence_quality << "\n";
    out << "- MSR device available: "
        << (report->environment.msr_device_available ? "yes" : "no") << "\n";
    out << "- RDTSCP supported: "
        << (report->environment.rdtscp_supported ? "yes" : "no") << "\n";
    out << "- Hypervisor detected: "
        << (report->environment.hypervisor_detected ? "yes" : "no") << "\n";
    out << "- Container detected: "
        << (report->environment.container_detected ? "yes" : "no") << "\n";
    out << "- Reasons: " << join_strings(report->environment.reasons) << "\n";
    out << "- Limitations: "
        << join_strings(report->environment.limitations) << "\n\n";
    out << "## Sensor Evidence\n";
    out << "These are passive availability signals; external tools are not executed or imported yet.\n";
    for (const auto& sensor : report->sensors) {
        out << "- " << sensor.name
            << ": available=" << (sensor.available ? "yes" : "no")
            << ", quality=" << sensor.quality
            << ", source=" << sensor.source
            << ", limitations=" << join_strings(sensor.limitations) << "\n";
    }
    out << "\n";
    if (report->baseline.available) {
        out << "## Baseline Comparison\n";
        out << "- Baseline path: " << report->baseline.path << "\n";
        out << "- Compared CPUs: " << report->baseline.compared_cpus << "\n";
        out << "- Global accepted/s drop: "
            << static_cast<int>(report->baseline.global_accepted_per_sec_drop_ratio * 100.0)
            << "%\n";
        out << "- Pressure detected: "
            << (report->baseline.pressure_detected ? "yes" : "no") << "\n";
        out << "- Affected CPUs: "
            << join_cpus(report->baseline.affected_cpus) << "\n\n";
    }
    if (report->scan_ran) {
        out << "## Candidate Summary\n";
        out << "- Sibling-aware primary CPUs: "
            << join_cpus(sibling_aware_primary_candidates(report, 12)) << "\n";
        out << "- All clean higher-throughput CPUs: "
            << join_cpus(first_cpu_ids(clean_higher_throughput_scans(report), 12)) << "\n\n";
        out << "## Recommended Runtime Policy\n";
        out << "- Available: "
            << (report->runtime_policy.available ? "yes" : "no") << "\n";
        out << "- Profile: " << report->runtime_policy.profile << "\n";
        out << "- CPU policy: " << report->runtime_policy.cpu_policy << "\n";
        out << "- Primary CPUs: "
            << join_cpus(report->runtime_policy.primary_cpus) << "\n";
        out << "- Secondary CPUs: "
            << join_cpus(report->runtime_policy.secondary_cpus) << "\n";
        out << "- Avoid CPUs: "
            << join_cpus(report->runtime_policy.avoid_cpus) << "\n";
        out << "- SMT policy: " << report->runtime_policy.smt_policy << "\n";
        out << "- Lower-throughput policy: "
            << report->runtime_policy.lower_throughput_policy << "\n";
        out << "- Requires longer validation: "
            << (report->runtime_policy.requires_longer_validation ? "yes" : "no") << "\n";
        out << "- Warnings: ";
        for (size_t i = 0; i < report->runtime_policy.warnings.size(); i++) {
            if (i != 0) out << ", ";
            out << report->runtime_policy.warnings[i];
        }
        if (report->runtime_policy.warnings.empty()) out << "none";
        out << "\n\n";
    }
    out << "## Findings\n";
    for (const auto& f : report->findings) {
        out << "- " << f.severity << " / " << f.category << ": "
            << f.message << " Evidence: " << f.evidence << "\n";
    }
    out << "\n## Recommendations\n";
    for (const auto& r : report->recommendations) {
        out << "- " << r.risk << ": " << r.action << "\n";
        out << "  - Reason: " << r.reason << "\n";
        out << "  - Why it matters: " << r.why_it_matters << "\n";
        out << "  - Safe suggestion: " << r.safe_suggestion << "\n";
        out << "  - Advanced suggestion: " << r.advanced_suggestion << "\n";
        out << "  - Advanced risk: " << r.advanced_risk << "\n";
        out << "  - Validate: `" << r.validation_command << "`\n";
    }
    if (report->scan_ran) {
        out << "\n## Scan Evidence\n";
        for (const auto& s : report->scans) {
            out << "- CPU " << s.cpu_id << ": p99=" << s.p99_ns
                << "ns p99.9=" << s.p99_9_ns
                << "ns contaminated_windows=" << s.contaminated_windows
                << " msr_delta=" << s.msr_delta
                << " accepted/s=" << static_cast<uint64_t>(s.accepted_per_sec)
                << " class=" << s.throughput_class << "\n";
        }
    }
    out << "\nInterpret this as advisory evidence. VIS Doctor does not apply system changes. Advanced suggestions require human review before use.\n";
    return out.str();
}

bool vis_doctor_write_file(const char* path, const std::string& content) {
    if (path == nullptr) return false;
    std::ofstream out(path);
    if (!out) return false;
    out << content;
    return true;
}
