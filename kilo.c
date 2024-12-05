#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include "logger.h"
#include <time.h>
#include <stdarg.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

// The CTRL_KEY macro bitwise-ANDs a character with the value 00011111, in binary.
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
// This repersents a single row of data/file
typedef struct erow {
  int size;
  int rsize; // size of render chars
  char *chars;
  char *render; // used to keep tabs and unprintable characters
} erow;

// global config of the edtiro
struct editorConfig {
  int cx, cy; // cursor x and y position in the file.
  int rx; //cursor position in render
  int screenrows; // total rows in the screen
  int screencols; // total columns in the screen
  int numrows; // total number of rows that are filled with data
  int rowoff;  // row offset from the top, while scrolling vertically
  int coloff; // column offset from the left to scroll horizontally
  erow *row; // all rows
  struct termios orig_termios; // original terminal settings
  Logger *logger;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
};

// editor keys mapped to numbers to avoid conflict with actual characters
enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DEL_KEY
};


struct editorConfig E;

/*** terminal ***/
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);  // clear terminal screen on exit
  write(STDOUT_FILENO, "\x1b[H", 3); // move the cursor to top left on exit.
  perror(s);                    // print error
  flush(E.logger);
  stop(E.logger);
  exit(1);
}


/*
 * disableRawMode(): Restores the terminal to its original settings
 *
 * This function:
 * - Takes the original terminal attributes stored in orig_termios
 * - Applies them back to the terminal using tcsetattr()
 * - Uses TCSAFLUSH to:
 *   1. Wait for all output to be transmitted
 *   2. Discard any unread input
 *   3. Apply the original settings
 *
 * This effectively:
 * - Restores canonical mode (line buffering)
 * - Re-enables terminal echoing
 * - Re-enables special character processing (Ctrl+C, Ctrl+Z, etc.)
 * - Returns terminal to its normal state before program execution
 *
 * Called automatically on program exit due to atexit() registration
 * Can also be called manually if needed
 */
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  /*
    ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary.
    We use the bitwise-NOT operator (~) on this value to get 11111111111111111111111111110111.
    We then bitwise-AND this value with the flags field, which forces the fourth bit in the flags field to become 0,
    and causes every other bit to retain its current value. Flipping bits like this is common in C.
   */
  // input flags
  raw.c_iflag &= ~(ICRNL | IXON | BRKINT | ISTRIP | INPCK);
  // output flags
  raw.c_oflag &= ~(OPOST);
  // local flags
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDOUT_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDOUT_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (seq[0] == '[') {
      //Page Up is sent as <esc>[5~ and Page Down is sent as <esc>[6~.
      if (seq[1] > '0' && seq[1] <= '9') {
        if (read(STDOUT_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '3':
            return DEL_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          }
        }
      } else {
        /*
          an arrow key sends multiple bytes as input to our program.
          These bytes are in the form of an escape sequence that starts with
          '\x1b', '[', followed by an 'A', 'B', 'C', or 'D' depending on which of the four arrow keys was pressed.
         */
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        }
      }
    }

    return '\x1b';
  } else {
    return c;
  }

  return c;
}

/**
 * Gets the current cursor position in the terminal
 * Uses ANSI escape sequences to query and parse cursor position
 *
 * @param rows Pointer to store the row position
 * @param cols Pointer to store the column position
 * @return 0 on success, -1 on failure
 */
int getCursorPosition(int *rows, int *cols) {
    char buf[32];                // Buffer to store the response from terminal
    unsigned int i = 0;

    // Send ANSI escape sequence to query cursor position
    // \x1b[6n requests cursor position report
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    // Read the response one character at a time
    // Response format will be: \x1b[rows;colsR
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')      // 'R' marks the end of the response
            break;
        i++;
    }

    buf[i] = '\0';              // Null terminate the string

    // Verify response starts with escape sequence "\x1b["
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;

    // Parse the rows and columns from the response
    // Format is: \x1b[rows;colsR
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

