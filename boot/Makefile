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
