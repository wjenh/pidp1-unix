/* bn.c - B compiler for PDP-7 Unix, writing PDP-1 instructions

   The restorers' b.c, (C) 2016 Robert Swierczek, GPL3, with its threaded code
   generator replaced by one that writes PDP-1 instructions for 1970's as.
   Its output is assembled after bop.s and brt.s, by as bop.s brt.s x.s.
   GPL3, as b.c is.
   It is still in the subset of C that B shares, so that the generator can be
   carried into b.b.
   The few places where the host's arithmetic is not the PDP-1's are marked
   "host".

   Every value is an 18-bit word, ones' complement, kept here in an int as its bits
   (0 to 0777777).
   An expression's value is described on a stack, dk dv dm dc, until an
   instruction needs it.
     dk, the kind, is 1 a number (dv the bits), 2 an external (dv its symtab
         index, .name), 3 an internal label (1f+dv), 4 a fixed word of the
         function (4f+dv, its parameters, autos and temporaries), 5 the value
         in AC, 7 a word of the function's literal pool (3f+dv).
     dm, the mode, is -1 the address itself (a number is its own value), 0 the
         word there, 1 the word the word there points to.
     dc, for a comparison, is its operator, with the left side in AC and the
         right side the word dk dv dm.
   acd is the entry whose value is in AC, -1 for none.

   To compile hello.b:
      gcc -std=gnu89 -w bn.c -o bn
      ./bn hello.b hello.s
      perl as7 -o h.a7 bop.s brt.s hello.s

   23-Nov-2026, Claude: comments cleaned for the release.
*/

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <fcntl.h>

/* runtime */
int fin;
int fout = 1;

void pexpr();
void error(int code);
void stmt();
void blkend();
void stmtlist();
void getcc();
void extdef();

xread() {
  char buf[1];
  if (read(fin, buf, 1) <= 0)
    return 4;
  return buf[0];
}

xwrite(c) {
  char buf[2];
  if (c & 0xff00) {
    buf[0] = (c >> 8) & 0xff;
    buf[1] = c & 0xff;
    write(fout, buf, 2);
  } else {
    buf[0] = c & 0xff;
    write(fout, buf, 1);
  }
}

xflush() {
}
#define eof xeof
#define read xread
#define write xwrite
#define flush xflush

main(int argc, char **argv) {
  extern symtab[], eof, *ns, nerror;
  extern fin, fout;

  if (argc > 1) {
    if (argc > 2) {
      if ((fout = creat(argv[2], 0777))<0) {
        error('fo');
        return(1);
      }
    }
    if ((fin = open(argv[1],0))<0) {
      error('fi');
      return(1);
    }
  }

  while (!eof) {
    ns = symtab + 51;
    extdef();
    blkend();
  }
  write('be');  /* the program's end, where brt.s's stack starts */
  write('nd');
  write('=.');
  write('\n');
  return(nerror!=0);
}

int *lookup() {
  extern symtab[], symbuf[], eof, *ns;
  auto *np, *sp, *rp;

  rp = symtab;
  while (rp < ns) {
    np = rp + 2;
    sp = symbuf;
    while (*np==*sp) {
      if (!*np)
        return(rp);
      np = np+1;
      sp = sp+1;
    }
    while (*np)
      np = np+1;
    rp = np+1;
  }
  sp = symbuf;
  if (ns >= symtab + 290) {
    error('sf');
    eof = 1;
    return(rp);
  }
  *ns = 0;
  ns[1] = 0;
  ns = ns+2;
  while (*ns = *sp) {
    ns = ns+1;
    sp = sp+1;
  }
  ns = ns+1;
  return(rp);
}

