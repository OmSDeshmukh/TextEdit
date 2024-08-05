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

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f) // Macro to mimic ctrl key from the keyboard
#define EDITOR_VERSION "0.0.1"
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#define _BSD_SOURCE

/*** Data ***/

// A structure to store the text present in the editor 
typedef struct erow{
    int size;
    char* chars;
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

    int numrows; // number of rows with text in current file
    erow *row; // array of row data

    int rowoff; // row offset for scrolling
    int coloff; // column offset for horizontal scrolling
} typedef editorConfig;

editorConfig E;

// all those keys with more than 1byte escape sequences
enum editorKey {
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


/*** file I/O ***/

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

void editorOpen(char* filename)
{
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
            if (E.cx != 0) {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            // We dont let cursor move past last character in respective row
            if (row && E.cx < row->size) {
                E.cx++;
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
}


// A function to process the keys pressed on the keyboard
void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c)
    {
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
                int times = E.screenrows;
                while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;
    }
}


/*** Output ***/

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
            int len = E.row[filerow].size - E.coloff;

            if(len < 0)
                len = 0;

            // Displaying only what is possible
            if(len > E.screencols)
                len = E.screencols;

            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }
        // escape sequence to clear a line
        abAppend(ab, "\x1b[K", 3);

        if(i<E.screenrows-1)
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

    // Displaying the cursor at the required location
    char buff[24];
    snprintf(buff,sizeof(buff),"\x1b[%d;%dH",E.cy - E.rowoff + 1, E.cx - E.coloff + 1);
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
    E.numrows = 0;
    E.row = NULL;
    E.rowoff = 0;
    E.coloff = 0;


    if(getWindowSize(&E.screenrows,&E.screencols) == -1)
        die("getWindowSize");
    
}


int main(int argc, char* argv[])
{
    enableRawMode();
    initEditor();

    if(argc >= 2)
        editorOpen(argv[1]);

    // reading text input in the termial 1 byte at a time
    // stdin by default it the shell/terminal
    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}