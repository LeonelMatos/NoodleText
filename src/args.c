#include <stdio.h>

///Used when given additional parameters on execution
///of the program. Verifies the argument and runs
/// accordingly. \arg \c char * arg
///\return -1 to end the program
int args_check (char * arg) {
    if (arg[0] != '-') return -1;

    switch (arg[1])
    {
        case 'h':
            printf("HELP\n\nNAME\n\tnoodle - NoodleText\n\n");
            printf("SYNOPSIS\n\tnoodle [options]\n\n");
            printf("NOTICE\n\tThis is still an early alpha version. Currently NoodleText holds very few functions and lacks content.\n\n");
            printf("DESCRIPTION\n\tNoodleText is a minimal and friendly editor. Inspired by the 'big guys' editors such as Nano and Windows\n\ttext editor, although more basic, it has the essencial and the heart of a worthy text editor.\n\n");
            printf("OPTIONS\n\t-h, 'help'\n\t\tA full description of the following options.\n\n");
            printf("\t-l, 'logs'\n\t\tClears the accumulated log files.\n\n");
            printf("Author\n\tLeonel Matos <https://github.com/leonelmatos/noodletext/>\n\n");
            printf("Version\n\t04.07.22   version 0.1\n\n\n");
            return -1;
        default:
            printf("\nInvalid input\n\n");
            return -1;
    }
}