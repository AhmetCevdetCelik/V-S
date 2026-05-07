#pragma once

/**
 * histogram.hpp
 *
 * Latency histogram for vis-jitter.
 * Buckets cover 0 to (VIS_HISTOGRAM_BUCKETS * VIS_BUCKET_WIDTH_NS) nanoseconds.
 * Samples beyond the last bucket go into overflow.
 *
 * License: MIT
 */

#include "vis_jitter.hpp"

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * Initialize a histogram — zero all buckets.
 * Must be called before vis_histogram_add().
 *
 * @param h  Histogram to initialize.
 */
void vis_histogram_init(vis_histogram_t* h);

/**
 * Add a single latency sample (in nanoseconds) to the histogram.
 *
 * @param h         Target histogram.
 * @param value_ns  Latency value in nanoseconds.
 */
void vis_histogram_add(vis_histogram_t* h, double value_ns);

/**
 * Compute percentile statistics from the histogram.
 * Populates all fields of vis_latency_t.
 *
 * @param h    Source histogram.
 * @param out  Output latency statistics.
 */
void vis_histogram_compute(const vis_histogram_t* h, vis_latency_t* out);