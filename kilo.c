// includes
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
// includes

#define CTRL_K(k) ((k)&0x1f)

// terminal
struct termios orig;

void die(char *s)
{
    perror(s);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig) == -1)
    {
        die("tcsetattr");
    }
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &orig) == -1)
    {
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = orig;
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        die("tcsetattr");
    }
}

char editorReadKey()
{
    int n;
    char c;
    if ((n = read(STDIN_FILENO, &c, 1)) == -1)
    {
        if (errno != EAGAIN)
        {
            die("read");
        }
    }

    return c;
}

// terminal

// input

void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c)
    {
        case CTRL_K('q'): 
            exit(0);
            break;
        default: break;
    }
}

// input

// output

void editorRefreshScreen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
}

// output

// init

int main()
{
    enableRawMode();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}