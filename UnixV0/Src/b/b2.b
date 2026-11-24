/* b2.b - B compiler for PDP-7 Unix, writing PDP-1 instructions, pass 2

   bn.c's native code generator, in the restorers' b.b's B, (C) 2016 Robert
   Swierczek, GPL3.
   It reads what pass 1, b1.b, wrote to btmp.
   That is text, copied to the output, and the generator's calls, each the
   byte 001, a letter and its arguments in signed decimal, each with a space
   after it.
   The output's fixed text (a function's head, a string's jump, labels,
   jumps, data words in octal) is written here, to keep pass 1 small.
   Where bn.c works around the host's arithmetic (its "host" marks), this
   pass does the PDP-1's, in neg and small and the masks.
   bn.c is the model, and the output is its output, byte for byte.

   Built on the host by
      ./bn b2.b b2.s
      perl as7 -o b2.a7 bop.s brt.s bx.s b2.s
   and its words from bgo on are system's bx, which pass 1 reads in.
*/

main() {
  auto c;

  while ((c = read()) != 4) {
    if (c == 1)
      op();
    else
      write(c);
  }
}

/* One call of the generator, its letter read next. */
op() {
  extrn line, dt, acd, nauto, mtmp, nlit, nfr, symtab;
  auto o, k, v, m, sp;

  o = read();
  if (o=='N') { /* the source's line */
    line = num();
    return;
  }
  if (o=='P') { /* push(k, v, m), an external's name after */
    k = num();
    v = num();
    m = num();
    if (k==2) {
      sp = symtab + v + 2;
      while ((*sp = read()) != ' ')
        sp = sp+1;
      *sp = 0;
    }
    push(k, v, m);
    return;
  }
  if (o=='B') {
    binop(num());
    return;
  }
  if (o=='F') {
    jumpc(num());
    return;
  }
  if (o=='Z') {
    sreset();
    return;
  }
  if (o=='R') { /* an argument pushed */
    load(dt-1);
    ws("dac bsp i*nidx bsp*n");
    dt = dt-1;
    acd = -1;
    return;
  }
  if (o=='J') { /* the call, its argument count */
    k = num();
    needac(-1);
    ws("jsp ");
    jopnd(dt-1);
    number(k);
    write('*n');
    toac(dt-1);
    return;
  }
  if (o=='C') {
    callee();
    return;
  }
  if (o=='[') {
    binop(40);
    uind();
    return;
  }
  if (o=='**') {
    uind();
    return;
  }
  if (o=='&') {
    uadr();
    return;
  }
  if (o=='-') {
    umin();
    return;
  }
  if (o=='!') {
    unot();
    return;
  }
  if (o=='L') {
    load(dt-1);
    return;
  }
  if (o=='Q') { /* a string's start, its words pass 1's */
    needac(-1);
    ws("law .+2*njmp 2f*n");
    return;
  }
  if (o=='A') { /* its end, its address in AC */
    ws("2:*n");
    push(5, 0, 0);
    acd = dt-1;
    return;
  }
  if (o=='T') { /* a label */
    write('l');
    number(num());
    ws("=.*n");
    return;
  }
  if (o=='X') {
    jump(num());
    return;
  }
  if (o=='W') { /* a while's jump back */
    ws("jmp l");
    number(num());
    write('*n');
    return;
  }
  if (o=='Y') { /* return */
    ws("jmp bret*n");
    sreset();
    return;
  }
  if (o=='D') { /* a data word */
    oct(num());
    write('*n');
    return;
  }
  if (o=='V') { /* an auto vector's pointer, from its offset in the frame, to its word */
    ws("law ");
    oct(num());
    ws("*nadd bfp*ndac 4f+");
    number(num());
    write('*n');
    return;
  }
  if (o=='G') { /* goto */
    callee();
    ws("jmp ");
    jopnd(dt-1);
    sreset();
    return;
  }
  if (o=='H') { /* a function's head */
    ws(".+1*njda bent*n4f*n");
    nauto = 2;
    mtmp = 0;
    nlit = 0;
    sreset();
    return;
  }
  if (o=='K') { /* its fixed words before the temporaries */
    nauto = num();
    return;
  }
  if (o=='E') { /* its end */
    nfr = num();
    fend();
    return;
  }
  error('op');
}

/* A call's argument, in signed decimal, ended by a space.
   A -0 comes as -0. */
num() {
  auto c, v, s;

  v = 0;
  s = 0;
  while ((c = read()) != ' ') {
    if (c == '-')
      s = 1;
    else
      v = v*10 + c - '0';
  }
  if (s) {
    if (v == 0)
      return(0777777);
    return(-v);
  }
  return(v);
}

/* A function's end, written as the return, the literals, the descriptor
   (N and F), and the N fixed words. */
fend() {
  extrn nauto, nfr, mtmp, nlit, lits;
  auto c;

  ws("jmp bret*n");
  if (nlit) {
    write('3:');
    c = 0;
    while (c < nlit) {
      oct(lits[c]);
      write('*n');
      c = c+1;
    }
  }
  c = nauto - 2 + mtmp;
  write('4:');
  number(c);
  write('*n');
  number(nfr);
  write('*n');
  if (c) {
    ws(".=.+");
    number(c);
    write('*n');
  }
}

