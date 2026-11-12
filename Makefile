# =============================================================================
# ixxc-server Makefile
# =============================================================================

SRC_DIR      := src/core
HDR_DIR      := header/prod
BUILD_DIR    := build
GEN_DIR      := $(BUILD_DIR)/generated
EXAMPLE      := $(BUILD_DIR)/example
BIN_DIR      := bin

CC           := gcc

WARNINGS     := -Wall -Wextra -Werror -Wpedantic -Wformat=2 -Wformat-security \
                -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
                -Wshadow -Wconversion -Wsign-conversion -Wnull-dereference \
                -Wdouble-promotion -Wundef -Wunused -Wuninitialized

SECURITY     := -fstack-protector-strong \
                -D_FORTIFY_SOURCE=3 \
                -fPIE \
                -fno-strict-aliasing \
                -fno-delete-null-pointer-checks \
                -fno-strict-overflow \
                -fstack-clash-protection

LDFLAGS_SEC  := -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
OPT          := -O2

CFLAGS       := $(OPT) $(WARNINGS) $(SECURITY) \
                -I$(HDR_DIR) -I$(GEN_DIR) -I. -std=c11
LDFLAGS      := $(LDFLAGS_SEC)

TOOL_CFLAGS  := -O2 -Wall -Wextra -std=c11 -I. -I$(HDR_DIR)

HTML_DATA_C   := $(GEN_DIR)/html_data.c
HTML_DATA_H   := $(GEN_DIR)/html_data.h
HEADER_DATA_C := $(GEN_DIR)/header_data.c
HEADER_DATA_H := $(GEN_DIR)/header_data.h
GENERATED     := $(HTML_DATA_C) $(HTML_DATA_H) $(HEADER_DATA_C) $(HEADER_DATA_H)

PATH_CRAWLER_SRC   := $(SRC_DIR)/path_crawler.c
PATH_CRAWLER_BIN   := $(BIN_DIR)/path_crawler

GENERATOR_SRC := $(SRC_DIR)/generate_c_frontend_source.c
GENERATOR     := $(BIN_DIR)/generate_c_frontend_source

# ---------------------------------------------------------------------------
# Explicit object list (so nothing is forgotten)
# ---------------------------------------------------------------------------
SERVER_OBJS := \
        $(BUILD_DIR)/server.o \
        $(BUILD_DIR)/log.o \
        $(BUILD_DIR)/process.o \
        $(BUILD_DIR)/security.o \
        $(BUILD_DIR)/http_parser.o \
        $(BUILD_DIR)/connection.o \
        $(BUILD_DIR)/socket.o \
        $(BUILD_DIR)/header_data.o \
        $(BUILD_DIR)/html_data.o

SERVER_BIN   := $(BIN_DIR)/ixxc_server

# ---------------------------------------------------------------------------
.PHONY: all clean dirs generate build run_path_crawler

# ---------------------------------------------------------------------------
# Example build step (build and run src/core/example.c)
# ---------------------------------------------------------------------------
example: $(BIN_DIR)/example
	@echo ">>> [EXAMPLE] Creating output directories..."
	@mkdir -p $(EXAMPLE) $(EXAMPLE)/crawling
	@echo ">>> [EXAMPLE] Running generator..."
	@$(BIN_DIR)/example
	@echo ">>> [EXAMPLE] Finished."

$(BIN_DIR)/example: $(SRC_DIR)/example.c | dirs
	@echo ">>> [EXAMPLE] Compiling..."
	$(CC) $(CFLAGS) -o $@ $<

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------
build: $(SERVER_BIN)

all: dirs run_path_crawler $(GENERATED) $(SERVER_BIN)

generate: run_path_crawler $(GENERATED)

dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR) $(GEN_DIR)

$(PATH_CRAWLER_BIN): $(PATH_CRAWLER_SRC) | dirs
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run_path_crawler: $(PATH_CRAWLER_BIN)
	@echo ">>> [PATH_CRAWLER] starte $(PATH_CRAWLER_BIN)"
	$(PATH_CRAWLER_BIN)
	@echo ">>> [PATH_CRAWLER] fertig"

