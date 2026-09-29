option casemap:none
EXTERN argentMonkeyLaunch:PROC
EXTERN argentMonkeyResume:QWORD
.code
argentMonkeyBridge PROC
    ; Native launch is stack-aligned. Replace only XMM5/XMM7 low floats,
    ; after the previous-velocity fallback, before native force/up-bias.
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 90h
    movdqu [rsp+20h], xmm0
    movdqu [rsp+30h], xmm1
    movdqu [rsp+40h], xmm2
    movdqu [rsp+50h], xmm3
    movdqu [rsp+60h], xmm4
    movdqu [rsp+70h], xmm5
    movdqu [rsp+80h], xmm7
    mov rcx, rdi
    lea rdx, [rsp+70h]
    lea r8, [rsp+80h]
    call argentMonkeyLaunch
    movdqu xmm0, [rsp+20h]
    movdqu xmm1, [rsp+30h]
    movdqu xmm2, [rsp+40h]
    movdqu xmm3, [rsp+50h]
    movdqu xmm4, [rsp+60h]
    movdqu xmm5, [rsp+70h]
    movdqu xmm7, [rsp+80h]
    add rsp, 90h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp QWORD PTR [argentMonkeyResume]
argentMonkeyBridge ENDP
END
