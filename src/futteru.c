#include <stdio.h>      // fprintf(), stdout, setlinebuf()
#include <stdlib.h>     // EXIT_SUCCESS, EXIT_FAILURE, rand()
#include <stdint.h>     // uint8_t, uint16_t, ...
#include <inttypes.h>   // PRIu8, PRIu16, ...
#include <unistd.h>     // getopt(), STDOUT_FILENO
#include <math.h>       // ceil()
#include <time.h>       // time(), nanosleep(), struct timespec
#include <signal.h>     // sigaction(), struct sigaction
#include <termios.h>    // struct winsize, struct termios, tcgetattr(), ...
#include <sys/ioctl.h>  // ioctl(), TIOCGWINSZ

// program information

#define PROGRAM_NAME "futteru"
#define PROGRAM_URL  "https://github.com/domsson/futteru"

#define PROGRAM_VER_MAJOR 0
#define PROGRAM_VER_MINOR 1
#define PROGRAM_VER_PATCH 0

// colors, adjust to your liking
// https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit

#define COLOR_BG   "\x1b[48;5;0m"   // background color, if to be used
#define COLOR_FG_0 "\x1b[38;5;15m"  // LAYER_FG: white
#define COLOR_FG_1 "\x1b[38;5;249m" // LAYER_BG: light grey

// these can be tweaked if need be

#define DROPS_BASE_VALUE 0.001
#define DROPS_FACTOR_MIN 1
#define DROPS_FACTOR_MAX 100
#define DROPS_FACTOR_DEF 10

#define SPEED_BASE_VALUE 1.00 
#define SPEED_FACTOR_MIN 1
#define SPEED_FACTOR_MAX 100
#define SPEED_FACTOR_DEF 10

// do not change these 

#define ANSI_FONT_RESET   "\x1b[0m"
#define ANSI_FONT_BOLD    "\x1b[1m"
#define ANSI_FONT_NORMAL  "\x1b[22m"
#define ANSI_FONT_FAINT   "\x1b[2m"

#define ANSI_HIDE_CURSOR  "\e[?25l"
#define ANSI_SHOW_CURSOR  "\e[?25h"

#define ANSI_CLEAR_SCREEN "\x1b[2J"
#define ANSI_CURSOR_RESET "\x1b[H"

#define BITMASK_FG 0x0F
#define BITMASK_BG 0xF0

#define LAYER_FG  1
#define LAYER_BG  2

#define NS_PER_SEC 1000000000

// for easy access later on

static char glyphs[] =
{
	' ',
	 42, // *
	 46, // .
	164, // ¤
	176, // °
	183, // ·
	215  // ×
};

static char *colors[] =
{
	COLOR_FG_0,
	COLOR_FG_1
};

#define NUM_GLYPHS sizeof(glyphs) / sizeof(glyphs[0])
#define NUM_COLORS sizeof(colors) / sizeof(colors[0])

// these are flags used for signal handling

static volatile int resized;   // window resize event received
static volatile int running;   // controls running of the main loop 

//  the matrix' data represents a 2D array of size cols * rows.
//  every data element is a 8 bit int which stores information
//  about that matrix cell as follows:
//
//   8   4   2   1   8   4   2   1
//   |   |   |   |   |   |   |   |
//   0   0   0   0   0   0   0   0
//  '-------------' '-------------'
//    BG GLYPH IDX    FG GLYPH IDX

typedef struct matrix
{
	uint8_t  *data;     // matrix data
	uint16_t  cols;     // number of columns
	uint16_t  rows;     // number of rows
	size_t char_count;  // current number of drops
	float  char_ratio;  // desired ratio of drops
}
matrix_s;

typedef struct options
{
	uint8_t speed;         // speed factor
	uint8_t drops;         // drops factor
	time_t  rands;         // seed for rand()
	uint8_t fg : 1;        // set foreground colors
	uint8_t bg : 1;        // set background color
	uint8_t help : 1;      // show help and exit
	uint8_t version : 1;   // show version and exit
}
options_s;

/*
 * Parse command line args into the provided options_s struct.
 */
