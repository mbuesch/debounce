/*
 * Signal debouncer
 *
 * This code is designed to run on an ATmega8/88 with 20MHz or 16MHz clock.
 *
 * Copyright (c) 2008 Michael Buesch <mb@bu3sch.de>
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

/* What is DWELL_TIME and what is ACTIVE_TIME?
 * Consider we have one input signal and one output signal.
 * The timeouts look like this:
 *
 *          ---------------
 *          |             |
 * input    |             |
 * ----------             ----------
 *
 *                ---------------
 *                |             |
 * output         |             |
 * ----------------             -----
 *
 *          ^--v--^       ^--v--^
 *             |             |
 *   ACTIVE_TIME             DWELL_TIME
 *
 * So the ACTIVE_TIME is the time for the output to respond to the input
 * signal and the DWELL_TIME is the additional dwell time the output stays
 * active after the input got deasserted.
 * ACTIVE_TIME should be fairly low; in the range of a few microseconds. It's
 * used for noise-cancelling.
 * Units for ACTIVE_TIME and DWELL_TIME are microseconds.
 */

#include "util.h"

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#define CPU_HZ			MHz(20)
//#define CPU_HZ			MHz(16)

#define MHz(hz)			(1000000ul * (hz))

/* Compat */
#ifdef MCUCSR
# define MCUSR		MCUCSR
#endif
#ifdef TIMSK
# define TIMSK1		TIMSK
#endif
#ifdef TIFR
# define TIFR1		TIFR
#endif


/**
 * struct input_pin - An input pin definition
 *
 * @input_port:		The signal input port. PORTB, PORTC, ...
 * @input_pin:		The signal input pin. PINB, PINC, ...
 * @input_ddr:		Data direction register for input_port.
 * @input_bit:		The bit number on the input_port.
 * @flags:		See enum input_pin_flags.
 */
struct input_pin {
	uint16_t input_port;
	uint16_t input_pin;
	uint16_t input_ddr;
	uint8_t input_bit;
	uint8_t flags;
};
/**
 * enum input_pin_flags - Flags for an input pin
 *
 * @INPUT_PULLUP:	Use pullups for the input pin.
 * @INPUT_INVERT:	Logically invert the input signal.
 */
enum input_pin_flags {
	INPUT_PULLUP		= (1 << 0),
	INPUT_INVERT		= (1 << 1),
};

/**
 * struct output_pin - Level triggered output pin
 *
 * @output_port:	The signal output port. PORTB, PORTC, ...
 * @output_ddr:		Data direction register for output_port.
 * @output_bit:		The bit number on the output_port.
 * @flags:		See enum output_pin_flags.
 */
struct output_pin {
	uint16_t output_port;
	uint16_t output_ddr;
	uint8_t output_bit;
	uint8_t flags;

	/* Trigger level */
	uint8_t level;
};
/**
 * enum output_pin_flags - Flags for an output pin
 *
 * @OUTPUT_INVERT:	Logically invert the output signal.
 */
enum output_pin_flags {
	OUTPUT_INVERT		= (1 << 0),
};

/**
 * struct connection - Logical connection between input and output pins
 *
 * @in:		Definition of the input pin.
 * @out:	Pointer to the output pin.
 */
struct connection {
	struct input_pin in;
	struct output_pin *out;

	bool input_is_asserted;
	uint32_t dwell_timeout;
};

