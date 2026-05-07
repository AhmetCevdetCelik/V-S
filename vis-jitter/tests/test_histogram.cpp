/**
 * test_histogram.cpp
 *
 * Manual test for histogram functions.
 * Adds known latency values and verifies percentile calculations.
 *
 * Run:
 *   ./test_histogram
 *
 * License: MIT
 */

#include "../include/histogram.hpp"

#include <cstdio>
#include <cinttypes>    // PRIu64

int main() {
    vis_histogram_t h;
    vis_latency_t   latency;

    printf("[test] Initializing histogram...\n");
    vis_histogram_init(&h);

    if (h.total_accepted != 0 || h.overflow != 0) {
        printf("[test] FAILED: histogram not zeroed after init.\n");
        return 1;
    }
    printf("[test] PASS: histogram initialized.\n");

    // Add 1000 samples at 80 ns and 10 samples at 150 ns
    // P99 should land in the 80 ns bucket, P99.99 in the 150 ns bucket
    printf("[test] Adding samples...\n");

    for (int i = 0; i < 1000; i++) {
        vis_histogram_add(&h, 80.0);
    }
    for (int i = 0; i < 10; i++) {
        vis_histogram_add(&h, 150.0);
    }

    printf("[test] total_accepted: %" PRIu64 "\n", h.total_accepted);
    printf("[test] overflow:       %" PRIu64 "\n", h.overflow);

    if (h.total_accepted != 1010) {
        printf("[test] FAILED: expected 1010 samples, got %" PRIu64 "\n",
               h.total_accepted);
        return 1;
    }
    printf("[test] PASS: sample count correct.\n");

    // Compute percentiles
    printf("[test] Computing percentiles...\n");
    vis_histogram_compute(&h, &latency);

    printf("[test] min_ns:    %.1f\n", latency.min_ns);
    printf("[test] p50_ns:    %.1f\n", latency.p50_ns);
    printf("[test] p99_ns:    %.1f\n", latency.p99_ns);
    printf("[test] p99_9_ns:  %.1f\n", latency.p99_9_ns);
    printf("[test] p99_99_ns: %.1f\n", latency.p99_99_ns);
    printf("[test] max_ns:    %.1f\n", latency.max_ns);

    // P99 should be in 80 ns range (bucket 8 → midpoint 85)
    if (latency.p99_ns > 100.0) {
        printf("[test] FAILED: P99 expected <= 100 ns, got %.1f\n",
               latency.p99_ns);
        return 1;
    }
    printf("[test] PASS: P99 within expected range.\n");

    // P99.99 should be in 150 ns range (bucket 15 → midpoint 155)
    if (latency.p99_99_ns < 100.0) {
        printf("[test] FAILED: P99.99 expected > 100 ns, got %.1f\n",
               latency.p99_99_ns);
        return 1;
    }
    printf("[test] PASS: P99.99 within expected range.\n");

    printf("[test] All tests passed.\n");
    return 0;
}