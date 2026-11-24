" brt.s, B's run time and 1970's library on the PDP-1, after bop.s
jmp bstart
.=020
bsp: bend
bfp: 0
bx: 0
bei: 0
ber: 0
bd: 0
bn: 0
bf: 0
ba: 0
bq: 0
bp: 0
bk: 0
brv: 0
bt: 0
lret: 0
lrv: 0
ibufp: 0
cibufp: 0
eibufp: 0
iflg: 0
obufp: 0
cobufp: 0
eobufp: 0
oflg: 0
lastv: 07770
.fin: 0
.fout: 1
bzero: 0
bone: 1
bm12: 07777
bo777: 0777
bfour: 4
beight: 8
b64: 64
.=0100
0
eem
jmp 0103 i
0101
lem
jmp 0106 i
0
0
bstart:
lac 07777 i
sad bfour
jmp 2f
sad beight
jmp 1f
law 9
add 07777
dap 3f
law 017
sys creat; 3: 0
spa
jmp bstop
dac .fout
1: law 5
add 07777
dap 3f
sys open; 3: 0; 0
spa
jmp bstop
dac .fin
2: lac lastv
dac eibufp
dac cibufp
sub b64
dac lastv
dac ibufp
dac eobufp
sub b64
dac lastv
dac obufp
dac cobufp
jsp .main i
0
bstop:
jsp bfls
sys exit
bent: 0
and bm12
dac bei
lac bent
and bm12
dac ber
lac bsp
dac bx
lac bei i
dac bd
lac bd i
dac bn
idx bd
lac bd i
dac bf
idx bd
dac ba
dac bq
lac bn
sza i
jmp 2f
cma
dac bk
1: lac bq i
dac bsp i
idx bq
idx bsp
isp bk
jmp 1b
2: lac bfp
dac bsp i
idx bsp
lac bei
dac bsp i
idx bsp
lac ber
dac bsp i
idx bsp
dac bfp
add bf
dac bsp
lac ber i
sza i
jmp 3f
dac bk
cma
add bx
dac bp
lac bn
sub bk
sma
jmp 4f
lac bn
dac bk
4: lac bk
sza i
jmp 3f
cma
dac bk
lac ba
dac bq
5: lac bp i
dac bq i
idx bp
idx bq
isp bk
jmp 5b
3: idx bei
dap 6f
6: jmp .
bret: dac brv
lac bfp
sub bone
dac bq
lac bq i
dac ber
lac bq
sub bone
dac bq
lac bq i
dac bei
lac bq
sub bone
dac bq
lac bq i
dac bfp
lac bei i
dac bd
lac bd i
dac bn
idx bd
idx bd
dac ba
lac bq
sub bn
dac bsp
dac bp
lac bn
sza i
jmp 2f
cma
dac bk
lac ba
dac bq
1: lac bp i
dac bq i
idx bp
idx bq
isp bk
jmp 1b
2: lac ber i
cma
add bsp
dac bsp
idx ber
dap 3f
lac brv
3: jmp .
bmul: 0
dac bq
jsp bgx
lac bmul
mul bt
scl 0777
scl 0377
add bzero
bmx: jmp .
bdiv: 0
dac bq
jsp bgx
lac bdiv
lio bdiv
scr 0777
scr 0377
div bt
cla
jmp bmx
bmod: 0
dac bq
jsp bgx
lac bmod
lio bmod
scr 0777
scr 0377
div bt
cli
dio bt
lac bt
jmp bmx
bgx: dap 1f
lac bq
and bm12
dac bq
lac bq i
dac bt
lac bt i
dac bt
idx bq
dap bmx
1: jmp .
.read: .+1
and bm12
dac lret
jsp getc
jmp lrt
.write: .+1
and bm12
dac lret
lac lret i
cma
add bsp
dac bq
lac bq i
dac lrv
rar 0777
jda putc
lac lrv
jda putc
lac lrv
jmp lrt
.flush: .+1
and bm12
dac lret
jsp bfls
jmp lrt
.array: .+1
and bm12
dac lret
lac lret i
cma
add bsp
dac bq
lac lastv
sub bq i
dac lastv
lrt: dac lrv
lac lret i
cma
add bsp
dac bsp
idx lret
dap 1f
lac lrv
1: jmp .
getc: dap 3f
lac iflg
dzm iflg
sza
jmp 3f
lac cibufp
sad eibufp
jmp 1f
lac cibufp i
and bo777
dac iflg
lac cibufp i
dac bt
idx cibufp
lac bt
rar 0777
and bo777
jmp 3f
1: lac ibufp
dap 2f
lac .fin
sys read; 2: 0; 64
sma sza i
jmp 4f
add ibufp
dac eibufp
lac ibufp
dac cibufp
jmp getc+1
4: lac bfour
3: jmp .
putc: 0
dap 3f
lac putc
and bo777
sza i
jmp 3f
dac bt
lac oflg
sza
jmp 1f
lac bt
ral 0777
dac cobufp i
dac oflg
jmp 3f
1: lac cobufp i
ior bt
dac cobufp i
dzm oflg
idx cobufp
sas eobufp
jmp 3f
lac obufp
dap 2f
lac .fout
sys write; 2: 0; 64
lac obufp
dac cobufp
3: jmp .
bfls: dap 3f
lac oflg
sza
idx cobufp
lac cobufp
sub obufp
sza i
jmp 3f
dac 2f+1
lac obufp
dac 2f
lac .fout
sys write; 2: 0; 0
lac obufp
dac cobufp
dzm oflg
3: jmp .
