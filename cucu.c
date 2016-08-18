#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define MAXTOKSZ 256

/* print fatal error message and exit */
static void error(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  exit(1);
}

//
// SYMBOLS
//
#define MAXSYMBOLS 4096
static struct sym {
  char type;
  int  addr;
  char name[MAXTOKSZ];
  int  nParams;
} sym[MAXSYMBOLS];

static int sympos = 0;
int stack_pos = 0;


//
// LEXER
//
static FILE *f;            /* input source file */
static char tok[MAXTOKSZ]; /* current token */
static int tokpos;         /* offset inside the current token */
static int nextc;          /* next char to be pushed into token */
static int linenum = 1;
static int _debug = 0;
static char context[MAXTOKSZ];
static int genPreamble = 0;
static int numPreambleVars = 0;
static int numGlobalVars = 0;
static int lastIsReturn = 0;
static int flagScanGlobalVars = 1;
static struct sym *currFunction = NULL;

/* read next char */
void readchr() {
  if (tokpos == MAXTOKSZ - 1) {
    tok[tokpos] = '\0';
    error("[line %d] Token too long: %s\n", linenum, tok);
  }
  tok[tokpos++] = nextc;
  nextc = fgetc(f);
  if ('\n'==nextc) {linenum++;}
}

/* read single token */
void readtok() {
  for (;;) {
    /* skip spaces */
    while (isspace(nextc)) {
      nextc = fgetc(f);
      if ('\n'==nextc) {linenum++;}
    }
    /* try to read a literal token */
    tokpos = 0;
    while (isalnum(nextc) || nextc == '_') {
      readchr();
    }
    /* if it's not a literal token */
    if (tokpos == 0) {
      while (nextc == '<' || nextc == '=' || nextc == '>' || nextc == '!' || nextc == '&' || nextc == '|') {
        readchr();
      }
    }
    /* if it's not special chars that looks like an operator */
    if (tokpos == 0) {
      /* try strings and chars inside quotes */
      if (nextc == '\'' || nextc == '"') {
        char c = nextc;
        readchr();
        while (nextc != c) {
          readchr();
        }
        readchr();
      } else if (nextc == '/') { // skip comments
        readchr();
        if (nextc == '*') {      // support comments of the form '/**/'
          nextc = fgetc(f);
          if ('\n'==nextc) {linenum++;}
          while (nextc != '/') {
            while (nextc != '*') {
              nextc = fgetc(f);
              if ('\n'==nextc) {linenum++;}
            }
            nextc = fgetc(f);
            if ('\n'==nextc) {linenum++;}
          }
          nextc = fgetc(f);
          if ('\n'==nextc) {linenum++;}
          continue;
        } else if (nextc == '/') { // support comments of the form '//'
          while (nextc != '\n') {
            nextc = fgetc(f);
            if ('\n'==nextc) {linenum++;}
          }
          nextc = fgetc(f);
          if ('\n'==nextc) {linenum++;}
          continue;
        }
      } else if (nextc != EOF) {
        /* otherwise it looks like a single-char symbol, like '+', '-' etc */
        readchr();
      }
    }
    break;
  }
  tok[tokpos] = '\0';
  if (_debug)  {
    printf("TOKEN: %s\n",tok);
  }
}

/* check if the current token machtes the string */
int peek(char *s) {
  return (strcmp(tok, s) == 0);
}

/* read the next token if the current token machtes the string */
int accept(char *s) {
  if (peek(s)) {
    readtok();
    return 1;
  }
  return 0;
}

/* throw fatal error if the current token doesn't match the string */
void expect(int srclinenum, char *s) {
  if (accept(s) == 0) {
    if (_debug) {
      error("[line %d ; srcline %d] Error: expected '%s', but found: %s\n", linenum, srclinenum, s, tok);
    } else {
      error("[line %d] Error: expected '%s', but found: %s\n", linenum, s, tok);
    }
  }
}

static struct sym *sym_find(char *s) {
  int i;
  struct sym *symbol = NULL;
  
  for (i = 0; i < sympos; i++) {
    if (strcmp(sym[i].name, s) == 0) {
      symbol = &sym[i];
    }
  }
  return symbol;
}

// ctx:  context
// name: symbol name
// type: symbol type
//       L - local symbol
//       F - function
//       G - global
//       U - undefined
// addr: symbol address
static struct sym *sym_declare(char *ctx, char *name, char type, int addr) {
  char sName[MAXTOKSZ];
  int  ii;

