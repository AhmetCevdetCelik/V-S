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

SRCS := $(SRC_DIR)/main.cpp \
        $(SRC_DIR)/measurement.cpp \
        $(SRC_DIR)/smi_audit.cpp \
        $(SRC_DIR)/histogram.cpp \
        $(SRC_DIR)/report.cpp

# Header dependencies for proper recompilation
HDRS := $(INC_DIR)/vis_jitter.hpp \
        $(INC_DIR)/measurement.hpp \
        $(INC_DIR)/smi_audit.hpp \
        $(INC_DIR)/histogram.hpp \
        $(INC_DIR)/report.hpp

TARGET := vis-jitter
TEST_SMI := test_smi_audit
TEST_HIST := test_histogram

# ---------------------------------------------------------------------------
# Default target
# ---------------------------------------------------------------------------

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

test: $(TEST_SMI) $(TEST_HIST)
	@echo "Running test_histogram..."
	./$(TEST_HIST)
	@echo ""
	@echo "Running test_smi_audit (requires root)..."
	sudo ./$(TEST_SMI)

$(TEST_SMI): $(TEST_DIR)/test_smi_audit.cpp $(SRC_DIR)/smi_audit.cpp $(INC_DIR)/smi_audit.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_smi_audit.cpp $(SRC_DIR)/smi_audit.cpp $(LDFLAGS)

$(TEST_HIST): $(TEST_DIR)/test_histogram.cpp $(SRC_DIR)/histogram.cpp $(INC_DIR)/histogram.hpp $(INC_DIR)/vis_jitter.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/test_histogram.cpp $(SRC_DIR)/histogram.cpp

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

clean:
	rm -f $(TARGET) $(TEST_SMI) $(TEST_HIST)

.PHONY: all test clean