/***********************************************
 * Function: getWindowSize
 * Parameters:
 *   - rows: pointer to store window height
 *   - cols: pointer to store window width
 * Returns:
 *   0 on success, -1 on failure
 * Purpose:
 *   Gets the terminal window dimensions using
 *   system calls or cursor positioning fallback
 ***********************************************/
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  // Try to get window size using TIOCGWINSZ system call
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // Fallback: Move cursor to bottom-right corner and get position
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  } else {
    // Store window dimensions in the provided pointers
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}



/*** row operations ***/

void editorUpdateRow(erow *row) {
  // count total number of tabs
  int tabs;
  for (int i; i > row->size; i++) {
    if (row->chars[i] == '\t') tabs++;
  }

  free(row->render);
  /* reserve space for tabs as well. Each tab takes 8 char of space.
     multplying with 7 because 1 space is already covered by size
  */
  row->render = malloc(row->size + (tabs * (KILO_TAB_STOP - 1)) + 1);

  int j;
  int idx = 0;
  // copy each char into render
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
  // extend memory for all existing rows + 1 for a new line
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  // next line
  int at = E.numrows;
  E.row[at].size = len;
  // allocate memory for actual data string
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);
  E.numrows++;
}


void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  /* allocate space for new char and string terminator */
  row->chars = realloc(row->chars, row->size + 2);
  /*
    Move data from at to new location which is at + 1.
    if at is end of line then nothing will be moved but
    if at is in the middle of the string then string to the right of at will be
    moved. size of bytes will be string size - bytes to the left (which is at)
    and + 1 for '\0'
   */
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++; // increment row size
  row->chars[at] = c; // insert char
  editorUpdateRow(row); // rerender row
}

/*** editor operations ***/

void editorInsertChar(int c) {
  /* insert row if it doesn't exist yet */
  if (E.cy == E.numrows) {
    editorAppendRow("", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++; // move cursor to next position on x
}



/*** file i/o ***/
void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  // open given file
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  // read each line. getline which can also manage memory so you don't need to
  // worry about allocation
  // reuse line pointer for new line.
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    // strip new line and carriage return from each line end.
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}


/*** append buffer, used to refresh editor in 1 step ***/
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT                                                              \
  { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len) {
  // allocate more memory
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** input, moving cursor position using arrow keys ***/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t') {
      int current_rx = rx;
      // this handles the case when u r in the middle of the tab
      rx += (KILO_TAB_STOP - 1)  - (rx % KILO_TAB_STOP);
      char message[100];
      snprintf(message, sizeof(message), "Encountered tab at rx %d and now rx is %d", current_rx, rx);
      info(message, E.logger);
    }
    rx++;
  }
  return rx;
}

void editorMoveCursor(int key) {
  erow *row  = (E.cy >= E.numrows) ? NULL: &E.row[E.cy];
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0){
      E.cx--;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size) {
     E.cx++;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows) {
      E.cy++;
    }
    break;
  }

  // snap back the cursor horizontally if use moves to a line shorter than previous line.
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }

}

void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
  case '\r':
    /* TODO */
    break;
  // quit on ctrl + q
  case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

 /***
     We also handle the Ctrl-H key combination, which sends the control code
     8,which is originally what the Backspace character would send back in
     the day. If you look at the ASCII table, you’ll see that ASCII code 8
     is named BS for “backspace”, and ASCII code 127 is named DEL for
     “delete”. But for whatever reason, in modern computers
     the Backspace key is mapped to 127 and the Delete key
     is mapped to the escape sequence <esc>[3~
 ***/
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    /* TODO */
    break;

  // move to beginning of the line
  case CTRL_KEY('a'):
    if (E.cy > 0) {
      E.cx = 0;
    }
    break;
  /* move to the end of the line */
  case CTRL_KEY('e'):
    if (E.cy > 0) {
      E.cx = E.row[E.cy].size;
    }
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screenrows;
    while (times--) {
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
  }
  break;

  case ARROW_RIGHT:
  case ARROW_LEFT:
  case ARROW_UP:
  case ARROW_DOWN:
    editorMoveCursor(c);
    break;

    /***

        Ctrl-L is traditionally used to refresh the screen in
        terminal programs. In our text editor, the screen
        refreshes after any keypress, so we don’t have
        to do anything else to implement that feature.
    ***/
  case CTRL_KEY('l'):
  case '\x1b':
    break;

  default:
    editorInsertChar(c);
    break;
  }
}

