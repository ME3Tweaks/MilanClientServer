;; MACROS

alignstack macro
    mov r8, 16
    xor rdx, rdx
    mov rax, rsp
    idiv r8             ; write remainder of 'rax / r8' (i.e. 'rsp / 16') division into rdx

    test rdx, rdx       ; set zf if rdx is zero, i.e. if rsp was 16-byte aligned

    mov r9, 0
    mov r8, 8          ; by default, assign number of bytes to subtract from rsp to 8
    cmovz r8, r9       ; if zf was set, reset number of bytes to subtract from rsp to 0
    sub rsp, r8

    xor rax, rax
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

UActorChannelReplicateActor1_actor DQ ?
UActorChannelReplicateActor2_bunch DQ ?
UActorChannelReplicateActor3_bunch DQ ?
UActorChannelReplicateActor3_prop DQ ?
UGameEngineLoadMap1_url DQ ?


;; FUNCTIONS

.code

externdef MlnUActorChannelReplicateActor1_Handler:proc
MlnUActorChannelReplicateActor1 proc
    mov UActorChannelReplicateActor1_actor, rdx  ; context channel's AActor pointer

    storeall
    mov rbp, rsp
    sub rsp, 20h
    alignstack

    mov rcx, UActorChannelReplicateActor1_actor
    call MlnUActorChannelReplicateActor1_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUActorChannelReplicateActor1 endp

externdef MlnUActorChannelReplicateActor2_Handler:proc
MlnUActorChannelReplicateActor2 proc
    lea rax, [rbp + 10h]  ; currently serializing local FOutBunch
    mov UActorChannelReplicateActor2_bunch, rax

    storeall
    mov rbp, rsp
    sub rsp, 20h
    alignstack

    mov rcx, UActorChannelReplicateActor2_bunch
    call MlnUActorChannelReplicateActor2_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUActorChannelReplicateActor2 endp

externdef MlnUActorChannelReplicateActor3_Handler:proc
MlnUActorChannelReplicateActor3 proc
    mov UActorChannelReplicateActor3_prop, rbx  ; currently iterated UProperty pointer
    lea rax, [rbp + 10h]  ; currently serializing local FOutBunch
    mov UActorChannelReplicateActor3_bunch, rax

    storeall
    mov rbp, rsp
    sub rsp, 20h
    alignstack

    mov rdx, UActorChannelReplicateActor3_bunch
    mov rcx, UActorChannelReplicateActor3_prop
    call MlnUActorChannelReplicateActor3_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUActorChannelReplicateActor3 endp

externdef MlnSeamlessTravelTick1_Handler:proc
MlnSeamlessTravelTick1 proc
    storeall
    mov rbp, rsp
    sub rsp, 20h
    alignstack
    
    lea rcx, [r12 + 4h]  ; start of FURL in SeamlessTravel
    call MlnSeamlessTravelTick1_Handler

    mov rsp, rbp
    restoreall
    ret
MlnSeamlessTravelTick1 endp

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
    mov UGameEngineLoadMap1_url, rdx  ; save FURL pointer from RDX

    storeall
    mov rbp, rsp
    sub rsp, 20h
    alignstack

    mov rcx, UGameEngineLoadMap1_url
    call MlnUGameEngineLoadMap1_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUGameEngineLoadMap1 endp

end
