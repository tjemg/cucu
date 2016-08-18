#define emits(s) emit(s, strlen(s))
static void error(const char *fmt, ...);

#define TYPE_NUM_SIZE 2
static int mem_pos = 0;

#define GEN_ADD   "pop B  \nA:=B+A \n"
#define GEN_ADDSZ strlen(GEN_ADD)

#define GEN_SUB   "pop B  \nA:=B-A \n"
#define GEN_SUBSZ strlen(GEN_SUB)

#define GEN_SHL   "pop B  \nA:=B<<A\n"
#define GEN_SHLSZ strlen(GEN_SHL)

#define GEN_SHR   "pop B  \nA:=B>>A\n"
#define GEN_SHRSZ strlen(GEN_SHR)

#define GEN_LESS  "pop B  \nA:=B<A \n"
#define GEN_LESSSZ strlen(GEN_LESS)

#define GEN_EQ "pop B  \nA:=B==A\n"
#define GEN_EQSZ strlen(GEN_EQ)
#define GEN_NEQ  "pop B  \nA:=B!=A\n"
#define GEN_NEQSZ strlen(GEN_NEQ)

#define GEN_OR "pop B  \nA:=B|A \n"
#define GEN_ORSZ strlen(GEN_OR)
#define GEN_AND  "pop B  \nA:=B&A \n"
#define GEN_ANDSZ strlen(GEN_AND)
#define GEN_XOR "pop B  \nA:=B^A \n"
#define GEN_XORSZ strlen(GEN_XOR)
#define GEN_DIV "pop B  \nA:=B/A \n"
#define GEN_DIVSZ strlen(GEN_DIV)
#define GEN_MUL "pop B  \nA:=B*A \n"
#define GEN_MULSZ strlen(GEN_MUL)
#define GEN_MOD "pop B  \nA:=B%A \n"
#define GEN_MODSZ strlen(GEN_MOD)

#define GEN_ASSIGN "pop B  \nM[B]:=A\n"
#define GEN_ASSIGNSZ strlen(GEN_ASSIGN)
#define GEN_ASSIGN8 "pop B  \nm[B]:=A\n"
#define GEN_ASSIGN8SZ strlen(GEN_ASSIGN8)

#define GEN_JMP "jmp....\n"
#define GEN_JMPSZ strlen(GEN_JMP)

#define GEN_JZ "jmz....\n"
#define GEN_JZSZ strlen(GEN_JZ)


struct _imm_struct {
  int nImm;
  int v[5];
};

int fixme_offset = 0;
int addrCnt = 0;

static struct _imm_struct _load_immediate( int32_t v );

static void gen_start(int nGlobalVars) {
  char buf[100];
  sprintf(buf,"GLOBALS %d\n", nGlobalVars);
  strcat(buf,"---\n");
  fixme_offset = strlen(buf) + 1 + 3;
  strcat(buf,"JMP xxxx\n");
  strcat(buf,"---\n");
  emits(buf);
}

static void gen_finish() {
  struct sym *funcmain = sym_find("_main");
  char s[32];
  if (NULL==funcmain) {
    error("ERROR: could not find main function\n");
  }
  sprintf(s, "%04x", funcmain->addr);
  memcpy(code+fixme_offset, s, 4);
  printf("%s", code);
}

// generate function pre-amble
// nVars: number of variables to save in the stack frame
static void gen_preamble(int nVars) {
  char buf[100];
  sprintf(buf,"PREAMB %d\n",nVars);
  emits(buf);
  stack_pos = stack_pos + 1;
}

static void gen_postamble(int nVars) {
  char buf[100];
  sprintf(buf,"POSTAMB %d\n",nVars);
  emits(buf);
  stack_pos = stack_pos + 1;
}

static void gen_call_cleanup(int nVars) {
  char buf[100];
  sprintf(buf,"DO CLEAN %d\n",nVars);
  emits(buf);
}

static void gen_ret(int nVars) {
  gen_postamble(nVars);
  emits("ret    \n");
  stack_pos = stack_pos - 1;
}

static void gen_const(int n) {
  char s[32];
  sprintf(s, "A:=%04x\n", n);
  emits(s);
}

static void gen_sym(struct sym *sym) {
  if (sym->type == 'G') {
    sym->addr = mem_pos;
    mem_pos = mem_pos + TYPE_NUM_SIZE;
  }
}

static void gen_loop_start() {}

static void gen_sym_addr(struct sym *sym) {
  gen_const(sym->addr);
}

static void gen_push() {
  emits("push A \n");
  stack_pos = stack_pos + 1;
}

static void gen_pop(int n) {
  char s[32];
  if (n > 0) {
    sprintf(s, "pop%04x\n", n);
    emits(s);
    stack_pos = stack_pos - n;
  }
}

static void gen_stack_addr(int addr) {
  char s[32];
  sprintf(s, "sp@%04x\n", addr);
  emits(s);
}

static void gen_unref(int type) {
  if (type == TYPE_INTVAR) {
    emits("A:=M[A]\n");
  } else if (type == TYPE_CHARVAR) {
    emits("A:=m[A]\n");
  }
}

static void gen_call() {
  emits("call A \n");
}

static void gen_array(char *array, int size) {
  int i = size;
  char *tok = array;
  /* put token on stack */
  for (; i >= 0; i-=2) {
    gen_const((tok[i] << 8 | tok[i-1]));
    gen_push();
  }
  /* put token address on stack */
  gen_stack_addr(0);
}


static void gen_patch(uint8_t *op, int value) {
  char s[32];
  sprintf(s, "%04x", value);
  memcpy(op-5, s, 4);
}

static struct _imm_struct _load_immediate( int32_t v ) {
  int      flag  = (v<0) ? 1 : 0;
  uint32_t tmp   = v;
  int      nOnes = 0;
  int      nImm  = 0;
  int      ii;
  int      vals[5];
  struct _imm_struct _ret;

  if (flag) {
    for (ii=0; ii<32; ii++) {
      if ( 0x80000000 == (tmp & 0x80000000) ) {
        nOnes++;
        tmp <<= 1;
      } else {
        break;
      }
    }
    if ( (nOnes>=1 ) && (nOnes<=4)  ) nImm = 5;
    if ( (nOnes>=5 ) && (nOnes<=11) ) nImm = 4;
    if ( (nOnes>=12) && (nOnes<=18) ) nImm = 3;
    if ( (nOnes>=19) && (nOnes<=25) ) nImm = 2;
    if ( (nOnes>=26) && (nOnes<=32) ) nImm = 1;
  } else {
    while (1) {
      if (0==tmp) break;
      tmp >>= 7;
      nImm++;
    }
    if (0==nImm) nImm++;
  }
  tmp = v;
  for (ii=0; ii<nImm; ii++) {
    vals[ii] = tmp & 0x7f;
    tmp >>= 7;
  }

  _ret.nImm = nImm;
  for (ii=0; ii<nImm; ii++) {
    _ret.v[ii] = 0x80 | vals[nImm-ii-1];
    //printf("0x%02x IM %d\n", 0x80 | vals[nImm-ii-1], vals[nImm-ii-1]);
  }
}