  strcpy(sName,ctx);
  strcat(sName,"_");
  strcat(sName,name);

  for (ii=0; ii<sympos; ii++) {
    if (0==strcmp(sym[ii].name,sName)) {
      error("[line %d] variable redefined '%s'\n",linenum,name);
    }
  }

  strncpy(sym[sympos].name, sName, MAXTOKSZ);
  sym[sympos].addr = addr;
  sym[sympos].type = type;
  sympos++;
  if (sympos > MAXSYMBOLS) {
    error("[line %d] Too many symbols\n",linenum);
  }
  return &sym[sympos-1];
}

/*
 * BACKEND
 */
#define MAXCODESZ 4096
static char code[MAXCODESZ];
static int codepos = 0;

static void emit(void *buf, size_t len) {
  memcpy(code + codepos, buf, len);
  codepos += len;
}

#define TYPE_NUM     0
#define TYPE_CHARVAR 1
#define TYPE_INTVAR  2

#ifndef GEN
#error "A code generator (backend) must be provided (use -DGEN=...)"
#else
#include GEN
#endif

/*
 * PARSER AND COMPILER
 */

static int expr();

// read type name:
//   int, char and pointers (int* char*) are supported
//   void is skipped (as if nothing was there)
//   NOTE: void * is not supported
static int typename() {
  if (peek("int") || peek("char") ) {
    readtok();
    while (accept("*"));
    return 1;
  }
  if (peek("void") ) {  // skip 'void' token
    readtok();
  }
  return 0;
}

static int parse_immediate_value() {
  if ( tok[0]=='0' ) {
    if (tok[1]==0 ) return 0;
    if (strlen(tok)<3) {
      error("[line %d] Invalid symbol: %s\n", linenum, tok);
    }
    if ( (tok[1]=='x') || (tok[1]=='X') ) {
      return strtol(tok+2, NULL, 16);
    } else
      if ( (tok[1]=='o') || (tok[1]=='o') ) {
        return strtol(tok+2, NULL, 8);
      } else
        if ( (tok[1]=='b') || (tok[1]=='b') ) {
          return strtol(tok+2, NULL, 2);
        } else {
          error("[line %d] Invalid symbol: %s\n", linenum, tok);
        }
  } else {
    return strtol(tok, NULL, 10);
  }
}

static int prim_expr() {
  int type = TYPE_NUM;
  if (isdigit(tok[0])) {
    int n = parse_immediate_value();
    gen_const(n);
  } else if (isalpha(tok[0])) {
    char symName[MAXTOKSZ];
    struct sym *s;

    strcpy(symName,context);
    strcat(symName,"_");
    strcat(symName,tok);
    s = sym_find(symName);  // find symbol in local context..
    if (s==NULL) {
      strcpy(symName,"_");
      strcat(symName,tok);
      s = sym_find(symName);  // find symbol in global context..
      if (s == NULL) {
        // symbol not found... this is an error...
        error("[line %d] Undeclared symbol: %s\n", linenum,tok);
      }
    }
    printf("SYM: %s\n",symName);
    if (s->type == 'L') {
      // Local Symbol
      gen_stack_addr(stack_pos - s->addr - 1);
    } else {
      // Other Symbols (Global)
      gen_sym_addr(s);
    }
    type = TYPE_INTVAR;
  } else if (accept("(")) {
    type = expr();
    expect(__LINE__,")");
  } else if (tok[0] == '"') {
    int i, j;
    i = 0; j = 1;
    while (tok[j] != '"') {
      if (tok[j] == '\\' && tok[j+1] == 'x') {
        char s[3] = {tok[j+2], tok[j+3], 0};
        uint8_t n = strtol(s, NULL, 16);
        tok[i++] = n;
        j += 4;
      } else {
        tok[i++] = tok[j++];
      }
    }
    tok[i] = 0;
    if (i % 2 == 0) {
      i++;
      tok[i] = 0;
    }
    gen_array(tok, i);
    type = TYPE_NUM;
  } else {
    error("[line %d] Unexpected primary expression: %s\n", linenum,tok);
  }
  readtok();
  return type;
}

static int binary(int type, int (*f)(), char *buf, size_t len) {
  if (type != TYPE_NUM) {
    gen_unref(type);
  }
  gen_push();
  type = f();
  if (type != TYPE_NUM) {
    gen_unref(type);
  }
  emit(buf, len);
  stack_pos = stack_pos - 1; /* assume that buffer contains a "pop" */
  return TYPE_NUM;
}

