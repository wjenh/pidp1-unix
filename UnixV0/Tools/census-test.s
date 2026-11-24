" census-test.s, the census's test input, in the restorers' as7 syntax.
" Every count below was made by hand, line by line, from the code after it.
" census.py --test checks each one, and fails on any nonzero count without one.
" It is assembled as a user program (relocated to 010000).
"
" expect lines 67
" expect words 37
" expect reserved 5
" expect instr 24
" expect jms 1
" expect jms_args 2
" expect sys 2
" expect sys_args 4
" expect call_open 1
" expect call_write 1
" expect law 1
" expect minus 1
" expect tad 1
" expect link 3
" expect combopr 1
" expect autoidx 1
" expect indirect 4
" expect xhalf 1
" expect eae 2
" expect dzm 1
" expect sad 1
" expect isz 1
" expect xct 1
" expect iot 2
" expect iot_dscs 1
" expect iot_tls 1

dscs = 0707141
tls = 0700406

start:
   jms sub; arg1; arg2          " jms with two inline arguments, 3 words
   sys open; name; 0            " 3 words
   sys write; name; 2           " 3 words
   law 17                       " law
   -1                           " minus, a negative constant, law or data
   tad d1                       " tad
   cll; ral                     " two words, two link operations
   cla cll sza                  " one combined operate, one link operation
   lac 10 i                     " auto-index, and indirect
   dac 15                       " direct to 015, no count
   lac name i                   " indirect
   lmq                          " EAE
   lrs 9                        " EAE
   lac 017777 i                 " indirect; same half as the code
   lac 0100                     " the other half, xhalf
   dzm name                     " dzm
   sad d1                       " sad
   isz name                     " isz
   xct name                     " xct
   dscs                         " IOT
   tls                          " IOT
   jmp start                    " no count
sub: 0
   jmp sub i                    " indirect
d1: 1
name: <a>b                      " one word of two characters
arg1: arg2: 0                   " two labels, one word
1: 0
   jmp 1b                       " no count
   . = .+5                      " five words reserved
end: 0
