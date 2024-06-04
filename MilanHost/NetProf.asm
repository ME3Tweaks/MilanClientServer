;; MACROS

alignstack macro
    mov r8, 16
    xor rdx, rdx
    mov rax, rsp
    idiv r8             ; write remainder of 'rax / r8' (i.e. 'rsp / 16') division into rdx

    test rdx, rdx       ; set zf if rdx is zero, i.e. if rsp was 16-byte aligned

    mov r9, 0
    mov rax, 8          ; by default, assign number of bytes to subtract from rsp to 8
    cmovz rax, r9       ; if zf was set, reset number of bytes to subtract from rsp to 0
    sub rsp, rax

    xor rcx, rcx        ; zero out all 'argument' registers we just used
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
endm

storeall macro
    pushfq
    push rbp
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
endm

restoreall macro
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rbp
    popfq
endm


;; VARIABLES

.data

UGameEngineLoadMap1_rdx DQ ?


;; FUNCTIONS

.code

externdef MlnUGameEngineTick1_Handler:proc
MlnUGameEngineTick1 proc
    storeall
    mov rbp, rsp
    sub rsp, 20h
    alignstack

    call MlnUGameEngineTick1_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUGameEngineTick1 endp

externdef MlnUGameEngineLoadMap1_Handler:proc
MlnUGameEngineLoadMap1 proc
    mov UGameEngineLoadMap1_rdx, rdx  ; save FURL pointer from RDX

    storeall
    mov rbp, rsp
    sub rsp, 20h
    alignstack

    mov rcx, UGameEngineLoadMap1_rdx
    call MlnUGameEngineLoadMap1_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUGameEngineLoadMap1 endp

end