static int postfix_expr() {
  int type = prim_expr();

  if (type == TYPE_INTVAR && accept("[")) {
    binary(type, expr, GEN_ADD, GEN_ADDSZ);
    expect(__LINE__,"]");
    type = TYPE_CHARVAR;
  } else if (accept("(")) {
    int prev_stack_pos = stack_pos;
    gen_push(); /* store function address */
    int call_addr = stack_pos - 1;
    if (accept(")") == 0) {
      expr();
      gen_push();
      while (accept(",")) {
        expr();
        gen_push();
      }
      expect(__LINE__,")");
    }
    type = TYPE_NUM;
    gen_stack_addr(stack_pos - call_addr - 1);
    gen_unref(TYPE_INTVAR);
    gen_call();
    if (currFunction) {
      gen_call_cleanup(currFunction->nParams);
    } else {
        error("[line %d] Error: unexpected function exit\n",linenum);
    }
    /* remove function address and args */
    gen_pop(stack_pos - prev_stack_pos);
    stack_pos = prev_stack_pos;
  }
  return type;
}

static int add_expr() {
  int type = postfix_expr();
  while (peek("+") || peek("-")) {
    if (accept("+")) {
      type = binary(type, postfix_expr, GEN_ADD, GEN_ADDSZ);
    } else if (accept("-")) {
      type = binary(type, postfix_expr, GEN_SUB, GEN_SUBSZ);
    }
  }
  return type;
}

static int shift_expr() {
  int type = add_expr();
  while (peek("<<") || peek(">>")) {
    if (accept("<<")) {
      type = binary(type, add_expr, GEN_SHL, GEN_SHLSZ);
    } else if (accept(">>")) {
      type = binary(type, add_expr, GEN_SHR, GEN_SHRSZ);
    }
  }
  return type;
}

static int rel_expr() {
  int type = shift_expr();
  while (peek("<")) {
    if (accept("<")) {
      type = binary(type, shift_expr, GEN_LESS, GEN_LESSSZ);
    }
  }
  return type;
}

static int eq_expr() {
  int type = rel_expr();
  while (peek("==") || peek("!=")) {
    if (accept("==")) {
      type = binary(type, rel_expr, GEN_EQ, GEN_EQSZ);
    } else if (accept("!=")) {
      type = binary(type, rel_expr, GEN_NEQ, GEN_NEQSZ);
    }
  }
  return type;
}

static int bitwise_expr() {
  int type = eq_expr();

  while (peek("|") || peek("&") || peek("^") || peek("/") || peek("*") || peek("%") ) {
    if (accept("|")) {        // expression '|'
      type = binary(type, eq_expr, GEN_OR, GEN_ORSZ);
    } else if (accept("&")) { // expression '&'
      type = binary(type, eq_expr, GEN_AND, GEN_ANDSZ);
    } else if (accept("^")) { // expression '^'
      type = binary(type, eq_expr, GEN_XOR, GEN_XORSZ);
    } else if (accept("/")) { // expression '/'
      type = binary(type, eq_expr, GEN_DIV, GEN_DIVSZ);
    } else if (accept("*")) { // expression '*'
      type = binary(type, eq_expr, GEN_MUL, GEN_MULSZ);
    } else if (accept("%")) { // expression '%'
      type = binary(type, eq_expr, GEN_MOD, GEN_MODSZ);
    }
  }
  return type;
}

static int expr() {
  int type = bitwise_expr();
  if (type != TYPE_NUM) {
    if (accept("=")) {
      printf("HERE 1=\n");
      gen_push(); expr(); 
      if (type == TYPE_INTVAR) {
        emit(GEN_ASSIGN, GEN_ASSIGNSZ);
      } else {
        emit(GEN_ASSIGN8, GEN_ASSIGN8SZ);
      }
      stack_pos = stack_pos - 1; // assume ASSIGN contains pop
      type = TYPE_NUM;
    } else {
      gen_unref(type);
    }
  }
  return type;
}

