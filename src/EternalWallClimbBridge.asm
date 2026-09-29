option casemap:none
EXTERN argentWallClimbGate:PROC
EXTERN argentWallClimbImpulse:PROC
EXTERN argentWallGateResume:QWORD
EXTERN argentWallImpulseResume:QWORD
.code
argentWallGateBridge PROC
    ; Mid-function hook: preserve all volatile registers and flags. The
    ; native wall-climb stack is already 16-byte aligned.
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 80h
    movdqu [rsp+20h], xmm0
    movdqu [rsp+30h], xmm1
    movdqu [rsp+40h], xmm2
    movdqu [rsp+50h], xmm3
    movdqu [rsp+60h], xmm4
    movdqu [rsp+70h], xmm5
    mov rcx, rdi
    movzx edx, r14b
    call argentWallClimbGate
    mov r14b, al ; replace only the native blocked-direction result
    movdqu xmm0, [rsp+20h]
    movdqu xmm1, [rsp+30h]
    movdqu xmm2, [rsp+40h]
    movdqu xmm3, [rsp+50h]
    movdqu xmm4, [rsp+60h]
    movdqu xmm5, [rsp+70h]
    add rsp, 80h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp QWORD PTR [argentWallGateResume]
argentWallGateBridge ENDP
argentWallImpulseBridge PROC
    ; Mid-function hook: preserve all volatile registers and flags. The
    ; native wall-climb stack is already 16-byte aligned.
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 80h
    movdqu [rsp+20h], xmm0
    movdqu [rsp+30h], xmm1
    movdqu [rsp+40h], xmm2
    movdqu [rsp+50h], xmm3
    movdqu [rsp+60h], xmm4
    movdqu [rsp+70h], xmm5
    mov rcx, rbx
    lea rdx, [rsp+20h] ; saved XMM0: forward X/Y
    lea r8, [rsp+0b0h] ; saved EAX: forward Z, replaced together with X/Y
    mov r9, [rbp+5fh] ; original jump-function return address
    call argentWallClimbImpulse
    movdqu xmm0, [rsp+20h]
    movdqu xmm1, [rsp+30h]
    movdqu xmm2, [rsp+40h]
    movdqu xmm3, [rsp+50h]
    movdqu xmm4, [rsp+60h]
    movdqu xmm5, [rsp+70h]
    add rsp, 80h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp QWORD PTR [argentWallImpulseResume]
argentWallImpulseBridge ENDP
END
