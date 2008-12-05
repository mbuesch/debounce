/*
 * Input->output connection definitions
 * for the joint-switches of a CNC machining center.
 */

static DEF_OUTPUT(C, 5, OUTPUT_INVERT);	/* X joint limit */
static DEF_OUTPUT(C, 4, NONE);		/* X joint REF */
static DEF_OUTPUT(C, 3, OUTPUT_INVERT);	/* Y joint limit */
static DEF_OUTPUT(C, 2, NONE);		/* Y joint REF */
static DEF_OUTPUT(C, 1, OUTPUT_INVERT);	/* Z joint limit */
static DEF_OUTPUT(C, 0, NONE);		/* Z joint REF */

static struct connection connections[] = {
	{ /* X+ joint limit input --> Joint limits common output */
		DEF_INPUT(D, 0, INPUT_PULLUP),
		.out = &output_pin_C5,
	},
	{ /* X- joint limit input --> Joint limits common output */
		DEF_INPUT(D, 1, INPUT_PULLUP),
		.out = &output_pin_C5,
	},
	{ /* X joint REF input --> X joint REF output */
		DEF_INPUT(D, 2, INPUT_PULLUP),
		.out = &output_pin_C4,
	},
	{ /* Y+ joint limit input --> Joint limits common output */
		DEF_INPUT(D, 3, INPUT_PULLUP),
		.out = &output_pin_C3,
	},
	{ /* Y- joint limit input --> Joint limits common output */
		DEF_INPUT(D, 4, INPUT_PULLUP),
		.out = &output_pin_C3,
	},
	{ /* Y joint REF input --> Y joint REF output */
		DEF_INPUT(D, 5, INPUT_PULLUP),
		.out = &output_pin_C2,
	},
	{ /* Z+ joint limit input --> Joint limits common output */
		DEF_INPUT(D, 6, INPUT_PULLUP),
		.out = &output_pin_C1,
	},
	{ /* Z- joint limit input --> Joint limits common output */
		DEF_INPUT(D, 7, INPUT_PULLUP),
		.out = &output_pin_C1,
	},
	{ /* Z joint REF input --> Z joint REF output */
		DEF_INPUT(B, 0, INPUT_PULLUP),
		.out = &output_pin_C0,
	},
};

static void emergency_shutdown(void)
{
	/* Assert all limit pins.
	 * Limit pins are active-low, so clear the bit. */
	PORTC &= ~(1 << 5);
	PORTC &= ~(1 << 3);
	PORTC &= ~(1 << 1);
}

/* Pin for timer testing. */
#define TIMER_TEST_PORT		PORTB
#define TIMER_TEST_DDR		DDRB
#define TIMER_TEST_BIT		1

/* Debounce timing. */

/* Dwell-time: Keep the output-pin asserted for this time
 *             after the input-pin was last successfully detected
 *             in asserted state. */
#define DEBOUNCE_DWELL_TIME	MSEC_TO_USEC(100) /* microseconds */
/* Active-time: The input pin must stay at least this time
 *              physically asserted to switch to asserted state
 *              in software. */
#define DEBOUNCE_ACTIVE_TIME	150 /* microseconds */