symbol() {
  extern symbuf[], ctab[], peeksym, peekc, eof, line, *csym, cval;
  auto b, c, ct, *sp;

  if (peeksym>=0) {
    c = peeksym;
    peeksym = -1;
    return(c);
  }
  if (peekc) {
    c = peekc;
    peekc = 0;
  } else {
    if (eof)
      return(0);
    c = read();
  }
loop:
  ct = ctab[c];
 
  if (ct==0) { /* eof */
    eof = 1;
    return(0);
  }
 
  if (ct==126) { /* white space */
    if (c=='\n')
      line = line+1;
    c = read();
    goto loop;
  }

  if (c=='=')
    return(subseq('=',80,60));

  if (c=='<')
    return(subseq('=',63,62));

  if (c=='>')
    return(subseq('=',65,64));

  if (c=='!')
    return(subseq('=',34,61));

  if (c=='$') {
    if (subseq('(',0,1))
      return(2);
    if (subseq(')',0,1))
      return(3);
  }
  if (c=='/') {
    if (subseq('*',1,0))
      return(43);
com:
    c = read();
com1:
    if (c==4) {
      eof = 1;
      error('*/'); /* eof */
      return(0);
    }
    if (c=='\n')
      line = line+1;
    if (c!='*')
      goto com;
    c = read();
    if (c!='/')
      goto com1;
    c = read();
    goto loop;
  }
  if (ct==124) { /* number */
    cval = 0;
    if (c=='0')
      b = 8;
    else
      b = 10;
    while(c >= '0' & c <= '9') {
      cval = cval*b + c -'0';
      c = read();
    }
    peekc = c;
    return(21);
  }
  if (c=='\'') { /* ' */
    getcc();
    return(21);
  }
  if (ct==123) { /* letter */
    sp = symbuf;
    while(ct==123 | ct==124) {
      if (sp<symbuf+9) {
        *sp = c;
        sp = sp+1;
      }
      ct = ctab[c = read()];
    }
    *sp = 0;
    peekc = c;
    csym = lookup();
    if (csym[0]==1) {
      cval = csym[1];
      return(19); /* keyword */
    }
    return(20); /* name */
  }
  if (ct==127) { /* unknown */
    error('sy');
    c = read();
    goto loop;
  }
  return(ctab[c]);
}

subseq(c,a,b) {
  extern peekc;

  if (!peekc)
    peekc = read();
  if (peekc != c)
    return(a);
  peekc = 0;
  return(b);
}

void getcc() {
  extern cval;
  auto c;

  cval = 0;
  if ((c = mapch('\'')) < 0)
    return;
  cval = c;
  if ((c = mapch('\'')) < 0)
    return;
  cval = cval * 512 + c;
  if (mapch('\'') >= 0)
    error('cc');
}

getstr() {
  auto i, c, d;

  i = 1;
loop:
  if ((c = mapch('"')) < 0) {
    number(2048);
    write('\n');
    return(i);
  }
  if ((d = mapch('"')) < 0) {
    number(c*512+4);
    write('\n');
    return(i);
  }
  number(c*512+d);
  write('\n');
  i = i+1;
  goto loop;
}

mapch(c) {
  extern peekc;
  auto a;

  if ((a=read())==c)
    return(-1);
 
  if (a=='\n' | a==0 | a==4) {
    error('cc');
    peekc = a;
    return(-1);
  }

  if (a=='*') {
    a=read();

    if (a=='0')
      return(0);

    if (a=='e')
      return(4);

    if (a=='(')
      return('{');

    if (a==')')
      return('}');

    if (a=='t')
      return('\t');

    if (a=='r')
      return('\r');

    if (a=='n')
      return('\n');
  }
  return(a);
}

