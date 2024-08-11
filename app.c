/*** includes ***/
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f) // Macro to mimic ctrl key from the keyboard
#define EDITOR_VERSION "0.0.1"
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#define _BSD_SOURCE
#define KILO_TAB_STOP 8

/*** Data ***/

// A structure to store the text present in the editor 
typedef struct erow{
    int size;
    int rsize;
    char* chars;
    char* render;
} erow;


// A global variable for storing state of our editor
struct editorConfig {
    // a struct variable for original terminal
    struct termios original_terminal;

    // to store screen mesurements
    int screenrows;
    int screencols;

    // measuments of the curser
    int cx; // which column
    int cy; // which row
    int rx; // handling tab spaces

    int numrows; // number of rows with text in current file
    erow *row; // array of row data

    int rowoff; // row offset for scrolling
    int coloff; // column offset for horizontal scrolling

    char* filename; // to store filename

    char statusmsg[80]; // for status messages
    char statusmsg_time;

    int dirty;// bit to keep track of data loaded into the editor

} typedef editorConfig;

editorConfig E;

// all those keys with more than 1byte escape sequences
enum editorKey {
    BACKSPACE = 127,
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

/***  Prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);

/*** Terminal ***/

// Custom error handling function
void die(const char* s)
{
    write(STDOUT_FILENO,"\x1b[2J",4);
    write(STDOUT_FILENO,"\x1b[H",3);

    perror(s);
    exit(1);
}

/* To disable the raw mode
setting the terminal back to its original attributes(Canonical Mode)
*/
void disableRawMode()
{
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_terminal) == -1)
        die("tcsetattr");
}


// to set the termial into raw mode
void enableRawMode()
{
    // get the attributes of the current terminal 
    if (tcgetattr(STDIN_FILENO, &E.original_terminal) == -1)
        die("tcgetattr");

    // Register the disableRawMode function to be automatically called when the program exits
    atexit(disableRawMode);

    // make a copy of the original attributes
    struct termios raw_terminal  = E.original_terminal;

    // Switching off different flags related to certain keyboard inputs
    raw_terminal.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw_terminal.c_oflag &= ~(OPOST);
    raw_terminal.c_cflag |= (CS8);
    raw_terminal.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw_terminal.c_cc[VMIN] = 0; // minimum number of bytess input before read() returns
    raw_terminal.c_cc[VTIME] = 1; // minimum amount of time before read() returns

    // A function to apply the modified changes
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_terminal) == -1)
        die("tcsetattr");
}

