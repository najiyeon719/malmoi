PYTHON3 ?= python3
CC ?= cc
AR ?= ar
CPPFLAGS ?= -Iinclude
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS ?= -lm
BUILD_DIR ?= build/core
EXEEXT ?=
EXAMPLE_LDFLAGS ?=
ifeq ($(OS),Windows_NT)
EXEEXT = .exe
EXAMPLE_LDFLAGS = -municode
endif
SOURCES = src/vecdb.c src/malmoi.c src/internal/search_backend.c src/internal/lexical.c
OBJECTS = $(BUILD_DIR)/vecdb.o $(BUILD_DIR)/malmoi.o $(BUILD_DIR)/search_backend.o $(BUILD_DIR)/lexical.o $(BUILD_DIR)/bridge.o
HEADERS = include/vecdb.h include/malmoi.h src/internal/search_backend.h src/internal/lexical.h
.PHONY: all core search-cli test core-test search-test fixture sanitize check demo-b demo-c demo-d
all: search-cli
$(BUILD_DIR):
ifeq ($(OS),Windows_NT)
	if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
else
	mkdir -p "$(BUILD_DIR)"
endif
$(BUILD_DIR)/vecdb.o: src/vecdb.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/malmoi.o: src/malmoi.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/search_backend.o: src/internal/search_backend.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/lexical.o: src/internal/lexical.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/libmalmoi.a: $(OBJECTS)
	$(RM) $@
	$(AR) rcs $@ $^
$(BUILD_DIR)/malmoi_cli$(EXEEXT): examples/malmoi_cli.c examples/cli_args.h $(BUILD_DIR)/libmalmoi.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/libmalmoi.a $(LDFLAGS) $(EXAMPLE_LDFLAGS) $(LDLIBS) -o $@
$(BUILD_DIR)/%$(EXEEXT): examples/%.c src/vecdb.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/vecdb.c $< $(LDFLAGS) $(EXAMPLE_LDFLAGS) $(LDLIBS) -o $@
$(BUILD_DIR)/test_vecdb$(EXEEXT): tests/test_vecdb.c src/vecdb.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/vecdb.c $< $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD_DIR)/test_search$(EXEEXT): tests/test_search.c src/vecdb.c src/internal/search_backend.c src/internal/lexical.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/vecdb.c src/internal/search_backend.c src/internal/lexical.c $< $(LDFLAGS) $(LDLIBS) -o $@
search-cli: $(BUILD_DIR)/malmoi_cli$(EXEEXT)
core: search-cli $(BUILD_DIR)/inspect$(EXEEXT) $(BUILD_DIR)/test_vecdb$(EXEEXT)
fixture:
	$(PYTHON3) tools/make_fixture.py --count 1000 --output build/fixtures/dev-1000.mlm --export-input
search-test: search-cli $(BUILD_DIR)/test_search$(EXEEXT)
	$(BUILD_DIR)/test_search$(EXEEXT) tests/fixtures/small.mlm
	$(PYTHON3) -S tests/test_public_api.py --cli $(BUILD_DIR)/malmoi_cli$(EXEEXT)
core-test: core fixture search-test
	$(BUILD_DIR)/test_vecdb$(EXEEXT) build/fixtures/dev-1000.mlm
	$(PYTHON3) -S tests/test_compatibility.py --inspect $(BUILD_DIR)/inspect$(EXEEXT)
test check: core-test
sanitize:
	$(MAKE) core-test BUILD_DIR=build/sanitize CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined"
demo-b: $(BUILD_DIR)/word_demo$(EXEEXT) fixture
	$(BUILD_DIR)/word_demo$(EXEEXT) build/fixtures/dev-1000.mlm
demo-c: $(BUILD_DIR)/sense_demo$(EXEEXT) fixture
	$(BUILD_DIR)/sense_demo$(EXEEXT) build/fixtures/dev-1000.mlm
demo-d: $(BUILD_DIR)/game_demo$(EXEEXT) fixture
	$(BUILD_DIR)/game_demo$(EXEEXT) build/fixtures/dev-1000.mlm

.PHONY: python-tools-test
python-tools-test:
	$(PYTHON3) -m unittest discover -s tests -p 'test_python_tools.py' -v
	$(PYTHON3) -m unittest discover -s tests -p 'test_export_e5.py' -v


$(BUILD_DIR)/bridge.o: src/bridge.c include/malmoi_bridge.h $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/bridge_cli$(EXEEXT): examples/bridge_cli.c examples/cli_args.h $(BUILD_DIR)/libmalmoi.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/libmalmoi.a $(LDFLAGS) $(EXAMPLE_LDFLAGS) $(LDLIBS) -o $@
$(BUILD_DIR)/test_bridge$(EXEEXT): tests/test_bridge.c $(BUILD_DIR)/libmalmoi.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/libmalmoi.a $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: ring-fixture bridge bridge-test
ring-fixture:
	$(PYTHON3) tools/make_ring_fixture.py --output build/fixtures/ring.mlm
bridge: $(BUILD_DIR)/bridge_cli$(EXEEXT)
bridge-test: ring-fixture $(BUILD_DIR)/test_bridge$(EXEEXT)
	MALMOI_DATA=build/fixtures/ring.mlm $(BUILD_DIR)/test_bridge$(EXEEXT)


$(BUILD_DIR)/test_meaning$(EXEEXT): tests/test_meaning.c $(BUILD_DIR)/libmalmoi.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/libmalmoi.a $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: meaning-fixture meaning-test
meaning-fixture:
	$(PYTHON3) tools/make_meaning_fixture.py --output build/fixtures/meaning.mlm
meaning-test: meaning-fixture $(BUILD_DIR)/test_meaning$(EXEEXT)
	MALMOI_DATA=build/fixtures/meaning.mlm $(BUILD_DIR)/test_meaning$(EXEEXT)