static void
parse_args(int argc, char **argv, options_s *opts)
{
	opterr = 0;
	int o;
	while ((o = getopt(argc, argv, "bfd:e:hr:s:V")) != -1)
	{
		switch (o)
		{
			case 'b':
				opts->bg = 1;
				break;
			case 'f':
				opts->fg = 1;
				break;
			case 'd':
				opts->drops = atoi(optarg);
				break;
			case 'h':
				opts->help = 1;
				break;
			case 'r':
				opts->rands = atol(optarg);
				break;
			case 's':
				opts->speed = atoi(optarg);
				break;
			case 'V':
				opts->version = 1;
				break;
		}
	}
}

/*
 * Print usage information.
 */
static void
help(const char *invocation, FILE *where)
{
	fprintf(where, "USAGE\n");
	fprintf(where, "\t%s [OPTIONS...]\n\n", invocation);
	fprintf(where, "OPTIONS\n");
	fprintf(where, "\t-b\tuse black background color\n");
	fprintf(where, "\t-d\tdensity factor (%"PRIu8" .. %"PRIu8", default: %"PRIu8")\n",
		       	DROPS_FACTOR_MIN, DROPS_FACTOR_MAX, DROPS_FACTOR_DEF);
	fprintf(where, "\t-h\tprint this help text and exit\n");
	fprintf(where, "\t-r\tseed for the random number generator\n");
	fprintf(where, "\t-s\tspeed factor (%"PRIu8" .. %"PRIu8", default: %"PRIu8")\n", 
			SPEED_FACTOR_MIN, SPEED_FACTOR_MAX, SPEED_FACTOR_DEF);
	fprintf(where, "\t-V\tprint version information and exit\n");
}

/*
 * Print version information.
 */
static void
version(FILE *where)
{
	fprintf(where, "%s %d.%d.%d\n%s\n", PROGRAM_NAME,
			PROGRAM_VER_MAJOR, PROGRAM_VER_MINOR, PROGRAM_VER_PATCH,
			PROGRAM_URL);
}

/*
 * Signal handler.
 */
static void
on_signal(int sig)
{
	switch (sig)
	{
		case SIGWINCH:
			resized = 1;
			break;
		case SIGINT:
		case SIGQUIT:
		case SIGTERM:
			running = 0;
			break;
	}
}

/*
 * Make sure `val` is within the range [min, max].
 */
static void
clamp_uint8(uint8_t *val, uint8_t min, uint8_t max)
{
	if (*val < min) { *val = min; return; }
	if (*val > max) { *val = max; return; }
}

/*
 * Return a pseudo-random int in the range [min, max].
 */
static int
rand_int(int min, int max)
{
	return min + rand() % ((max + 1) - min);
}

/*
 *
 */
static uint8_t 
rand_glyph()
{
	return rand_int(0, NUM_GLYPHS - 1);
}

//
// Functions to manipulate individual matrix cell values
//

/*
 * Create a 8 bit matrix value from the given 8 bit values representing 
 * the cell's foreground and background GLYPH indices.
 */
static uint8_t
val_new(uint8_t fg, uint8_t bg)
{
	return (BITMASK_BG & (bg << 4)) | (BITMASK_FG & fg);
}

/*
 * 
 */
static uint8_t
val_get_fg(uint8_t value)
{
	return value & BITMASK_FG;
}

/*
 *
 */
static uint8_t
val_get_bg(uint8_t value)
{
	return (value & BITMASK_BG) >> 4;
}

//
// Functions to access / set matrix values
//

/*
 * Get the matrix array index for the given row and column.
 */
static int
mat_idx(matrix_s *mat, int row, int col)
{
	return row * mat->cols + col;
}

/*
 * Get the 8 bit matrix value from the cell at the given row and column.
 */
static uint8_t
mat_get_value(matrix_s *mat, int row, int col)
{
	if (row >= mat->rows) return 0;
	if (col >= mat->cols) return 0;
	return mat->data[mat_idx(mat, row, col)];
}

/*
 *
 */
