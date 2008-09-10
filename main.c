/*
 * Signal debouncer
 *
 * This code is designed to run on an ATmega88 with 20MHz clock.
 *
 * Copyright (c) 2008 Michael Buesch <mb@bu3sch.de>
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>


#define ARRAY_SIZE(x)		(sizeof(x) / sizeof((x)[0]))

/* Memory barrier.
* The CPU doesn't have runtime reordering, so we just
* need a compiler memory clobber. */
#define mb()			__asm__ __volatile__("" : : : "memory")

typedef _Bool			bool;


/* Pin for timer testing. Only if compiled with debugging support. */
#define TIMER_TEST_PORT		PORTC
#define TIMER_TEST_DDR		DDRC
#define TIMER_TEST_BIT		5


#define DEBOUNCE_DWELL_TIME	10 /* centiseconds */

#if DEBUG
# undef DEBOUNCE_DWELL_TIME
# define DEBOUNCE_DWELL_TIME	200
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
	bool output_is_asserted;
	uint8_t dwell_time;
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



#define MMIO8(mem_addr)		_MMIO_BYTE(mem_addr)


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

	for (i = 0; i < ARRAY_SIZE(connections); i++) {
		conn = &(connections[i]);

		/* Init DDR registers */
		MMIO8(conn->in.input_ddr) &= ~(1 << conn->in.input_bit);
		MMIO8(conn->out->output_ddr) |= (1 << conn->out->output_bit);
		conn->input_is_asserted = 0;

		/* Enable/Disable pullup */
		if (conn->in.flags & INPUT_PULLUP)
			MMIO8(conn->in.input_port) |= (1 << conn->in.input_bit);
		else
			MMIO8(conn->in.input_port) &= ~(1 << conn->in.input_bit);

		/* Disable output signal */
		conn->out->level = 0;
		conn->output_is_asserted = 0;
		output_hw_set(conn->out, 0);
	}
}

static void scan_one_input_pin(struct connection *conn)
{
	uint8_t hw_input_asserted;

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
		 * Wait for the dwell time... */
		cli();
		if (hw_input_asserted) {
			/* The hardware pin is still active,
			 * restart the dwell time. */
			conn->dwell_time = 0;
		}
		if (conn->dwell_time < DEBOUNCE_DWELL_TIME) {
			sei();
			/* wait... */
			return;
		}
		conn->input_is_asserted = 0;
		sei();
	}

	if (hw_input_asserted) {
		if (!conn->output_is_asserted) {
			conn->output_is_asserted = 1;
			output_level_inc(conn->out);
		}
		conn->dwell_time = 0;
		mb();
		conn->input_is_asserted = 1;
	} else {
		if (conn->output_is_asserted) {
			conn->output_is_asserted = 0;
			output_level_dec(conn->out);
		}
	}
}

static void scan_input_pins(void)
{
	uint8_t i;

	while (1) {
		for (i = 0; i < ARRAY_SIZE(connections); i++) {
			scan_one_input_pin(&(connections[i]));
			wdt_reset();
		}
	}
}

/* System jiffies timer at 100Hz */
ISR(TIMER1_COMPA_vect)
{
	struct connection *conn;
	uint8_t i;

	for (i = 0; i < ARRAY_SIZE(connections); i++) {
		conn = &(connections[i]);
		if (conn->input_is_asserted) {
			if (conn->dwell_time < 0xFF)
				conn->dwell_time++;
		}
	}

	/* Timer debugging: In debugging mode, put a signal on the I/O pin
	 * for testing the timer frequency with an oscilloscope. */
#if DEBUG
	TIMER_TEST_DDR |= (1 << TIMER_TEST_BIT);
	TIMER_TEST_PORT ^= (1 << TIMER_TEST_BIT);
#endif
}

/* System timer calibration. Calibrated to 100Hz (20Mhz crystal) */
#define SYSTIMER_TIMERFREQ	((1 << CS10) | (1 << CS12)) /* == CPU_HZ/1024 */
#define SYSTIMER_CMPVAL		195

static void setup_jiffies(void)
{
	/* Initialize the system timer */
	TCCR1B = (1 << WGM12) | SYSTIMER_TIMERFREQ; /* Speed */
	OCR1A = SYSTIMER_CMPVAL; /* CompareMatch value */
	TIMSK1 |= (1 << OCIE1A); /* IRQ mask */
}

int main(void)
{
	cli();
#if !DEBUG
	wdt_enable(WDTO_500MS);
#endif
	wdt_reset();

	setup_jiffies();
	setup_ports();

	sei();

	scan_input_pins();
}
