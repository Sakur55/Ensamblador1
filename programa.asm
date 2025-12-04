section .data
array dd 5, 2, 8, 1, 9, 3
len equ ($ - array) / 4

section .text
global _start

_start:
mov ecx, len ; B9 06 00 00 00
dec ecx ; 49

externo:
mov esi, 0 ; BE 00 00 00 00
mov edx, 0 ; BA 00 00 00 00

interno:
mov eax, [array + esi*4] ; 8B 04 B5 [addr array]
mov ebx, [array + esi*4 + 4] ; 8B 5C B5 04
cmp eax, ebx ; 39 D8
jle no_intercambiar ; 7E 05

; Intercambiar
mov [array + esi*4], ebx ; 89 1C B5 [addr array]
mov [array + esi*4 + 4], eax ; 89 44 B5 04
mov edx, 1 ; BA 01 00 00 00

no_intercambiar:
inc esi ; 46
cmp esi, ecx ; 39 CE
jl interno ; 7C DF

test edx, edx ; 85 D2
jz fin ; 74 05
dec ecx ; 49
jnz externo ; 75 D3

fin:
int 0x80 ; CD 80
