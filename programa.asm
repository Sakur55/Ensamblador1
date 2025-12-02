section .data
resultado dd 0
numero dd 5

section .text
global _start

_start:
mov ecx, [numero]           ;8B 0D [addr numero]
mov eax, 1                  ;B8 01 00 00 00

calcular:
cmp ecx, 1                  ;83 F9 01
jle fin                     ;7E 05
imul eax, ecx               ;0F AF C1
dec ecx                     ;49
jmp calcular                ;EB F4

fin:
mov [resultado], eax        ;A3 [addr resultado]
int 0x80                    ;CD 80