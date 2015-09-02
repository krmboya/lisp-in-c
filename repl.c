#include <stdio.h>
#include <stdlib.h>

/* if compiling for Windows, compile these functions */
#ifdef _WIN32
#include <string.h>

static char buffer[2048];

/* fake readline function */
char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy;
}

/* Fake add_history function */
void add_history(char* unused) {};

/* if not Windows, include the editline headers */
#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

int main(int arg, char** argv) {

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");

  /*Never ending loop*/
  while (1) {
    // output prompt and get input
    char* input = readline("lispy> ");
    
    // Add input to history
    add_history(input);
    
    // echo output back to user
    printf("No you're a %s\n", input);

    // free retrieved input
    free(input);
  }
  return 0;
}
