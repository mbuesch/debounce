/*
 * Signal debouncer
 *
 * The debouncing strategy is:
 * 	Trigger OUT immediately when IN is logical 1.
 * 	Ignore the IN signal for a certain dwell time.
 * 	Switch OUT off after the dwell time, if IN is no longer logical 1.
 * 	This ensures a low logical 1 latency.
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
typedef _Bool			bool;


/**
 * struct pin - Digital signal line for debouncing
 *
 * @input_port:		The signal input port. PORTB, PORTC, ...
 * @input_pin:		The signal input pin. PINB, PINC, ...
 * @input_ddr:		Data direction register for input_port.
 * @input_bit:		The bit number on the input_port.
 *
 * @output_port:	The signal output port. PORTB, PORTC, ...
 * @output_ddr:		Data direction register for output_port.
 * @output_bit:		The bit number on the output_port.
 *
 * @flags:		INPUT_PULLUP, OUTPUT_INVERT.
 */
struct pin {
	uint8_t input_port;
	uint8_t input_pin;
	uint8_t input_ddr;
	uint8_t input_bit;
	uint8_t output_port;
	uint8_t output_ddr;
	uint8_t output_bit;
	uint8_t flags;

	uint8_t dwell_time;
};

enum { /* flags */
	/* Static configuration flags */
	INPUT_PULLUP		= (1 << 0),
	OUTPUT_INVERT		= (1 << 1),

	/* Dynamic runtime flags.
	 * Do not set in static config below. */
	IS_ASSERTED		= (1 << 2),
	LOGICAL_OUT_ON		= (1 << 3),
};

