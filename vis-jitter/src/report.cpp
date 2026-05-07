/**
 * report.cpp
 *
 * Report generation for vis-jitter.
 * JSON serialization and terminal summary output.
 *
 * License: MIT
 */

#include "../include/report.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

void vis_report_print_summary(const vis_report_t* report) {
    if (report == nullptr) return;

    const vis_results_t*   r = &report->results;
    const vis_smi_audit_t* s = &report->smi_audit;
    const vis_detected_t*  d = &report->detected;

    printf("\n");
    printf("========================================\n");
    printf(" vis-jitter report\n");
    printf("========================================\n");
    printf(" Generated : %s\n", report->generated_at);
    printf(" Report ID : %s\n", report->report_id);
    printf("----------------------------------------\n");
    printf(" System (detected)\n");
    printf("   Core        : %u\n",  d->cpu_core);
    printf("   Frequency   : %.3f GHz\n", d->frequency_ghz);
    printf("   NUMA node   : %u\n",  d->numa_node);
    printf("   SMT active  : %s\n",  d->smt_active        ? "yes" : "no");
    printf("   TSC invar.  : %s\n",  d->tsc_invariant      ? "yes" : "no");
    printf("   RDTSCP      : %s\n",  d->rdtscp_supported   ? "yes" : "no");
    printf("----------------------------------------\n");
    printf(" SMI audit\n");
    printf("   Policy      : %s\n",  s->rejection_policy);
    printf("   Events      : %u\n",  s->events_detected);
    printf("   Rejected    : %llu samples\n",
           (unsigned long long)s->samples_rejected);
    printf("----------------------------------------\n");
    printf(" Latency results\n");
    printf("   Accepted    : %llu samples\n",
           (unsigned long long)r->samples_accepted);
    printf("   Core migr.  : %llu rejected\n",
           (unsigned long long)r->core_migration_rejected);
    printf("   min         : %.1f ns\n", r->latency_ns.min_ns);
    printf("   p50         : %.1f ns\n", r->latency_ns.p50_ns);
    printf("   p99         : %.1f ns\n", r->latency_ns.p99_ns);
    printf("   p99.9       : %.1f ns\n", r->latency_ns.p99_9_ns);
    printf("   p99.99      : %.1f ns\n", r->latency_ns.p99_99_ns);
    printf("   max         : %.1f ns\n", r->latency_ns.max_ns);
    printf("----------------------------------------\n");
    // DESIGN CHOICE: Show all percentiles, even those that fail.
    // Transparency > looking good. If P99.9 fails, the user
    // deserves to know — not just a binary pass/fail.
    if (r->determinism_pass) {
        printf(" VERDICT: PASS — P99 %.1f ns <= threshold %.1f ns\n",
               r->latency_ns.p99_ns, r->threshold_ns);
    } else {
        printf(" VERDICT: FAIL — P99 %.1f ns > threshold %.1f ns\n",
               r->latency_ns.p99_ns, r->threshold_ns);
    }

    printf("========================================\n\n");
}

