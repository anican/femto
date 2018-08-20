/*** includes ***/
#include<string.h>
#include<ctype.h>
#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<termios.h>
#include<unistd.h>
#include<sys/ioctl.h>

/*** fields ***/

#define CTRL_KEY(k) ((k) & 0x1f)
struct editorConfig {
    int screenRows;
    int screenCols;
    struct termios orig_termios; // stores original attrib. of terminal before opening femto
};

struct editorConfig E;

/*** terminal settings/terminal input ***/

void die(const char* sh) {
    // clears screen and repositions cursor on exit
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);

    perror(sh);
    exit(1);
}

// Resets terminal to its original state after exiting femto
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

// Raw mode processes each input key once it is pressed
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    atexit(disableRawMode); // Disables raw mode when program exits

    struct termios raw = E.orig_termios; // original terminal state

    // Enables control characters to be read in as input
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP); // input flags
    raw.c_oflag &= ~(OPOST); // output flag
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // local flags
    raw.c_cflag |= (CS8); // sets char size to 8bits/byte
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

// waits for one keypress and returns it
char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDOUT_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int* rows, int* cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** mutable string buffer ***/
struct abuf {
    char* b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char* s, int len) {
    char* appstr = realloc(ab->b, ab->len + len);

    if (appstr == NULL) return;
    memcpy(&appstr[ab->len], s, len);
    ab->b = appstr;
    ab->len += len;
}

void abFree(struct abuf* ab) {
    free(ab->b);
}

/*** output ***/
void editorDrawRows(struct abuf* ab) {
    for (int y = 0; y < E.screenRows; y++) {
        abAppend(ab, "~", 1);

        if (y < E.screenRows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    // specifies number of bytes we are writing: \x1b is <esc> key
    abAppend(&ab, "\x1b[2J", 4);
    // repositions cursor at row=1;col=1
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    // reposition after drawing '~'s
    abAppend(&ab, "\x1b[H", 3);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input: mapping keys to functions ***/

// waits for keypress and returns it
void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** initialize ***/
void initEditor() {
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
