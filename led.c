#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>

#define LED_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)
#define ESPSEQ '\x1b'
#define M_TOP "\x1b[H", 3
#define M_BTMR "\x1b[999C\x1b[999B", 12
#define CLR_SCRN "\x1b[2J", 4
#define GET_CUR_POS "\x1b[6n", 4
#define CLR_LINE "\x1b[K", 3
#define CURS_ON "\x1b[?25h", 6
#define CURS_OFF "\x1b[?25l", 6

#define SCROLL_ROWS 10

#define SET_CURS_POS "\x1b[%d;%dH"

#define ABUF_INIT {NULL, 0}

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  DEL_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
};

typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig {
    int cx;
    int cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow* row;
    struct termios orig_termios;
};

struct abuf {
    char* b;
    int len;
};

struct editorConfig E;

void abAppend(struct abuf* ab, const char* s, int len) {
    char* new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(new+(ab->len), s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

void editorClearScreen() {
    write(STDIN_FILENO, CLR_SCRN);
    write(STDIN_FILENO, M_TOP);
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

void drawWelcome(struct abuf* ab){
    char welcom[80];
    int welcomlen;
    int padding;

    welcomlen = snprintf(welcom, sizeof(welcom),
                         "Led Editor -- version %s",
                         LED_VERSION);
    if (E.screencols < welcomlen) welcomlen = E.screencols;
    padding = (E.screencols - welcomlen) / 2 - 1;
    while (padding--) abAppend(ab, " ", 1);
    abAppend(ab, welcom, welcomlen);

}

void editorScroll() {
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }

    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.cx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf* ab) {
    int y;

    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            abAppend(ab, "~", 1);
            if (E.numrows == 0 && y == E.screenrows / 3) drawWelcome(ab);
        } else {
            int len = E.row[filerow].size - E.coloff;
             if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }

        abAppend(ab, CLR_LINE);
        if (y < E.screenrows - 1) abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, CURS_OFF);
    abAppend(&ab, M_TOP);
    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), SET_CURS_POS, (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, CURS_ON);
    write(STDIN_FILENO, ab.b, ab.len);
    abFree(&ab);
}

int editorReadKey() {
    char c;
    int nread;
    while (( nread = read(STDIN_FILENO, &c, 1) != 1)) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == ESPSEQ) {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return ESPSEQ;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return ESPSEQ;

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return ESPSEQ;
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
        return ESPSEQ;    
    } else {
        return c;
    }

}

void editorMoveCursor(int key) {
    erow* row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cy == E.row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0)  E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.screenrows)  E.cy++;
            break;
    }
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    if (row && E.cx > row->size) {
        E.cx = row->size;
    }
}

void processKeypress() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            editorClearScreen();
            exit(0);
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int scroll = 0;
                while (scroll++ <= SCROLL_ROWS)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_DOWN:
            editorMoveCursor(c);
            break;
    }
}

int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDIN_FILENO, GET_CUR_POS) != 4) return -1;

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
        if(write(STDIN_FILENO, M_BTMR) != 12) die("move cursor to bottom right");
        return getCursorPosition(rows, cols);
    }else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow)*(E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

// file io
void editorOpen(char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    if(getWindowSize( &E.screenrows, &E.screencols) != 0) die("getWindowSize");
}

int main(int argc, char* argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        processKeypress();
    }

	return 0;
}