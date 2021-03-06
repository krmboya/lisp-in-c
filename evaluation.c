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

// custom macros
#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }


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
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

// forward declarations
void lval_print(lval* v);
lval* lval_eval(lval* v);
lval* builtin_op(lval*, char*);

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

// create a pointer to a new empty qexpr lval
lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
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

  case LVAL_QEXPR:
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
  // Returns t converted to an lval

  // if symbol or number, return conversion to that type
  if (strstr(t->tag, "number")) {
    return lval_read_num(t);
  }
  
  if (strstr(t->tag, "symbol")) {
    return lval_sym(t->contents);
  }

  // if root (>) or sexpr or qexpr, create empty list
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }

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
  case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
  }
}


void lval_println(lval* v) {lval_print(v); putchar('\n');}


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


lval* builtin_head(lval* a) {
  // Given a QEXPR within a SEXPR, returns its head

  // check error conditions
  LASSERT(a, a->count == 1, "Function 'head' needs exactly one argument!");

  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, 
	  "Function 'head' passed incorrect type!");
  
  LASSERT(a, a->cell[0]->count > 1, 
	  "Function 'head' passed {}!");
  
  // take the Q-expr
  lval* v = lval_take(a, 0);

  // take the first q-expr element and delete rest
  return lval_take(v, 0);
  
}

lval* builtin_tail(lval* a) {
  // Given a QEXPR within an SEXPR, returns its tail

  // check error conditions
  LASSERT(a, a->count == 1, "Function 'head' needs exactly one argument!");

  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, 
	  "Function 'head' passed incorrect type!");
  
  LASSERT(a, a->cell[0]->count > 1, 
	  "Function 'head' passed {}!");

  // take the Q-expr
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
  
}

lval* builtin_list(lval* a) {
  // Converts SEXPR a to QEXPR
  
  a->type = LVAL_QEXPR;
  return a;
  
}

lval* builtin_eval(lval* a) {
  // Given a QEXPR within an SEXPR, return its evaluation as an SEXPR

  // check error conditions
  LASSERT(a, a->count == 1, "Function 'eval' needs exactly one argument!");

  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, 
	  "Function 'eval' passed incorrect type!");

  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(x);
}

lval* lval_join(lval* x, lval* y) {
  // Combines elements in x and y

  while(y->count) {
    x = lval_add(x, lval_pop(y, 0));
  }

  lval_del(y);
  return x;
}

lval* builtin_join(lval* a) {
  // Joins all the qexpr's in sepxr a

  // precondition
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
	    "Function 'join' passed incorrect type!");
  }

  // pop off first child
  lval* x = lval_pop(a, 0);

  // pop off the rest
  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}

lval* builtin(lval* a, char* func) {
  if (strcmp("list", func) == 0) { return builtin_list(a); }
  if (strcmp("head", func) == 0) { return builtin_head(a); }
  if (strcmp("tail", func) == 0) { return builtin_tail(a); }
  if (strcmp("join", func) == 0) { return builtin_join(a); }
  if (strcmp("eval", func) == 0) { return builtin_eval(a); }
  if (strstr("+-/*", func)) { return builtin_op(a, func); }

  // non matched
  lval_del(a);
  return lval_err("Unknown Function!");
}

lval* lval_eval_sexpr(lval* v) {
  // Returns the evaluation of an sexpr tree

  // evaluate all children (if any)
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(v->cell[i]);
  }

  // Return any error found
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  // `()', return as is
  if (v->count == 0) { return v; }

  // single expr, return child
  if (v->count == 1) { return lval_take(v, 0); }

  // > 1 children, ensure first element is symbol or return error
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression does not start with symbol!");
  }

  // call builtin with operator
  lval* result = builtin(v, f->sym);
  lval_del(f);
  return result;
}

lval* lval_eval(lval* v) {
  // Returns the evaluation of an expression
  
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
  return v;  // others remain the same
}

lval* builtin_op(lval* a, char* op) {
  // Returns result of operator on arguments in `a'

  // ensure all arguments are numbers, or return error
  for (int i=0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM ) {
      lval_del(a);
      return lval_err("Cannot operate on a non-number!");
    }
  }

  // pop off 1st element
  lval* x = lval_pop(a, 0);

  // check if is unary negation
  if ((strcmp(op, "-") == 0) && a->count == 0) {
    x->num = -x->num;
  }

  // for each of the remaining args..
  while(a->count > 0) {

    lval* y = lval_pop(a, 0);  // pop next arg

    if (strcmp(op, "+") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0) { x->num *= y->num; }
    if (strcmp(op, "/") == 0) {
      if (y->num == 0) {
	lval_del(x); lval_del(y);
	x = lval_err("Division by zero!"); break;
      }
      x->num /= y->num;
    }
    lval_del(y); // finished with arg
  }

  lval_del(a);  // finished with arg list
  return x;
}


int main(int arg, char** argv) {

  /* Create parsers */
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  /* Define them with the following language */
  mpca_lang(MPCA_LANG_DEFAULT, "number : /-?[0-9]+/ ;                        \
                                symbol : '+' | '-' | '*' | '/' | \"list\" |  \
                                         \"head\" | \"tail\" | \"join\" |    \
                                         \"eval\";                           \
                                sexpr  : '(' <expr>* ')' ;                   \
                                qexpr  : '{' <expr>* '}' ;                   \
                                expr   : <number> | <symbol> | <sexpr> |     \
                                         <qexpr>;                            \
                                lispy  : /^/ <expr>* /$/ ;",
	    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

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
      // lval_read - converts into an internal form
      // lval_eval - evaluates the internal form
      // lval_print - prints out the internal form
      lval* x = lval_eval(lval_read(r.output));
      lval_println(x);  // print expr structure
      lval_del(x);  // delete expr structure
    } else {
      // print error
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    
    // free retrieved input
    free(input);
  }

  // undefine and delete parsers
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  return 0;
}
