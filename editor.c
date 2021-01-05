/*** includes ***/
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

/*** defines ***/
#define EDITOR_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editor_key {
	ARROW_LEFT = 'a',
  	ARROW_RIGHT = 'd',
  	ARROW_UP = 'w',
  	ARROW_DOWN = 's'
};

/*** data ***/
struct pconfig {
	int cx, cy;
	int screen_rows;
	int screen_cols;
	struct termios orig_termios;
};

struct pconfig E;

/*** terminal ***/
void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disable_raw(void) {
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enable_raw(void) {
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	atexit(disable_raw);
	
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr");
}

char read_key(void) {
	int nread;
	char c;

	while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if(nread == -1 && errno != EAGAIN)
			die("read");
	}

	if(c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
    	if (read(STDIN_FILENO, &seq[1], 1) != 1)
    		return '\x1b';

    	if(seq[0] == '[') {
    		switch(seq[1]) {
    		case 'A': return ARROW_UP;
	        case 'B': return ARROW_DOWN;
	        case 'C': return ARROW_RIGHT;
	        case 'D': return ARROW_LEFT;
    		}
    	}
    	return '\x1b';
	}

	return c;
}

int get_cursor_pos(int *rows, int *cols) {
	char buffer[32];
	unsigned int i = 0;

	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	printf("\r\n");
	while(i < sizeof(buffer)) {
		if(read(STDIN_FILENO, &buffer[i], 1) != 1)
			break;
		if(buffer[i] == 'R')
			break;
		i++;
	}
	buffer[i] = '\0';

	if (buffer[0] != '\x1b' || buffer[1] != '[')
		return -1;
	if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2)
		return -1;
	return 0;
}

int get_window_size(int *rows, int *cols) {
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;
		return -1;
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** append buffer ***/
struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abuf_append(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if(new == NULL)
		return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abuf_free(struct abuf *ab) {
	free(ab->b);
}

/*** input ***/
void move_cursor(char key) {
	switch(key) {
	case ARROW_LEFT:
		E.cx--;
		break;
	case ARROW_RIGHT:
		E.cx++;
		break;
	case ARROW_UP:
		E.cy--;
		break;
	case ARROW_DOWN:
		E.cy++;
		break;	
	}
}

void process_keypress(void) {
	char c = read_key();

	switch(c) {
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;

	case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
		move_cursor(c);
		break;
	}
}

/*** output ***/
void draw_row(struct abuf *ab) {
	int y;
	for(y = 0;y < E.screen_rows;y++) {
		if(y == E.screen_rows / 3) {
			char welcome[80];
			int welcome_len = snprintf(
				welcome,
				sizeof(welcome),
				"Edwin's editor --- version %s",
				EDITOR_VERSION
			);
			if(welcome_len > E.screen_cols)
				welcome_len = E.screen_cols;
			int padding = (E.screen_cols - welcome_len) / 2;
			if(padding) {
				abuf_append(ab, "~", 1);
				padding--;
			}
			while(padding--)
				abuf_append(ab, " ", 1);
			abuf_append(ab, welcome, welcome_len);
		} else {
			abuf_append(ab, "~", 1);
		}
		abuf_append(ab, "\x1b[K", 3);
		if(y < E.screen_rows - 1)
			abuf_append(ab, "\r\n", 2);
	}
}

void refresh_screen(void) {
	struct abuf ab = ABUF_INIT;

	abuf_append(&ab, "\x1b[?25l", 6);
	// abuf_append(&ab, "\x1b[2J", 4);
	abuf_append(&ab, "\x1b[H", 3);
	draw_row(&ab);

	char buffer[32];
	snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  	abuf_append(&ab, buffer, strlen(buffer));
	
	abuf_append(&ab, "\x1b[?25l", 6);
	write(STDOUT_FILENO, ab.b, ab.len);
	abuf_free(&ab);
}

/*** init ***/
void editor_init(void) {
	E.cx = 0;
	E.cy = 0;

	if(get_window_size(&E.screen_rows, &E.screen_cols) == -1)
		die("get_window_size");
}


int main(void) {
	enable_raw();
	editor_init();
	
	while(1) {
		refresh_screen();
		process_keypress();
	}

	return 0;
}
