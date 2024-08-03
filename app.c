/*** includes ***/
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f) // Macro to mimic ctrl key from the keyboard



/*** Data ***/

// A global variable for storing state of our editor
struct editorConfig {
    // a struct variable for original terminal
    struct termios original_terminal;

    // to store screen mesurements
    int screenrows;
    int screencols;

} typedef editorConfig;

editorConfig E;


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
char editorReadKey()
{
    int nread;

    char c;
    if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
        die("read");
    
        
    // if(iscntrl(c))
    //     printf("%d\r\n",c);
    // else
    //     printf("%d (%c)\r\n",c,c);

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

// A function to process the keys pressed on the keyboard
void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO,"\x1b[2J",4);
        write(STDOUT_FILENO,"\x1b[H",3);
        exit(0);
    
    default:
        break;
    }
}


/*** Output ***/

// To mark each row in our editor
void editorDrawRows()
{
    for(int i=0;i<E.screenrows;i++)
    {
        write(STDOUT_FILENO,"~",1);

        if(i<E.screenrows-1)
            write(STDOUT_FILENO,"\r\n",2);
    }
    printf("%d",E.screenrows);
        
}


// To clear screen and put cursor at the start of the editor
void editorRefreshScreen()
{
    // writing the escape sequence for clearing the entire screen 
    write(STDOUT_FILENO,"\x1b[2J",4);

    // moving the cursor to the top left
    write(STDOUT_FILENO,"\x1b[H",3);

    editorDrawRows();

    write(STDOUT_FILENO,"\x1b[H",3);
}


/*** Init ***/

// initialise all the fields of our editor
void initEditor()
{
    if(getWindowSize(&E.screenrows,&E.screencols) == -1)
        die("getWindowSize");
}


int main()
{
    enableRawMode();
    initEditor();

    // reading text input in the termial 1 byte at a time
    // stdin by default it the shell/terminal
    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}