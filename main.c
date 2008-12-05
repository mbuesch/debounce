/*
 * Signal debouncer
 *
 * This code is designed to run on an ATmega88 with 20MHz or 16MHz clock.
 *
 * Copyright (c) 2008 Michael Buesch <mb@bu3sch.de>
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

//#define CPU_HZ			MHz(20)
#define CPU_HZ			MHz(16)


#define ARRAY_SIZE(x)		(sizeof(x) / sizeof((x)[0]))
#define MHz(hz)			(1000000ul * (hz))

/* Memory barrier.
 * The CPU doesn't have runtime reordering, so we just
 * need a compiler memory clobber. */
#define mb()			__asm__ __volatile__("" : : : "memory")

typedef _Bool			bool;


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
 * The lower half is the hardware timer counter.
 * We keep this in a 32bit variable to avoid the need for bitshifting
 * in get_jiffies(). (Bitshift>1 is expensive on AVR).
 * So the lower 16 bits of jiffies_high16 will always be zero. */
static uint32_t jiffies_high16;

static void setup_jiffies(void)
{
	/* Initialize the system timer */
	TCCR1A = 0;
	TCCR1B = SYSTIMER_TIMERFREQ; /* Speed */
}

//TODO: We could probably do this with the overflow IRQ.
static inline bool jiffies_maintanance(void)
{
	bool overflow = 0;
	uint16_t low16;
	static uint16_t last_low16;

	/* Check for a carry in the low 16bits (hardware counter)
	 * and increment the high software part in case it overflew.
	 * This is based on the assumption that this function is
	 * called at least once per possible overflow interval.
	 * That means we must ensure a call every 20 ms (or more often). */
	low16 = TCNT1;
	if (low16 < last_low16) {
		/* Carry detected */
		jiffies_high16 += 0x10000ul;
		overflow = 1;
	}
	last_low16 = low16;

	return overflow;
}

static uint32_t get_jiffies(void)
{
	uint32_t low, high;

	/* Protect against (unlikely) overflow-while-read. */
	jiffies_maintanance();
	while (1) {
		mb();
		low = TCNT1;
		high = jiffies_high16;
		mb();
		if (!jiffies_maintanance())
			break; /* No overflow */
	}

	return (low | high);
}

/* Put a 5ms signal onto the test pin. */
static inline void jiffies_test(void)
{
	uint32_t now, next;

	now = get_jiffies();
	next = now + MSEC_TO_JIFFIES(5);
	while (1) {
		jiffies_maintanance();
		now = get_jiffies();
		if (time_after(now, next)) {
			TIMER_TEST_PORT ^= (1 << TIMER_TEST_BIT);
			next = now + MSEC_TO_JIFFIES(5);
		}
	}
}

/* Set the hardware state of an output pin. */
static inline void output_hw_set(struct output_pin *out, bool state)
{
	if (out->flags & OUTPUT_INVERT)
		state = !state;
	if (state)
		MMIO8(out->output_port) |= (1 << out->output_bit);
	else
		MMIO8(out->output_port) &= ~(1 << out->output_bit);
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
		MMIO8(conn->in.input_ddr) &= ~(1 << conn->in.input_bit);
		MMIO8(conn->out->output_ddr) |= (1 << conn->out->output_bit);

		/* Enable/Disable pullup */
		if (conn->in.flags & INPUT_PULLUP)
			MMIO8(conn->in.input_port) |= (1 << conn->in.input_bit);
		else
			MMIO8(conn->in.input_port) &= ~(1 << conn->in.input_bit);

		/* Disable output signal */
		conn->out->level = 0;
		output_hw_set(conn->out, 0);

		conn->input_is_asserted = 0;
		conn->dwell_timeout = now + USEC_TO_JIFFIES(DEBOUNCE_ACTIVE_TIME);
	}
}

static void scan_one_input_pin(struct connection *conn)
{
	uint8_t hw_input_asserted;
	uint32_t now = get_jiffies();

	/* Get the input state */
	hw_input_asserted = (MMIO8(conn->in.input_pin) & (1 << conn->in.input_bit));
	if (conn->in.flags & INPUT_PULLUP) {
		/* With pullup the logical state is flipped */
		hw_input_asserted = !hw_input_asserted;
	}
	if (conn->in.flags & INPUT_INVERT) {
		/* User requested invert */
		hw_input_asserted = !hw_input_asserted;
	}

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

	while (1) {
		for (i = 0; i < ARRAY_SIZE(connections); i++) {
			jiffies_maintanance();
			scan_one_input_pin(&(connections[i]));
			wdt_reset();
		}
	}
}

int main(void)
{
	cli();

//FIXME
#if 0
	/* Check if we had a major hardware fault. */
	if ((MCUSR & (1 << WDRF)) ||
	    (MCUSR & (1 << BORF))) {
		emergency_shutdown();
		while (1);
	}
#endif

	TIMER_TEST_DDR |= (1 << TIMER_TEST_BIT);
#if !DEBUG
	wdt_enable(WDTO_500MS);
#endif
	wdt_reset();

	setup_jiffies();
	jiffies_test();
	setup_ports();

	scan_input_pins();
}
