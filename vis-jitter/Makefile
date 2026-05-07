# vis-jitter Makefile
# Usage:
#   make          — build vis-jitter
#   make test     — build and run all tests
#   make clean    — remove build artifacts

CXX      := g++
CXXFLAGS := -Wall -Wextra -O2 -std=c++17 -Iinclude
LDFLAGS  := -lnuma -lpthread

SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests
BUILD_DIR := build

SRCS := $(SRC_DIR)/main.cpp \
        $(SRC_DIR)/measurement.cpp \
        $(SRC_DIR)/smi_audit.cpp \
        $(SRC_DIR)/histogram.cpp \
        $(SRC_DIR)/report.cpp

TARGET := vis-jitter

# ---------------------------------------------------------------------------
# Default target
# ---------------------------------------------------------------------------

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

TEST_SMI := test_smi_audit
TEST_HIST := test_histogram

test: $(TEST_SMI) $(TEST_HIST)
	@echo "Running test_smi_audit (requires root)..."
	sudo ./$(TEST_SMI)
	@echo "Running test_histogram..."
	./$(TEST_HIST)

$(TEST_SMI): $(TEST_DIR)/test_smi_audit.cpp $(SRC_DIR)/smi_audit.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_HIST): $(TEST_DIR)/test_histogram.cpp $(SRC_DIR)/histogram.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

clean:
	rm -f $(TARGET) $(TEST_SMI) $(TEST_HIST)

.PHONY: all test clean