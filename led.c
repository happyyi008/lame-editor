#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>

#define CTRL_KEY(k) ((k) & 0x1f)

#define M_TOP "\x1b[H"
#define M_BTMR "\x1b[999C\x1b[999B"
#define CLR_SCRN "\x1b[2J"
#define GET_CUR_POS "\x1b[6n"

struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

void editorClearScreen() {
    write(STDIN_FILENO, CLR_SCRN, 4);
    write(STDIN_FILENO, M_TOP, 3);
}

void die(const char* s) {
    editorClearScreen();
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode() {
    struct termios raw;
    atexit(disableRawMode);

    if (tcgetattr(STDIN_FILENO, &raw) == -1) die("tcgetattr");
    E.orig_termios = raw;

    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

void editorDrawRows() {
    int y;

    for (y = 0; y < E.screenrows-1; y++) {
        write(STDIN_FILENO, "~\r\n", 3);
    }
    write(STDIN_FILENO, "~", 1);

}

void editorRefreshScreen() {
    editorClearScreen();
    editorDrawRows();
    write(STDIN_FILENO, M_TOP, 3);
}

char editorReadKey() {
    char c;
    int nread;
    while (( nread = read(STDIN_FILENO, &c, 1) != 1)) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

void processKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            editorClearScreen();
            exit(0);
            break;
    }
}

int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDIN_FILENO, GET_CUR_POS, 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;        
        i++;
    }

    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDIN_FILENO, M_BTMR, 12) != 12) die("move cursor to bottom right");
        return getCursorPosition(rows, cols);
    }else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void initEditor() {
    editorClearScreen();
    if(getWindowSize( &E.screenrows, &E.screencols) != 0) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        processKeypress();
    }

	return 0;
}