$(GENERATED): $(GENERATOR) | dirs
	@echo "[GEN] html_data ..."
	./$(GENERATOR)
	@touch $(GENERATED)
	@echo "[OK] $(GENERATED)"

$(GENERATOR): $(GENERATOR_SRC) ixxc_endpoints.h | dirs
	$(CC) $(TOOL_CFLAGS) $< $(SRC_DIR)/meme_types_handler.c -o $@

# ---------------------------------------------------------------------------
# Server depends on generated files for frontend
# so after path_crawler + generate
# ---------------------------------------------------------------------------
$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "[OK] Server: $@"

$(BUILD_DIR)/server.o:              $(SRC_DIR)/server.c               $(GENERATED) | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/log.o:                 $(SRC_DIR)/log.c                  $(GENERATED) | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process.o:             $(SRC_DIR)/process.c              $(GENERATED) | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/security.o:            $(SRC_DIR)/security.c             $(GENERATED) | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/http_parser.o:         $(SRC_DIR)/http_parser.c          $(GENERATED) | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/connection.o:          $(SRC_DIR)/connection.c           $(GENERATED) | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/socket.o:              $(SRC_DIR)/socket.c               $(GENERATED) | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/html_data.o:           $(HTML_DATA_C) $(HTML_DATA_H)     | dirs
	$(CC) $(CFLAGS) -c $(HTML_DATA_C) -o $@

$(BUILD_DIR)/header_data.o:         $(HEADER_DATA_C) $(HEADER_DATA_H) | dirs
	$(CC) $(CFLAGS) -c $(HEADER_DATA_C) -o $@

# ---------------------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	rm my_server_access.log
	@echo "[OK] clean"

# -----------------------------------------------------------------------------
# TESTS
# -----------------------------------------------------------------------------
TEST_DIR         := src/test
TEST_BIN         := $(BIN_DIR)/test_ixxc
TEST_RING_BUF    := $(BIN_DIR)/test_ring_buffer
TEST_CONST_BIN   := $(BIN_DIR)/test_constant_time
TEST_MTH_BIN     := $(BIN_DIR)/test_meme_types_handler
TEST_LOG_BIN     := $(BIN_DIR)/test_log_file
TEST_HTTP_PARSER := $(BIN_DIR)/test_http_parser
TEST_CFLAGS      := $(TOOL_CFLAGS) -DUSE_TEST_ENDPOINTS

$(TEST_BIN): $(TEST_DIR)/test_ixxc.c | dirs
	$(CC) $(TOOL_CFLAGS) $< -o $@ -lcurl -lpthread

$(TEST_RING_BUF): $(TEST_DIR)/test_ring_buffer.c | dirs
	$(CC) $(TOOL_CFLAGS) $< -o $@ -lcurl -lpthread

$(TEST_CONST_BIN): $(TEST_DIR)/test_constant_time.c $(SRC_DIR)/security.c | dirs
	$(CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_MTH_BIN): $(TEST_DIR)/test_meme_types.c | dirs
	$(CC) $(TEST_CFLAGS) $< -o $@

$(TEST_LOG_BIN): $(TEST_DIR)/test_log_file.c | dirs
	$(CC) $(TOOL_CFLAGS) $< -o $@

$(TEST_HTTP_PARSER): $(TEST_DIR)/test_http_parser.c $(SRC_DIR)/http_parser.c | dirs
	$(CC) $(TOOL_CFLAGS) $< -o $@

.PHONY: test
test: $(TEST_CONST_BIN) $(TEST_BIN) $(TEST_MTH_BIN) $(TEST_RING_BUF) $(TEST_LOG_BIN) $(TEST_HTTP_PARSER)
	@echo "Running ring buffer test..."
	@$(TEST_RING_BUF)
	@echo "Running test_meme_types_handler..."
	@$(TEST_MTH_BIN)
	@echo "Running test_constant_time 5x..."
	@for i in 1 2 3 4 5; do $(TEST_CONST_BIN); done
	@echo "Running test_ixxc..."
	@$(TEST_BIN)
	@echo "Running test_log_file..."
	@$(TEST_LOG_BIN)
	@echo "Running test_http_parser..."
	@$(TEST_HTTP_PARSER)
	@echo "All tests finished."
