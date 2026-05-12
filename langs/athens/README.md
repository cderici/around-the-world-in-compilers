# Athens

Athens is a small expression-oriented language I built on top of LLVM.  It follows the same core design as the classic Kaleidoscope language, but structured as a modular and extensible compiler instead of a single-file example. I separated the components (e.g. lexer, parser, codegen, etc) so I can experiment with swapping front-end pieces and reuse parts of the system in other languages I'm building (depending on the compatiblity, I may turn all these into interfaces so I can plug-and-play).

Athens can either run programs through LLVM's ORC JIT or emit standalone LLVM IR. You can run: 
```
./build/bin/athens --llvmir program.ath > program.ll
```
to produce the LLVM IR without extra driver noise and use it with external tools such as `opt`, `llc`, etc.

Here's the [language grammar](#language-grammar) if you're curious. Check out [test programs](https://github.com/cderici/around-the-world-in-26-languages/tree/main/test-programs) to see example code.

## Build

Get some dependencies:
```
sudo apt-get update && sudo apt-get install -y wget gnupg lsb-release
```

Install llvm and clang 20:
```
wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 20
```

Build Athens:
```
make
```

## Run

For the REPL, run Athens with no input file.
```
./build/bin/athens
```

Pass an Athens source file as the first argument.
```
./build/bin/athens my-source.ath
```

To get the LLVM IR, pass in in the `--llvmir` flag:
```
./build/bin/athens --llvmir test-programs/fib.ath > fibsIR.ll
```

That flag is useful for REPL experimentation too:
```
./build/bin/athens --llvmir
```

There are some test programs that you can check out:

```
./build/bin/athens test-programs/mandelbrot.ath
```

should give you:

```
*******************************************************************************
*******************************************************************************
****************************************++++++*********************************
************************************+++++...++++++*****************************
*********************************++++++++.. ...+++++***************************
*******************************++++++++++..   ..+++++**************************
******************************++++++++++.     ..++++++*************************
****************************+++++++++....      ..++++++************************
**************************++++++++.......      .....++++***********************
*************************++++++++.   .            ... .++**********************
***********************++++++++...                     ++**********************
*********************+++++++++....                    .+++*********************
******************+++..+++++....                      ..+++********************
**************++++++. ..........                        +++********************
***********++++++++..        ..                         .++********************
*********++++++++++...                                 .++++*******************
********++++++++++..                                   .++++*******************
*******++++++.....                                    ..++++*******************
*******+........                                     ...++++*******************
*******+... ....                                     ...++++*******************
*******+++++......                                    ..++++*******************
*******++++++++++...                                   .++++*******************
*********++++++++++...                                  ++++*******************
**********+++++++++..        ..                        ..++********************
*************++++++.. ..........                        +++********************
******************+++...+++.....                      ..+++********************
*********************+++++++++....                    ..++*********************
***********************++++++++...                     +++*********************
*************************+++++++..   .            ... .++**********************
**************************++++++++.......      ......+++***********************
****************************+++++++++....      ..++++++************************
*****************************++++++++++..     ..++++++*************************
*******************************++++++++++..  ...+++++**************************
*********************************++++++++.. ...+++++***************************
***********************************++++++....+++++*****************************
***************************************++++++++********************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
*******************************************************************************
0.000000
```

## Language Grammar

```
module                 ::= top-levels
top-levels             ::= top-level top-levels | ε

top-level              ::= function-def
                         | extern-decl
                         | expression

function-def           ::= "def"    prototype expression
extern-decl            ::= "extern" prototype

; ---------- prototypes ----------
prototype              ::= named-prot
                         | unary-op-prot
                         | binary-op-prot

named-prot             ::= identifier "(" parameter-list ")"
unary-op-prot          ::= "unary"  operator-char "(" identifier ")"
binary-op-prot         ::= "binary" operator-char int "(" parameter-list ")"

parameter-list         ::= identifier parameter-list-tail | ε
parameter-list-tail    ::= identifier parameter-list-tail | ε

; ---------- expressions ----------
expression             ::= var-expr
                         | if-expr
                         | for-expr
                         | binary-expr

var-expr               ::= "var" var-binding-list "in" expression
var-binding-list       ::= var-binding var-binding-list-tail
var-binding-list-tail  ::= "," var-binding var-binding-list-tail | ε
var-binding            ::= identifier optional-init
optional-init          ::= "=" expression | ε

if-expr                ::= "if" expression "then" expression "else" expression

for-expr               ::= "for" identifier "=" expression "," expression
                           optional-for-step "in" expression

optional-for-step      ::= "," expression | ε

; ---------- binary / unary / primary ----------
binary-expr            ::= unary-expr binary-expr-tail
binary-expr-tail       ::= operator-char unary-expr binary-expr-tail | ε

unary-expr             ::= unary-prefix
                         | primary-expr

unary-prefix           ::= operator-char unary-expr

primary-expr           ::= number
                         | paren-expr
                         | id-or-func-call-expr

paren-expr             ::= "(" expression ")"

id-or-func-call-expr   ::= identifier id-or-func-call-suffix
id-or-func-call-suffix ::= "(" argument-list ")" | ε
argument-list          ::= expression argument-list-tail | ε
argument-list-tail     ::= "," expression argument-list-tail | ε

; ---------- terminals ----------
identifier             ::= alphanumeric symbols
number                 ::= every number in athens is a double
operator-char          ::= any non-alphanumeric, non-whitespace character
                           except delimiter characters like "(", ")", ","
                           (athens supports user-defined operators)
```
