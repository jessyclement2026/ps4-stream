SDK ?= $(CURDIR)/ps4-payload-sdk
CC := $(SDK)/bin/orbis-clang
LINKER := $(shell find $(SDK) -name '*.x' | head -n 1)

CFLAGS := -O2 -Wall -fPIC -ffreestanding -fno-builtin \
          -DPBS_PS4 -DPBS_PS4_DYNAMIC_LAYOUT=1 -DPBS_PS4_REMOTE_AVCAP=1 \
          -DPBS_PS4_WORKER_BOOT=1 -DPBS_PS4_EXPORTED_READ=1 \
          -Isrc -I$(SDK)/target/include

LDFLAGS := -T $(LINKER) -L$(SDK)/target/lib -lSceLibcInternal -lkernel

SRCS := main.c $(shell find src -name '*.c' \
        ! -path 'src/openh264*' \
        ! -path 'src/common/http*' \
        ! -name 'main.c' \
        ! -name 'probe.c' \
        ! -name 'worker_probe_main.c')

TARGETS := ps4-stream-720p.elf ps4-stream-1080p.elf

all: $(TARGETS)

ps4-stream-720p.elf: $(SRCS)
	$(CC) $(CFLAGS) -DHEIGHT=720 $(SRCS) -o $@ $(LDFLAGS)

ps4-stream-1080p.elf: $(SRCS)
	$(CC) $(CFLAGS) -DHEIGHT=1080 $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