void expr(lev) {
  extern peeksym, *csym, cval, isn, symtab[], dt, acd;
  auto o, n;

  o = symbol();

  if (o==21) { /* number */
case21:
    push(1, cval & 0777777, -1);
    goto loop;
  }

  if (o==122) { /* string */ /* its address in AC, the words jumped over */
    needac(-1);
    write('la');
    write('w ');
    write('.+');
    write('2\n');
    write('jm');
    write('p ');
    write('2f');
    write('\n');
    getstr();
    write('2:');
    write('\n');
    push(5, 0, 0);
    acd = dt-1;
    goto loop;
  }

  if (o==20) { /* name */
    if (*csym==0) { /* not seen */
      if ((peeksym=symbol())==6) { /* ( */
        *csym = 6; /* extrn */
      } else {
        *csym = 2; /* internal */
        csym[1] = isn;
        isn = isn+1;
      }
    }
    if (*csym==5) /* auto */
      push(4, csym[1], 0);
    else if (*csym==6) /* extrn */
      push(2, csym-symtab, 0);
    else /* internal */
      push(3, csym[1], 0);
    goto loop;
  }

  if (o==34) { /* ! */
    expr(1);
    unot();
    goto loop;
  }

  if (o==41) { /* - */
    peeksym = symbol();
    if (peeksym==21) { /* number */
      peeksym = -1;
      cval = neg(cval & 0777777);
      goto case21;
    }
    expr(1);
    umin();
    goto loop;
  }

  if (o==47) { /* & */
    expr(1);
    uadr();
    goto loop;
  }

  if (o==42) { /* * */
    expr(1);
    uind();
    goto loop;
  }

  if (o==6) { /* ( */
    peeksym = o;
    pexpr();
    goto loop;
  }
  error('ex');
  push(1, 0, -1);

loop:
  o = symbol();

  if (lev>=14 & o==80) { /* = */
    expr(14);
    binop(o);
    goto loop;
  }
  if (lev>=10 & o==48) { /* | ^ */
    expr(9);
    binop(o);
    goto loop;
  }
  if (lev>=8 & o==47) { /* & */
    expr(7);
    binop(o);
    goto loop;
  }
  if (lev>=7 & o>=60 & o<=61) { /* == != */
    expr(6);
    binop(o);
    goto loop;
  }
  if (lev>=6 & o>=62 & o<=65) { /* <= < >= > */
    expr(5);
    binop(o);
    goto loop;
  }
  if (lev>=4 & o>=40 & o<=41) { /* + - */
    expr(3);
    binop(o);
    goto loop;
  }
  if (lev>=3 & o>=42 & o<=43) { /* * / */
    expr(2);
    binop(o);
    goto loop;
  }
  if (lev>=3 & o==44) { /* % */
    expr(2);
    binop(o);
    goto loop;
  }
  if (o==4) { /* [ */
    expr(15);
    if (symbol() != 5)
      error('[]');
    binop(40);
    uind();
    goto loop;
  }
  if (o==6) { /* ( */ /* the arguments pushed, jsp f i, their count */
    callee();
    n = 0;
    o = symbol();
    if (o!=7) { /* ) */
      peeksym = o;
      while (o!=7) {
        expr(15);
        load(dt-1);
        write('da');
        write('c ');
        write('bs');
        write('p ');
        write('i\n');
        write('id');
        write('x ');
        write('bs');
        write('p\n');
        dt = dt-1;
        acd = -1;
        n = n+1;
        o = symbol();
        if (o!=7 & o!=9) { /* ) , */
          error('ex');
          return;
        }
      }
    }
    needac(-1);
    write('js');
    write('p ');
    jopnd(dt-1);
    number(n);
    write('\n');
    toac(dt-1);
    goto loop;
  }

  peeksym = o;
}

void pexpr() {
  if (symbol()==6) { /* ( */
    expr(15);
    if (symbol()==7) /* ) */
      return;
  }
  error('()');
}

void declare(kw) {
  extern *csym, cval, nauto, nfr;
  auto o;

  while((o=symbol())==20) { /* name */
    if (kw==6) { /* extrn */
      *csym = 6;
      o = symbol();
    } else { /* auto/param */
      *csym = 5; /* auto */
      csym[1] = nauto;
      o = symbol();
      if (kw==5 & o==21) { /* auto & number */ /* the vector in the frame */
        write('la');
        write('w ');
        oct(nfr);
        write('\n');
        write('ad');
        write('d ');
        write('bf');
        write('p\n');
        write('da');
        write('c ');
        write('4f');
        write('+');
        number(nauto);
        write('\n');
        nfr = nfr + cval;
        o = symbol();
      }
      nauto = nauto+1;
    }
    if (o!=9) /* , */
      goto done;
  }
done:
  if (o==1 & kw!=8 | o==7 & kw==8) /* auto/extrn ;  param ')' */
    return;
syntax:
  error('[]'); /* declaration syntax */
}

