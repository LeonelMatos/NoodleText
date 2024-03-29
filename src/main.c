/*--- INCLUDE ---*/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include "args.h"

/*--- DEFINES ---*/

/// Value that represents the CTRL key
#define CTRL_KEY(k) ((k)&0x1f)

#define NOODLE_VERSION "0.0.1"

enum editorKey
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*--- DATA ---*/

/// @brief \name editorRow : a row of text
/// Contains the stored text and its size
typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig
{
    /// cx, cy: current position of the cursor
    int cx, cy;
    // Row of the file the user scrolled to
    /// Offset related to the row of text
    int rowoff;
    /// Offset related to the column of text
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    /// Placeholder for the terminal's default settings
    struct termios orig_termios;
};

struct editorConfig E;

///=die. Error handling function using unistd.h \arg (string) s: error id name
void kill(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

/*--- TERMINAL OPTIONS ---*/
/// Resets the terminal's attributes with orig_termios
void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        kill("tcsetattr");
}

/// Sets the terminal's attributes
void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        kill("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        kill("tcgetattr");
}

/// Handles the reading of STDIN and stores a char at a time in input.
/// \return \c(char) input: key input from STDIN
int editorReadKey()
{
    int nread;
    char input;
    while ((nread = read(STDIN_FILENO, &input, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            kill("read");
    }

    if (input == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {

                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';

                if (seq[2] == '~')
                    switch (seq[1])
                    {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
            }
            else
                switch (seq[1])
                {
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
        else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }
    else
        return input;
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

/// Gives the size of the terminal \arg (*int) rows: window rows
///\arg (*int) cols: window collumns
int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        // Moves the cursor forward/down to the down-right corner of the terminal
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*--- ROW_OPERATIONS ---*/

void editorAppendRow(char *s, size_t len)
 {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
 }


/*--- FILE_I/O ---*/

void editorOpen(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) kill("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
            linelen--;
        
        editorAppendRow(line, linelen);
    }
    
    free(line);
    fclose(fp);

}

/*--- APPEND_BUFFER ---*/

// Definitions
/// Append buffer: dynamic string type struct.
struct abuf
{
    /// Char pointer to the buffer in memory with length
    char *b;
    /// int length value of the buffer
    int len;
};

/// Represents an empty buffer, used as a constructor
/// for the abuf type struct
#define ABUF_INIT {NULL, 0}

// Functions

/// Appends a string \arg s to the a given \arg abuf struct
/// with a \arg len size by allocating enough memory for \c s
void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

/// Deallocates the dynamic memory used by the given \arg ab abuf struct
void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*--- OUTPUT ---*/


/// Check if the cursor moved outside the editor window
void editorScroll() 
{
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }
    if (E.cx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;
    }
}

/// Draws the rows on the side of the editor
void editorDrawRows(struct abuf *ab)
{
    /// Number of rows to draw
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;

        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];
                int welcome_len = snprintf(welcome, sizeof(welcome),
                "<< NoodleText || version %s FILE: %d>>", NOODLE_VERSION, E.numrows);

                if (welcome_len > E.screencols)
                    welcome_len = E.screencols;

                int padding = (E.screencols - welcome_len) / 2;

                if (padding)
                {
                    abAppend(ab, "*", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);

                abAppend(ab, welcome, welcome_len);

            }
            else
            {
                abAppend(ab, "*", 1);
            }
        } else {
            int len = E.row[filerow].size - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }

        // abAppend(ab, "*", 1);

        abAppend(ab, "\x1b[K", 3);

        if (y < E.screenrows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

/// Clears the screen of the editor
void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT; // Creates the abuf buffer with INIT
    // Instead of always using write(STDOUT_FILENO,...), the abAppend(...) will
    // gather the text and write it all at once

    abAppend(&ab, "\x1b[?25l", 6); // Adds the string to the buffer
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len); // Writes out all the accumulated strings

    abFree(&ab); // After writing, frees the allocated memory
}

/*--- INPUT ---*/

/// @brief Receives the given WASD keys and moves the cursor accordingly
/// @param key Relates the key with WASD to move the cursor
void editorMoveCursor(int key)
{
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
            E.cx--;
        break;
    case ARROW_RIGHT:
            E.cx++;
        break;
    case ARROW_UP:
        if (E.cy != 0)
            E.cy--;
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows)
            E.cy++;
        break;
    }
}

/// Processes the result/command of each key
/// Calls the \c editorReadKey() function to receive
/// the user's input
void editorProcessKeypress()
{
    /// Input holder with stdin
    int input = editorReadKey();

    switch (input)
    {
    case CTRL_KEY('q'): // Exits the editor
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case HOME_KEY:  // Moves the cursor to the left/right
        E.cx = 0;   // edges of the screen
        break;

    case END_KEY:
        E.cx = E.screencols - 1;
        break;

    case PAGE_UP:   // Moves the cursor to the beginning
    case PAGE_DOWN: // or the end of the editor
    {
        int times = E.screenrows;
        while (times--)
            editorMoveCursor(input == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    case ARROW_UP: // Moves the cursor
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(input);
    }
}

/*--- INIT ---*/

/// Initialize all the fields in the E struct after enabling raw mode in the editor
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        kill("getWindowSize");
}

/// The Main function.
///\arg Receives arguments when called, used as options
int main(int argc, char *argv[])
{
    // Check arguments for extra options
    if (argc >= 2)
    {
        if (argv[1][0] == '-') {
            if (args_check(argv[1]) == -1) return 0;
        }
        
    }

    enableRawMode();
    initEditor();

    if (argc >= 2 && argv[1][0] != '-')
        editorOpen(argv[1]);

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    printf("\n");
    return 0;
}