#ifndef ZILO_H
#define ZILO_H

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define ZILO_VERSION "0.0.1"
#define ZILO_TAB_STOP 8 
#define ZILO_QUIT_TIMES 3

typedef struct erow {
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;

typedef struct editor_config {
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios orig_termios;
} editor_config;

typedef struct abuf {
	char *b;
	int len;
} abuf;

enum editor_key {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

//terminal

void die(const char *s);
void disable_raw_mode(void);
void enable_raw_mode(void);
int editor_read_key(void);
int get_cursor_position(int *rows, int *cols);
int get_window_size(int *rows, int *cols);

//row operations

int editor_row_cx_to_rx(erow *row, int cx);
int editor_row_rx_to_cx(erow *row, int rx);
void editor_update_row(erow *row);
void editor_insert_row(int at, char *s, size_t len);
void editor_free_row(erow *row);
void editor_del_row(int at);
void editor_row_insert_char(erow *row, int at, int c);
void editor_row_append_string(erow *row, char *s, size_t len);
void editor_row_del_char(erow *row, int at);

//editor operations

void editor_insert_char(int c);
void editor_insert_newline(void);
void editor_del_char(void);

//file i-o

char *editor_rows_to_string(int *buflen);
void editor_open(char *filename);
void editor_save(void);

//search

void editor_search_callback(char *query, int key);
void editor_search(void);

//append buffer

void ab_append(abuf *ab, const char *s, int len);

//output

void editor_scroll(void);
void editor_draw_rows(abuf *ab);
void editor_draw_status_bar(abuf *ab);
void editor_draw_message_bar(abuf *ab);
void editor_refresh_screen(void);
void editor_set_status_message(const char *fmt, ...);

//input

char *editor_prompt(char *prompt, void (*callback)(char *, int));
void editor_move_cursor(int key);
void editor_process_keypress(void);

//init

void init_editor(void);


#endif
