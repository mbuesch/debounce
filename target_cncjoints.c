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
		DEF_INPUT(D, 0, NONE),
		.out = &output_pin_C5,
	},
	{ /* X- joint limit input --> Joint limits common output */
		DEF_INPUT(D, 1, NONE),
		.out = &output_pin_C5,
	},
	{ /* X joint REF input --> X joint REF output */
		DEF_INPUT(D, 2, NONE),
		.out = &output_pin_C4,
	},
	{ /* Y+ joint limit input --> Joint limits common output */
		DEF_INPUT(D, 3, NONE),
		.out = &output_pin_C3,
	},
	{ /* Y- joint limit input --> Joint limits common output */
		DEF_INPUT(D, 4, NONE),
		.out = &output_pin_C3,
	},
	{ /* Y joint REF input --> Y joint REF output */
		DEF_INPUT(D, 5, NONE),
		.out = &output_pin_C2,
	},
	{ /* Z+ joint limit input --> Joint limits common output */
		DEF_INPUT(D, 6, NONE),
		.out = &output_pin_C1,
	},
	{ /* Z- joint limit input --> Joint limits common output */
		DEF_INPUT(D, 7, NONE),
		.out = &output_pin_C1,
	},
	{ /* Z joint REF input --> Z joint REF output */
		DEF_INPUT(B, 0, NONE),
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

/* Pin for debugging. */
#define TEST_PORT		PORTB
#define TEST_DDR		DDRB
#define TEST_BIT		1

/* Debounce timing. */

#define DEBOUNCE_DWELL_TIME	MSEC_TO_USEC(100)
/* We tolerate a joint move of max 5 microns for the ACTIVE_TIME.
 * That's good enough for limits and refs. */
#define DEBOUNCE_ACTIVE_TIME	200 /* microseconds */
