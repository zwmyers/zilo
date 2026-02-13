#include "zilo.h"

editor_config e_c;

//terminal ----------------------------------------------------------------

void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disable_raw_mode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &e_c.orig_termios) == -1) {
		die("tcsetattr");
	}
}

void enable_raw_mode() {
	if (tcgetattr(STDIN_FILENO, &e_c.orig_termios) == -1) {
		die("tcgetattr");
	}
	atexit(disable_raw_mode);

	struct termios raw = e_c.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
		die("tcsetattr");
	}
}

int editor_read_key() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) {
			die("read");
		}
	}
	if(c == '\x1b') {
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) {
			return '\x1b';
		}
		if (read(STDIN_FILENO, &seq[1], 1) != 1) {
			return '\x1b';

		}
		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) {
					return '\x1b';
				}
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == '0') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		return '\x1b';
	} else {
		return c;
	}
}

int get_cursor_position(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
		return -1;
	}
	
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) {
			break;
		}
		if (buf[i] == 'R') {
			break;
		}
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') {
		return -1;
	}
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
		return -1;
	}

	return 0;
}

int get_window_size(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			return -1;
		}
		return get_cursor_position(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}




//row operations --------------------------------------------------------

int editor_row_cx_to_rx(erow *row, int cx) {
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') {
			rx += (ZILO_TAB_STOP - 1) - (rx % ZILO_TAB_STOP);
		}
		rx++;
	}
	return rx;
}

void editor_update_row(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			tabs++;
		}
	}

	free(row->render);
	row->render = malloc(row->size + tabs*(ZILO_TAB_STOP - 1) + 1);

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % ZILO_TAB_STOP != 0) {
				row->render[idx++] = ' ';
			}
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editor_insert_row(int at, char *s, size_t len) {
	if (at < 0 || at > e_c.numrows) return;

	e_c.row = realloc(e_c.row, sizeof(erow) * (e_c.numrows + 1));
	memmove(&e_c.row[at + 1], &e_c.row[at], sizeof(erow) * (e_c.numrows - at));

	e_c.row[at].size = len;
	e_c.row[at].chars = malloc(len + 1);
	memcpy(e_c.row[at].chars, s, len);
	e_c.row[at].chars[len] = '\0';

	e_c.row[at].rsize = 0;
	e_c.row[at].render = NULL;
	editor_update_row(&e_c.row[at]);

	e_c.numrows++;
	e_c.dirty++;
}

void editor_free_row(erow *row) {
	free(row->render);
	free(row->chars);
}

void editor_del_row(int at) {
	if (at < 0 || at >= e_c.numrows) return;
	editor_free_row(&e_c.row[at]);
	memmove(&e_c.row[at], &e_c.row[at + 1], sizeof(erow) * (e_c.numrows - at - 1));
	e_c.numrows--;
	e_c.dirty++;
}

void editor_row_insert_char(erow *row, int at, int c) {
	if (at < 0 || at > row->size) {
		at = row->size;
	}
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editor_update_row(row);
	e_c.dirty++;
}

void editor_row_append_string(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editor_update_row(row);
	e_c.dirty++;

}

void editor_row_del_char(erow *row, int at) {
	if (at < 0 || at >= row->size) {
		return;
	}
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editor_update_row(row);
	e_c.dirty++;
}



//editor operations -------------------------------------------------------

void editor_insert_char(int c) {
	if (e_c.cy == e_c.numrows) {
		editor_insert_row(e_c.numrows, "", 0);
	}
	editor_row_insert_char(&e_c.row[e_c.cy], e_c.cx, c);
	e_c.cx++;
}

void editor_insert_newline() {
	if (e_c.cx == 0) {
		editor_insert_row(e_c.cy, "", 0);
	} else {
		erow *row = &e_c.row[e_c.cy];
		editor_insert_row(e_c.cy + 1, &row->chars[e_c.cx], row->size - e_c.cx);
		row = &e_c.row[e_c.cy];
		row->size = e_c.cx;
		row->chars[row->size] = '\0';
		editor_update_row(row);
	}
	e_c.cy++;
	e_c.cx = 0;
}

void editor_del_char() {
	if (e_c.cy == e_c.numrows) return;
	if (e_c.cx == 0 && e_c.cy == 0) return;

	erow *row = &e_c.row[e_c.cy];
	if (e_c.cx > 0) {
		editor_row_del_char(row, e_c.cx - 1);
		e_c.cx--;
	} else {
		e_c.cx = e_c.row[e_c.cy - 1].size;
		editor_row_append_string(&e_c.row[e_c.cy - 1], row->chars, row->size);
		editor_del_row(e_c.cy);
		e_c.cy--;
	}
}


