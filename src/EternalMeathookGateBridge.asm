option casemap:none
EXTERN argentMeathookCandidateView:PROC
EXTERN argentMeathookGateResume:QWORD
.code
argentMeathookGateBridge PROC
    ; Per-candidate check re-reads player eye origin and forward.
    ; r13=weapon, [rbp+b8h]=owner, rbp+30h=eye, rbp+10h=query forward.
    ; Replace saved RAX with the local forward pointer only on success.
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
    mov rcx, r13
    mov rdx, [rbp+0b8h]
    lea r8, [rbp+30h]
    lea r9, [rbp+10h]
    call argentMeathookCandidateView
    test al, al
    jz nativeView
    lea rax, [rbp+10h]
    mov QWORD PTR [rsp+0b0h], rax
nativeView:
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
    jmp QWORD PTR [argentMeathookGateResume]
argentMeathookGateBridge ENDP
END

