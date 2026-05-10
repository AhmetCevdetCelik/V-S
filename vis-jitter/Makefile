# vis-jitter Makefile
# Usage:
#   make           — build vis-jitter
#   make vis-doctor — build VIS Doctor
#   make vis-run   — build VIS Run
#   make vis-compare — build VIS Compare Lite
#   make test      — build and run rootless tests
#   make test-root — build and run the MSR/SMI test (requires sudo)
#   make test-all  — run rootless tests, then root-required tests
#   make lint      — run rootless lightweight quality checks
#   make clean     — remove build artifacts

CXX      := g++
# -O2 chosen deliberately over -O3:
# O3 enables aggressive inlining and vectorization that can introduce
# unpredictable latencies. For a measurement tool, consistency > speed.
CXXFLAGS := -Wall -Wextra -O2 -std=c++17 -Iinclude
LDFLAGS  := -lnuma -lpthread

SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests

SRCS := $(SRC_DIR)/main.cpp \
        $(SRC_DIR)/measurement.cpp \
        $(SRC_DIR)/smi_audit.cpp \
        $(SRC_DIR)/histogram.cpp \
        $(SRC_DIR)/report.cpp

DOCTOR_SRCS := $(SRC_DIR)/doctor_main.cpp \
               $(SRC_DIR)/doctor.cpp \
               $(SRC_DIR)/measurement.cpp \
               $(SRC_DIR)/smi_audit.cpp \
               $(SRC_DIR)/histogram.cpp \
               $(SRC_DIR)/report.cpp

RUN_SRCS := $(SRC_DIR)/vis_run_main.cpp \
            $(SRC_DIR)/vis_run.cpp

COMPARE_SRCS := $(SRC_DIR)/vis_compare_main.cpp \
                $(SRC_DIR)/vis_compare.cpp

# Header dependencies ensure recompilation when structs or APIs change.
# Without these, make would happily link old .o files with new headers —
# a silent source of measurement bugs.
HDRS := $(INC_DIR)/vis_jitter.hpp \
        $(INC_DIR)/measurement.hpp \
        $(INC_DIR)/smi_audit.hpp \
        $(INC_DIR)/histogram.hpp \
        $(INC_DIR)/report.hpp \
        $(INC_DIR)/doctor.hpp \
        $(INC_DIR)/vis_run.hpp \
        $(INC_DIR)/vis_compare.hpp

TARGET := vis-jitter
DOCTOR_BIN := vis-doctor
RUN_BIN := vis-run
COMPARE_BIN := vis-compare
TEST_SMI := test_smi_audit
TEST_HIST := test_histogram
TEST_REPORT := test_report_json
TEST_API := test_public_api
TEST_DOCTOR := test_doctor
TEST_RUN := test_vis_run
TEST_COMPARE := test_vis_compare

# ---------------------------------------------------------------------------
# Default target
# ---------------------------------------------------------------------------

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

$(DOCTOR_BIN): $(DOCTOR_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(DOCTOR_SRCS) $(LDFLAGS)

$(RUN_BIN): $(RUN_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(RUN_SRCS)

$(COMPARE_BIN): $(COMPARE_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(COMPARE_SRCS)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Rootless tests are safe for local development and CI.
test: $(TEST_HIST) $(TEST_REPORT) $(TEST_API) $(TEST_DOCTOR) $(TEST_RUN) $(TEST_COMPARE)
	@echo "Running test_histogram..."
	./$(TEST_HIST)
	@echo ""
	@echo "Running test_report_json..."
	./$(TEST_REPORT)
	@echo ""
	@echo "Running test_public_api..."
	./$(TEST_API)
	@echo ""
	@echo "Running test_doctor..."
	./$(TEST_DOCTOR)
	@echo ""
	@echo "Running test_vis_run..."
	./$(TEST_RUN)
	@echo ""
	@echo "Running test_vis_compare..."
	./$(TEST_COMPARE)

test-root: $(TEST_SMI)
	@echo ""
	@echo "Running test_smi_audit (requires root)..."
	sudo ./$(TEST_SMI)

test-all: test test-root

$(TEST_SMI): $(TEST_DIR)/test_smi_audit.cpp $(SRC_DIR)/smi_audit.cpp $(INC_DIR)/smi_audit.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_smi_audit.cpp $(SRC_DIR)/smi_audit.cpp $(LDFLAGS)

$(TEST_HIST): $(TEST_DIR)/test_histogram.cpp $(SRC_DIR)/histogram.cpp $(INC_DIR)/histogram.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_histogram.cpp $(SRC_DIR)/histogram.cpp

$(TEST_REPORT): $(TEST_DIR)/test_report_json.cpp $(SRC_DIR)/report.cpp $(INC_DIR)/report.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_report_json.cpp $(SRC_DIR)/report.cpp

$(TEST_API): $(TEST_DIR)/test_public_api.cpp $(SRC_DIR)/measurement.cpp $(SRC_DIR)/smi_audit.cpp $(SRC_DIR)/histogram.cpp $(INC_DIR)/measurement.hpp $(INC_DIR)/smi_audit.hpp $(INC_DIR)/histogram.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_public_api.cpp $(SRC_DIR)/measurement.cpp $(SRC_DIR)/smi_audit.cpp $(SRC_DIR)/histogram.cpp $(LDFLAGS)

$(TEST_DOCTOR): $(TEST_DIR)/test_doctor.cpp $(SRC_DIR)/doctor.cpp $(SRC_DIR)/measurement.cpp $(SRC_DIR)/smi_audit.cpp $(SRC_DIR)/histogram.cpp $(SRC_DIR)/report.cpp $(INC_DIR)/doctor.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_doctor.cpp $(SRC_DIR)/doctor.cpp $(SRC_DIR)/measurement.cpp $(SRC_DIR)/smi_audit.cpp $(SRC_DIR)/histogram.cpp $(SRC_DIR)/report.cpp $(LDFLAGS)

$(TEST_RUN): $(TEST_DIR)/test_vis_run.cpp $(SRC_DIR)/vis_run.cpp $(INC_DIR)/vis_run.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_vis_run.cpp $(SRC_DIR)/vis_run.cpp

$(TEST_COMPARE): $(TEST_DIR)/test_vis_compare.cpp $(SRC_DIR)/vis_compare.cpp $(INC_DIR)/vis_compare.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_vis_compare.cpp $(SRC_DIR)/vis_compare.cpp

# ---------------------------------------------------------------------------
# Lint / quality checks
# ---------------------------------------------------------------------------

format-check:
	../scripts/check-format.sh

cppcheck:
	../scripts/run-cppcheck.sh

lint: format-check

lint-extra: lint cppcheck

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

clean:
	rm -f $(TARGET) $(DOCTOR_BIN) $(RUN_BIN) $(COMPARE_BIN) $(TEST_SMI) $(TEST_HIST) $(TEST_REPORT) $(TEST_API) $(TEST_DOCTOR) $(TEST_RUN) $(TEST_COMPARE)

.PHONY: all test test-root test-all format-check cppcheck lint lint-extra clean
# ---------------------------------------------------------------------------
# Why no install target?
# V1 is single-binary, single-purpose. No system-wide installation needed.
# Run it where it's built: sudo ./vis-jitter --cpu 2 --duration 60
# ---------------------------------------------------------------------------
