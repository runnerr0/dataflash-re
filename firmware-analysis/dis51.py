# Minimal-but-complete 8051 disassembler (MCS-51)
# Usage: python3 dis51.py "[('label',start,end), ...]"   (addresses are ints)
# Reads the de-mirrored 16KB image at df282_extract/Df31f2.82 (adjust path as needed).
import sys

T = {}
def s(o,m,l): T[o]=(m,l)

s(0x00,"NOP",1); s(0x01,"AJMP",2); s(0x02,"LJMP a16",3); s(0x03,"RR A",1)
s(0x04,"INC A",1); s(0x05,"INC %d",2)
s(0x06,"INC @R0",1); s(0x07,"INC @R1",1)
for r in range(8): s(0x08+r,f"INC R{r}",1)
s(0x10,"JBC %b,%r",3); s(0x11,"ACALL",2); s(0x12,"LCALL a16",3); s(0x13,"RRC A",1)
s(0x14,"DEC A",1); s(0x15,"DEC %d",2); s(0x16,"DEC @R0",1); s(0x17,"DEC @R1",1)
for r in range(8): s(0x18+r,f"DEC R{r}",1)
s(0x20,"JB %b,%r",3); s(0x21,"AJMP",2); s(0x22,"RET",1); s(0x23,"RL A",1)
s(0x24,"ADD A,#%i",2); s(0x25,"ADD A,%d",2); s(0x26,"ADD A,@R0",1); s(0x27,"ADD A,@R1",1)
for r in range(8): s(0x28+r,f"ADD A,R{r}",1)
s(0x30,"JNB %b,%r",3); s(0x31,"ACALL",2); s(0x32,"RETI",1); s(0x33,"RLC A",1)
s(0x34,"ADDC A,#%i",2); s(0x35,"ADDC A,%d",2); s(0x36,"ADDC A,@R0",1); s(0x37,"ADDC A,@R1",1)
for r in range(8): s(0x38+r,f"ADDC A,R{r}",1)
s(0x40,"JC %r",2); s(0x41,"AJMP",2); s(0x42,"ORL %d,A",2); s(0x43,"ORL %d,#%i",3)
s(0x44,"ORL A,#%i",2); s(0x45,"ORL A,%d",2); s(0x46,"ORL A,@R0",1); s(0x47,"ORL A,@R1",1)
for r in range(8): s(0x48+r,f"ORL A,R{r}",1)
s(0x50,"JNC %r",2); s(0x51,"ACALL",2); s(0x52,"ANL %d,A",2); s(0x53,"ANL %d,#%i",3)
s(0x54,"ANL A,#%i",2); s(0x55,"ANL A,%d",2); s(0x56,"ANL A,@R0",1); s(0x57,"ANL A,@R1",1)
for r in range(8): s(0x58+r,f"ANL A,R{r}",1)
s(0x60,"JZ %r",2); s(0x61,"AJMP",2); s(0x62,"XRL %d,A",2); s(0x63,"XRL %d,#%i",3)
s(0x64,"XRL A,#%i",2); s(0x65,"XRL A,%d",2); s(0x66,"XRL A,@R0",1); s(0x67,"XRL A,@R1",1)
for r in range(8): s(0x68+r,f"XRL A,R{r}",1)
s(0x70,"JNZ %r",2); s(0x71,"ACALL",2); s(0x72,"ORL C,%b",2); s(0x73,"JMP @A+DPTR",1)
s(0x74,"MOV A,#%i",2); s(0x75,"MOV %d,#%i",3); s(0x76,"MOV @R0,#%i",2); s(0x77,"MOV @R1,#%i",2)
for r in range(8): s(0x78+r,f"MOV R{r},#%i",2)
s(0x80,"SJMP %r",2); s(0x81,"AJMP",2); s(0x82,"ANL C,%b",2); s(0x83,"MOVC A,@A+PC",1)
s(0x84,"DIV AB",1); s(0x85,"MOV %d,%d2",3); s(0x86,"MOV %d,@R0",2); s(0x87,"MOV %d,@R1",2)
for r in range(8): s(0x88+r,f"MOV %d,R{r}",2)
s(0x90,"MOV DPTR,#%I",3); s(0x91,"ACALL",2); s(0x92,"MOV %b,C",2); s(0x93,"MOVC A,@A+DPTR",1)
s(0x94,"SUBB A,#%i",2); s(0x95,"SUBB A,%d",2); s(0x96,"SUBB A,@R0",1); s(0x97,"SUBB A,@R1",1)
for r in range(8): s(0x98+r,f"SUBB A,R{r}",1)
s(0xA0,"ORL C,/%b",2); s(0xA1,"AJMP",2); s(0xA2,"MOV C,%b",2); s(0xA3,"INC DPTR",1)
s(0xA4,"MUL AB",1); s(0xA5,"db A5",1); s(0xA6,"MOV @R0,%d",2); s(0xA7,"MOV @R1,%d",2)
for r in range(8): s(0xA8+r,f"MOV R{r},%d",2)
s(0xB0,"ANL C,/%b",2); s(0xB1,"ACALL",2); s(0xB2,"CPL %b",2); s(0xB3,"CPL C",1)
s(0xB4,"CJNE A,#%i,%r",3); s(0xB5,"CJNE A,%d,%r",3); s(0xB6,"CJNE @R0,#%i,%r",3); s(0xB7,"CJNE @R1,#%i,%r",3)
for r in range(8): s(0xB8+r,f"CJNE R{r},#%i,%r",3)
s(0xC0,"PUSH %d",2); s(0xC1,"AJMP",2); s(0xC2,"CLR %b",2); s(0xC3,"CLR C",1)
s(0xC4,"SWAP A",1); s(0xC5,"XCH A,%d",2); s(0xC6,"XCH A,@R0",1); s(0xC7,"XCH A,@R1",1)
for r in range(8): s(0xC8+r,f"XCH A,R{r}",1)
s(0xD0,"POP %d",2); s(0xD1,"ACALL",2); s(0xD2,"SETB %b",2); s(0xD3,"SETB C",1)
s(0xD4,"DA A",1); s(0xD5,"DJNZ %d,%r",3); s(0xD6,"XCHD A,@R0",1); s(0xD7,"XCHD A,@R1",1)
for r in range(8): s(0xD8+r,f"DJNZ R{r},%r",2)
s(0xE0,"MOVX A,@DPTR",1); s(0xE1,"AJMP",2); s(0xE2,"MOVX A,@R0",1); s(0xE3,"MOVX A,@R1",1)
s(0xE4,"CLR A",1); s(0xE5,"MOV A,%d",2); s(0xE6,"MOV A,@R0",1); s(0xE7,"MOV A,@R1",1)
for r in range(8): s(0xE8+r,f"MOV A,R{r}",1)
s(0xF0,"MOVX @DPTR,A",1); s(0xF1,"ACALL",2); s(0xF2,"MOVX @R0,A",1); s(0xF3,"MOVX @R1,A",1)
s(0xF4,"CPL A",1); s(0xF5,"MOV %d,A",2); s(0xF6,"MOV @R0,A",1); s(0xF7,"MOV @R1,A",1)
for r in range(8): s(0xF8+r,f"MOV R{r},A",1)

