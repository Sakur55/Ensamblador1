section .data
fib0 dd 0
fib1 dd 1
n dd 10

section .text
global _start

_start:
mov ecx, [n] ; 8B 0D [addr n]
mov eax, [fib0] ; A1 [addr fib0]
mov ebx, [fib1] ; 8B 1D [addr fib1]

bucle_fib:
mov edx, eax ; 89 C2
add edx, ebx ; 01 DA
mov eax, ebx ; 89 D8
mov ebx, edx ; 89 D3

; Aquí iría código para imprimir
loop bucle_fib ; E2 F2

int 0x80 ; CD 80