void extdef() {
  extern peeksym, *csym, cval, nauto, nfr, mtmp, nlit, lits[];
  auto o, c;

  o = symbol();
  if (o==0 | o==1) /* eof ; */
    return;

  if (o!=20) /* name */
    goto syntax;

  csym[0] = 6; /* extrn */
  write('.');
  name(csym + 2);
  write(':');
  o=symbol();

  if (o==2 | o==6) { /* $( ( */
    write('.+');
    write('1\n');
    write('jd');
    write('a ');
    write('be');
    write('nt');
    write('\n');
    write('4f');
    write('\n');
    nauto = 2;
    nfr = 0;
    mtmp = 0;
    nlit = 0;
    sreset();
    if (o==6) { /* ( */
      declare(8); /* param */
      if ((o=symbol())!=2) /* $( */
        goto syntax;
    }
    while((o=symbol())==19 & cval<10) /* auto extrn */
      declare(cval);
    peeksym = o;
    stmtlist();
    write('jm');
    write('p ');
    write('br');
    write('et');
    write('\n');
    /* the literals; the descriptor, N and F; the N fixed words */
    if (nlit) {
      write('3:');
      c = 0;
      while (c < nlit) {
        oct(lits[c]);
        write('\n');
        c = c+1;
      }
    }
    c = nauto - 2 + mtmp;
    write('4:');
    number(c);
    write('\n');
    number(nfr);
    write('\n');
    if (c) {
      write('.=');
      write('.+');
      number(c);
      write('\n');
    }
    return;
  }

  if (o==41) { /* - */
    if (symbol()!=21) /* number */
      goto syntax;
    oct(neg(cval & 0777777));
    write('\n');
    return;
  }

  if (o==21) { /* number */
    oct(cval & 0777777);
    write('\n');
    return;
  }

  if (o==1) { /* ; */
    write('0\n');
    return;
  }

  if (o==4) { /* [ */
    c = 0;
    if ((o=symbol())==21) { /* number */
      c = cval;
      o = symbol();
    }
    if (o!=5) /* ] */
      goto syntax;
    write('.+');
    write('1\n');
    if ((o=symbol())==1) /* ; */
      goto done;
    while (o==21 | o==41) { /* number - */
      cval = cval & 0777777;
      if (o==41) { /* - */
        if (symbol()!=21)
          goto syntax;
        cval = neg(cval & 0777777);
      }
      oct(cval);
      write('\n');
      c = c-1;
      if ((o=symbol())==1) /* ; */
        goto done;
      if (o!=9) /* , */
        goto syntax;
      else
        o = symbol();
    }
    goto syntax;
done:
    if (c>0) {
      write('.=');
      write('.+');
      number(c);
      write('\n');
    }
    return;
  }

  if (o==0) /* eof */
    return;

syntax:
  error('xx');
  stmt();
}

void stmtlist() {
  extern peeksym, eof;
  auto o;

  while (!eof) {
    if ((o = symbol())==3) /* $) */
      return;
    peeksym = o;
    stmt();
  }
  error('$)'); /* missing $) */
}

