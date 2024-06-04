;; MACROS

storeall macro
    pushfq
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
    popfq
endm


;; VARIABLES

.data

UGameEngineLoadMap1_rdx DQ ?


;; FUNCTIONS

.code

MlnUGameEngineTick1 proc
    ret
MlnUGameEngineTick1 endp

externdef MlnUGameEngineLoadMap1_Handler:proc
MlnUGameEngineLoadMap1 proc
    mov UGameEngineLoadMap1_rdx, rdx  ; save FURL pointer from RDX
    storeall
    sub rsp, 28h
    mov rcx, UGameEngineLoadMap1_rdx
    call MlnUGameEngineLoadMap1_Handler
    add rsp, 28h
    restoreall
    ret
MlnUGameEngineLoadMap1 endp

end
