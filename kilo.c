// includes
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
// includes

/**
 * @brief This macro does a bitwise and against 0001 1111
 * which clears out the key modifiers like CTRL so we can
 * compare against the char value to test which key was pressed.
 * */
#define CTRL_K(k) ((k)&0x1f)

// terminal
/**
 * @brief Holds the size of our terminal.
 * */
struct editorConfig {
  int screenRows;
  int screenCols;
  struct termios orig;
};

struct editorConfig E;

/**
 * @brief Exits with an error.
 * */
void die(char *s) {
  perror(s);
  exit(1);
}

/**
 * @brief Resets the attributes of the terminal to their original
 * settings.
 * */
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig) == -1) {
    die("tcsetattr");
  }
}

/**
 * @brief Prepares the terminal for editing code by disabling certain
 * signals like ctrl+C
 * */
void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig) == -1) {
    die("tcgetattr");
  }
  atexit(disableRawMode);

  struct termios raw = E.orig;
  raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  }
}

/**
 * @brief Reads a key press input.
 * */
char editorReadKey() {
  int n;
  char c;
  if ((n = read(STDIN_FILENO, &c, 1)) == -1) {
    if (errno != EAGAIN) {
      die("read");
    }
  }

  return c;
}

/**
 * @brief Gets the x/y positino of the cursor.
 * */
int getCursorPosition(int *rows, int *cols) {
  if (write(STDIN_FILENO, "\x1b[6n", 4) != 4) {
    return -1;
  }

  char buf[32];
  int i;
  for (i = 0; i < sizeof(buf); i++) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) {
      break;
    }

    if (buf[i] == 'R') {
      break;
    }
  }

  buf[i] = '\0';

  // printf("\r\nbuf[1]: '%s'\r\n", &buf[1]);

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

/**
 * @brief Gets window size in rows and columns.
 * */
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;

    return getCursorPosition(rows, cols);
  }

  *rows = ws.ws_row;
  *cols = ws.ws_col;
  return 0;
}

struct abuf {
  char *b;
  int len;
};

/**
 * @brief Initializes screen redraw buffer string.
 * */
#define ABUF_INIT {NULL, 0}

void initEditor() {
  if (getWindowSize(&E.screenRows, &E.screenCols) == -1) {
    die("getWindowSize");
  }
}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *newString = realloc(ab->b, ab->len + len);

  if (newString == NULL) {
    return;
  }

  memcpy(&newString[ab->len], s, len);
  ab->b = newString;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

// terminal

// input

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
    case CTRL_K('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    default:
      break;
  }
}

// input

// output

void editorDrawRows(struct abuf *ab) {
  for (int y = 0; y < E.screenRows; y++) {
    abAppend(ab, "~", 1);

    if (y < E.screenRows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[2j", 4);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  abAppend(&ab, "\x1b[H", 3);
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

// output

// init

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}