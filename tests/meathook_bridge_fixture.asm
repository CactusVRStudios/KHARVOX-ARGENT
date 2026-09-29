option casemap:none
EXTERN argentMeathookBridge:PROC
PUBLIC bridgeFixtureResume
.code
bridgeFixture PROC
 push rbp
 push r13
 push r14
 push rbx
 push r12
 sub rsp, 180h
 lea rbp, [rsp+20h]
 movdqu [rsp+150h], xmm6
 mov r12d, ecx
 mov ebx, 0
 pxor xmm6, xmm6
 mov r13, 1234h
 mov r14, 5678h
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
 jmp argentMeathookBridge
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
 test r12d, r12d
 jz unchanged
 cmp ebx, 40400000h
 jne failed
 movq rax, xmm6
 mov rdx, 400000003f800000h
 cmp rax, rdx
 jne failed
 jmp success
unchanged:
 test ebx, ebx
 jnz failed
 movq rax, xmm6
 test rax, rax
 jnz failed
success:
 xor eax, eax
 jmp finished
failed:
 mov eax, 1
finished:
 movdqu xmm6, [rsp+150h]
 add rsp, 180h
 pop r12
 pop rbx
 pop r14
 pop r13
 pop rbp
 ret
bridgeFixture ENDP
END