//file i-o --------------------------------------------------------------

char *editor_rows_to_string(int *buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < e_c.numrows; j++) {
		totlen += e_c.row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (j = 0; j < e_c.numrows; j++) {
		memcpy(p, e_c.row[j].chars, e_c.row[j].size);
		p += e_c.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editor_open(char *filename) {
	free(e_c.filename);
	e_c.filename = strdup(filename);

	FILE *fp = fopen(filename, "r");
	if (!fp) {
		die("fopen");
	}
	
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
			linelen--;
		}
		editor_insert_row(e_c.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	e_c.dirty = 0;
}

void editor_save() {
	if (e_c.filename == NULL) {
		e_c.filename = editor_prompt("SAVE AS: %s (ESC TO CANCEL)");
		if (e_c.filename == NULL) {
			editor_set_status_message("SAVE ABORTED");
			return;
		}
	}
	
	int len;
	char *buf = editor_rows_to_string(&len);

	int fd = open(e_c.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				e_c.dirty = 0;
				editor_set_status_message("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}
	free(buf);
	editor_set_status_message("Can't save: I/O error: %s", strerror(errno));
}



//append buffer ---------------------------------------------------------

void ab_append(abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL) {
		return;
	}

	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void ab_free(abuf *ab) {
	free(ab->b);
}




//output ----------------------------------------------------------------

void editor_scroll() {
	e_c.rx = 0;
	if (e_c.cy < e_c.numrows) {
		e_c.rx = editor_row_cx_to_rx(&e_c.row[e_c.cy], e_c.cx);
	}

	if (e_c.cy < e_c.rowoff) {
		e_c.rowoff = e_c.cy;
	}
	if (e_c.cy >= e_c.rowoff + e_c.screenrows) {
		e_c.rowoff = e_c.cy - e_c.screenrows + 1;
	}
	if (e_c.rx < e_c.coloff) {
		e_c.coloff = e_c.rx;
	}
	if (e_c.rx >= e_c.coloff + e_c.screencols) {
		e_c.coloff = e_c.rx - e_c.screencols + 1;
	}
}

void editor_draw_rows(abuf *ab) {
	int y;
	for (y = 0; y < e_c.screenrows; y++) {
		int filerow = y + e_c.rowoff;
		if (filerow >= e_c.numrows) {
			if (e_c.numrows == 0 && y == e_c.screenrows/3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome),
						"Zilo editor -- version %s", ZILO_VERSION);
				if (welcomelen > e_c.screencols) {
					welcomelen = e_c.screencols;
				}
				int padding = (e_c.screencols - welcomelen)/2;
				if (padding) {
					ab_append(ab, "~", 1);
					padding--;
				}
				while (padding--) {
					ab_append(ab, " ", 1);
				}
				ab_append(ab, welcome, welcomelen);
			} else {
				ab_append(ab, "~", 1);
			}
		} else {
			int len = e_c.row[filerow].rsize - e_c.coloff;
			if (len < 0) {
				len = 0;
			}
			if (len > e_c.screencols) {
				len = e_c.screencols;
			}
			ab_append(ab, &e_c.row[filerow].render[e_c.coloff], len);
		}

		ab_append(ab, "\x1b[K", 3);
		ab_append(ab, "\r\n", 2);
	}
}

void editor_draw_status_bar(abuf *ab) {
	ab_append(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 												e_c.filename ? e_c.filename : "[NO NAME]", e_c.numrows,
						  e_c.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", e_c.cy + 1, e_c.numrows);
	if (len > e_c.screencols) {
		len = e_c.screencols;
	}
	ab_append(ab, status, len);
	while (len < e_c.screencols) {
		if (e_c.screencols - len == rlen) {
			ab_append(ab, rstatus, rlen);
			break;
		} else {
			ab_append(ab, " ", 1);
			len++;
		}
	}
	ab_append(ab, "\x1b[m", 3);
	ab_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(abuf *ab) {
	ab_append(ab, "\x1b[K", 3);
	int msglen = strlen(e_c.statusmsg);
	if (msglen > e_c.screencols) {
		msglen = e_c.screencols;
	}
	if (msglen && time(NULL) - e_c.statusmsg_time < 5) {
		ab_append(ab, e_c.statusmsg, msglen);
	}
}

void editor_refresh_screen() {
	editor_scroll();

	abuf ab = ABUF_INIT;
	
	ab_append(&ab, "\x1b[?25l", 6);
	ab_append(&ab, "\x1b[H", 3);
	
	editor_draw_rows(&ab);
	editor_draw_status_bar(&ab);
	editor_draw_message_bar(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (e_c.cy - e_c.rowoff) + 1,											  					(e_c.rx - e_c.coloff) + 1);
	ab_append(&ab, buf, strlen(buf));

	ab_append(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	ab_free(&ab);
}

void editor_set_status_message(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(e_c.statusmsg, sizeof(e_c.statusmsg), fmt, ap);
	va_end(ap);
	e_c.statusmsg_time = time(NULL);
}




//input ----------------------------------------------------------------------

char *editor_prompt(char *prompt) {
	size_t bufsize = 128;
	char *buf = malloc(bufsize);

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editor_set_status_message(prompt, buf);
		editor_refresh_screen();

		int c = editor_read_key();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE){
			if (buflen !=0) buf[--buflen] = '\0';
		} else if (c == '\x1b') {
			editor_set_status_message("");
			free(buf);
			return NULL;
		} else if (c == '\r') {
			if (buflen != 0) {
				editor_set_status_message("");
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}
	}
}

void editor_move_cursor(int key) {
	erow *row = (e_c.cy >= e_c.numrows) ? NULL : &e_c.row[e_c.cy];

	switch (key) {
		case ARROW_LEFT:
			if (e_c.cx != 0) {
				e_c.cx--;
			} else if (e_c.cy > 0) {
				e_c.cy--;
				e_c.cx = e_c.row[e_c.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && e_c.cx < row->size) {
				e_c.cx++;
			} else if (row && e_c.cx == row->size) {
				e_c.cy++;
				e_c.cx = 0;
			}
			break;
		case ARROW_UP:
			if (e_c.cy != 0) {
				e_c.cy--;
			}
			break;
		case ARROW_DOWN:
			if (e_c.cy < e_c.numrows) {
				e_c.cy++;
			}
			break;
	}

	row = (e_c.cy >= e_c.numrows) ? NULL : &e_c.row[e_c.cy];
	int rowlen = row ? row->size : 0;
	if (e_c.cx > rowlen) {
		e_c.cx = rowlen;
	}
}

void editor_process_keypress() {
	static int quit_times = ZILO_QUIT_TIMES;

	int c = editor_read_key();

	switch (c) {
		case '\r':
			editor_insert_newline();
			break;

		case CTRL_KEY('q'):
			if (e_c.dirty && quit_times > 0) {
				editor_set_status_message("WARNING: FILE HAS UNSAVED CHANGES"
									"PRESS CTRL(Q) %d MORE TIMES TO QUIT", quit_times);
				quit_times--;
				return;
			}
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
		
		case CTRL_KEY('s'):
			editor_save();
			break;

		case HOME_KEY:
			e_c.cx = 0;
			break;
		case END_KEY:
			if (e_c.cy < e_c.numrows) {
				e_c.cx = e_c.row[e_c.cy].size;
			}
			break;
		
		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (c == DEL_KEY) editor_move_cursor(ARROW_RIGHT);
			editor_del_char();
			break;

		case PAGE_UP:
		case PAGE_DOWN: {
				if (c == PAGE_UP) {
					e_c.cy = e_c.rowoff;
				} else if (c == PAGE_DOWN) {
					e_c.cy = e_c.rowoff + e_c.screenrows - 1;
					if (e_c.cy > e_c.numrows) {
						e_c.cy = e_c.numrows;
					}
				}

				int times = e_c.screenrows;
				while (times--) {
					editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
			}
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editor_move_cursor(c);
			break;

		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editor_insert_char(c);
			break;
	}
	quit_times = ZILO_QUIT_TIMES;
}




//init ---------------------------------------------------------------------

void init_editor() {
	e_c.cx = 0;
	e_c.cy = 0;
	e_c.rx = 0;
	e_c.rowoff = 0;
	e_c.coloff = 0;
	e_c.numrows = 0;
	e_c.row = NULL;
	e_c.dirty = 0;
	e_c.filename = NULL;
	e_c.statusmsg[0] = '\0';
	e_c.statusmsg_time = 0;
	if (get_window_size(&e_c.screenrows, &e_c.screencols) == -1) {
		die("get_window_size");
	}
	e_c.screenrows -= 2;
}

int main(int argc, char *argv[]) {
	enable_raw_mode();
	init_editor();
	if (argc >= 2) {
		editor_open(argv[1]);
	}
	
	editor_set_status_message("HELP: CTRL(S) = SAVE | CTRL(Q) = QUIT");

	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}
	
	return EXIT_SUCCESS;
}

