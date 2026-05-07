# vis-jitter Makefile
# Usage:
#   make          — build vis-jitter
#   make test     — build and run all tests
#   make clean    — remove build artifacts

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

# Header dependencies ensure recompilation when structs or APIs change.
# Without these, make would happily link old .o files with new headers —
# a silent source of measurement bugs.
HDRS := $(INC_DIR)/vis_jitter.hpp \
        $(INC_DIR)/measurement.hpp \
        $(INC_DIR)/smi_audit.hpp \
        $(INC_DIR)/histogram.hpp \
        $(INC_DIR)/report.hpp

TARGET := vis-jitter
TEST_SMI := test_smi_audit
TEST_HIST := test_histogram
TEST_REPORT := test_report_json

# ---------------------------------------------------------------------------
# Default target
# ---------------------------------------------------------------------------

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Run rootless tests first, then test_smi_audit (requires root for MSR access).
# Order matters: if the basic logic fails, don't bother with root.
test: $(TEST_HIST) $(TEST_REPORT)
	@echo "Running test_histogram..."
	./$(TEST_HIST)
	@echo ""
	@echo "Running test_report_json..."
	./$(TEST_REPORT)
	@echo ""
	@echo "Building test_smi_audit (requires libnuma)..."
	$(MAKE) $(TEST_SMI)
	@echo ""
	@echo "Running test_smi_audit (requires root)..."
	sudo ./$(TEST_SMI)

$(TEST_SMI): $(TEST_DIR)/test_smi_audit.cpp $(SRC_DIR)/smi_audit.cpp $(INC_DIR)/smi_audit.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_smi_audit.cpp $(SRC_DIR)/smi_audit.cpp $(LDFLAGS)

$(TEST_HIST): $(TEST_DIR)/test_histogram.cpp $(SRC_DIR)/histogram.cpp $(INC_DIR)/histogram.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_histogram.cpp $(SRC_DIR)/histogram.cpp

$(TEST_REPORT): $(TEST_DIR)/test_report_json.cpp $(SRC_DIR)/report.cpp $(INC_DIR)/report.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_report_json.cpp $(SRC_DIR)/report.cpp

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

clean:
	rm -f $(TARGET) $(TEST_SMI) $(TEST_HIST) $(TEST_REPORT)

.PHONY: all test clean
# ---------------------------------------------------------------------------
# Why no install target?
# V1 is single-binary, single-purpose. No system-wide installation needed.
# Run it where it's built: sudo ./vis-jitter --cpu 2 --duration 60
# ---------------------------------------------------------------------------
