/*
 * Input->output connection definitions
 * for the joint-switches of a CNC machining center.
 */

static DEF_OUTPUT(C, 5, NONE); /* Joint limits common */
static DEF_OUTPUT(C, 4, NONE); /* X joint REF */
static DEF_OUTPUT(C, 3, NONE); /* Y joint REF */
static DEF_OUTPUT(C, 2, NONE); /* Z joint REF */

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
		.out = &output_pin_C5,
	},
	{ /* Y- joint limit input --> Joint limits common output */
		DEF_INPUT(D, 4, INPUT_PULLUP),
		.out = &output_pin_C5,
	},
	{ /* Y joint REF input --> Y joint REF output */
		DEF_INPUT(D, 5, INPUT_PULLUP),
		.out = &output_pin_C3,
	},
	{ /* Z+ joint limit input --> Joint limits common output */
		DEF_INPUT(D, 6, INPUT_PULLUP),
		.out = &output_pin_C5,
	},
	{ /* Z- joint limit input --> Joint limits common output */
		DEF_INPUT(D, 7, INPUT_PULLUP),
		.out = &output_pin_C5,
	},
	{ /* Z joint REF input --> Z joint REF output */
		DEF_INPUT(B, 0, INPUT_PULLUP),
		.out = &output_pin_C2,
	},
};

/* Pin for timer testing. */
#define TIMER_TEST_PORT		PORTB
#define TIMER_TEST_DDR		DDRB
#define TIMER_TEST_BIT		1

/* Debounce timing. */
#define DEBOUNCE_DWELL_TIME	10 /* centiseconds */