void stmt() {
  extern peeksym, peekc, *csym, cval, isn, nauto, dt;
  auto o, o1, o2;

next:
  o = symbol();

  if (o==0) { /* eof */
    error('fe'); /* Unexpected eof */
    return;
  }

  if (o==1 | o==3) /* ; $) */
    return;

  if (o==2) { /* $( */
    stmtlist();
    return;
  }

  if (o==19) { /* keyword */

    if (cval==10) { /* goto */
      expr(15);
      callee();
      write('jm');
      write('p ');
      jopnd(dt-1);
      sreset();
      goto semi;
    }

    if (cval==11) { /* return */
      if ((peeksym=symbol())==6) { /* ( */
        pexpr();
        load(dt-1);
      }
      write('jm');
      write('p ');
      write('br');
      write('et');
      write('\n');
      sreset();
      goto semi;
    }

    if (cval==12) { /* if */
      pexpr();
      o1 = isn;
      isn = isn+1;
      jumpc(o1);
      stmt();
      o = symbol();
      if (o==19 & cval==14) { /* else */
        o2 = isn;
        isn = isn+1;
        jump(o2);
        label(o1);
        stmt();
        label(o2);
        return;
      }
      peeksym = o;
      label(o1);
      return;
    }

    if (cval==13) { /* while */
      o1 = isn;
      isn = isn+1;
      label(o1);
      pexpr();
      o2 = isn;
      isn = isn+1;
      jumpc(o2);
      stmt();
      write('jm'); /* back to the test, whose label is already set */
      write('p ');
      write('l');
      number(o1);
      write('\n');
      label(o2);
      return;
    }

    error('sx');
    goto syntax;
  }

  if (o==20 & peekc==':') { /* name : */
    peekc = 0;
    if (!*csym) {
      *csym = 2; /* param */
      csym[1] = isn;
      isn = isn+1;
    } else if (*csym != 2) {
      error('rd');
      goto next;
    }
    label(csym[1]);
    goto next;
  }

  peeksym = o;
  expr(15);
  sreset();

semi:
  o = symbol();
  if (o==1) /* ; */
    return;

syntax:
  error('sz');
  goto next;
}

void blkend() {
  extern isn;
  auto i;

  if (!isn)
    return;
  write('1:');
  i = 0;
  while (i < isn) {
    write('l');
    number(i);
    write('\n');
    i = i+1;
  }
  isn = 0;
}

/* The jump to label n when the condition on the stack's top is false. */
jumpc(n) {
  extern dt, dc[], dk[], dv[], dm[];
  auto i, o;

  i = dt-1;
  o = dc[i];
#ifdef BROKEN
  /* -DBROKEN exchanges the skips of == and !=, a wrong compiler that shows a
     test of the output can fail */
  if (o==60 | o==61)
    o = 121-o;
#endif
  if (o) {
    if (o==60) /* == */
      write('sa');
    else if (o==61) /* != */
      write('sa');
    else
      write('su');
    if (o==60)
      write('s ');
    else if (o==61)
      write('d ');
    else
      write('b ');
    opnd(i);
    if (o==63) /* < */
      sma();
    if (o==64) /* >= */
      spa();
    if (o==65) { /* > */
      sma();
      write(' s');
      write('za');
      write(' i');
    }
    if (o==62) { /* <= */
      sma();
      write(' s');
      write('za');
    }
    if (o>=62)
      write('\n');
  } else {
    load(i);
    write('sz');
    write('a ');
    write('i\n');
  }
  jump(n);
  sreset();
}

sma() {
  write('sm');
  write('a');
}

spa() {
  write('sp');
  write('a');
}

/* A jump forward to label n, through the label table. */
jump(n) {
  write('jm');
  write('p ');
  write('1f');
  write('+');
  number(n);
  write(' i');
  write('\n');
}

label(n) {
  write('l');
  number(n);
  write('=.');
  write('\n');
}

/* The expression stack's operations. */

push(k, v, m) {
  extern dt, dk[], dv[], dm[], dc[];

  if (dt >= 40) {
    error('ex');
    dt = 39;
  }
  dk[dt] = k;
  dv[dt] = v;
  dm[dt] = m;
  dc[dt] = 0;
  dt = dt+1;
}

sreset() {
  extern dt, acd, ntmp;

  dt = 0;
  acd = -1;
  ntmp = 0;
}

/* Entry i is now the value in AC. */
toac(i) {
  extern acd, dk[], dm[], dc[];

  dk[i] = 5;
  dm[i] = 0;
  dc[i] = 0;
  acd = i;
}

/* A temporary, the next fixed word after the autos. */
tmp() {
  extern nauto, ntmp, mtmp;

  ntmp = ntmp+1;
  if (ntmp > mtmp)
    mtmp = ntmp;
  return(nauto + ntmp - 1);
}

