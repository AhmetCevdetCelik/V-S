/**
 * histogram.cpp
 *
 * Latency histogram implementation for vis-jitter.
 * Buckets are fixed-width (VIS_BUCKET_WIDTH_NS nanoseconds each).
 * P50, P99, P99.9, P99.99 are computed by walking the bucket array.
 *
 * License: MIT
 */

#include "../include/histogram.hpp"

#include <cstring>      // memset()
#include <cstdio>       // fprintf()

// ---------------------------------------------------------------------------
// Internal helper
// ---------------------------------------------------------------------------

/**
 * Compute a single percentile value from the histogram.
 *
 * @param h           Source histogram.
 * @param percentile  Value between 0.0 and 100.0 (e.g. 99.0 for P99).
 * @return            Latency in nanoseconds at the given percentile.
 */
static double compute_percentile(const vis_histogram_t* h, double percentile) {
    if (h->total_accepted == 0) {
        return 0.0;
    }

    // How many samples must we pass before we reach this percentile?
    uint64_t target = static_cast<uint64_t>(
        (percentile / 100.0) * static_cast<double>(h->total_accepted)
    // Walk buckets cumulatively. When we cross the target,
    // return the midpoint of that bucket.
    // Chose midpoint over bucket start because it's a better
    // approximation for sparse histograms.
    );

    uint64_t cumulative = 0;

    for (int i = 0; i < VIS_HISTOGRAM_BUCKETS; i++) {
        cumulative += h->buckets[i];

        if (cumulative >= target) {
            // Return the midpoint of this bucket
            return (i * VIS_BUCKET_WIDTH_NS) + (VIS_BUCKET_WIDTH_NS / 2.0);
        }
    }

    // All samples were in overflow — return the upper edge of the last bucket
    return VIS_HISTOGRAM_BUCKETS * VIS_BUCKET_WIDTH_NS;
}

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

void vis_histogram_init(vis_histogram_t* h) {
    if (h == nullptr) return;
    memset(h->buckets, 0, sizeof(h->buckets));
    h->overflow       = 0;
    h->total_accepted = 0;
}

void vis_histogram_add(vis_histogram_t* h, double value_ns) {
    if (h == nullptr || value_ns < 0.0) return;

    h->total_accepted++;

    int bucket_index = static_cast<int>(value_ns / VIS_BUCKET_WIDTH_NS);

    if (bucket_index >= VIS_HISTOGRAM_BUCKETS) {
        h->overflow++;
    } else {
        h->buckets[bucket_index]++;
    }
}

void vis_histogram_compute(const vis_histogram_t* h, vis_latency_t* out) {
    if (h == nullptr || out == nullptr) return;

    if (h->total_accepted == 0) {
        fprintf(stderr, "[vis-jitter] WARNING: histogram is empty, "
                        "cannot compute latency statistics.\n");
        return;
    }
    // Find min: first non-empty bucket
    // This approach is O(buckets) but buckets are only 500.
    // For V1, clarity beats micro-optimization.
    // Walk buckets to find min and max
    out->min_ns = 0.0;
    out->max_ns = 0.0;

    // Find min: first non-empty bucket
    for (int i = 0; i < VIS_HISTOGRAM_BUCKETS; i++) {
        if (h->buckets[i] > 0) {
            out->min_ns = i * VIS_BUCKET_WIDTH_NS;
            break;
        }
    }

    // Find max: last non-empty bucket
    for (int i = VIS_HISTOGRAM_BUCKETS - 1; i >= 0; i--) {
        if (h->buckets[i] > 0) {
            out->max_ns = (i * VIS_BUCKET_WIDTH_NS) + VIS_BUCKET_WIDTH_NS;
            break;
        }
    }

    // Compute percentiles
    out->p50_ns   = compute_percentile(h, 50.0);
    out->p99_ns   = compute_percentile(h, 99.0);
    out->p99_9_ns = compute_percentile(h, 99.9);
    out->p99_99_ns= compute_percentile(h, 99.99);
}
// ---------------------------------------------------------------------------
// D E S I G N   N O T E S
// ---------------------------------------------------------------------------
// Percentiles are computed from binned data (histogram), not raw samples.
// Trade-off: we lose nanosecond precision on individual percentiles
// in exchange for O(1) memory and O(buckets) compute.
// This keeps vis-jitter lightweight even on billion-sample runs.