static uint8_t
mat_get_fg(matrix_s *mat, int row, int col)
{
	return val_get_fg(mat_get_value(mat, row, col));
}

/*
 *
 */
static uint8_t
mat_get_bg(matrix_s *mat, int row, int col)
{
	return val_get_bg(mat_get_value(mat, row, col));
}

static uint8_t
mat_get_glyph(matrix_s *mat, int row, int col, uint8_t layer)
{
	if (layer == LAYER_FG)
	{
		return val_get_fg(mat_get_value(mat, row, col));
	}
	if (layer == LAYER_BG)
	{
		return val_get_bg(mat_get_value(mat, row, col));
	}
	return 0;
}

/*
 * Set the 8 bit matrix value for the cell at the given row and column.
 */
static uint8_t
mat_set_value(matrix_s *mat, int row, int col, uint8_t value)
{
	if (row >= mat->rows) return 0;
	if (col >= mat->cols) return 0;
	return mat->data[mat_idx(mat, row, col)] = value;
}

/*
 *
 */
static uint8_t
mat_set_fg(matrix_s *mat, int row, int col, uint8_t glyph)
{
	return mat_set_value(mat, row, col, 
			val_new(glyph, mat_get_bg(mat, row, col)));
}

/*
 * 
 */
static uint8_t
mat_set_bg(matrix_s *mat, int row, int col, uint8_t glyph)
{
	return mat_set_value(mat, row, col, 
			val_new(mat_get_fg(mat, row, col), glyph));
}

static void 
mat_set_glyph(matrix_s *mat, int row, int col, uint8_t glyph, uint8_t layer)
{
	if (layer & LAYER_FG)
	{
		mat_set_value(mat, row, col, 
			val_new(glyph, mat_get_bg(mat, row, col)));
	}
	if (layer & LAYER_BG)
	{
		mat_set_value(mat, row, col, 
			val_new(mat_get_fg(mat, row, col), glyph));
	}
}

//
// Functions to create, manipulate and print a matrix
//

/*
 * Print the matrix to stdout.
 */
static void
mat_print(matrix_s *mat)
{
	uint8_t value = 0;
	uint8_t  fg = 0;
	uint8_t  bg = 0;
	size_t   size  = mat->cols * mat->rows;

	for (int i = 0; i < size; ++i)
	{
		value = mat->data[i];
		fg = val_get_fg(value);
		bg = val_get_bg(value);

		if (fg)
		{
			fputs(colors[0], stdout);
			fputs(ANSI_FONT_BOLD, stdout);
			fputc(glyphs[fg], stdout);
			continue;
		}
		if (bg)
		{
			fputs(colors[1], stdout);
			//fputs(ANSI_FONT_FAINT, stdout);
			fputs(ANSI_FONT_NORMAL, stdout);
			fputc(glyphs[bg], stdout);
			continue;
		}
		else
		{
			fputc(glyphs[0], stdout);
			continue;
		}
	}

	fflush(stdout);
}

/*
 * Add a drop to the matrix at the specified position.
 */
static void
mat_add_drop(matrix_s *mat, int row, int col, int layer)
{
	if (col < 0)          return;
	if (col >= mat->cols) return;
	if (row < 0)          return;
	if (row >= mat->rows) return;

	uint8_t glyph = mat_get_glyph(mat, row, col, layer);
	
	if (glyph)
	{
		return;
	}
	
	mat_set_glyph(mat, row, col, rand_int(1, NUM_GLYPHS - 1), layer);
	mat->char_count += 1;
	return;
}

/*
 * Move every cell down one row, potentially adding a new tail cell at the top.
 * Returns 1 if a DROP 'fell off the bottom', otherwise 0.
 */
static int
mat_mov_col(matrix_s *mat, int col, uint8_t layer)
{
	uint8_t glyph = 0;

	// manually check the bottom-most cell: is there a drop?
	int dropped = mat_get_glyph(mat, mat->rows - 1, col, layer) ? 1 : 0;
	
	// iterate all cells in this column, moving each down one cell
	for (int row = mat->rows - 1; row >= 0; --row)
	{
		glyph = mat_get_glyph(mat, row, col, layer);

		if (glyph)
		{
			// move the cell one down
			mat_set_glyph(mat, row+1, col, glyph, layer);
			// null the current cell
			mat_set_glyph(mat, row, col, 0, layer);
			continue;
		}
	}

	return dropped;
}

