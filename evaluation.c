#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"

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

// Declare a Lisp Value struct
typedef struct lval {
  int type;
  long num;
  char* err; // error strings
  char* sym; // symbols
  
  int count; // count of child lvals
  struct lval** cell;  // pointer to list of pointers to lvals
  
} lval;

// enumeration of possible lval types
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR };

// create pointer to a new number lval
lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

// create a pointer to a new error lval
lval* lval_err(char* m) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}

// create a pointer to a new symbol lval
lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

// create a pointer to a new empty sexpr lval
lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

// free lval types
void lval_del(lval* v) {
  switch (v->type) {
    
    
  case LVAL_NUM:
    // do nothing special for lval number type
    break;

  case LVAL_ERR:
    free(v->err);
    break;

  case LVAL_SYM:
    free(v->sym);
    break;

  case LVAL_SEXPR:
    // delete all child elements
    for (int i = 0; i < v->count; i++) {
      lval_del(v->cell[i]);
    }

    // free memory allocated to the pointers
    free(v->cell);
    break;
  }

  // free memory allocated to the lval struct itself
  free(v);
}

// return lval number (or error)
lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

lval* lval_add(lval* v, lval* x) {
  // Adds a new element to sexpr pointed by `v'
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval* lval_read(mpc_ast_t* t) {

  // if symbol or number, return conversion to that type
  if (strstr(t->tag, "number")) {
    return lval_read_num(t);
  }
  
  if (strstr(t->tag, "symbol")) {
    return lval_sym(t->contents);
  }

  // if root (>) or sexpr, create empty list
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }

  // fill the created list with any valid expressions contained within
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") ==  0) { continue; }
    if (strcmp(t->children[i]->contents, ")") ==  0) { continue; }
    if (strcmp(t->children[i]->contents, "{") ==  0) { continue; }
    if (strcmp(t->children[i]->contents, "}") ==  0) { continue; }
    if (strcmp(t->children[i]->tag,  "regex") ==  0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
  
}

void lval_print(lval *v); // forward declaration

void lval_expr_print(lval* v, char open, char close) {
  // prints an sexpr's children
  
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    // print value contained within
    lval_print(v->cell[i]);

    // Only print trailing space if not last element
    if (i != (v->count-1)) {
      putchar(' ');
    }
  }
  putchar(close);
}

void lval_print(lval* v) {
  switch (v->type) {
  case LVAL_NUM: printf("%li", v->num); break;
  case LVAL_ERR: printf("Error: %s", v->err); break;
  case LVAL_SYM: printf("%s", v->sym); break;
  case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
  }
}

void lval_println(lval* v) {lval_print(v); putchar('\n');}

lval* lval_eval_sexpr(lval* v) {
  // Returns the evaluation of an sexpr tree

  // evaluate children
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(v->cell[i]);
  }

  // error checking
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  // empty expr
  if (v->count == 0) { return v; }

  // single expr
  if (v->count == 1) { return lval_take(v, 0); }

  // ensure first element is symbol
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression does not start with symbol!");
  }

  // call with operator
  lval* result = builtin_op(v, f->sym);
  lval_del(f);
  return result;
}

lval* lval_eval(lval* v) {
  // Returns the evaluation of an expression
  
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
  return v;  // others remain the same
}

lval* lval_pop(lval* v, int i) {
  // Pops off item i of sexpr

  lval* x = v->cell[i];

  // shift cell items backwards
  
  memmove(&v->cell[i],   // dest
	  &v->cell[i+1], // src
	  sizeof(lval*) * (v->count-i-1) ); // size

  // decrease count
  v->count--;

  // Reallocate memory used
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

lval* lval_take(lval* v, int i) {
  // Retrieves item i of sexpr and deletes the rest

  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}


int main(int arg, char** argv) {

  /* Create parsers */
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  /* Define them with the following language */
  mpca_lang(MPCA_LANG_DEFAULT, "number : /-?[0-9]+/ ;                        \
                                symbol : '+' | '-' | '*' | '/' ;             \
                                sexpr  : '(' <expr>* ')' ;                   \
                                expr   : <number> | <symbol> | <sexpr> ;     \
                                lispy  : /^/ <expr>* /$/ ;",
	    Number, Symbol, Sexpr, Expr, Lispy);

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");

  /*Never ending loop*/
  while (1) {
    // output prompt and get input
    char* input = readline("lispy> ");
    
    // Add input to history
    add_history(input);
    
    // Attempt to parse the input
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval* x = lval_read(r.output); // return sexpr structure
      lval_println(x);  // print sexpr structure
      lval_del(x);  // delete sexpr structure
    } else {
      // print error
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    
    // free retrieved input
    free(input);
  }

  // undefine and delete parsers
  mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Lispy);
  return 0;
}
