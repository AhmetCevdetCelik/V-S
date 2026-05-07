#pragma once

/**
 * report.hpp
 *
 * Report generation for vis-jitter.
 * Provides JSON serialization and human-readable terminal summary.
 *
 * License: MIT
 */

#include "vis_jitter.hpp"

/**
 * Print a human-readable summary of the report to stdout.
 *
 * @param report  Fully populated report struct.
 */
void vis_report_print_summary(const vis_report_t* report);

/**
 * Serialize a report to a JSON string.
 * Caller is responsible for free()-ing the returned pointer.
 *
 * @param report  Fully populated report struct.
 * @return        Heap-allocated JSON string, or nullptr on failure.
 */
char* vis_report_to_json(const vis_report_t* report);