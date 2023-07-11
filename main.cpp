#include <iostream>
#include <cstdio>
#include <fstream>
#include <cstring>

#define int long long

using namespace std;

int token; //current token
char *src, *old_src; //pointer to source code string

int poolsize; //default size of text/data/stack
int line; //line number

//VM memory segments
int *text,        //text segment
*old_text,        //for dump text segment
*stack;           //stack
char *datasgm;    //data segment

//VM registers
int *pc,          //program counter, it stores a memory address which points to the next instruction to be run
*bp,              //stack pointer, which always points to the top of the stack
*sp,              //base pointer, points to some elements on the stack; it is used in function calls
ax,               //a general register that we use to store the result of an instruction
cycle;

//VM instructions
enum {
    LEA, IMM, JMP, CALL, JZ, JNZ, ENT, ADJ, LEV, LI, LC, SI, SC, PUSH,
    OR, XOR, AND, EQ, NE, LT, GT, LE, GE, SHL, SHR, ADD, SUB, MUL, DIV, MOD,
    OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT
};

// tokens and classes (operators last and in precedence order)
enum {
    Num=128, Fun, Sys, Glo, Loc, Id,
    Char, Else, Enum, If, Int, Return, Sizeof, While,
    Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};

//symbol table
int token_val; //value of current token (mainly for number)
int *current_id, //current parsed ID
*symbols; //symbol table

// fields of identifier
enum {
    Token, Hash, Name, Type, Class, Value, BType, BClass, BValue, IdSize
};

//for lexical analysis
//get the next token (will ignore spaces tabs etc.)
void next() {
    char *last_pos;
    int hash;
    while ((token=*src)) {
        ++src;

        //parse token here
        if (token=='\n')
            line++;
        else if (token>='a' && token<='z' || token>='A' && token<='Z' || token=='_') {
            last_pos=src-1;
            hash=token;
            while ((*src>='a' && *src<='z') || (*src>='A' && *src<='Z') || (*src>='0' && *src<='9') || (*src=='_')) {
                hash=hash*147+*src;
                src++;
            }
            // look for existing identifier, linear search
            current_id=symbols;
            while (current_id[Token]) {
                if (current_id[Hash]==hash && !memcmp((char *) current_id[Name], last_pos, src-last_pos)) {
                    //found one, return
                    token=current_id[Token];
                    return;
                }
                current_id=current_id+IdSize;
            }
            //store new ID
            current_id[Name]=(int) last_pos;
            current_id[Hash]=hash;
            token=current_id[Token]=Id;
            return;
        } else if (token>='0' && token<='9') {
            //parse number, three kinds: dec(123) hex(0x123) oct(017)
            token_val=token-'0';
            if (token_val>0) {
                //dec, starts with [1-9]
                while (*src>='0' && *src<='9') {
                    token_val=token_val*10+*src++ -'0';
                }
            } else {
                //starts with number 0
                if (*src=='x' || *src=='X') {
                    //hex
                    token=*++src;
                    while ((token>='0' && token<='9') || (token>='a' && token<='f') || (token>='A' && token<='F')) {
                        token_val=token_val*16+(token&15)+(token>='A' ? 9 : 0);
                        token=*++src;
                    }
                } else {
                    //oct
                    while (*src>='0' && *src<='7') {
                        token_val=token_val*8+*src++ -'0';
                    }
                }
            }
            token=Num;
            return;
        } else if (token=='"' || token=='\'') {
            //parse string literal, currently, the only supported escape
            //character is '\n', store the string literal into data.
            last_pos=datasgm;
            while (*src!=0 && *src!=token) {
                token_val=*src++;
                if (token_val=='\\') {
                    // escape character
                    token_val=*src++;
                    if (token_val=='n') {
                        token_val='\n';
                    }
                }
                if (token=='"') {
                    *datasgm++=token_val;
                }
            }

            src++;
            //if it is a single character, return Num token
            if (token=='"')
                token_val=(int) last_pos;
            else
                token=Num;
            return;
        } else if (token=='/') {
            if (*src=='/') {
                //skip comments (lookahead concept)
                while (*src!=0 && *src!='\n') {
                    ++src;
                }
            } else {
                //divide operator
                token=Div;
                return;
            }
        } else if (token=='=') {
            token=Eq;
            return;
        } else if (token=='+') {
            // parse '+' and '++'
            if (*src=='+') {
                src++;
                token=Inc;
            } else {
                token=Add;
            }
            return;
        } else if (token=='-') {
            // parse '-' and '--'
            if (*src=='-') {
                src++;
                token=Dec;
            } else {
                token=Sub;
            }
            return;
        } else if (token=='!') {
            // parse '!='
            if (*src=='=') {
                src++;
                token=Ne;
            }
            return;
        } else if (token=='<') {
            // parse '<=', '<<' or '<'
            if (*src=='=') {
                src++;
                token=Le;
            } else if (*src=='<') {
                src++;
                token=Shl;
            } else if (*src=='-') {
                src++;
                token=Assign;
            } else {
                token=Lt;
            }
            return;
        } else if (token=='>') {
            // parse '>=', '>>' or '>'
            if (*src=='=') {
                src++;
                token=Ge;
            } else if (*src=='>') {
                src++;
                token=Shr;
            } else {
                token=Gt;
            }
            return;
        } else if (token=='|') {
            // parse '|' or '||'
            if (*src=='|') {
                src++;
                token=Lor;
            } else {
                token=Or;
            }
            return;
        } else if (token=='&') {
            // parse '&' and '&&'
            if (*src=='&') {
                src++;
                token=Lan;
            } else {
                token=And;
            }
            return;
        } else if (token=='^') {
            token=Xor;
            return;
        } else if (token=='%') {
            token=Mod;
            return;
        } else if (token=='*') {
            token=Mul;
            return;
        } else if (token=='[') {
            token=Brak;
            return;
        } else if (token=='?') {
            token=Cond;
            return;
        } else if (token=='~' || token==';' || token=='{' || token=='}' || token=='(' || token==')' || token==']' ||
                   token==',' || token==':') {
            // directly return the character as token;
            return;
        }
    }
}

