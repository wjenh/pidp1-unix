/* b1.b - B compiler for PDP-7 Unix, writing PDP-1 instructions, pass 1

   The restorers' b.b, (C) 2016 Robert Swierczek, GPL3, with its threaded
   code generator replaced by bn.c's native one, and split in two passes.
   GPL3, as b.b is.
   This pass, the lexer and the parser, writes the names, the numbers of
   strings and label tables, and each call of the generator to the file
   btmp.
   bx.s then reads pass 2, b2.b, from system's bx over the bank, and pass 2
   writes the output.
   A call is the byte 001, a letter, and its arguments, each in signed
   decimal and a space.
   An N gives the source's line first, whenever it has moved, for pass 2's
   messages.
   The rest of the output's text is pass 2's, so that this pass fits its
   bank.
   bn.c is the model, and the output is its output, byte for byte.

   Built on the host by
      ./bn b1.b b1.s
      perl as7 -o b1.a7 bop.s brt.s bx.s b1.s

   On the PDP-1, b x.b x.s compiles a program, then as bop.s brt.s x.s
   assembles it.
*/

main() {
  extrn symtab, eof, ns;

  btmp();
  while (!eof) {
    ns = symtab + 51;
    extdef();
    blkend();
  }
  ws("bend=.*n"); /* the program's end, where brt.s's stack starts */
  bchain();
}

lookup() {
  extrn symtab, symbuf, eof, ns;
  auto np, sp, rp;

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
  extrn symbuf, ctab, peeksym, peekc, eof, line, csym, cval;
  auto b, c, ct, sp;

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
    if (c=='*n')
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
    if (subseq('**',1,0))
      return(43);
com:
    c = read();
com1:
    if (c==4) {
      eof = 1;
      error('**/'); /* eof */
      return(0);
    }
    if (c=='*n')
      line = line+1;
    if (c!='**')
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
      if (b==8 & cval >= 040000) /* past 0377777, the top bit set by | */
        cval = ((cval-040000)*8 + c - '0') | 0400000;
      else
        cval = cval*b + c -'0';
      c = read();
    }
    peekc = c;
    return(21);
  }
  if (c=='*'') { /* ' */
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
  extrn peekc;

  if (!peekc)
    peekc = read();
  if (peekc != c)
    return(a);
  peekc = 0;
  return(b);
}