/* A number's word in the literal pool, 3f+k.
   The pool holds one word for each value. */
lit(v) {
  extern lits[], nlit;
  auto k;

  k = 0;
  while (k < nlit) {
    if (lits[k] == v)
      return(k);
    k = k+1;
  }
  if (nlit >= 100) {
    error('lt');
    return(0);
  }
  lits[nlit] = v;
  nlit = nlit+1;
  return(k);
}

/* The address field of entry i, its line ended. */
opnd(i) {
  extern dk[], dv[], dm[], symtab[];
  auto k;

  k = dk[i];
  if (k==1)
    oct(dv[i] & 07777);
  else if (k==2) {
    write('.');
    name(symtab + dv[i] + 2);
  } else {
    if (k==3)
      write('1f');
    else if (k==4)
      write('4f');
    else
      write('3f');
    write('+');
    number(dv[i]);
  }
  if (dm[i]==1)
    write(' i');
  write('\n');
}

/* A jump's address field, to entry i's value. */
jopnd(i) {
  extern dm[];

  if (dm[i]==0) {
    dm[i] = 1;
    opnd(i);
  } else {
    dm[i] = 0;
    opnd(i);
  }
}

/* Entry i made a value a jump can reach, a word or an address. */
callee() {
  extern dt, acd, dk[], dm[], dc[];
  auto i;

  i = dt-1;
  if (dc[i] | dk[i]==5)
    mem(i);
  else if (dm[i]==1) {
    load(i);
    spill(i);
  }
}

/* AC stored in a temporary, which entry i becomes. */
spill(i) {
  extern acd, dk[], dv[], dm[], dc[];
  auto t;

  t = tmp();
  write('da');
  write('c ');
  write('4f');
  write('+');
  number(t);
  write('\n');
  dk[i] = 4;
  dv[i] = t;
  dm[i] = 0;
  dc[i] = 0;
  if (acd==i)
    acd = -1;
}

/* AC is about to change.
   The entry in it, if not i, is kept in a temporary. */
needac(i) {
  extern acd, dc[];
  auto j;

  if (acd < 0 | acd==i)
    return;
  j = acd;
  if (dc[j])
    mat(j);
  spill(j);
}

/* Entry i's value into AC. */
load(i) {
  extern acd, dk[], dv[], dm[], dc[];
  auto v;

  if (dc[i]) {
    mat(i);
    return;
  }
  if (dk[i]==5)
    return;
  needac(i);
  if (dm[i] < 0) {
    if (dk[i]==1) {
      v = dv[i];
      if (small(v)) {
        write('la');
        write('w ');
        oct(v);
        write('\n');
      } else if (small(neg(v))) {
        write('la');
        write('w ');
        write('i ');
        oct(neg(v));
        write('\n');
      } else {
        dk[i] = 7;
        dv[i] = lit(v);
        dm[i] = 0;
        write('la');
        write('c ');
        opnd(i);
      }
    } else {
      dm[i] = 0;
      write('la');
      write('w ');
      opnd(i);
    }
  } else {
    write('la');
    write('c ');
    opnd(i);
  }
  toac(i);
}

/* Entry i made a word an instruction can address. */
mem(i) {
  extern dk[], dv[], dm[], dc[];

  if (dc[i] | dk[i]==5) {
    load(i);
    spill(i);
    return;
  }
  if (dm[i] < 0) {
    if (dk[i]==1) {
      dk[i] = 7;
      dv[i] = lit(dv[i]);
      dm[i] = 0;
      return;
    }
    load(i);
    spill(i);
  }
}

/* A comparison's value, 0 or 1, into AC.
   AC holds its left side. */