SFR={0x80:"P0",0x81:"SP",0x82:"DPL",0x83:"DPH",0x87:"PCON",0x88:"TCON",0x89:"TMOD",
0x8A:"TL0",0x8B:"TL1",0x8C:"TH0",0x8D:"TH1",0x90:"P1",0x98:"SCON",0x99:"SBUF",
0xA0:"P2",0xA8:"IE",0xB0:"P3",0xB8:"IP",0xD0:"PSW",0xE0:"ACC",0xF0:"B"}
BIT={0x98:"RI",0x99:"TI",0x9A:"RB8",0x9B:"TB8",0xAC:"ES",0xA8:"EX0",0xAA:"EX1",
0xAF:"EA",0xB2:"P3.2",0xB4:"P3.4",0xB5:"P3.5",0xB7:"P3.7",0xB0:"P3.0",0x8E:"TR1",0x8F:"TF1"}
def d(a): return SFR.get(a,f"0x{a:02X}")
def b(a):
    if a in BIT: return BIT[a]
    return f"{d(a&0xF8)}.{a&7}" if (a&0xF8) in SFR else f"bit0x{a:02X}"

def disasm(data, start, end):
    a=start; out=[]
    while a<end:
        op=data[a]; mn,ln=T.get(op,("db 0x%02X"%op,1))
        bs=data[a:a+ln]; txt=mn
        if op in (0x01,0x11,0x21,0x31,0x41,0x51,0x61,0x71,0x81,0x91,0xA1,0xB1,0xC1,0xD1,0xE1,0xF1):
            tgt=((a+2)&0xF800)|((op>>5)<<8)|data[a+1]
            txt=("ACALL" if op&0xF==1 else "AJMP")+f" 0x{tgt:04X}"
        elif "%I" in txt:
            txt=txt.replace("%I",f"0x{(data[a+1]<<8)|data[a+2]:04X}")
        elif "a16" in txt:
            txt=txt.replace("a16",f"0x{(data[a+1]<<8)|data[a+2]:04X}")
        else:
            if op==0x85:
                txt=f"MOV {d(data[a+2])},{d(data[a+1])}"
            else:
                if "%b" in txt and "%r" in txt:
                    txt=txt.replace("%b",b(data[a+1])).replace("%r",f"0x{a+3+((data[a+2]^0x80)-0x80):04X}")
                elif "%d" in txt and "%r" in txt and "%i" in txt:
                    txt=txt.replace("%i",f"0x{data[a+1]:02X}").replace("%r",f"0x{a+3+((data[a+2]^0x80)-0x80):04X}")
                elif "%d" in txt and "%r" in txt:
                    txt=txt.replace("%d",d(data[a+1])).replace("%r",f"0x{a+3+((data[a+2]^0x80)-0x80):04X}")
                elif txt.count("%i")==1 and "%r" in txt and op in (0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF):
                    txt=txt.replace("%i",f"0x{data[a+1]:02X}").replace("%r",f"0x{a+3+((data[a+2]^0x80)-0x80):04X}")
                else:
                    if "%d" in txt and "%i" in txt:
                        txt=txt.replace("%d",d(data[a+1])).replace("%i",f"0x{data[a+2]:02X}")
                    elif "%d" in txt:
                        txt=txt.replace("%d",d(data[a+1]))
                    if "%i" in txt:
                        txt=txt.replace("%i",f"0x{data[a+1]:02X}")
                    if "%b" in txt:
                        txt=txt.replace("%b",b(data[a+1]))
                    if "%r" in txt:
                        rel=data[a+ln-1]; txt=txt.replace("%r",f"0x{a+ln+((rel^0x80)-0x80):04X}")
        out.append(f"  {a:04X}: {bs.hex(' '):<10}  {txt}")
        a+=ln
    return "\n".join(out)

if __name__=="__main__":
    d_=open('df282_extract/Df31f2.82','rb').read()[:0x4000]
    for name,st,en in eval(sys.argv[1]):
        print(f"==== {name}  0x{st:04X}-0x{en:04X} ====")
        print(disasm(d_,st,en)); print()