getcc() {
  extrn cval;
  auto c;

  cval = 0;
  if ((c = mapch('*'')) < 0)
    return;
  cval = c;
  if ((c = mapch('*'')) < 0)
    return;
  cval = cval * 512 + c;
  if (mapch('*'') >= 0)
    error('cc');
}

getstr() {
  auto i, c, d;

  i = 1;
loop:
  if ((c = mapch('"')) < 0) {
    number(2048);
    write('*n');
    return(i);
  }
  if ((d = mapch('"')) < 0) {
    number(c*512+4);
    write('*n');
    return(i);
  }
  number(c*512+d);
  write('*n');
  i = i+1;
  goto loop;
}

mapch(c) {
  extrn peekc;
  auto a;

  if ((a=read())==c)
    return(-1);

  if (a=='*n' | a==0 | a==4) {
    error('cc');
    peekc = a;
    return(-1);
  }

  if (a=='**') {
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
      return('*t');

    if (a=='r')
      return('*r');

    if (a=='n')
      return('*n');
  }
  return(a);
}

expr(lev) {
  extrn peeksym, csym, cval, isn, symtab;
  auto o, n;

  o = symbol();

  if (o==21) { /* number */
case21:
    push(1, cval, -1);
    goto loop;
  }

  if (o==122) { /* string */ /* its address in AC, the words jumped over */
    g('Q'); /* needac(-1), the jump over the words */
    getstr();
    g('A'); /* their end; their address in AC */
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
    g('!');
    goto loop;
  }

  if (o==41) { /* - */
    peeksym = symbol();
    if (peeksym==21) { /* number */
      peeksym = -1;
      cval = neg(cval);
      goto case21;
    }
    expr(1);
    g('-');
    goto loop;
  }

  if (o==47) { /* & */
    expr(1);
    g('&');
    goto loop;
  }

  if (o==42) { /* * */
    expr(1);
    g('**');
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
    g('['); /* binop(40), uind() */
    goto loop;
  }
  if (o==6) { /* ( */ /* the arguments pushed, jsp f i, their count */
    g('C'); /* callee() */
    n = 0;
    o = symbol();
    if (o!=7) { /* ) */
      peeksym = o;
      while (o!=7) {
        expr(15);
        g('R'); /* the argument pushed */
        n = n+1;
        o = symbol();
        if (o!=7 & o!=9) { /* ) , */
          error('ex');
          return;
        }
      }
    }
    g('J'); /* the call */
    ga(n);
    goto loop;
  }

  peeksym = o;
}

pexpr() {
  if (symbol()==6) { /* ( */
    expr(15);
    if (symbol()==7) /* ) */
      return;
  }
  error('()');
}

declare(kw) {
  extrn csym, cval, nauto, nfr;
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
        g('V');
        ga(nfr);
        ga(nauto);
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

extdef() {
  extrn peeksym, csym, cval, nauto, nfr;
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
    nauto = 2;
    nfr = 0;
    g('H'); /* the function's head, and the generator's start */
    if (o==6) { /* ( */
      declare(8); /* param */
      if ((o=symbol())!=2) /* $( */
        goto syntax;
    }
    while((o=symbol())==19 & cval<10) /* auto extrn */
      declare(cval);
    peeksym = o;
    g('K'); /* the fixed words before the temporaries */
    ga(nauto);
    stmtlist();
    g('E'); /* the end, with the literals and the descriptor */
    ga(nfr);
    return;
  }

  if (o==41) { /* - */
    if (symbol()!=21) /* number */
      goto syntax;
    g('D'); /* a data word, in octal */
    ga(neg(cval));
    return;
  }

  if (o==21) { /* number */
    g('D');
    ga(cval);
    return;
  }

  if (o==1) { /* ; */
    write('0*n');
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
    ws(".+1*n");
    if ((o=symbol())==1) /* ; */
      goto done;
    while (o==21 | o==41) { /* number - */
      if (o==41) { /* - */
        if (symbol()!=21)
          goto syntax;
        cval = neg(cval);
      }
      g('D');
      ga(cval);
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
      ws(".=.+");
      number(c);
      write('*n');
    }
    return;
  }

  if (o==0) /* eof */
    return;

syntax:
  error('xx');
  g('K');
  ga(nauto);
  stmt();
}

stmtlist() {
  extrn peeksym, eof;
  auto o;

  while (!eof) {
    if ((o = symbol())==3) /* $) */
      return;
    peeksym = o;
    stmt();
  }
  error('$)'); /* missing $) */
}

stmt() {
  extrn peeksym, peekc, csym, cval, isn;
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
      g('G');
      goto semi;
    }

    if (cval==11) { /* return */
      if ((peeksym=symbol())==6) { /* ( */
        pexpr();
        g('L'); /* load(dt-1) */
      }
      g('Y'); /* jmp bret, sreset() */
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
      g('W'); /* back to the test, whose label is already set */
      ga(o1);
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
  g('Z');

semi:
  o = symbol();
  if (o==1) /* ; */
    return;

syntax:
  error('sz');
  goto next;
}

blkend() {
  extrn isn;
  auto i;

  if (!isn)
    return;
  write('1:');
  i = 0;
  while (i < isn) {
    write('l');
    number(i);
    write('*n');
    i = i+1;
  }
  isn = 0;
}

/* A jump forward to label n, through the label table. */
jump(n) {
  g('X');
  ga(n);
}

label(n) {
  g('T');
  ga(n);
}

/* The calls of pass 2's generator. */

/* A call, 001 and its letter, with the line first when it has moved. */
g(c) {
  extrn line, sline;

  if (line != sline) {
    sline = line;
    write(01116); /* 001 N */
    ga(line);
  }
  write(512 + c);
}

/* An argument of the call, in signed decimal, then a space. */
ga(v) {
  number(v);
  write(' ');
}

/* An operand pushed; an external's name follows, then a space. */
push(k, v, m) {
  extrn symtab;

  g('P');
  ga(k);
  ga(v);
  ga(m);
  if (k==2) {
    name(symtab + v + 2);
    write(' ');
  }
}

binop(o) {
  g('B');
  ga(o);
}

/* The jump to label n when the condition is false. */
jumpc(n) {
  g('F');
  ga(n);
}

/* A string's characters, two a word, to its *e. */
ws(s) {
  auto w;

  while ((w = *s) / 512 != 4) {
    if (w % 512 == 4) {
      write(w / 512);
      return;
    }
    write(w);
    s = s+1;
  }
}

/* The negation, never making -0.
   -v is the complement, but for v 0. */
neg(v) {
  if (v == 0)
    return(0);
  return(-v);
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

name(s) {
  while (*s) {
    write(*s);
    s = s+1;
  }
}

error(code) {
  extrn line, eof, csym, nerror, fout;
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
  write('*n');
  flush();
  fout = f;
}

/* storage */

symtab[300] /* class value name */
  1, 5,'a','u','t','o', 0 ,
  1, 6,'e','x','t','r','n', 0 ,
  1,10,'g','o','t','o', 0 ,
  1,11,'r','e','t','u','r','n', 0 ,
  1,12,'i','f', 0 ,
  1,13,'w','h','i','l','e', 0 ,
  1,14,'e','l','s','e', 0 ;

ctab[]
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
  123,123,123,  2, 48,  3,127,127;  /*  x   y   z   {   |   }   ~  DEL */

symbuf[10];
peeksym -1;
peekc;
eof;
line 1;
sline;
csym;
ns;
cval;
isn;
nerror;
nauto;
nfr;
