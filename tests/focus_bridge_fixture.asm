option casemap:none
EXTERN argentFocusBridge:PROC
PUBLIC bridgeFixtureResume
.code
bridgeFixture PROC
 push rbp
 push r15
 push rsi
 sub rsp, 100h
 lea rbp, [rsp+90h]
 mov r15, 1234h
 mov rsi, 5678h
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
 jmp argentFocusBridge
bridgeFixtureResume::
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
 pmovmskb eax, xmm5
 cmp eax, 0ffffh
 jne failed
 xor eax, eax
 jmp finished
failed:
 mov eax, 1
finished:
 add rsp, 100h
 pop rsi
 pop r15
 pop rbp
 ret
bridgeFixture ENDP
END