// A function for reading the key presses and return the characters pressed
int editorReadKey()
{
    int nread;

    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) 
        if (nread == -1 && errno != EAGAIN) die("read");
    
    // when the read characters are escape sequences
    if(c=='\x1b')
    {
        char seq[3];

        // if nothing after that, return the escape seq only
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
                {
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
            } 
            else 
            {
                switch (seq[1]) 
                {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } 
        else if (seq[0] == 'O')     
        {
            switch (seq[1]) 
            {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }
    // when read characters are usual characters
    else
        return c;
}

// to get the cursor position
int getCursorPosition(int *rows, int* cols)
{
    char buff[32];
    unsigned int i = 0;

    // to get the device status report
    if(write(STDOUT_FILENO,"\x1b[6n",4) != 4) 
        return -1;


    // to handle the reply of the above status report obtained
    while(i<sizeof(buff)-1)
    {
        if(read(STDIN_FILENO, &buff[i], 1) != 1) 
            return -1;

        if(buff[i]=='R')
            break;
        i++;
    }

    buff[i] = '\0';

    // printing the buffer
    // printf("\r\n&buff[1]: '%s'\r\n",&buff[1]);

    // parsing the buffer(i.e. the reply)
    if(buff[0] != '\x1b' || buff[1] != '[')
        return -1;
    
    if(sscanf(&buff[2], "%d;%d", rows, cols)!=2)
        return -1;

    return 0;
}


// A function to get the window size of the current terminal
int getWindowSize(int *rows, int* cols)
{
    struct winsize ws;

    if(ioctl(STDIN_FILENO,TIOCGWINSZ,&ws) == -1 || ws.ws_col == 0)
    {
        if(write(STDOUT_FILENO,"\x1b[999C\x1b[999B",12)!=12)
            return -1;

        // fallback mechanism for getting window size
        return getCursorPosition(&E.screenrows,&E.screencols);
    }
    else
    {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}


/*** Row Operations ***/
// These are about the row buffer 

// copy all characters into the render of a row
void editorUpdateRow(erow *row) 
{
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++) 
    {
        if (row->chars[j] == '\t') 
        {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
        } 
        else 
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

// making changes in a certain row in the buffer
void editorRowInsertChar(erow* row, int at, int c)
{
    if(at < 0 || at > row->size)
        at = row->size;

    row->chars = realloc(row->chars, row->size+2);

    // basically all character at and beyound at are moved 1 index further to accomodate the new character
    memmove(&row->chars[at+1], &row->chars[at], row->size -at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

int editorRowCxToRx(erow* row, int cx)
{
    int rx = 0;
    int j;
    for(j=0;j<cx;j++)
    {
        if(row->chars[j]=='\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}



/*** file I/O ***/

// initialising each row in the editor
void editorAppendRow(char *s, size_t len) 
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

// combining the characters in all the rows as one continuous string
char* editorRowsToString(int *buflen)
{
    int totlen = 0;
    int j;
    for(j=0;j<E.numrows;j++)
        totlen += E.row[j].size + 1;
    
    *buflen = totlen;
    
    char* buf = malloc(totlen);
    char* p = buf;
    for(j=0;j<E.numrows;j++)
    {
        memcpy(p,E.row[j].chars,E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorOpen(char* filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    // reading input from a file
    FILE *fp = fopen(filename, "r");
    if(!(fp))
        die("fopen"); // error message

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while(linelen > 0 && (line[linelen-1]=='\n' || line[linelen-1]=='\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

// to save contents into the file
void editorSave()
{
    if(E.filename == NULL)
        return;

    int len;
    char* buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if(fd!=-1)
    {
        if(ftruncate(fd, len)!=-1)
        {
            if(write(fd, buf, len)==len)
            {
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
    
}


/*** Editor operations ***/
// These are about the actual visible editor

void editorInsertChar(int c)
{
    // if on the last line, add a new row to the editor
    if(E.cy == E.numrows)
    {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}


/*** Append Buffer ***/
// Kind of like a dynamic buffer

struct abuf
{
    char* b;
    int len;
}typedef abuf;

#define ABUF_INIT {NULL,0} // Empty buffer(like a constructor)

// To append the string s to our current buffer
void abAppend(abuf *ab, const char* s, int len)
{
    // allocate enough memory to hold new string
    char* new = realloc(ab->b,ab->len + len);

    if( new == NULL)
        return;
    
    memcpy(&new[ab->len],s,len);
    ab->b = new;
    ab->len += len;
}

// Like a destructor to free the buffer
void abFree(abuf *ab)
{
    free(ab->b);
}



/*** Input/Keypress handling ***/

// Function for cursor movement handling
void editorMoveCursor(int key) 
{
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) 
    {
        case ARROW_LEFT:
            if (E.cx != 0) 
            {
                E.cx--;
            }
            else if(E.cy > 0)
            {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            // We dont let cursor move past last character in respective row
            if (row && E.cx < row->size) {
                E.cx++;
            }
            else if(row && E.cx == row->size)
            {
                E.cy++;
                E.cx =0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    row = (E.cy > E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if(E.cx > rowlen)
        E.cx = rowlen;
}


// A function to process the keys pressed on the keyboard
void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c)
    {
        case '\r':
            /* TODO */
            break;
        case CTRL_KEY('q'):
            write(STDOUT_FILENO,"\x1b[2J",4);
            write(STDOUT_FILENO,"\x1b[H",3);
            exit(0);
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if(c==PAGE_UP)
                    E.cy = E.rowoff;
                else if(c==PAGE_DOWN)
                {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }
            }
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.numrows)
                E.cx = E.row[E.cy].size;
            break;

        case BACKSPACE:
        case DEL_KEY:
        case CTRL_KEY('h'):
            /*TODO*/
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        default:
            editorInsertChar(c);
            break;
    }
}


/*** Output ***/

void editorDrawMessageBar(struct  abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) 
        msglen = E.screencols;
    // if (msglen && time(NULL) - E.statusmsg_time < 500)
    abAppend(ab, E.statusmsg, msglen);
}

// Custom printf type function for displaying message
void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

void editorDrawStatusBar(struct abuf* ab)
{
    // inverting colors
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];

    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                            E.filename ? E.filename : "[No Name]", E.numrows,
                            E.dirty ? "(modified)" : "");
    // for the rendering
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                E.cy + 1, E.numrows);

    if (len > E.screencols) 
        len = E.screencols;
    abAppend(ab, status, len);

    while(len < E.screencols)
    {
        if (E.screencols - len == rlen) 
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    // reverse the inversion
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}


void editorScroll() 
{
    E.rx = 0;
    if(E.cy < E.numrows)
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

    if (E.cy < E.rowoff) 
    {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) 
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) 
    {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) 
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

// To mark each row in our editor
void editorDrawRows(abuf *ab)
{
    for(int i=0;i<E.screenrows;i++)
    {
        int filerow = i + E.rowoff;
        if(filerow>=E.numrows)
        {
            // Display the welcome message
            if(E.numrows==0 && i==E.screenrows/3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Text editor -- version %s", EDITOR_VERSION);
                if (welcomelen > E.screencols) 
                    welcomelen = E.screencols;

                // centring
                int padding = (E.screencols - welcomelen)/2;
                if(padding)
                {
                    abAppend(ab,"~",1);
                    padding--;
                }
                while(padding--)
                    abAppend(ab," ",1);


                abAppend(ab, welcome, welcomelen);
            }
            else
                abAppend(ab,"~",1);
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff;

            if(len < 0)
                len = 0;

            // Displaying only what is possible
            if(len > E.screencols)
                len = E.screencols;

            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        // escape sequence to clear a line
        abAppend(ab, "\x1b[K", 3);

        // if(i<E.screenrows-1)
        abAppend(ab,"\r\n",2);
    }
}



// To clear screen and put cursor at the start of the editor
void editorRefreshScreen()
{
    editorScroll();
    abuf ab = ABUF_INIT;

    // escape sequence for hiding the cursor
    abAppend(&ab, "\x1b[?25l", 6);
    // moving the cursor to the top left so that tilde can be printed correctly from start
    abAppend(&ab,"\x1b[H",3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    // Displaying the cursor at the required location
    char buff[24];
    snprintf(buff,sizeof(buff),"\x1b[%d;%dH",E.cy - E.rowoff + 1, E.rx - E.coloff + 1);
    abAppend(&ab, buff, strlen(buff));


    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO,ab.b,ab.len);
    abFree(&ab);
}


/*** Init ***/

// initialise all the fields of our editor
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.numrows = 0;
    E.row = NULL;
    E.rowoff = 0;
    E.coloff = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.dirty = 0;


    if(getWindowSize(&E.screenrows,&E.screencols) == -1)
        die("getWindowSize");
    E.screenrows -= 2; // setting screen rows to -2 so that our application thinks there are two lesser lines and we can use it for the status bar
    
}


int main(int argc, char* argv[])
{
    enableRawMode();
    initEditor();

    if(argc >= 2)
        editorOpen(argv[1]);

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

    // reading text input in the termial 1 byte at a time
    // stdin by default it the shell/terminal
    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}