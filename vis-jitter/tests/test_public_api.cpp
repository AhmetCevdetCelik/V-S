/**
 * test_public_api.cpp
 *
 * Rootless smoke test for public API symbols and invalid-argument guards.
 *
 * License: MIT
 */

#include "../include/vis_jitter.hpp"

#include <cstdio>
#include <sched.h>

int main() {
    vis_status_t status = vis_jitter_run(
        0,
        1,
        100.0,
        nullptr,
        nullptr,
        nullptr
    );
    if (status != vis_status_t::VIS_ERR_INVALID_ARG) {
        std::printf("[test] FAILED: null report should be invalid arg.\n");
        return 1;
    }

    vis_report_t report;
    status = vis_jitter_run(
        CPU_SETSIZE,
        1,
        100.0,
        nullptr,
        nullptr,
        &report
    );
    if (status != vis_status_t::VIS_ERR_INVALID_ARG) {
        std::printf("[test] FAILED: CPU_SETSIZE core should be invalid arg.\n");
        return 1;
    }

    status = vis_jitter_run(
        0,
        1,
        -1.0,
        nullptr,
        nullptr,
        &report
    );
    if (status != vis_status_t::VIS_ERR_INVALID_ARG) {
        std::printf("[test] FAILED: negative threshold should be invalid arg.\n");
        return 1;
    }

    std::printf("[test] PASS: public API links and rejects invalid args.\n");
    return 0;
}