mat(i) {
  extern dc[];
  auto o;

  o = dc[i];
  dc[i] = 0;
  if (o==60 | o==61) { /* == != */
    write('xo');
    write('r ');
    opnd(i);
    write('sz');
    write('a\n');
    write('la');
    write('w ');
    write('1\n');
    if (o==60) {
      write('xo');
      write('r ');
      write('bo');
      write('ne');
      write('\n');
    }
  } else {
    if (o==63 | o==64) { /* < >= */
      write('su');
      write('b ');
      opnd(i);
    } else { /* > <= */
      write('cm');
      write('a\n');
      write('ad');
      write('d ');
      opnd(i);
    }
    if (o==64 | o==62) { /* >= <= */
      write('cm');
      write('a\n');
    }
    write('ra');
    write('l ');
    write('1\n');
    write('an');
    write('d ');
    write('bo');
    write('ne');
    write('\n');
  }
  toac(i);
}

/* The operator o on the stack's two top entries, the result in the lower. */
binop(o) {
  extern dt, acd, dk[], dv[], dm[], dc[];
  auto l, r, x, m;

  r = dt-1;
  l = dt-2;
  x = l;
  m = r;
  if (o==80) { /* = */
    x = r;
    m = l;
  } else if ((dc[r] | dk[r]==5) & o!=43 & o!=44) {
    /* the right side is in AC, so the operator is turned around */
    x = r;
    m = l;
    if (o>=62 & o<=65) {
      if (o>=64)
        o = o-2;
      else
        o = o+2;
    }
    if (o==41) /* - */
      o = 1;
  }
  mem(m);
  load(x);
  if (o>=60 & o<=65) { /* a comparison, its value made only when needed */
    dk[l] = dk[m];
    dv[l] = dv[m];
    dm[l] = dm[m];
    dc[l] = o;
    acd = l;
    dt = dt-1;
    return;
  }
  if (o==80)
    write('da');
  else if (o==40)
    write('ad');
  else if (o==41)
    write('su');
  else if (o==1) {
    write('cm');
    write('a\n');
    write('ad');
  } else if (o==47)
    write('an');
  else if (o==48)
    write('io');
  else
    write('jd');
  if (o==80)
    write('c ');
  else if (o==40 | o==1)
    write('d ');
  else if (o==41)
    write('b ');
  else if (o==47)
    write('d ');
  else if (o==48)
    write('r ');
  else {
    write('a ');
    if (o==43)
      write('bd');
    else
      write('bm');
    if (o==42)
      write('ul');
    else if (o==43)
      write('iv');
    else
      write('od');
    write('\n');
  }
  opnd(m);
  dt = dt-1;
  toac(l);
}

umin() {
  extern dt, dk[], dc[];
  auto i;

  i = dt-1;
  if (dc[i] | dk[i]==5) {
    load(i);
    write('cm');
    write('a\n');
    write('ad');
    write('d ');
    write('bz');
    write('er');
    write('o\n');
  } else {
    mem(i);
    needac(i);
    write('cl');
    write('a\n');
    write('su');
    write('b ');
    opnd(i);
  }
  toac(i);
}

unot() {
  extern dt, acd, dk[], dv[], dm[], dc[];
  auto i, o;

  i = dt-1;
  o = dc[i];
  if (o) {
    if (o<=61)
      dc[i] = 121-o;
    else
      dc[i] = 127-o;
    return;
  }
  load(i);
  dk[i] = 7;
  dv[i] = lit(0);
  dm[i] = 0;
  dc[i] = 60;
  acd = i;
}

uind() {
  extern dt, dk[], dm[], dc[];
  auto i;

  i = dt-1;
  if (dc[i] | dk[i]==5)
    mem(i);
  else if (dm[i]==1) {
    load(i);
    mem(i);
  } else {
    dm[i] = dm[i]+1;
    return;
  }
  dm[i] = 1;
}

uadr() {
  extern dt, dk[], dm[], dc[];
  auto i;

  i = dt-1;
  if (dc[i] | dk[i]==5)
    mem(i);
  if (dm[i] >= 0)
    dm[i] = dm[i]-1;
}