//parser expression
void expression(int level) {
    //do nothing
}

//program entry point
void program() {
    next(); //get next token
    while (token>0) { //while not end of file
        //printf("token is: %c\n", token); //debug
        next();
    }
}

//the entrance for virtual machine
//used to interpret target instructions
int eval() {
    int op, *tmp;
    int nr=0;
    while (true) {
        op=*pc++; //get next operation code
        //cout<<nr++<<endl; //debugging
        if (op==IMM)
            ax=*pc++; //load immediate value to ax
        else if (op==LC)
            ax=*(char *) ax; //load character to ax, address in ax
        else if (op==LI)
            ax=*(int *) ax; //load integer to ax, address in ax
        else if (op==SC)
            ax=*(char *) *sp++=ax; //save character to address, value in ax
        else if (op==SI)
            ax=*(int *) *sp++=ax; //save integer to address, value in ax
        else if (op==PUSH)
            *--sp=ax; //push the value of ax onto the stack
        else if (op==JMP)
            pc=(int *) *pc; //jump to the address
        else if (op==JZ)
            pc=ax ? pc+1 : (int *) *pc; //jump if ax is zero
        else if (op==JNZ)
            pc=ax ? (int *) *pc : pc+1; //jump if ax is not zero
        else if (op==CALL) {
            *--sp=(int) (pc+1);
            pc=(int *) *pc;
        } //call subroutine
            // else if (op == RET)
            // pc = (int *)*sp++; // return from subroutine
        else if (op==ENT) {
            *--sp=(int) bp;
            bp=sp;
            sp=sp-*pc++;
        } //make new stack frame
        else if (op==ADJ)
            sp=sp+*pc++; //remove arguments from frame
        else if (op==LEV) {
            sp=bp;
            bp=(int *) *sp++;
            pc=(int *) *sp++;
        } //restore call frame
        else if (op==LEA)
            ax=(int) (bp+*pc++); //load address for arguments
            // Mathematical instructions yay
        else if (op==OR)
            ax=*sp++|ax;
        else if (op==XOR)
            ax=*sp++^ax;
        else if (op==AND)
            ax=*sp++&ax;
        else if (op==EQ)
            ax=*sp++==ax;
        else if (op==NE)
            ax=*sp++!=ax;
        else if (op==LT)
            ax=*sp++<ax;
        else if (op==LE)
            ax=*sp++<=ax;
        else if (op==GT)
            ax=*sp++>ax;
        else if (op==GE)
            ax=*sp++>=ax;
        else if (op==SHL)
            ax=*sp++<<ax;
        else if (op==SHR)
            ax=*sp++>>ax;
        else if (op==ADD)
            ax=*sp++ +ax;
        else if (op==SUB)
            ax=*sp++ -ax;
        else if (op==MUL)
            ax=*sp++*ax;
        else if (op==DIV)
            ax=*sp++/ax;
        else if (op==MOD)
            ax=*sp++%ax;
        else if (op==EXIT) {
            printf("exit(%lld)\n", *sp);
            return *sp;
        } else if (op==PRTF) {
            tmp=sp+pc[1];
            ax=printf((char *) tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]);
        } else if (op==MALC)
            ax=(int) malloc(*sp);
        else if (op==MSET)
            ax=(int) memset((char *) sp[2], sp[1], *sp);
        else if (op==MCMP)
            ax=memcmp((char *) sp[2], (char *) sp[1], *sp);
        else {
            printf("unknown instruction:%d\n", op);
            return -1;
        }
        /*
         * -the stack grows from high address to low address so that when we push a new element to the stack, SP decreases
         *  *sp++ = pop one element from the stack
         *  *--sp = push one element onto the stack
         *
         * -JZ, JNZ are used to implement if-else statement conditional jump
         *
         *
         * -Function implementation (CALL, RET, ENT)
         * A function is a block of code, it may be physically far form the instruction we are currently executing.
         * So we'll need to JMP to the starting point of a function.
         * Then why introduce a new instruction CALL?
         * Because we'll need to do some bookkeeping: store the current execution position so that the program can resume after function call returns.
         * So we'll need CALL <addr> to call the function whose starting point is <addr> and RET to fetch the bookkeeping information to resume previous execution.
         *
         * ENT <size> is called when we are about to enter the function call to "make a new calling frame".
         * It will store the current PC value onto the stack, and save some space(<size> bytes) to store the local variables for function.
         *
         *
         * -Mathematical instructions
         * each operator has two arguments: the first one is stored on the top of the stack while the second is stored in AX
         * the final result is stored in AX
         */
    }
    printf("error");
    return 0;
}

