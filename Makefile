DEBUG		?= 0

ARCH		?= atmega88
AVRDUDE_ARCH	?= m88
AVRDUDE		?= avrdude
AVRDUDE_SPEED	?= 1
PROGRAMMER	?= avrisp2
PROGPORT	?= usb

CC		= avr-gcc
OBJCOPY		= avr-objcopy
SIZE		= avr-size

CFLAGS		= -mmcu=$(ARCH) -std=c99 -g0 -O2 -fomit-frame-pointer -Wall -fpack-struct
CFLAGS		+= "-Dinline=inline __attribute__((__always_inline__))"
TARGET		?= 0 # default
CFLAGS		+= -DDEBUG=$(DEBUG) -DTARGET=$(TARGET)


# The fuse bits
# Ext Clock, Startup 6CK/14CK + 65ms
# BOD 4.3V
# SPI enabled
LFUSE	= 0xE0
HFUSE	= 0xDC
EFUSE	= 0xF9

OBJECTS = main.o
BIN	= debounce.bin
HEX	= debounce.hex
EEP	= debounce.eep.hex

all: $(HEX)

main.o: target_cncjoints.c

%.s: %.c
	$(CC) $(CFLAGS) -S $*.c

$(BIN): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(BIN) $(OBJECTS) $(LDFLAGS)

$(HEX): $(BIN)
	$(OBJCOPY) -R.eeprom -O ihex $(BIN) $(HEX)
#	$(OBJCOPY) -j.eeprom --set-section-flags=.eeprom="alloc,load" \
#		   --change-section-lma .eeprom=0 -O ihex $(BIN) $(EEP)
	$(SIZE) $(BIN)

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
	-rm -f *~ *.o $(BIN)

distclean: clean
	-rm -f *.s $(HEX) $(EEP)

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