/* The jump to label n when the condition on the stack's top is false. */
jumpc(n) {
  extrn dt, dc, dk, dv, dm;
  auto i, o;

  i = dt-1;
  o = dc[i];
  if (o) {
    if (o==60) /* == */
      ws("sas ");
    else if (o==61) /* != */
      ws("sad ");
    else
      ws("sub ");
    opnd(i);
    if (o==63) /* < */
      ws("sma*n");
    if (o==64) /* >= */
      ws("spa*n");
    if (o==65) /* > */
      ws("sma sza i*n");
    if (o==62) /* <= */
      ws("sma sza*n");
  } else {
    load(i);
    ws("sza i*n");
  }
  jump(n);
  sreset();
}

/* A jump forward to label n, through the label table. */
jump(n) {
  ws("jmp 1f+");
  number(n);
  ws(" i*n");
}

/* The expression stack's operations. */

push(k, v, m) {
  extrn dt, dk, dv, dm, dc;

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
  extrn dt, acd, ntmp;

  dt = 0;
  acd = -1;
  ntmp = 0;
}

/* Entry i is now the value in AC. */
toac(i) {
  extrn acd, dk, dm, dc;

  dk[i] = 5;
  dm[i] = 0;
  dc[i] = 0;
  acd = i;
}

/* A temporary, the next fixed word after the autos. */
tmp() {
  extrn nauto, ntmp, mtmp;

  ntmp = ntmp+1;
  if (ntmp > mtmp)
    mtmp = ntmp;
  return(nauto + ntmp - 1);
}

/* A number's word in the literal pool, 3f+k.
   The pool holds one word for each value. */
lit(v) {
  extrn lits, nlit;
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
  extrn dk, dv, dm, symtab;
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
  write('*n');
}

/* A jump's address field, to entry i's value. */
jopnd(i) {
  extrn dm;

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
  extrn dt, acd, dk, dm, dc;
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
  extrn acd, dk, dv, dm, dc;
  auto t;

  t = tmp();
  ws("dac 4f+");
  number(t);
  write('*n');
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
  extrn acd, dc;
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
  extrn acd, dk, dv, dm, dc;
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
        ws("law ");
        oct(v);
        write('*n');
      } else if (small(neg(v))) {
        ws("law i ");
        oct(neg(v));
        write('*n');
      } else {
        dk[i] = 7;
        dv[i] = lit(v);
        dm[i] = 0;
        ws("lac ");
        opnd(i);
      }
    } else {
      dm[i] = 0;
      ws("law ");
      opnd(i);
    }
  } else {
    ws("lac ");
    opnd(i);
  }
  toac(i);
}

/* Entry i made a word an instruction can address. */
mem(i) {
  extrn dk, dv, dm, dc;

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
  extrn dc;
  auto o;

  o = dc[i];
  dc[i] = 0;
  if (o==60 | o==61) { /* == != */
    ws("xor ");
    opnd(i);
    ws("sza*nlaw 1*n");
    if (o==60)
      ws("xor bone*n");
  } else {
    if (o==63 | o==64) /* < >= */
      ws("sub ");
    else /* > <= */
      ws("cma*nadd ");
    opnd(i);
    if (o==64 | o==62) /* >= <= */
      ws("cma*n");
    ws("ral 1*nand bone*n");
  }
  toac(i);
}

/* The operator o on the stack's two top entries, the result in the lower. */
binop(o) {
  extrn dt, acd, dk, dv, dm, dc;
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
    ws("dac ");
  else if (o==40)
    ws("add ");
  else if (o==41)
    ws("sub ");
  else if (o==1)
    ws("cma*nadd ");
  else if (o==47)
    ws("and ");
  else if (o==48)
    ws("ior ");
  else if (o==42)
    ws("jda bmul*n");
  else if (o==43)
    ws("jda bdiv*n");
  else
    ws("jda bmod*n");
  opnd(m);
  dt = dt-1;
  toac(l);
}

umin() {
  extrn dt, dk, dc;
  auto i;

  i = dt-1;
  if (dc[i] | dk[i]==5) {
    load(i);
    ws("cma*nadd bzero*n");
  } else {
    mem(i);
    needac(i);
    ws("cla*nsub ");
    opnd(i);
  }
  toac(i);
}

unot() {
  extrn dt, acd, dk, dv, dm, dc;
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
  extrn dt, dk, dm, dc;
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
  extrn dt, dk, dm, dc;
  auto i;

  i = dt-1;
  if (dc[i] | dk[i]==5)
    mem(i);
  if (dm[i] >= 0)
    dm[i] = dm[i]-1;
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

/* The negation, never making -0.
   -v is the complement, but for v 0. */
neg(v) {
  if (v == 0)
    return(0);
  return(-v);
}

/* A word from 0 to 07777, which law loads. */
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

name(s) {
  while (*s) {
    write(*s);
    s = s+1;
  }
}

/* A message at the line pass 1 gave last; at most 20. */
error(code) {
  extrn line, nerror, fout;
  auto f;

  if (nerror==20)
    return;
  nerror = nerror+1;
  flush();
  f = fout;
  fout = 1;
  write(code);
  write(' ');
  printn(line);
  write('*n');
  flush();
  fout = f;
}

/* storage, with the externals' names where pass 1's symtab has them */

symtab[300];
line;
nerror;
nauto;

/* the expression stack, the temporaries, the frame and the literal pool */
dk[40];
dv[40];
dm[40];
dc[40];
dt;
acd -1;
ntmp;
mtmp;
nfr;
lits[100];
nlit;