/*
 * Update the matrix by moving all drops down one cell and potentially 
 * adding new drops at the top of the matrix.
 */
static void 
mat_update(matrix_s *mat, uint8_t layer)
{
	// move each column down one cell, possibly dropping some drops
	for (int col = 0; col < mat->cols; ++col)
	{
		mat->char_count -= mat_mov_col(mat, col, layer);
	}
	
	// add new drops at the top, trying to get to the desired drop count
	int drops_desired = (mat->cols * mat->rows) * mat->char_ratio;
	int drops_missing = drops_desired - mat->char_count; 
	int drops_to_add  = ceil(drops_missing / (float) mat->rows);

	for (int i = 0; i <= drops_to_add; ++i)
	{
		mat_add_drop(mat, 0, rand_int(0, mat->cols - 1), layer);
	}
}

/*
 * Make it rain by randomly adding DROPs to the matrix, based on the 
 * char_ratio of the given matrix.
 *
 * TODO a nicer implementation would be to base the number of drops 
 *      to add on the char_count field; however, we then also need to
 *      make sure that we reset this field to 0 on matrix resize and 
 *      before calling this function.
 */
static void
mat_rain(matrix_s *mat)
{
	int num = (int) (mat->cols * mat->rows) * mat->char_ratio;

	int c = 0;
	int r = 0;

	for (int i = 0; i < num; ++i)
	{
		c = rand_int(0, mat->cols - 1);
		r = rand_int(0, mat->rows - 1);
		mat_add_drop(mat, r, c, rand_int(LAYER_FG, LAYER_BG));
	}
}

/*
 * Creates or recreates (resizes) the given matrix.
 * Returns -1 on error (out of memory), 0 on success.
 */
static int
mat_init(matrix_s *mat, uint16_t rows, uint16_t cols, float char_ratio)
{
	mat->data = realloc(mat->data, sizeof(mat->data) * rows * cols);
	if (mat->data == NULL)
	{
		return -1;
	}
	
	mat->rows = rows;
	mat->cols = cols;

	mat->char_count = 0;
	mat->char_ratio = char_ratio;
	
	return 0;
}

/*
 * Free ALL the memory \o/
 */
void
mat_free(matrix_s *mat)
{
	free(mat->data);
}

/*
 * Try to figure out the terminal size, in character cells, and return that 
 * info in the given winsize structure. Returns 0 on succes, -1 on error.
 * However, you might still want to check if the ws_col and ws_row fields 
 * actually contain values other than 0. They should. But who knows.
 */
int
cli_wsize(struct winsize *ws)
{
#ifndef TIOCGWINSZ
	return -1;
#endif
	return ioctl(STDOUT_FILENO, TIOCGWINSZ, ws);
}

/*
 * Turn echoing of keyboard input on/off.
 */
static int
cli_echo(int on)
{
	struct termios ta;
	if (tcgetattr(STDIN_FILENO, &ta) != 0)
	{
		return -1;
	}
	ta.c_lflag = on ? ta.c_lflag | ECHO : ta.c_lflag & ~ECHO;
	return tcsetattr(STDIN_FILENO, TCSAFLUSH, &ta);
}

/*
 * Prepare the terminal for the next paint iteration.
 */
static void
cli_clear()
{
	//fputs(ANSI_CLEAR_SCREEN, stdout); // just for debug, remove otherwise
	fputs(ANSI_CURSOR_RESET, stdout);
}

/*
 * Prepare the terminal for our matrix shenanigans.
 */
static void
cli_setup(options_s *opts)
{
	fputs(ANSI_HIDE_CURSOR, stdout);

	if (opts->bg)
	{
		fputs(COLOR_BG, stdout);
	}

	fputs(ANSI_CLEAR_SCREEN, stdout); // clear screen
	fputs(ANSI_CURSOR_RESET, stdout); // cursor back to position 0,0
	cli_echo(0);                      // don't show keyboard input
	
	// set the buffering to fully buffered, we're adult and flush ourselves
	setvbuf(stdout, NULL, _IOFBF, 0);
}

