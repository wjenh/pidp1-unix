" bx.s, btmp and bchain, the link between b's two passes, after brt.s in each
bxs: 0
bxf: 0
bxt: 0142164; 0155160; 0040040; 0040040
bxn: 0142170; 0040040; 0040040; 0040040
bxy: 0163171; 0163164; 0145155; 0040040
.btmp: .+1
and bm12
dac lret
lac .fout
dac bxs
law 017
sys creat; bxt
spa
jmp bstop
dac .fout
jmp lrt
.bchain: .+1
jsp bfls
lac .fout
sys close
lac bxs
dac .fout
sys open; bxt; 0
spa
jmp bstop
dac .fin
lac eibufp
dac cibufp
dzm iflg
sys open; bxn; 0
sma
jmp 1f
sys link; bxy; bxn; bxn
sys open; bxn; 0
dac bxf
sys unlink; bxn
lac bxf
spa
jmp bstop
1: dac bxf
law bgo
cma
add lastv
dac 2f
lac bxf
sys read; bgo; 2: 0
lac bxf
sys close
lac bgo+1
dac bsp
lac bgo
dap 3f
3: jsp . i
0
lac .fin
sys close
sys unlink; bxt
jmp bstop
bgo: .main
bend
