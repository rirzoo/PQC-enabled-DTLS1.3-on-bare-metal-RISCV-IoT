BUILD_DIR?=../build/sim

include $(BUILD_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

#Wolf includes

SRCS += $(wildcard wolfcrypt/src/*.c)
SRCS += $(wildcard src/*.c)
OBJECTS  = crt0.o main.o
OBJECTS += dtls_io.o
OBJECTS += $(SRCS:.c=.o)
INC  = -I./wolfssl -I./wolfssl/wolfcrypt -I.

all: boot.bin

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

vpath %.a $(PACKAGES:%=../%)

# CFLAGS += $(INC) -DWOLFSSL_USER_SETTINGS

CFLAGS += $(INC)
CFLAGS += -DWOLFSSL_USER_SETTINGS
# VexRiscv 'standard' (rv32im) has NO hardware unaligned load/store support and
# the generic LiteX isr() does not emulate the resulting exception, so an
# unaligned access hangs (infinite trap loop). GCC otherwise merges wolfSSL's
# byte-wise c16toa/c32toa writes into a single unaligned sw. -mstrict-align
# forces GCC to only emit naturally-aligned accesses. See PROGRESS.md (B5).
CFLAGS += -mstrict-align

# RX transport strategy (see dtls_io.c). DTLS_RX_IRQ=1 builds the interrupt-driven
# RX path (ethmac ISR drains the MAC during crypto, no server pacing needed);
# 0 keeps the polled path (udp_service in the pump loop + server-side DTLS_PACE_US).
# Requires `make clean` to switch (a CFLAGS change does not rebuild existing .o files).
# DEFAULT = 1 (interrupt RX): the ML-DSA-44 2x2 benchmark selected cell B1 (interrupt
# RX, nrxslots=2, no pacing) as production -- interrupt RX beat poll+pacing at every
# slot count and its latency is independent of slot depth, so the 2-slot config is
# fastest-tier AND cheapest (BENCHMARKS.md, § ML-DSA-44). Build DTLS_RX_IRQ=0 only to
# reproduce the polled comparison cells.
DTLS_RX_IRQ ?= 1
ifeq ($(DTLS_RX_IRQ),1)
CFLAGS += -DDTLS_RX_IRQ
endif

boot.elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -T linker.ld -N -o $@ \
		$(OBJECTS) \
		$(PACKAGES:%=-L$(BUILD_DIR)/software/%) \
		-Wl,--whole-archive \
		-Wl,--gc-sections \
		-Wl,-Map,$@.map \
		$(LIBS:lib%=-l%)

ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.o=.d)

donut.o: CFLAGS   += -w

VPATH = $(BIOS_DIRECTORY):$(BIOS_DIRECTORY)/cmds:$(CPU_DIRECTORY)


%.o: %.cpp
	$(compilexx)

%.o: %.c
	$(compile)

%.o: %.S
	$(assemble)

clean:
	$(RM) $(OBJECTS) boot.elf boot.bin .*~ *~

.PHONY: all clean