#define DEF_INPUT(portid, bit)				\
	.input_port	= _SFR_ADDR(PORT##portid),	\
	.input_pin	= _SFR_ADDR(PIN##portid),	\
	.input_ddr	= _SFR_ADDR(DDR##portid),	\
	.input_bit	= bit

#define DEF_OUTPUT(portid, bit)				\
	.output_port	= _SFR_ADDR(PORT##portid),	\
	.output_ddr	= _SFR_ADDR(DDR##portid),	\
	.output_bit	= bit

static struct pin pins[] = {
	{ /* B1 -> B0 */
		DEF_INPUT(B, 1),
		DEF_OUTPUT(B, 0),
		.flags		= INPUT_PULLUP,
	},
	{ /* B2 -> D7 */
		DEF_INPUT(B, 2),
		DEF_OUTPUT(D, 7),
		.flags		= INPUT_PULLUP,
	},
	{ /* B3 -> D6 */
		DEF_INPUT(B, 3),
		DEF_OUTPUT(D, 6),
		.flags		= INPUT_PULLUP,
	},
	{ /* B4 -> D5 */
		DEF_INPUT(B, 4),
		DEF_OUTPUT(D, 5),
		.flags		= INPUT_PULLUP,
	},
	{ /* C0 -> D4 */
		DEF_INPUT(C, 0),
		DEF_OUTPUT(D, 4),
		.flags		= INPUT_PULLUP,
	},
	{ /* C1 -> D3 */
		DEF_INPUT(C, 1),
		DEF_OUTPUT(D, 3),
		.flags		= INPUT_PULLUP,
	},
	{ /* C2 -> D2 */
		DEF_INPUT(C, 2),
		DEF_OUTPUT(D, 2),
		.flags		= INPUT_PULLUP,
	},
	{ /* C3 -> D1 */
		DEF_INPUT(C, 3),
		DEF_OUTPUT(D, 1),
		.flags		= INPUT_PULLUP,
	},
	{ /* C4 -> D0 */
		DEF_INPUT(C, 4),
		DEF_OUTPUT(D, 0),
		.flags		= INPUT_PULLUP,
	},
};

/* Pin for timer testing. Only if compiled with debugging support. */
#define TIMER_TEST_PORT		PORTC
#define TIMER_TEST_DDR		DDRC
#define TIMER_TEST_BIT		5


#define MMIO8(mem_addr)		_MMIO_BYTE((uint16_t)(mem_addr))

/* Set the output pin to a new state */
static inline void output_set(struct pin *pin, bool state)
{
	if (state)
		pin->flags |= LOGICAL_OUT_ON;
	else
		pin->flags &= ~LOGICAL_OUT_ON;

	if (pin->flags & OUTPUT_INVERT)
		state = !state;
	if (state)
		MMIO8(pin->output_port) |= (1 << pin->output_bit);
	else
		MMIO8(pin->output_port) &= ~(1 << pin->output_bit);
}

static void setup_ports(void)
{
	struct pin *pin;
	uint8_t i;

	for (i = 0; i < ARRAY_SIZE(pins); i++) {
		pin = &(pins[i]);

		/* Init DDR registers */
		MMIO8(pin->input_ddr) &= ~(1 << pin->input_bit);
		MMIO8(pin->output_ddr) |= (1 << pin->output_bit);

		/* Enable/Disable pullup */
		if (pin->flags & INPUT_PULLUP)
			MMIO8(pin->input_port) |= (1 << pin->input_bit);
		else
			MMIO8(pin->input_port) &= ~(1 << pin->input_bit);

		/* Disable output signal */
		output_set(pin, 0);
	}
}

static void scan_one_pin(struct pin *pin)
{
	uint8_t hw_input_asserted;

	/* Get the input state */
	hw_input_asserted = (MMIO8(pin->input_pin) & (1 << pin->input_bit));
	if (pin->flags & INPUT_PULLUP) {
		/* With pullup the logical state is flipped */
		hw_input_asserted = !hw_input_asserted;
	}

	if (pin->flags & IS_ASSERTED) {
		/* Signal currently is asserted in software.
		 * Wait for the dwell time... */
		cli();
		if (hw_input_asserted) {
			/* The hardware pin is still active,
			 * restart the dwell time. */
			pin->dwell_time = 0;
		}
		if (pin->dwell_time < 10) {
			sei();
			/* wait... */
			return;
		}
		pin->flags &= ~IS_ASSERTED;
		sei();
	}

	if (hw_input_asserted) {
		if (!(pin->flags & LOGICAL_OUT_ON))
			output_set(pin, 1);
		cli();
		pin->flags |= IS_ASSERTED;
		pin->dwell_time = 0;
		sei();
	} else {
		if (pin->flags & LOGICAL_OUT_ON)
			output_set(pin, 0);
	}
}

static void scan_input_pins(void)
{
	uint8_t i;

	while (1) {
		for (i = 0; i < ARRAY_SIZE(pins); i++) {
			scan_one_pin(&(pins[i]));
			wdt_reset();
		}
	}
}

/* System jiffies timer at 100Hz */
ISR(TIMER1_COMPA_vect)
{
	struct pin *pin;
	uint8_t i;

	for (i = 0; i < ARRAY_SIZE(pins); i++) {
		pin = &(pins[i]);
		if (pin->flags & IS_ASSERTED) {
			if (pin->dwell_time < 0xFF)
				pin->dwell_time++;
		}
	}

	/* Timer debugging: In debugging mode, put a signal on the I/O pin
	 * for testing the timer frequency with an oscilloscope. */
#if DEBUG
	TIMER_TEST_DDR |= (1 << TIMER_TEST_BIT);
	TIMER_TEST_PORT ^= (1 << TIMER_TEST_BIT);
#endif
}

/* System timer calibration. Calibrated to 100Hz (16Mhz crystal) */
#define SYSTIMER_TIMERFREQ	((1 << CS10) | (1 << CS12)) /* == CPU_HZ/1024 */
#define SYSTIMER_CMPVAL		156

static void setup_jiffies(void)
{
	/* Initialize the system timer */
	TCCR1B = (1 << WGM12) | SYSTIMER_TIMERFREQ; /* Speed */
	OCR1A = SYSTIMER_CMPVAL; /* CompareMatch value */
	TIMSK |= (1 << OCIE1A); /* IRQ mask */
}

int main(void)
{
	cli();

	wdt_enable(WDTO_500MS);
	wdt_reset();

	setup_jiffies();
	setup_ports();

	sei();

	scan_input_pins();
}
