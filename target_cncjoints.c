/*
 * Input->output connection definitions
 * for the joint-switches of a CNC machining center.
 */

static DEF_OUTPUT(B, 0, NONE); /* Joint limits common */
static DEF_OUTPUT(D, 7, NONE); /* X joint REF */
static DEF_OUTPUT(D, 6, NONE); /* Y joint REF */
static DEF_OUTPUT(D, 5, NONE); /* Z joint REF */
#if 0
static DEF_OUTPUT(D, 4, NONE);
static DEF_OUTPUT(D, 3, NONE);
static DEF_OUTPUT(D, 2, NONE);
static DEF_OUTPUT(D, 1, NONE);
static DEF_OUTPUT(D, 0, NONE);
#endif

static struct connection connections[] = {
	{ /* X+ joint limit input --> Joint limits common output */
		DEF_INPUT(B, 1, INPUT_PULLUP),
		.out = &output_pin_B0,
	},
	{ /* X- joint limit input --> Joint limits common output */
		DEF_INPUT(B, 2, INPUT_PULLUP),
		.out = &output_pin_B0,
	},
	{ /* X joint REF input --> X joint REF output */
		DEF_INPUT(B, 3, INPUT_PULLUP),
		.out = &output_pin_D7,
	},
	{ /* Y+ joint limit input --> Joint limits common output */
		DEF_INPUT(B, 4, INPUT_PULLUP),
		.out = &output_pin_B0,
	},
	{ /* Y- joint limit input --> Joint limits common output */
		DEF_INPUT(C, 0, INPUT_PULLUP),
		.out = &output_pin_B0,
	},
	{ /* Y joint REF input --> Y joint REF output */
		DEF_INPUT(C, 1, INPUT_PULLUP),
		.out = &output_pin_D6,
	},
	{ /* Z+ joint limit input --> Joint limits common output */
		DEF_INPUT(C, 2, INPUT_PULLUP),
		.out = &output_pin_B0,
	},
	{ /* Z- joint limit input --> Joint limits common output */
		DEF_INPUT(C, 3, INPUT_PULLUP),
		.out = &output_pin_B0,
	},
	{ /* Z joint REF input --> Z joint REF output */
		DEF_INPUT(C, 4, INPUT_PULLUP),
		.out = &output_pin_D5,
	},
};
