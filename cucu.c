#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

/* print fatal error message and exit */
static void error(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(1);
}

/*
 * LEXER
 */
#define MAXTOKSZ 256
static FILE *f;            /* input source file */
static char tok[MAXTOKSZ]; /* current token */
static int tokpos;         /* offset inside the current token */
static int nextc;          /* next char to be pushed into token */
static int linenum = 1;
static int _debug = 0;

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

/*
 * SYMBOLS
 */
#define MAXSYMBOLS 4096
static struct sym {
	char type;
	int  addr;
	char name[MAXTOKSZ];
} sym[MAXSYMBOLS];
static int sympos = 0;

int stack_pos = 0;

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

static struct sym *sym_declare(char *name, char type, int addr) {
	strncpy(sym[sympos].name, name, MAXTOKSZ);
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

#define TYPE_NUM  0
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
		struct sym *s = sym_find(tok);
		if (s == NULL) {
			error("[line %d] Undeclared symbol: %s\n", linenum,tok);
		}
		if (s->type == 'L') {
			gen_stack_addr(stack_pos - s->addr - 1);
		} else {
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
  if (accept("{")) {
    int prev_stack_pos = stack_pos;
    while (accept("}") == 0) {
      statement();
    }
    gen_pop(stack_pos-prev_stack_pos);
    stack_pos = prev_stack_pos;
  } else if (typename()) {
    struct sym *var = sym_declare(tok, 'L', stack_pos);
    readtok();
    if (accept("=")) {
      expr();
    }
    gen_push(); /* make room for new local variable */
    var->addr = stack_pos-1;
    expect(__LINE__,";");
  } else if (accept("if")) {
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
  } else if (accept("while")) {
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
  } else if (accept("return")) {
    if (peek(";") == 0) {
      expr();
    }
    expect(__LINE__,";");
    gen_pop(stack_pos); /* remove all locals from stack (except return address) */
    gen_ret();
  } else {
    expr();
    expect(__LINE__,";");
  }
}

static void compile() {
	while (tok[0] != 0) { // until EOF
		if (typename() == 0) {
			error("[line %d] Error: type name expected\n",linenum);
		}
		struct sym *var = sym_declare(tok, 'U', 0);
		readtok();
		if (accept(";")) {
			var->type = 'G';
			gen_sym(var);
			continue;
		}
		expect(__LINE__,"(");
		int argc = 0;
		for (;;) {
			argc++;
			if (typename() == 0) {
				break;
			}
			sym_declare(tok, 'L', -argc-1);
			readtok();
			if (peek(")")) {
				break;
			}
			expect(__LINE__,",");
		}
		expect(__LINE__,")");
		if (accept(";") == 0) {
			stack_pos = 0;
			var->addr = codepos;
			var->type = 'F';
			gen_sym(var);
			statement(); // function body
			gen_ret();   // FIXME: another ret if user forgets to put 'return'
		}
	}
}

int main(int argc, char *argv[]) {
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
	gen_start();
	compile();
	gen_finish();
  //_load_immediate(0xffaaba94); printf("\n");
  //_load_immediate(0x000aba94); printf("\n");
  //_load_immediate(0xcd0);      printf("\n");
	return 0;
}

