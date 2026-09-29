option casemap:none
EXTERN argentMeathookTargetView:PROC
EXTERN argentMeathookResume:QWORD
.code
argentMeathookBridge PROC
    ; FindTarget has completed native eye origin and view-angle accessors.
    ; r13=weapon, r14=owner, rbp+20h=origin, rbp+90h=idAngles.
    ; XMM6/EBX cache the origin XY/Z and must match the replaced stack values.
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
    mov rdx, r14
    lea r8, [rbp+20h]
    lea r9, [rbp+90h]
    call argentMeathookTargetView
    test al, al
    jz nativeView
    movsd xmm6, QWORD PTR [rbp+20h]
    mov ebx, DWORD PTR [rbp+28h]
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
    jmp QWORD PTR [argentMeathookResume]
argentMeathookBridge ENDP
END
