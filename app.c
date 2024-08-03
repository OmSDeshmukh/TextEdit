/*** includes ***/
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>

/*** Data ***/
// a struct variable for original terminal
struct termios original_terminal;


/*** Terminal ***/

// Custom error handling function
void die(const char* s)
{
    perror(s);
    exit(1);
}

/* To disable the raw mode
setting the terminal back to its original attributes(Canonical Mode)
*/
void disableRawMode()
{
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal) == -1)
        die("tcsetattr");
}


// to set the termial into raw mode
void enableRawMode()
{
    // get the attributes of the current terminal 
    if (tcgetattr(STDIN_FILENO, &original_terminal) == -1)
        die("tcgetattr");

    // Register the disableRawMode function to be automatically called when the program exits
    atexit(disableRawMode);

    // make a copy of the original attributes
    struct termios raw_terminal  = original_terminal;

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



/*** Init ***/
int main()
{
    enableRawMode();

    // reading text input in the termial 1 byte at a time
    // stdin by default it the shell/terminal
    while(1)
    {
        char c = '\0';
        if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
            die("read");

        if(iscntrl(c))
            printf("%d\r\n",c);
        else
            printf("%d (%c)\r\n",c,c);

        if (c=='q')
            break;
    }
    return 0;
}