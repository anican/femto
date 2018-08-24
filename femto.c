/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include<string.h>
#include<ctype.h>
#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<termios.h>
#include<unistd.h>
#include<sys/ioctl.h>
#include<sys/types.h>

/*** fields ***/

#define FEMTO_VERS "1.0.0"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_DOWN,
    ARROW_UP,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};
typedef struct erow {
    int size;
    char* chars;
} erow;

struct editorConfig {
    int cursorX;
    int cursorY;
    int rowoff;
    int coloff;
    int screenRows;
    int screenCols;
    int nrows;
    erow* row;
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
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        } else if (seq[0] == '0') {
            switch (seq[1]) {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
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

/*** row operations ***/
void editorAppendRow(char* s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.nrows + 1));

    int at = E.nrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.nrows++;
}

/*** file io ***/
void editorOpen(char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        // strips last character if it is carriage return or newline
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r')) {
            linelen--;
        }
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
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
void editorScroll() {
    if (E.cursorY < E.rowoff) {
        E.rowoff = E.cursorY;
    }
    if (E.cursorY >= E.rowoff + E.screenRows) {
        E.rowoff = E.cursorY - E.screenRows + 1;
    }
    if (E.cursorX < E.coloff) {
        E.coloff = E.cursorX;
    }
    if (E.cursorX >= E.coloff + E.screenCols) {
        E.coloff = E.cursorX - E.screenCols + 1;
    }
}

void editorDrawRows(struct abuf* ab) {
    for (int y = 0; y < E.screenRows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.nrows) { //drawing row before or after end of text buffer
            if (E.nrows == 0 && y == E.screenRows / 3) {
                char greeting[80];
                int greetinglen = snprintf(greeting, sizeof(greeting), " Femto by Anirudh Canumalla -- Version: %s", FEMTO_VERS);
                if (greetinglen > E.screenCols) greetinglen = E.screenCols;
                // to center a string: divide screen width by 2 and subtract half of str
                // length from that
                int padding = (E.screenCols - greetinglen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, greeting, greetinglen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].size - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screenCols) len = E.screenCols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }
        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenRows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    // repositions cursor at row=1;col=1
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    // reposition after drawing '~'s
    char buf[32];
    // "\x1b[%d;%dH"
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
            (E.cursorY - E.rowoff) + 1, (E.cursorX - E.coloff) + 1);

    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input: mapping keys to functions ***/

//allows user to move around screen
void editorMoveCursor(int key) {
    erow* row = (E.cursorY >= E.nrows) ? NULL : &E.row[E.cursorY];

    switch (key) {
        case ARROW_LEFT:
            if (E.cursorX != 0) {
                E.cursorX--;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cursorX < row->size) {
                E.cursorX++;
            }
            break;
        case ARROW_UP:
            if (E.cursorY != 0) {
                E.cursorY--;
            }
            break;
        case ARROW_DOWN:
            if (E.cursorY < E.nrows) {
                E.cursorY++;
            }
            break;
    }
    row = (E.cursorY >= E.nrows) ? NULL : &E.row[E.cursorY];
    int rowlen = row ? row->size : 0;
    if (E.cursorX > rowlen) {
        E.cursorX = rowlen;
    }
}

// waits for keypress and returns it
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY:
            E.cursorX = 0;
            break;

        case END_KEY:
            E.cursorX = E.screenCols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenRows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** initialize ***/
void initEditor() {
    E.cursorX = 0;
    E.cursorY = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.nrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) die("getWindowSize");
}

int main(int argc, char* argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        // opens filename specified
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
