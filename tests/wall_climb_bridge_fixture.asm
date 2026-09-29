option casemap:none
EXTERN argentWallGateBridge:PROC
EXTERN argentWallImpulseBridge:PROC
PUBLIC wallGateFixtureResume
PUBLIC wallImpulseFixtureResume
.code
wallGateFixture PROC
 push rbp
 push rbx
 push rdi
 push rsi
 push r14
 sub rsp, 100h
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
 cmp rax, rax
 jmp argentWallGateBridge
wallGateFixtureResume::
 jnz gateFailed
 cmp r14, 123400h
 jne gateFailed
 cmp rax, 11h
 jne gateFailed
 cmp rcx, 22h
 jne gateFailed
 cmp rdx, 33h
 jne gateFailed
 cmp r8, 44h
 jne gateFailed
 cmp r9, 55h
 jne gateFailed
 cmp r10, 66h
 jne gateFailed
 cmp r11, 77h
 jne gateFailed
 pmovmskb eax, xmm0
 cmp eax, 0ffffh
 jne gateFailed
 pmovmskb eax, xmm1
 cmp eax, 0ffffh
 jne gateFailed
 pmovmskb eax, xmm2
 cmp eax, 0ffffh
 jne gateFailed
 pmovmskb eax, xmm3
 cmp eax, 0ffffh
 jne gateFailed
 pmovmskb eax, xmm4
 cmp eax, 0ffffh
 jne gateFailed
 pmovmskb eax, xmm5
 cmp eax, 0ffffh
 jne gateFailed
 xor eax, eax
 jmp gateFinished
gateFailed:
 mov eax, 1
gateFinished:
 add rsp, 100h
 pop r14
 pop rsi
 pop rdi
 pop rbx
 pop rbp
 ret
wallGateFixture ENDP
wallImpulseFixture PROC
 push rbp
 push rbx
 push rdi
 push rsi
 push r14
 sub rsp, 100h
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
 cmp rax, rax
 jmp argentWallImpulseBridge
wallImpulseFixtureResume::
 jnz impulseFailed
 cmp rax, 41f00000h ; forward Z = 30.f through the saved EAX lane
 jne impulseFailed
 cmp rcx, 22h
 jne impulseFailed
 cmp rdx, 33h
 jne impulseFailed
 cmp r8, 44h
 jne impulseFailed
 cmp r9, 55h
 jne impulseFailed
 cmp r10, 66h
 jne impulseFailed
 cmp r11, 77h
 jne impulseFailed
 movq rax, xmm0
 mov rcx, 41a0000041200000h
 cmp rax, rcx
 jne impulseFailed
 cmp r14, 123401h
 jne impulseFailed
 pmovmskb eax, xmm1
 cmp eax, 0ffffh
 jne impulseFailed
 pmovmskb eax, xmm2
 cmp eax, 0ffffh
 jne impulseFailed
 pmovmskb eax, xmm3
 cmp eax, 0ffffh
 jne impulseFailed
 pmovmskb eax, xmm4
 cmp eax, 0ffffh
 jne impulseFailed
 pmovmskb eax, xmm5
 cmp eax, 0ffffh
 jne impulseFailed
 xor eax, eax
 jmp impulseFinished
impulseFailed:
 mov eax, 1
impulseFinished:
 add rsp, 100h
 pop r14
 pop rsi
 pop rdi
 pop rbx
 pop rbp
 ret
wallImpulseFixture ENDP
END
