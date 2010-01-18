# The architecture configuration
ARCH		= atmega88
AVRDUDE_ARCH	= m88
AVRDUDE		= avrdude
AVRDUDE_SPEED	= 1
PROGRAMMER	= avrisp2
PROGPORT	= usb

# The toolchain definitions
CC		= avr-gcc
OBJCOPY		= avr-objcopy
SIZE		= avr-size
READELF		= avr-readelf
SPARSE		= sparse

TARGET		= 0		# Target selection:  make TARGET=0
DEBUG		= 0		# Debug build:  make DEBUG=1

V		= @		# Verbose build:  make V=1
C		= 0		# Sparsechecker build:  make C=1
Q		= $(V:1=)
QUIET_CC	= $(Q:@=@echo '     CC       '$@;)$(CC)
QUIET_DEPEND	= $(Q:@=@echo '     DEPEND   '$@;)$(CC)
QUIET_OBJCOPY	= $(Q:@=@echo '     OBJCOPY  '$@;)$(OBJCOPY)
QUIET_SIZE	= $(Q:@=@echo '     SIZE     '$@;)$(SIZE)
QUIET_READELF	= $(Q:@=@echo '     READELF  '$@;)$(READELF)
ifeq ($(C),1)
QUIET_SPARSE	= $(Q:@=@echo '     SPARSE   '$@;)$(SPARSE)
else
QUIET_SPARSE	= @/bin/true
endif

CFLAGS		= -mmcu=$(ARCH) -std=c99 -O2 -Wall \
		  "-Dinline=inline __attribute__((__always_inline__))" \
		  -DDEBUG=$(DEBUG) -DTARGET=$(TARGET)

SPARSEFLAGS	= $(CFLAGS) -I "/usr/lib/avr/include" -D__AVR_ARCH__=5 \
		  -D__AVR_ATmega88__=1 -D__ATTR_PROGMEM__="" -Dsignal=dllexport \
		  -Dexternally_visible=dllexport


# The fuse bits
# Ext Clock, Startup 6CK/14CK + 65ms
# BOD off
# SPI enabled
LFUSE	= 0xE0
HFUSE	= 0xDF
EFUSE	= 0xF9

SRCS	= main.c
NAME	= debounce
BIN	= $(NAME).bin
HEX	= $(NAME).hex
EEP	= $(NAME).eep.hex

.SUFFIXES:
.PHONY: all avrdude install_flash install_eeprom install reset writefuse clean distclean
.DEFAULT_GOAL := all

DEPS = $(sort $(patsubst %.c,dep/%.d,$(1)))
OBJS = $(sort $(patsubst %.c,obj/%.o,$(1)))

# Generate dependencies
$(call DEPS,$(SRCS)): dep/%.d: %.c 
	@mkdir -p $(dir $@)
	$(QUIET_DEPEND) -o $@.tmp -MM -MG -MT "$@ $(patsubst dep/%.d,obj/%.o,$@)" $(CFLAGS) $< && mv -f $@.tmp $@

-include $(call DEPS,$(SRCS))

# Generate object files
$(call OBJS,$(SRCS)): obj/%.o:
	@mkdir -p $(dir $@)
	$(QUIET_SPARSE) $(SPARSEFLAGS) $<
	$(QUIET_CC) -o $@ -c $(CFLAGS) $<

all: $(HEX)

%.s: %.c
	$(QUIET_CC) $(CFLAGS) -S $*.c

$(BIN): $(call OBJS,$(SRCS))
	$(QUIET_CC) $(CFLAGS) -o $(BIN) $(call OBJS,$(SRCS)) $(LDFLAGS)

$(HEX): $(BIN)
	$(QUIET_OBJCOPY) -R.eeprom -O ihex $(BIN) $(HEX)
#	$(QUIET_OBJCOPY) -j.eeprom --set-section-flags=.eeprom="alloc,load" \
#			 --change-section-lma .eeprom=0 -O ihex $(BIN) $(EEP)
	$(QUIET_SIZE) $(BIN)
	$(QUIET_READELF) -S $(BIN) | egrep '(Name|text|eeprom|data|bss)'

avrdude:
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) -t

install_flash:
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) -U flash:w:$(HEX)

install_eeprom:
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) -U eeprom:w:$(EEP)

install: all install_flash

# Reset the microcontroller through avrdude
reset:
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) \
	 -U signature:r:/dev/null:i -q -q

writefuse:
	$(AVRDUDE) -B 5 -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) -q -q \
	 -U lfuse:w:$(LFUSE):m \
	 -U hfuse:w:$(HFUSE):m
#	 -U efuse:w:$(EFUSE):m

clean:
	rm -Rf *~ *.o obj dep $(BIN)

distclean: clean
	rm -f *.s $(HEX) $(EEP)

help:
	@echo "Debouncer Makefile"
	@echo ""
	@echo "BUILD TARGETS  (make TARGET=x):"
	@echo "  TARGET=0 - Build target for \"cncjoints\""
	@echo ""
	@echo ""
	@echo "Cleanup:"
	@echo "  all       - build the firmware (default target)"
	@echo "  clean     - remove object files"
	@echo "  distclean - remove object, binary and hex files"
	@echo ""
	@echo "avrdude operations:"
	@echo "  install   - flash the program code"
	@echo "  writefuse - write the fuse bits"
	@echo "  reset     - pull the external device reset pin"
	@echo "  avrdude   - run avrdude in interactive mode"
	@echo ""
	@echo "Generic:"
	@echo "  *.s       - create an assembly file from a *.c file"
