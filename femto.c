#include<ctype.h>
#include<stdio.h>
#include<stdlib.h>
#include<termios.h>
#include<unistd.h>

// stores original attrib. of terminal before opening femto
struct termios orig_termios;

// Resets terminal to its original state after exiting femto
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Raw mode processes each input key once it is pressed
void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode); // Disables raw mode when program exits

    struct termios raw = orig_termios;
    // Reading input byte by byte
    raw.c_lflag &= ~(ECHO | ICANON);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        if (iscntrl(c)) {
            printf("%d\n", c);
        } else {
            printf("%d ('%c')\n", c, c);
        }
    }

    return 0;
}


