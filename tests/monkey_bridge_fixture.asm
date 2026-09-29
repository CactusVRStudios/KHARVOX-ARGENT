option casemap:none
EXTERN argentMonkeyBridge:PROC
PUBLIC monkeyFixtureResume
.code
monkeyFixture PROC
 push rbp
 push rbx
 push rdi
 push rsi
 push r14
 sub rsp, 100h
 movdqu [rsp+0e0h], xmm7
 lea rbp, [rsp+90h]
 mov rdi, 5678h
 mov rbx, 5678h
 mov r14, 123401h
 mov qword ptr [rbp+5fh], 9999h
 mov rax, 11h
 mov rcx, 22h
 mov rdx, 33h
 mov r8, 44h
 mov r9, 55h
 mov r10, 66h
 mov r11, 77h
 pcmpeqd xmm0, xmm0
 movdqa xmm1, xmm0
 movdqa xmm2, xmm0
 movdqa xmm3, xmm0
 movdqa xmm4, xmm0
 movdqa xmm5, xmm0
 movdqa xmm7, xmm0
 cmp rax, rax
 jmp argentMonkeyBridge
monkeyFixtureResume::
 jnz failed
 cmp rax, 11h
 jne failed
 cmp rcx, 22h
 jne failed
 cmp rdx, 33h
 jne failed
 cmp r8, 44h
 jne failed
 cmp r9, 55h
 jne failed
 cmp r10, 66h
 jne failed
 cmp r11, 77h
 jne failed
 pmovmskb eax, xmm0
 cmp eax, 0ffffh
 jne failed
 pmovmskb eax, xmm1
 cmp eax, 0ffffh
 jne failed
 pmovmskb eax, xmm2
 cmp eax, 0ffffh
 jne failed
 pmovmskb eax, xmm3
 cmp eax, 0ffffh
 jne failed
 pmovmskb eax, xmm4
 cmp eax, 0ffffh
 jne failed
 movd eax, xmm5
 cmp eax, 41200000h
 jne failed
 movd eax, xmm7
 cmp eax, 41a00000h
 jne failed
 psrldq xmm5, 4
 pmovmskb eax, xmm5
 cmp eax, 0fffh
 jne failed
 psrldq xmm7, 4
 pmovmskb eax, xmm7
 cmp eax, 0fffh
 jne failed
 xor eax, eax
 jmp finished
failed:
 mov eax, 1
finished:
 movdqu xmm7, [rsp+0e0h]
 add rsp, 100h
 pop r14
 pop rsi
 pop rdi
 pop rbx
 pop rbp
 ret
monkeyFixture ENDP
END
