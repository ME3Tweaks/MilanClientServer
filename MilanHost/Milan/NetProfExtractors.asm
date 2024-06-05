;; MACROS

initstack macro space
    sub rsp, space
    and rsp, -0Fh
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

UChannelSendBunch1_bunch                DQ ?
UChannelSendBunch2_bunch                DQ ?
UActorChannelReplicateActor1_actor      DQ ?
UActorChannelReplicateActor2_bunch      DQ ?
UActorChannelReplicateActor3_bunch      DQ ?
UActorChannelReplicateActor3_prop       DQ ?
InternalReplicateFunction1_chan         DQ ?
InternalReplicateFunction1_bunch        DQ ?
InternalReplicateFunction1_func         DQ ?
UGameEngineLoadMap1_url                 DQ ?


;; FUNCTIONS

.code

externdef MlnUChannelSendBunch1_Handler:proc
MlnUChannelSendBunch1 proc
    mov UChannelSendBunch1_bunch, rcx  ; context pointer to the out bunch serializing merged bits

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UChannelSendBunch1_bunch
    call MlnUChannelSendBunch1_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUChannelSendBunch1 endp

externdef MlnUChannelSendBunch2_Handler:proc
MlnUChannelSendBunch2 proc
    mov UChannelSendBunch2_bunch, rbx  ; pointer to the local serializing bunch

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UChannelSendBunch2_bunch
    call MlnUChannelSendBunch2_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUChannelSendBunch2 endp

externdef MlnUActorChannelReplicateActor1_Handler:proc
MlnUActorChannelReplicateActor1 proc
    mov UActorChannelReplicateActor1_actor, rdx  ; context channel's AActor pointer

    storeall
    mov rbp, rsp
    initstack 20h

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
    initstack 20h

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
    initstack 20h

    mov rdx, UActorChannelReplicateActor3_bunch
    mov rcx, UActorChannelReplicateActor3_prop
    call MlnUActorChannelReplicateActor3_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUActorChannelReplicateActor3 endp

externdef MlnInternalReplicateFunction1_Handler:proc
MlnInternalReplicateFunction1 proc
    mov InternalReplicateFunction1_chan, rcx  ; resolved actor channel, since we can't get actor itself here
    mov InternalReplicateFunction1_bunch, rdx  ; currently serializing local out bunch
    mov InternalReplicateFunction1_func, r13

    storeall
    mov rbp, rsp
    initstack 20h

    mov r8, InternalReplicateFunction1_func
    mov rdx, InternalReplicateFunction1_bunch
    mov rcx, InternalReplicateFunction1_chan
    call MlnInternalReplicateFunction1_Handler

    mov rsp, rbp
    restoreall
    ret
MlnInternalReplicateFunction1 endp

externdef MlnSeamlessTravelTick1_Handler:proc
MlnSeamlessTravelTick1 proc
    storeall
    mov rbp, rsp
    initstack 20h
    
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
    initstack 20h

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
    initstack 20h

    mov rcx, UGameEngineLoadMap1_url
    call MlnUGameEngineLoadMap1_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUGameEngineLoadMap1 endp

end