#define DEF_INPUT(portid, bit, _flags)				\
	.in = {							\
		.input_port	= _SFR_ADDR(PORT##portid),	\
		.input_pin	= _SFR_ADDR(PIN##portid),	\
		.input_ddr	= _SFR_ADDR(DDR##portid),	\
		.input_bit	= bit,				\
		.flags		= _flags			\
	}

#define DEF_OUTPUT(portid, bit, _flags)				\
	struct output_pin output_pin_##portid##bit = {		\
		.output_port	= _SFR_ADDR(PORT##portid),	\
		.output_ddr	= _SFR_ADDR(DDR##portid),	\
		.output_bit	= bit,				\
		.flags		= _flags,			\
	}
#define NONE	0



#if TARGET==0
# include "target_cncjoints.c"
#else
# error "You must define a valid build target!"
# error "Example:  make TARGET=0"
# error "See  make help  for more information"
#endif

/* Override dwell times in debugging mode. */
#if DEBUG
# undef DEBOUNCE_DWELL_TIME
# define DEBOUNCE_DWELL_TIME	MSEC_TO_USEC(4000)
# undef DEBOUNCE_ACTIVE_TIME
# define DEBOUNCE_ACTIVE_TIME	MSEC_TO_USEC(2000)
#endif

#define MMIO8(mem_addr)		_MMIO_BYTE(mem_addr)
#define U32(value)		((uint32_t)(value))
#define U64(value)		((uint64_t)(value))



/* System timer calibration. */
#if CPU_HZ == MHz(20)
# define SYSTIMER_TIMERFREQ	(1 << CS11) /* == CPU_HZ/8 */
# define JIFFIES_PER_SECOND	U64(2500000)
#elif CPU_HZ == MHz(16)
# define SYSTIMER_TIMERFREQ	(1 << CS11) /* == CPU_HZ/8 */
# define JIFFIES_PER_SECOND	U64(2000000)
#else
# error "No timer calibration for the selected CPU frequency available."
#endif
/* Convert values to jiffies. (Expensive on non-const values!) */
#define MSEC_TO_JIFFIES(msec)	U32(U64(msec) * JIFFIES_PER_SECOND / U64(1000))
#define USEC_TO_JIFFIES(usec)	U32(U64(usec) * JIFFIES_PER_SECOND / U64(1000000))
/* Convert time values. (Expensive on non-const values!) */
#define USEC_TO_MSEC(usec)	U64(U64(usec) / U64(1000))
#define MSEC_TO_USEC(msec)	U64(U64(msec) * U64(1000))

/* Jiffies timing helpers derived from the Linux Kernel sources.
 * These inlines deal with timer wrapping correctly.
 *
 * time_after(a, b) returns true if the time a is after time b.
 *
 * Do this with "<0" and ">=0" to only test the sign of the result. A
 * good compiler would generate better code (and a really good compiler
 * wouldn't care). Gcc is currently neither.
 */
#define time_after(a, b)	((int32_t)(b) - (int32_t)(a) < 0)
#define time_before(a, b)	time_after(b, a)

/* Upper 16-bit half of the jiffies counter.
 * The lower half is the hardware timer counter. */
static uint16_t jiffies_high16;

/* Timer 1 overflow IRQ handler.
 * This handler is executed on overflow of the (low) hardware part of
 * the jiffies counter. It does only add 0x10000 to the 32bit software
 * counter. So it basically adds 1 to the high 16bit software part of
 * the counter. */

#define JIFFY_ISR_NAME	stringify(TIMER1_OVF_vect)
__asm__(
".text					\n"
".global " JIFFY_ISR_NAME "		\n"
JIFFY_ISR_NAME ":			\n"
"	push r0				\n"
"	in r0, __SREG__			\n"
"	push r16			\n"
"	lds r16, jiffies_high16 + 0	\n"
"	subi r16, lo8(-1)		\n"
"	sts jiffies_high16 + 0, r16	\n"
"	lds r16, jiffies_high16 + 1	\n"
"	sbci r16, hi8(-1)		\n"
"	sts jiffies_high16 + 1, r16	\n"
"	pop r16				\n"
"	out __SREG__, r0		\n"
"	pop r0				\n"
"	reti				\n"
".previous				\n"
);

static uint32_t get_jiffies(void)
{
	uint16_t low;
	uint16_t high;

	/* We protect against (unlikely) overflow-while-read. */
	irq_disable();
	while (1) {
		if (unlikely(TIFR1 & (1 << TOV1))) {
			jiffies_high16++;
			TIFR1 |= (1 << TOV1); /* Clear it */
		}
		mb();
		low = TCNT1;
		high = jiffies_high16;
		mb();
		if (likely(!(TIFR1 & (1 << TOV1))))
			break; /* No overflow */
	}
	irq_enable();

	/* This 16bit shift basically is for free. */
	return ((((uint32_t)high) << 16) | low);
}

/* Put a 5ms signal onto the test pin. */
static void jiffies_test(void)
{
	uint32_t now, next;

	return; /* Disabled */

	irq_enable();
	now = get_jiffies();
	next = now + MSEC_TO_JIFFIES(5);
	while (1) {
		wdt_reset();
		now = get_jiffies();
		if (time_after(now, next)) {
			TEST_PORT ^= (1 << TEST_BIT);
			next = now + MSEC_TO_JIFFIES(5);
		}
	}
}

static void setup_jiffies(void)
{
	/* Initialize the system timer */
	TCCR1A = 0;
	TCCR1B = SYSTIMER_TIMERFREQ; /* Speed */
	TIMSK1 |= (1 << TOIE1); /* Overflow IRQ */
	jiffies_test();
}

/* We can keep this in SRAM. It's not that big. */
static const uint8_t bit2mask_lt[] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
};

/* Convert a bit-number to a bit-mask.
 * Only valid for bitnr<=7.
 */
#define BITMASK(bitnr)	(__builtin_constant_p(bitnr) ? (1 << (bitnr)) : bit2mask_lt[(bitnr)])

/* Set the hardware state of an output pin. */
static inline void output_hw_set(struct output_pin *out, bool state)
{
	if (out->flags & OUTPUT_INVERT)
		state = !state;
	if (state)
		MMIO8(out->output_port) |= BITMASK(out->output_bit);
	else
		MMIO8(out->output_port) &= ~BITMASK(out->output_bit);
}

/* Increment the trigger level of an output. */
static inline void output_level_inc(struct output_pin *out)
{
	if (out->level == 0)
		output_hw_set(out, 1);
	out->level++;
}

/* Decrement the trigger level of an output. */
static inline void output_level_dec(struct output_pin *out)
{
	out->level--;
	if (out->level == 0)
		output_hw_set(out, 0);
}

static void setup_ports(void)
{
	struct connection *conn;
	uint8_t i;
	uint32_t now = get_jiffies();

	for (i = 0; i < ARRAY_SIZE(connections); i++) {
		conn = &(connections[i]);

		/* Init DDR registers */
		MMIO8(conn->in.input_ddr) &= ~BITMASK(conn->in.input_bit);
		MMIO8(conn->out->output_ddr) |= BITMASK(conn->out->output_bit);

		/* Enable/Disable pullup */
		if (conn->in.flags & INPUT_PULLUP)
			MMIO8(conn->in.input_port) |= BITMASK(conn->in.input_bit);
		else
			MMIO8(conn->in.input_port) &= ~BITMASK(conn->in.input_bit);

		/* Disable output signal */
		conn->out->level = 0;
		output_hw_set(conn->out, 0);

		conn->input_is_asserted = 0;
		conn->dwell_timeout = now + USEC_TO_JIFFIES(DEBOUNCE_ACTIVE_TIME);
	}
}

static void scan_one_input_pin(struct connection *conn, uint32_t now)
{
	uint8_t hw_input_asserted;

	/* Get the input state */
	hw_input_asserted = (MMIO8(conn->in.input_pin) & BITMASK(conn->in.input_bit));
	/* The hw input state meaning changes, if PULLUP xor INVERT is used.*/
	if (!!(conn->in.flags & INPUT_PULLUP) ^ !!(conn->in.flags & INPUT_INVERT))
		hw_input_asserted = !hw_input_asserted;

	if (conn->input_is_asserted) {
		/* Signal currently is asserted in software.
		 * Try to detect !hw_input_asserted, but honor the dwell time. */
		if (hw_input_asserted) {
			/* The hardware pin is still active.
			 * Restart the dwell time. */
			conn->dwell_timeout = now + USEC_TO_JIFFIES(DEBOUNCE_DWELL_TIME);
		}
		if (hw_input_asserted || time_before(now, conn->dwell_timeout)) {
			/* wait... */
			return;
		}
		conn->input_is_asserted = 0;
		output_level_dec(conn->out);
		conn->dwell_timeout = now + USEC_TO_JIFFIES(DEBOUNCE_ACTIVE_TIME);
	} else {
		/* Signal currently is _not_ asserted in software.
		 * Try to detect hw_input_asserted, but honor the dwell time. */
		if (!hw_input_asserted) {
			/* The hardware pin still isn't active.
			 * Restart the dwell time. */
			conn->dwell_timeout = now + USEC_TO_JIFFIES(DEBOUNCE_ACTIVE_TIME);
		}
		if (!hw_input_asserted || time_before(now, conn->dwell_timeout)) {
			/* wait... */
			return;
		}
		conn->input_is_asserted = 1;
		output_level_inc(conn->out);
		conn->dwell_timeout = now + USEC_TO_JIFFIES(DEBOUNCE_DWELL_TIME);
	}
}

static void scan_input_pins(void)
{
	uint8_t i;
	uint32_t now;

	while (1) {
		now = get_jiffies();
		for (i = 0; i < ARRAY_SIZE(connections); i++) {
			scan_one_input_pin(&(connections[i]), now);
			wdt_reset();
		}
#if 0
		TEST_PORT ^= (1 << TEST_BIT);
#endif
	}
}

static void major_fault(void)
{
	emergency_shutdown();
	/* Pull test port high for failure indication. */
	TEST_DDR |= (1 << TEST_BIT);
	TEST_PORT |= (1 << TEST_BIT);
	while (1);
}

int main(void)
{
	irq_disable();
	TEST_DDR |= (1 << TEST_BIT);
	TEST_PORT &= ~(1 << TEST_BIT);

	setup_jiffies();
	setup_ports();

#if 0
	/* Check if we had a major fault. */
	if (!(MCUSR & (1 << PORF))) {
		if (MCUSR & (1 << WDRF))
			major_fault(); /* Watchdog triggered */
	}
	MCUSR = 0;

#if !DEBUG
	wdt_enable(WDTO_500MS);
#endif
	wdt_reset();
#endif

	irq_enable();
	scan_input_pins();
}
