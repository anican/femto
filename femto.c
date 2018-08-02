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
    raw.c_lflag &= ~(ECHO); // disables printing each key to terminal

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
    return 0;
}