/*
 * Make sure the terminal goes back to its normal state.
 */
static void
cli_reset()
{
	fputs(ANSI_FONT_RESET, stdout);   // resets font colors and effects
	fputs(ANSI_SHOW_CURSOR, stdout);  // show the cursor again
	fputs(ANSI_CLEAR_SCREEN, stdout); // clear screen
	fputs(ANSI_CURSOR_RESET, stdout); // cursor back to position 0,0
	cli_echo(1);                      // show keyboard input

	setvbuf(stdout, NULL, _IOLBF, 0);
}

int
main(int argc, char **argv)
{
	// set signal handlers for the usual susspects plus window resize
	struct sigaction sa = { .sa_handler = &on_signal };
	sigaction(SIGINT,   &sa, NULL);
	sigaction(SIGQUIT,  &sa, NULL);
	sigaction(SIGTERM,  &sa, NULL);
	sigaction(SIGWINCH, &sa, NULL);

	// parse command line options
	options_s opts = { 0 };
	parse_args(argc, argv, &opts);

	if (opts.help)
	{
		help(argv[0], stdout);
		return EXIT_SUCCESS;
	}

	if (opts.version)
	{
		version(stdout);
		return EXIT_SUCCESS;
	}

	if (opts.speed == 0)
	{
		opts.speed = SPEED_FACTOR_DEF;
	}

	if (opts.drops == 0)
	{
		opts.drops = DROPS_FACTOR_DEF;
	}

	if (opts.rands == 0)
	{
		opts.rands = time(NULL);
	}
	
	// make sure the values are within expected/valid range
	clamp_uint8(&opts.speed, SPEED_FACTOR_MIN, SPEED_FACTOR_MAX);
	clamp_uint8(&opts.drops, DROPS_FACTOR_MIN, DROPS_FACTOR_MAX);

	// get the terminal dimensions
	struct winsize ws = { 0 };
	if (cli_wsize(&ws) == -1)
	{
		fprintf(stderr, "Failed to determine terminal size\n");
		return EXIT_FAILURE;
	}

	if (ws.ws_col == 0 || ws.ws_row == 0)
	{
		fprintf(stderr, "Terminal size not appropriate\n");
		return EXIT_FAILURE;
	}

	// calculate some spicy values from the options
	float wait = SPEED_BASE_VALUE / (float) opts.speed;
	float drops_ratio = DROPS_BASE_VALUE * opts.drops;

	// set up the nanosleep struct
	uint8_t  sec  = (int) wait;
	uint32_t nsec = (wait - sec) * NS_PER_SEC;
	struct timespec ts = { .tv_sec = sec, .tv_nsec = nsec };
	
	// seed the random number generator with the current unix time
	srand(opts.rands);

	// initialize the matrix
	matrix_s mat = { 0 }; 
	mat_init(&mat, ws.ws_row, ws.ws_col, drops_ratio);

	// prepare the terminal for our shenanigans
	cli_setup(&opts);
 
	uint8_t layer = LAYER_FG;
	running = 1;
	while(running)
	{
		if (resized)
		{
			// query the terminal size again
			cli_wsize(&ws);
			
			// reinitialize the matrix
			mat_init(&mat, ws.ws_row, ws.ws_col, drops_ratio);
			mat_rain(&mat); // TODO maybe this isn't desired?
			resized = 0;
		}

		cli_clear();
		mat_print(&mat);                // print to the terminal
		mat_update(&mat, LAYER_FG);     // move all drops down one row
		if (layer == LAYER_BG)
		{
			mat_update(&mat, LAYER_BG);
		}
		nanosleep(&ts, NULL);
		layer = layer == LAYER_FG ? LAYER_BG : LAYER_FG;
	}

	// make sure all is back to normal before we exit
	mat_free(&mat);	
	cli_reset();
	return EXIT_SUCCESS;
}