/* A word's bits in octal, with a leading 0 past 7. */
oct(v) {
  auto d;

  d = (v & 0300000) / 0100000 + 4 * ((v & 0400000) != 0);
  v = v & 077777;
  if (d) {
    write('0');
    write('0' + d);
    octd(v, 5);
    return;
  }
  if (v > 7)
    write('0');
  octd(v, 0);
}

/* n's octal digits, at least k of them. */
octd(n, k) {
  if (n > 7 | k > 1)
    octd(n / 8, k-1);
  write('0' + n % 8);
}

/* The ones' complement negation of a word's bits, never making -0 (host). */
neg(v) {
  if (v == 0)
    return(0);
  return(v ^ 0777777);
}

/* A word from 0 to 07777, which law loads (host). */
small(v) {
  return(v >= 0 & v <= 07777);
}

printn(n) {
  if (n > 9) {
    printn(n / 10);
    n = n % 10;
  }
  write(n + '0');
}

number(x) {
  if (x < 0) {
    write('-');
    x = -x;
  }
  printn(x);
}

name(int *s) {
  while (*s) {
    write(*s);
    s = s+1;
  }
}

void error(code) {
  extern line, eof, *csym, nerror, fout;
  auto f;

  if (eof | nerror==20) {
    eof = 1;
    return;
  }
  nerror = nerror+1;
  flush();
  f = fout;
  fout = 1;
  write(code);
  write(' ');
  if (code=='rd' | code=='un') {
    name(csym + 2);
    write(' ');
  }
  printn(line);
  write('\n');
  flush();
  fout = f;
}

/* storage */

int symtab[300] = { /* class value name */
  1, 5,'a','u','t','o', 0 ,
  1, 6,'e','x','t','r','n', 0 ,
  1,10,'g','o','t','o', 0 ,
  1,11,'r','e','t','u','r','n', 0 ,
  1,12,'i','f', 0 ,
  1,13,'w','h','i','l','e', 0 ,
  1,14,'e','l','s','e', 0 };

int ctab[] = {
    0,127,127,127,  0,127,127,127,  /* NUL SOH STX ETX EOT ENQ ACK BEL */
  127,126,126,127,127,127,127,127,  /* BS  TAB LF  VT  FF  CR  SO  SI  */
  127,127,127,127,127,127,127,127,  /* DLE DC1 DC2 DC3 DC4 NAK SYN ETB */
  127,127,127,127,127,127,127,127,  /* CAN EM  SUB ESC FS  GS  RS  US  */
  126, 34,122,127,127, 44, 47,121,  /* SPC  !   "   #   $   %   &   '  */
    6,  7, 42, 40,  9, 41,127, 43,  /*  (   )   *   +   ,   -   .   /  */
  124,124,124,124,124,124,124,124,  /*  0   1   2   3   4   5   6   7  */
  124,124,  8,  1, 63, 80, 65, 90,  /*  8   9   :   ;   <   =   >   ?  */
  127,123,123,123,123,123,123,123,  /*  @   A   B   C   D   E   F   G  */
  123,123,123,123,123,123,123,123,  /*  H   I   J   K   L   M   N   O  */
  123,123,123,123,123,123,123,123,  /*  P   Q   R   S   T   U   V   W  */
  123,123,123,  4,127,  5, 48,127,  /*  X   Y   Z   [   \   ]   ^   _  */
  127,123,123,123,123,123,123,123,  /*  `   a   b   c   d   e   f   g  */
  123,123,123,123,123,123,123,123,  /*  h   i   j   k   l   m   n   o  */
  123,123,123,123,123,123,123,123,  /*  p   q   r   s   t   u   v   w  */
  123,123,123,  2, 48,  3,127,127}; /*  x   y   z   {   |   }   ~  DEL */

int symbuf[10];
int peeksym = -1;
int peekc;
int eof;
int line = 1;
int *csym;
int *ns;
int cval;
int isn;
int nerror;
int nauto;

/* the expression stack, the temporaries, the frame and the literal pool */
int dk[40];
int dv[40];
int dm[40];
int dc[40];
int dt;
int acd = -1;
int ntmp;
int mtmp;
int nfr;
int lits[100];
int nlit;