char* vis_report_to_json(const vis_report_t* report) {
    if (report == nullptr) return nullptr;

    const vis_results_t*   r = &report->results;
    const vis_smi_audit_t* s = &report->smi_audit;
    const vis_detected_t*  d = &report->detected;
    const vis_asserted_t*  a = &report->asserted;

    // Allocate a generous buffer for the JSON output
    // NOTE: V1 uses a fixed 4KB buffer for JSON serialization.
    // This is a deliberate trade-off: simplicity over safety.
    // V2 will switch to dynamic sizing via snprintf(nullptr, 0, ...).
    const size_t buf_size = 4096;
    char* buf = static_cast<char*>(malloc(buf_size));
    if (buf == nullptr) return nullptr;

    snprintf(buf, buf_size,
        "{\n"
        "  \"vis_report\": {\n"
        "    \"schema_version\": \"%s\",\n"
        "    \"report_id\": \"%s\",\n"
        "    \"generated_at\": \"%s\",\n"
        "    \"generator\": \"%s\",\n"
        "    \"system\": {\n"
        "      \"detected\": {\n"
        "        \"cpu_core\": %u,\n"
        "        \"frequency_ghz\": %.3f,\n"
        "        \"numa_node\": %u,\n"
        "        \"smt_active\": %s,\n"
        "        \"tsc_invariant\": %s,\n"
        "        \"rdtscp_supported\": %s\n"
        "      },\n"
        // This separation is VIS's core differentiator.
        // detected = measured and verifiable. asserted = user's claim.
        // A regulator reads this and knows exactly what's proven vs stated.
        "      \"asserted\": {\n"
        "        \"p_state\": \"%s\",\n"
        "        \"c_states_disabled\": \"%s\",\n"
        "        \"hugepages_1gb\": %s,\n"
        "        \"egress_memory\": \"%s\",\n"
        "        \"rx_buffer_memory\": \"%s\",\n"
        "        \"ddio_enabled\": %s\n"
        "      },\n"
        "      \"verification_note\": \"asserted fields are user-supplied; not verified by vis-jitter\"\n"
        "    },\n"
        "    \"smi_audit\": {\n"
        "      \"msr_start\": %llu,\n"
        "      \"msr_end\": %llu,\n"
        "      \"events_detected\": %u,\n"
        "      \"samples_rejected\": %llu,\n"
        "      \"rejection_policy\": \"%s\"\n"
        "    },\n"
        "    \"results\": {\n"
        "      \"samples_accepted\": %llu,\n"
        "      \"core_migration_rejected\": %llu,\n"
        "      \"latency_ns\": {\n"
        "        \"min\": %.1f,\n"
        "        \"p50\": %.1f,\n"
        "        \"p99\": %.1f,\n"
        "        \"p99_9\": %.1f,\n"
        "        \"p99_99\": %.1f,\n"
        "        \"max\": %.1f\n"
        "      },\n"
        "      \"determinism_verdict\": \"%s\",\n"
        "      \"threshold_ns\": %.1f\n"
        "    }\n"
        "  }\n"
        "}\n",
        report->schema_version,
        report->report_id,
        report->generated_at,
        report->generator,
        d->cpu_core,
        d->frequency_ghz,
        d->numa_node,
        d->smt_active       ? "true" : "false",
        d->tsc_invariant     ? "true" : "false",
        d->rdtscp_supported  ? "true" : "false",
        a->p_state,
        a->c_states_disabled,
        a->hugepages_1gb     ? "true" : "false",
        a->egress_memory,
        a->rx_buffer_memory,
        a->ddio_enabled      ? "true" : "false",
        (unsigned long long)s->msr_start,
        (unsigned long long)s->msr_end,
        s->events_detected,
        (unsigned long long)s->samples_rejected,
        s->rejection_policy,
        (unsigned long long)r->samples_accepted,
        (unsigned long long)r->core_migration_rejected,
        r->latency_ns.min_ns,
        r->latency_ns.p50_ns,
        r->latency_ns.p99_ns,
        r->latency_ns.p99_9_ns,
        r->latency_ns.p99_99_ns,
        r->latency_ns.max_ns,
        r->determinism_pass ? "PASS" : "FAIL",
        r->threshold_ns
    );

    return buf;
}
// Fix 4: stubs for functions declared in vis_jitter.hpp
vis_report_t* vis_report_from_json(const char* /*json_path*/) {
    // V1 stub — JSON deserialization planned for V2
    fprintf(stderr, "[vis-jitter] vis_report_from_json: not implemented in V1\n");
    return nullptr;
    // V1 stub — JSON deserialization planned for V2.
    // Currently reading reports is not critical; generating them is.
}

char* vis_report_sign(const vis_report_t* /*report*/,
                      const char*         /*private_key_path*/) {
    // V1 stub — signing available in commercial tier only
    fprintf(stderr, "[vis-jitter] vis_report_sign: not available in open source V1\n");
    return nullptr;
}