#undef int

int main(int argc, char *argv[]) {

#define int intptr_t

    //printf("Hello World!\n");
    argc--;
    argv++;

    poolsize=256*1024; // arbitrary size
    line=1;

    ifstream file;
    file.open(*argv, std::ios::in);

    //check memory allocation = problem for later
    /*
    if ((fd = file.is_open(*argv, std::ios::out)) < 0) {
        printf("could not open(%s)\n", *argv);
        return -1;
    }
     */

    if (!(src=old_src=static_cast<char *>(malloc(poolsize)))) {
        printf("could not malloc(%d) for source area\n", poolsize);
        return -1;
    }

    //read the source file = problem for later
    /*
    if ((i = istream::read(fd, src, poolsize-1)) <= 0) {
        printf("read() returned %d\n", i);
        return -1;
    }
     */

    file.getline(src, poolsize-1); //read the whole file into src with poolsize-1 as the max length
    //file>>src; //only reads one word
    file.close();

    // allocate memory for virtual machine
    if (!(text=old_text=static_cast<int *>(malloc(poolsize)))) {
        printf("could not malloc(%d) for text area\n", poolsize);
        return -1;
    }
    if (!(datasgm=static_cast<char *>(malloc(poolsize)))) {
        printf("could not malloc(%d) for data area\n", poolsize);
        return -1;
    }
    if (!(stack=static_cast<int *>(malloc(poolsize)))) {
        printf("could not malloc(%d) for stack area\n", poolsize);
        return -1;
    }

    memset(text, 0, poolsize);
    memset(datasgm, 0, poolsize);
    memset(stack, 0, poolsize);

    bp=sp=(int *) ((int) stack+poolsize);
    ax=0;

    //test "assembly" language through demo code
    int i=0;
    text[i++]=IMM;
    text[i++]=10;
    text[i++]=PUSH;
    text[i++]=IMM;
    text[i++]=20;
    text[i++]=ADD;
    text[i++]=PUSH;
    text[i++]=EXIT;
    pc=text;

    //TODO: lexer (part 3)

    program();
    return eval();
}