static void statement() {
  lastIsReturn = 0;
  if (accept("{")) {
    int prev_stack_pos = stack_pos;
    while (accept("}") == 0) {
      statement();
    }
    gen_pop(stack_pos-prev_stack_pos);
    stack_pos = prev_stack_pos;
    strcpy(context,"");
    genPreamble = 0;
    numPreambleVars = 0;
    return;
  }
  if (typename()) {
    struct sym *var = sym_declare(context,tok, 'L', stack_pos);
    printf("GENERATE_VAR %s_%s\n",context,tok);
    readtok();
    if (accept("=")) {
      printf("HERE 2=\n");
      expr();
    }
    numPreambleVars++;
    // gen_push(); // make room for new local variable
    var->addr = stack_pos-1;
    expect(__LINE__,";");
    return;
  }
  // if we arrive here, we can generate the preamble
  if (genPreamble) {
    genPreamble = 0;
    printf("Generate Preamble (nvars = %d)\n",numPreambleVars);
    gen_preamble(numPreambleVars);
  }

  if (accept("if")) {
    expect(__LINE__,"(");
    expr();
    emit(GEN_JZ, GEN_JZSZ);
    int p1 = codepos;
    expect(__LINE__,")");
    int prev_stack_pos = stack_pos;
    statement();
    emit(GEN_JMP, GEN_JMPSZ);
    int p2 = codepos;
    gen_patch(code + p1, codepos);
    if (accept("else")) {
      stack_pos = prev_stack_pos;
      statement();
    }
    stack_pos = prev_stack_pos;
    gen_patch(code + p2, codepos);
    return;
  }
  if (accept("while")) {
    expect(__LINE__,"(");
    int p1 = codepos;
    gen_loop_start();
    expr();
    emit(GEN_JZ, GEN_JZSZ);
    int p2 = codepos;
    expect(__LINE__,")");
    statement();
    emit(GEN_JMP, GEN_JMPSZ);
    gen_patch(code + codepos, p1);
    gen_patch(code + p2, codepos);
    return;
  }
  if (accept("return")) {
    if (peek(";") == 0) {
      expr();
    }
    expect(__LINE__,";");
    gen_pop(stack_pos); // remove all locals from stack (except return address)
    lastIsReturn = 1;
    gen_ret(numPreambleVars);
    return;
  }
  // we should process an expression...
  expr();
  expect(__LINE__,";");
}

static void compile() {
  while (tok[0] != 0) { // until EOF
    if (typename() == 0) {
      error("[line %d] Error: type name expected\n",linenum);
    }
    struct sym *var = sym_declare(context,tok, 'U', 0);
    readtok();
    if (accept(";")) {
      if (1==flagScanGlobalVars) {
        var->type = 'G';
        numGlobalVars++;
        gen_sym(var);
        continue;
      } else {
        error("[line %d] Error: unexpected global variable declaration\n",linenum);
      }
    }
    if (1==flagScanGlobalVars) {
      gen_start(numGlobalVars);
      flagScanGlobalVars = 0;
    }
    expect(__LINE__,"(");
    int argc = 0;
    for (;;) {
      argc++;
      if (typename() == 0) {
        break;
      }
      printf("GEN_PARM_VAR %s_%s\n",var->name,tok);
      sym_declare(var->name,tok, 'L', -argc-1);
      readtok();
      if (peek(")")) {
        break;
      }
      expect(__LINE__,",");
    }
    expect(__LINE__,")");
    if (accept(";") == 0) {
      if (strcmp(context,"")!=0 ) {
        error("");
      }
      stack_pos = 0;
      var->addr = codepos;
      var->type = 'F';
      var->nParams = argc;
      gen_sym(var);
      printf("FUNCTION: %s with %d params\n",var->name, argc);
      strcpy(context,var->name);
      genPreamble = 1;
      numPreambleVars = 0;
      currFunction = var;
      statement(); // function body
      if (!lastIsReturn) {
        gen_ret(numPreambleVars);   // issue a ret if user forgets to put 'return'
      }
    }
  }
}

int main(int argc, char *argv[]) {
  int ii;

  strcpy(context,"");
  if (argc>1) {
    _debug = 1;
  } else {
    _debug = 0;
  }
  
  f = stdin;
  // prefetch first char and first token
  nextc = fgetc(f);
  if ('\n'==nextc) {linenum++;}
  readtok();
  compile();
  //_load_immediate(0xffaaba94); printf("\n");
  //_load_immediate(0x000aba94); printf("\n");
  //_load_immediate(0xcd0);      printf("\n");

  if (_debug) {
    printf("\n");
    printf("****************\n");
    printf("* Symbol Table *\n");
    printf("****************\n");
    printf("NAME\t\tADDR\t\tTYPE\n");
    for (ii=0; ii<sympos; ii++) {
      printf("%s\t\t0x%08x\t\t%c\n",sym[ii].name, sym[ii].addr, sym[ii].type);
    }
    printf("\n");
  }
  printf("**********\n");
  printf("* Output *\n");
  printf("**********\n");
  printf("\n");
  gen_finish();
	return 0;
}

