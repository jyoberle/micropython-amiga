include ../../py/mkenv.mk
-include mpconfigport.mk

ifdef OS
	WINDOWS = 1
	SHELL = cmd.exe
endif

BUILD_VERBOSE = 1
MKDIR = mkdir.exe
CP = cp.exe
SED = sed.exe
RM = rm.exe
TOUCH = touch.exe
CAT = cat.exe
PYTHON = python
CC = m68k-amiga-elf-gcc
ELF2HUNK = elf2hunk.exe
program = ../out/a
OUT = ../../$(program)
CLEAN_EXTRA = $(TOP)/mpy-cross/build # to clean also mpy-cross

ifdef WINDOWS
	SDKDIR = $(abspath $(dir $(shell where $(CC)))..\m68k-amiga-elf\sys-include)
	MUISDKDIR = $(abspath $(dir $(shell where $(CC)))..\m68k-amiga-elf\sys-include\SDK\MUI\C\include)
else
	SDKDIR = $(abspath $(dir $(shell which $(CC)))../m68k-amiga-elf/sys-include)
	MUISDKDIR = $(abspath $(dir $(shell which $(CC)))../m68k-amiga-elf/sys-include/SDK/MUI/C/include)
endif

# The directory where the static libraries are located
#LIBSDIR = $(abspath ../libs)
LIBSDIR = ../../libs

CROSS = 0

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# MicroPython feature configurations
MICROPY_ROM_TEXT_COMPRESSION ?= 1

FROZEN_MANIFEST ?= manifest.py

# include py core make definitions
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

ifeq ($(CROSS), 1)
CROSS_COMPILE ?= arm-none-eabi-
endif

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(MUISDKDIR)
INC += -I$(LIBSDIR)/library
INC += -I$(LIBSDIR)/library/include

ifeq ($(CROSS), 1)
DFU = $(TOP)/tools/dfu.py
PYDFU = $(TOP)/tools/pydfu.py
CFLAGS_CORTEX_M4 = -mthumb -mtune=cortex-m4 -mcpu=cortex-m4 -msoft-float -fsingle-precision-constant -Wdouble-promotion -Wfloat-conversion
CFLAGS += $(INC) -Wall -Werror -std=c99 -nostdlib $(CFLAGS_CORTEX_M4) $(COPT)
LDFLAGS += -nostdlib -T stm32f405.ld -Map=$@.map --cref --gc-sections
else
LD = $(CC)
CFLAGS += $(INC) -Wall -Werror -Wdouble-promotion -Wfloat-conversion -std=c99 $(COPT)
LDFLAGS += -Wl,-Map=$@.map,--cref -Wl,--gc-sections
endif

CFLAGS += -Wno-unused-parameter -Wno-attributes -Wno-error -fno-builtin -g -MP -MMD -m68000 -Ofast -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fomit-frame-pointer -fno-tree-loop-distribution -flto -fno-exceptions -DMP_ENDIANNESS_BIG -U__i386__ -D__stdargs= 
static_libs := -lm -ldebug -lnet -lunix -lc -lm -lamiga # to use UNIX paths

LDFLAGS += -Wno-attributes -nostartfiles -Wl,--emit-relocs,-Ttext=0

CSUPEROPT = -Os # save some code space

# Tune for Debugging or Optimization
CFLAGS += -g  # always include debug info in the ELF
ifeq ($(DEBUG), 1)
CFLAGS += -O0
else
CFLAGS += -Os -DNDEBUG
CFLAGS += -fdata-sections -ffunction-sections
endif

# Flags for optional C++ source code
CXXFLAGS += $(filter-out -std=c99,$(CFLAGS))

LIBS = -L${LIBSDIR} $(static_libs)

SRC_C = \
	main.c \
	mphalport.c \
	input.c \
	vfs_amiga.c \
	vfs_amiga_file.c \
	modsocket.c

SHARED_SRC_C += $(addprefix shared/,\
	runtime/gchelper_generic.c \
	runtime/pyexec.c \
	runtime/stdout_helpers.c \
	libc/printf.c \
	readline/readline.c \
	runtime/interrupt_char.c \
	)


$(info PY_O = $(PY_O))
OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_CXX:.cpp=.o))
OBJ += $(addprefix $(BUILD)/, $(SHARED_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(EXTMOD_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(LIB_SRC_C:.c=.o))

# List of sources for qstr extraction
SRC_QSTR += $(SRC_C) $(SRC_CXX) $(SHARED_SRC_C) $(EXTMOD_SRC_C)

ifeq ($(CROSS), 1)
all: $(BUILD)/firmware.dfu
else
all: $(OUT).exe  $(BUILD)/frozen_content.c
endif

$(OUT).exe: $(BUILD)/firmware.elf
	@$(CP) $^ $(OUT).elf
	$(ELF2HUNK) $^ $@ -s

$(BUILD)/firmware.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(Q)$(SIZE) $@

$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(Q)$(OBJCOPY) -O binary -j .isr_vector -j .text -j .data $^ $(BUILD)/firmware.bin

$(BUILD)/firmware.dfu: $(BUILD)/firmware.bin
	$(ECHO) "Create $@"
	$(Q)$(PYTHON) $(DFU) -b 0x08000000:$(BUILD)/firmware.bin $@

include $(TOP)/py/mkrules.mk
