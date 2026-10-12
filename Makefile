CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -fPIC
R2_CFLAGS := $(shell pkg-config --cflags r_core)
R2_LIBS := $(shell pkg-config --libs r_core)
UTIL_CFLAGS := $(shell pkg-config --cflags r_util)
UTIL_LIBS := $(shell pkg-config --libs r_util)
EXT := $(shell [ "$$(uname -s)" = Darwin ] && echo dylib || echo so)
PLUGDIR ?= $(shell r2 -H R2_USER_PLUGINS)
PLUGIN := build/core_discord.$(EXT)
LIB_SRCS := src/presence.c src/ipc.c src/worker.c

all: $(PLUGIN)

build:
	mkdir -p build

$(PLUGIN): src/core_discord.c $(LIB_SRCS) src/*.h | build
	$(CC) $(CFLAGS) $(R2_CFLAGS) -shared -o $@ src/core_discord.c $(LIB_SRCS) $(R2_LIBS) -lpthread

build/test_presence: test/test_presence.c src/presence.c src/presence.h test/t.h | build
	$(CC) $(CFLAGS) $(UTIL_CFLAGS) -o $@ test/test_presence.c src/presence.c $(UTIL_LIBS)

build/test_ipc: test/test_ipc.c test/fake_discord.c src/ipc.c src/*.h test/*.h | build
	$(CC) $(CFLAGS) $(UTIL_CFLAGS) -o $@ test/test_ipc.c test/fake_discord.c src/ipc.c $(UTIL_LIBS) -lpthread

build/test_worker: test/test_worker.c test/fake_discord.c $(LIB_SRCS) src/*.h test/*.h | build
	$(CC) $(CFLAGS) $(UTIL_CFLAGS) -o $@ test/test_worker.c test/fake_discord.c $(LIB_SRCS) $(UTIL_LIBS) -lpthread

build/fake_discord: test/fake_discord_main.c test/fake_discord.c test/fake_discord.h | build
	$(CC) $(CFLAGS) -o $@ test/fake_discord_main.c test/fake_discord.c -lpthread

TESTS := $(wildcard test/test_*.c)
TEST_BINS := $(patsubst test/%.c,build/%,$(TESTS))

test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do ./$$t; done

integration: $(PLUGIN) build/fake_discord
	EXT=$(EXT) sh test/integration.sh

install: $(PLUGIN)
	mkdir -p "$(PLUGDIR)"
	cp $(PLUGIN) "$(PLUGDIR)/"

uninstall:
	rm -f "$(PLUGDIR)/core_discord.$(EXT)"

clean:
	rm -rf build

.PHONY: all test integration install uninstall clean
