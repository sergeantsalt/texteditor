/**
 * @file kilo.c
 * @brief This is kilo, a command line text editor
 * */

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
 * @brief Version info
 * */
#define KILO_VERSION "0.0.1"

/**
 * @brief This macro does a bitwise and against 0001 1111
 * which clears out the key modifiers like CTRL so we can
 * compare against the char value to test which key was pressed.
 * */
#define CTRL_K(k) ((k)&0x1f)

/**
 * @brief All special keypresses.
 * */
enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
};

// terminal
/**
 * @brief Holds the size of our terminal.
 * */
struct editorConfig {
  int cx, cy;
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
 * @brief Gets keys that are sent with modifiers
 * */
int parseEscapeSeq() {
  char seq[3];

  if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
  if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

  if (seq[0] == '[') {
    if (seq[1] >= '0' && seq[1] <= '9') {
      if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
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
  } else if (seq[0] == 'O') {
    switch (seq[1]) {
      case 'H': return HOME_KEY;
      case 'F': return END_KEY;
    }
  }

  return '\x1b';
}

/**
 * @brief Reads a key press input.
 * */
int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') {
    return parseEscapeSeq();
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

/**
 * @brief Gets the window setup for drawing
 * */
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  
  if (getWindowSize(&E.screenRows, &E.screenCols) == -1) {
    die("getWindowSize");
  }
}

/**
 * @brief Appends the screen text buffer
 * */
void abAppend(struct abuf *ab, const char *s, int len) {
  char *newString = realloc(ab->b, ab->len + len);

  if (newString == NULL) {
    return;
  }

  memcpy(&newString[ab->len], s, len);
  ab->b = newString;
  ab->len += len;
}

/**
 * @brief Frees the buffer
 * */
void abFree(struct abuf *ab) {
  free(ab->b);
}

// terminal

// input

void editorMoveCursor(int key) {
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      }
      break;
    case ARROW_RIGHT:
      if (E.cx != E.screenCols - 1) {
        E.cx++;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy != E.screenRows - 1) {
        E.cy++;
      }
      break;
  }
}

/**
 * @brief Process key presses
 * */
void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
    case CTRL_K('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      E.cx = E.screenCols - 1;
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screenRows;
        while (times--) {
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
      }

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
    default:
      break;
  }
}

// input

void getWelcomeString(char *buffer, int arrayLen, int *sLen) {
  int len = snprintf(buffer, arrayLen,
    "Kilo editor -- version %s", KILO_VERSION);
  if (len > E.screenCols)
    len = E.screenCols;

  *sLen = len;
}

// output
/**
 * @brief Draws tildes that make up the border
 * */
void editorDrawRows(struct abuf *ab) {
  for (int y = 0; y < E.screenRows; y++) {
    if (y == E.screenRows / 3) {
      char welcome[80];
      int len;

      getWelcomeString(welcome, sizeof(welcome), &len);
      int padding = (E.screenCols - len) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }

      while (padding--) abAppend(ab, " ", 1);

      abAppend(ab, welcome, len);
    } else {
      abAppend(ab, "~", 1);
    }

    // Erases to the right of the cursor
    // args 3-0 default 0
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenRows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

/**
 * @brief Refreshes the screen.
 * */
void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  // Command H moves the cursor to the int values given
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

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