/*** output ***/
// scroll editor on each refresh
void editorScroll() {
  E.rx = 0;
  // make sure u r on a row
  if (E.cy < E.numrows) {
    /*
      handle tabs that were convered to space,
      instead of stepping up 1 char we will skip size of tab
      or any other char that was replaced using render storage.
     */
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  // if offset away from current cursor position bring it back.
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  //if offset is behind the current cursor position bring it forward.
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

  // if offset away from current cursor position bring it back.
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }

  //if offset is behind the current cursor position bring it forward.
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }

}

/***********************************************
 * Function: editorDrawRows
 * Parameters:
 *   - ab: Append buffer to store output
 * Purpose:
 *   Draws each row of the editor display, handling
 *   both file content and welcome message
 ***********************************************/
void editorDrawRows(struct abuf *ab) {
  // Loop through each row of the screen
  for (int y = 0; y < E.screenrows; y++) {
    // Calculate which row of the file we're currently drawing
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      // Display welcome message if no file is open
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        // Format welcome message with version
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Kilo editor --version %s", KILO_VERSION);
        // Truncate welcome message if it's too long
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;

        // Center the welcome message
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        // Add left padding spaces
        while (padding--)
          abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        // Display tilde for empty lines
        abAppend(ab, "~", 1);
      }
    } else {
      // Display actual file content for this row
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;

      // Truncate line if it's longer than screen width
      if (len > E.screencols)
        len = E.screencols;
      // Append a portion of the current row's text to the output buffer
      // - ab: the append buffer to write to
      // - &E.row[filerow].chars[E.coloff]: pointer to the text starting at the horizontal scroll offset
      // - len: number of characters to append, limited by screen width
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    // Clear line to right of cursor
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  /**
     The escape sequence <esc>[7m switches to inverted colors,
     and <esc>[m switches back to normal formatting.
     Let’s draw a blank white status bar of inverted space characters.
  **/
  abAppend(ab, "\x1b[7m", 4);

  char status[80], rstatus[80];
  /* show file name and total rows */
  int len = snprintf(status, sizeof(status), "%20s - %d lines",
                     E.filename ? E.filename : "[No Name]", E.numrows);

  /* show current row / total rows */
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

  if (len > E.screencols) len = E.screencols;

  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}


void editorRefreshScreen() {
  editorScroll();
  struct abuf ab = ABUF_INIT;
  // hide cursor
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  // set cursor position
  char buf[32];
  // Position cursor using ANSI escape sequence \x1b[row;colH
  // Subtract row/col offsets to handle scrolling - when text is scrolled,
  // we need to adjust the actual cursor position relative to the visible window
  // Add 1 since terminal uses 1-based indexing for cursor positions
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));


  // show cursor
  abAppend(&ab, "\x1b[?25h", 6);
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/* var args */
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time  = time(NULL);
}


/*** init ***/
void initEditor() {
  E.cx = 0;
  E.rx = 0;
  E.cy = 0;
  E.numrows = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.logger = createLogger();
  if (!E.logger || !(E.logger->logfile)) die("createLogger");
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2; // saving two lines for status bar and message.
}


int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  char c;
  editorSetStatusMessage("Help: Ctrl-Q = quit");
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
    flush(E.logger);
  }
  flush(E.logger);
  stop(E.logger);
  return 0;
}
