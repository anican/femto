// current position is at "Enter" on the fifth one
/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>

/*** fields ***/

#define FEMTO_VERS "1.0.0"
#define FEMTO_TAB_STOP 8
#define FEMTO_QUIT_TIMES 2
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    BACKSPACE = 127,
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
    int rsize;
    char* chars;
    char* render;
} erow;

struct editorConfig {
    int cursorX;
    int cursorY;
    int rx;
    int rowoff;
    int coloff;
    int screenRows;
    int screenCols;
    int nrows;
    erow* row;
    int sincemodif; // tells whether file has been modified since open
    char* filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios; // stores original attrib. of terminal before opening femto
};

struct editorConfig E;

/*** prototypes ***/
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char* editorPrompt(char* prompt, void (*callback)(char *, int));

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
int editorRowCxToRx(erow* row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        // if its a tab character calculate the different rx appropriately
        if (row->chars[j] == '\t') {
            rx += (FEMTO_TAB_STOP - 1) - (rx % FEMTO_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

int editorRowRxToCx(erow* row, int rx) {
    int cur_rx = 0, i;
    for (i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            cur_rx += (FEMTO_TAB_STOP - 1) - (cur_rx % FEMTO_TAB_STOP);
        }
        cur_rx++;

        if (cur_rx > rx) return i;
    }
    return i;
}

void editorUpdateRow(erow* row) {
    int tabs = 0;

    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') tabs++;
    }

    free(row->render);
    row->render = malloc(row->size + 1 + tabs*(FEMTO_TAB_STOP));

    int idx = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            row->render[idx++] = row->chars[i];
            while (idx % FEMTO_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[i];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorInsertRow(int at, char* s, size_t len) {
    if (at < 0 || at > E.nrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.nrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.nrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.nrows++;
    E.sincemodif++;
}

void editorFreeRow(erow* row) {
    free(row->render);
    free(row->chars);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.nrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.nrows - at - 1));
    E.nrows--;
    E.sincemodif++;
}

void editorRowInsertChar(erow* row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.sincemodif++;
}

void editorRowAppendString(erow* row, char* s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.sincemodif++;
}

void editorRowDelChar(erow* row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.sincemodif++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
    if (E.cursorY == E.nrows) {
        editorInsertRow(E.nrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cursorY], E.cursorX, c);
    E.cursorX++;
}

void editorInsertNewline() {
    if (E.cursorX == 0) {
        editorInsertRow(E.cursorY, "", 0);
    } else {
        erow* row = &E.row[E.cursorY];
        editorInsertRow(E.cursorY + 1, &row->chars[E.cursorX], row->size - E.cursorX);
        row = &E.row[E.cursorY];
        row->size = E.cursorX;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cursorY++;
    E.cursorX = 0;
}

void editorDelChar() {
    // return immediately if cursor is past end of the file
    if (E.cursorY == E.nrows) return;
    if (E.cursorX == 0 && E.cursorY == 0) return;

    erow* row = &E.row[E.cursorY];
    if (E.cursorX > 0) {
        editorRowDelChar(row, E.cursorX - 1);
        E.cursorX--;
    } else {
        E.cursorX = E.row[E.cursorY - 1].size;
        editorRowAppendString(&E.row[E.cursorY - 1], row->chars, row->size);
        editorDelRow(E.cursorY);
        E.cursorY--;
    }
}


/*** file io ***/
char* editorRowsToString(int* buflen) {
    int totalLen = 0;
    for (int i = 0; i < E.nrows; i++) {
       totalLen += E.row[i].size + 1;
    }
    *buflen = totalLen;

    char* buf = malloc(totalLen);
    char* p = buf;

    for (int i = 0; i < E.nrows; i++) {
        memcpy(p, E.row[i].chars, E.row[i].size);
        p += E.row[i].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(char* filename) {
    free(E.filename);
    E.filename = strdup(filename);

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
        editorInsertRow(E.nrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.sincemodif = 0;
}

void editorSave() {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    int len;
    char* buf = editorRowsToString(&len);

    // 0644 is std permissions you usually wants for text file
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if  (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.sincemodif = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);

    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/
void editorFindCallback(char* query, int key) {
    static int last_match = -1;
    static int direction = 1;

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match;

    for (int i = 0; i < E.nrows; i++) {
        current += direction;
        if (current == -1) current = E.nrows - 1;
        else if (current == E.nrows) current = 0;

        erow* row = &E.row[current];
        char* match = strstr(row->render, query);
        if (match) {
            last_match = current;
            E.cursorY = current;

            E.cursorY = current;
            E.cursorX = editorRowRxToCx(row, match - row->render);
            E.rowoff = E.nrows;
            break;
        }
    }
}

void editorFind() {
    int cx = E.cursorX;
    int cy = E.cursorY;
    int coff = E.coloff;
    int roff = E.rowoff;

    char* query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

    if (query) {
        free(query);
    } else {
        E.cursorX = cx;
        E.cursorY = cy;
        E.coloff = coff;
        E.rowoff = roff;
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
void editorScroll() {
    E.rx = 0;
    if (E.cursorY < E.nrows) {
        E.rx = editorRowCxToRx(&E.row[E.cursorY], E.cursorX);
    }

    if (E.cursorY < E.rowoff) {
        E.rowoff = E.cursorY;
    }
    if (E.cursorY >= E.rowoff + E.screenRows) {
        E.rowoff = E.cursorY - E.screenRows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screenCols) {
        E.coloff = E.rx - E.screenCols + 1;
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
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screenCols) len = E.screenCols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf* ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.nrows, E.sincemodif ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cursorY + 1, E.nrows);
    if (len > E.screenCols) len = E.screenCols;
    abAppend(ab, status, len);

    while (len < E.screenCols) {
        if (E.screenCols - len == rlen) {
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

void editorDrawMessageBar(struct abuf* ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screenCols) msglen = E.screenCols;
    if (msglen && time(NULL) - E.statusmsg_time < 5) {
        abAppend(ab, E.statusmsg, msglen);
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    // repositions cursor at row=1;col=1
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    // reposition after drawing '~'s
    char buf[32];
    // "\x1b[%d;%dH"
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
            (E.cursorY - E.rowoff) + 1, (E.rx - E.coloff) + 1);

    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** input: mapping keys to functions ***/

// allows user to "save a file as" when ./femto is entered with no arguments
char* editorPrompt(char* prompt, void (*callback)(char*, int)) {
    size_t bufsize = 128;
    char* buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) {
                buf[--buflen] = '\0';
            }
        } else if (c == '\x1b') {
            editorSetStatusMessage("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
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

        if (callback) callback(buf, c);
    }
}

//allows user to move around screen
void editorMoveCursor(int key) {
    erow* row = (E.cursorY >= E.nrows) ? NULL : &E.row[E.cursorY];

    switch (key) {
        case ARROW_LEFT:
            if (E.cursorX != 0) {
                E.cursorX--;
            } else if (E.cursorY > 0) {
                // set backspace=indent,eol
                E.cursorY--;
                E.cursorX = E.row[E.cursorY].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cursorX < row->size) {
                E.cursorX++;
            } else if (row && E.cursorX == row->size) {
                //moves right at EOL
                E.cursorY++;
                E.cursorX = 0;
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
    static int quit_times = FEMTO_QUIT_TIMES;

    int c = editorReadKey();

    switch (c) {
        case '\r': // enter key
            editorInsertNewline();
            break;

        case CTRL_KEY('q'):
            if (E.sincemodif && quit_times > 0) {
                editorSetStatusMessage("File has unwritten changes. ", "Press Ctrl-Q %d times to quit without saving.", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case HOME_KEY:
            E.cursorX = 0;
            break;

        case END_KEY:
            if (E.cursorY < E.nrows) {
                E.cursorX = E.row[E.cursorY].size;
            }
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            /* TODO */
            if (c == DEL_KEY) {
                editorMoveCursor(ARROW_RIGHT);
            }
            editorDelChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.cursorY = E.rowoff;
                } else if (c == PAGE_DOWN) {
                    E.cursorY = E.rowoff + E.screenRows - 1;
                    if (E.cursorY > E.nrows) {
                        E.cursorY = E.nrows;
                    }
                }

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

        case CTRL_KEY('l'):
        case '\x1b': //ignores escape key presses
            break;

        default:
            editorInsertChar(c);
            break;

    }

    quit_times = FEMTO_QUIT_TIMES;
}

/*** initialize ***/
void initEditor() {
    E.cursorX = 0;
    E.cursorY = 0;
    E.rx = 0;
    E.sincemodif = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.nrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) die("getWindowSize");
    E.screenRows -= 2;
}

int main(int argc, char* argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        // opens filename specified
        editorOpen(argv[1]);
    }

    const char* message = "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find";
    editorSetStatusMessage(message);

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
