# Host unit tests for the framework-agnostic kegboard core.
#
# These build with a plain host compiler: no ESPHome, no PlatformIO, no
# toolchain download. Run them with `make -C tests/core`.

CORE_DIR := ../../components/kegboard

CXX ?= c++

# Build output is scoped by OS and architecture. The repository is frequently
# shared between a host and a container (or a VM) over a bind mount, and an
# unscoped build/ directory means one platform's binaries look up-to-date to
# the other -- which fails as "cannot execute binary file" rather than as
# anything self-explanatory.
HOST_TAG := $(shell uname -s)-$(shell uname -m)
BUILD_DIR := build/$(HOST_TAG)

# -Werror is deliberately not on by default: contributors run a range of
# compiler versions, and a newly-added warning in someone's toolchain should
# not block them from running the tests. CI passes STRICT=1 to enforce it.
ifeq ($(STRICT),1)
WARN_FLAGS := -Wall -Wextra -Werror
else
WARN_FLAGS := -Wall -Wextra
endif

CXXFLAGS ?= -std=c++17 -O1 -g
ALL_CXXFLAGS := $(CXXFLAGS) $(WARN_FLAGS) -I$(CORE_DIR) -I.

CORE_SRCS := \
	$(CORE_DIR)/pour_session.cpp \
	$(CORE_DIR)/tick_series.cpp \
	$(CORE_DIR)/kegbot_request.cpp \
	$(CORE_DIR)/auth_session.cpp \
	$(CORE_DIR)/json_writer.cpp \
	$(CORE_DIR)/events.cpp

CORE_HDRS := \
	$(CORE_DIR)/pour_session.h \
	$(CORE_DIR)/tick_series.h \
	$(CORE_DIR)/kegbot_request.h \
	$(CORE_DIR)/ring_queue.h \
	$(CORE_DIR)/auth_session.h \
	$(CORE_DIR)/json_writer.h \
	$(CORE_DIR)/events.h

TESTS := \
	test_pour_session \
	test_tick_series \
	test_kegbot_request \
	test_ring_queue \
	test_auth_session \
	test_json_writer \
	test_events

BINARIES := $(addprefix $(BUILD_DIR)/,$(TESTS))

.PHONY: all test clean
all: test

test: $(BINARIES)
	@fail=0; \
	for t in $(BINARIES); do \
		./$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; \
	echo "All core tests passed."

$(BUILD_DIR)/%: %.cpp $(CORE_SRCS) $(CORE_HDRS) test_support.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(ALL_CXXFLAGS) -o $@ $< $(CORE_SRCS)

clean:
	rm -rf build
