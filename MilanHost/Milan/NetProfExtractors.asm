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

UChannelSendBunch1_bunch                    DQ ?
UChannelSendBunch2_bunch                    DQ ?
UActorChannelReplicateActor1_actor          DQ ?
UActorChannelReplicateActor2_bunch          DQ ?
UActorChannelReplicateActor3_bunch          DQ ?
UActorChannelReplicateActor3_prop           DQ ?
InternalReplicateFunction1_chan             DQ ?
InternalReplicateFunction1_bunch            DQ ?
InternalReplicateFunction1_func             DQ ?
UWorldNotifyControlMessageHello_conn        DQ ?
UWorldNotifyControlMessageHello_plat        DQ ?
UWorldNotifyControlMessageHello_ver         DQ ?
UWorldNotifyControlMessageHello_minver      DQ ?
UWorldNotifyControlMessageNetspeed_conn     DQ ?
UWorldNotifyControlMessageNetspeed_speed    DQ ?
UWorldNotifyControlMessageNetspeed_clamp    DQ ?
UWorldNotifyControlMessageLogin_conn        DQ ?
UWorldNotifyControlMessageLogin_err         DQ ?
UWorldNotifyControlMessageUses_conn         DQ ?
UWorldNotifyControlMessageUses_pckg         DQ ?
UWorldNotifyControlMessageHave_conn         DQ ?
UWorldNotifyControlMessageHave_guid         DQ ?
UWorldNotifyControlMessageHave_gen          DQ ?
UWorldNotifyControlMessageJoin_conn         DQ ?
UWorldNotifyControlMessageJoin_id           DQ ?
UWorldNotifyControlMessageJoin_err          DQ ?
UWorldNotifyControlMessageUnload_conn       DQ ?
UWorldNotifyControlMessageUnload_guid       DQ ?
UWorldNotifyControlMessageUnknown19_conn    DQ ?
UWorldNotifyControlMessageUnknown19_unkcrc  DQ ?
UWorldNotifyControlMessageUnknown19_bunch   DQ ?
UWorldNotifyControlMessageUnknown21_conn    DQ ?
UWorldNotifyControlMessageUnknown21_guid    DQ ?
UWorldNotifyControlMessageUnknown21_flag    DQ ?
UWorldNotifyControlMessageUnknown21_bunch   DQ ?


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

externdef MlnUTcpNetDriverTickDispatch_Handler:proc
MlnUTcpNetDriverTickDispatch proc
    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, rdi  ; pointer to the fresh connection
    call MlnUTcpNetDriverTickDispatch_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUTcpNetDriverTickDispatch endp

externdef MlnUWorldNotifyControlMessageHello_Handler:proc
MlnUWorldNotifyControlMessageHello proc
    mov rax, [rsp+80h]
    mov UWorldNotifyControlMessageHello_plat, rax    ; PlatformType, [rsp+70h]
    mov rax, [rbp-78h]
    mov UWorldNotifyControlMessageHello_ver, rax     ; RemoteMinVersion
    mov rax, [rsp+88h]
    mov UWorldNotifyControlMessageHello_minver, rax  ; RemoteVersion, [rsp+78h]
    mov UWorldNotifyControlMessageHello_conn, r15    ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageHello_conn
    mov rdx, UWorldNotifyControlMessageHello_plat
    mov r8, UWorldNotifyControlMessageHello_ver
    mov r9, UWorldNotifyControlMessageHello_minver
    call MlnUWorldNotifyControlMessageHello_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageHello endp

extern MlnUWorldNotifyControlMessageNetspeed_Handler:proc
MlnUWorldNotifyControlMessageNetspeed proc
    mov rax, [rbp+130h]
    mov UWorldNotifyControlMessageNetspeed_speed, rax  ; Speed
    mov rax, [r15+70h]
    mov UWorldNotifyControlMessageNetspeed_clamp, rax  ; ClampedSpeed
    mov UWorldNotifyControlMessageNetspeed_conn, r15   ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageNetspeed_conn
    mov rdx, UWorldNotifyControlMessageNetspeed_speed
    mov r8, UWorldNotifyControlMessageNetspeed_clamp
    call MlnUWorldNotifyControlMessageNetspeed_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageNetspeed endp

extern MlnUWorldNotifyControlMessageLogin_Handler:proc
MlnUWorldNotifyControlMessageLogin proc
    lea rax, [rsp+50h]
    mov UWorldNotifyControlMessageLogin_err, rax   ; LoginError, [rsp+40h]
    mov UWorldNotifyControlMessageLogin_conn, r15  ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageLogin_conn
    mov rdx, UWorldNotifyControlMessageLogin_err
    call MlnUWorldNotifyControlMessageLogin_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageLogin endp

extern MlnUWorldNotifyControlMessageUses_Handler:proc
MlnUWorldNotifyControlMessageUses proc
    lea rax, [rbp - 50h]
    mov UWorldNotifyControlMessageUses_pckg, rax  ; Package
    mov UWorldNotifyControlMessageUses_conn, r15  ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageUses_conn
    mov rdx, UWorldNotifyControlMessageUses_pckg
    call MlnUWorldNotifyControlMessageUses_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageUses endp

extern MlnUWorldNotifyControlMessageHave_Handler:proc
MlnUWorldNotifyControlMessageHave proc
    lea rax, [rsp + 60h]
    mov UWorldNotifyControlMessageHave_guid, rax  ; Guid, [rsp + 50h]
    mov rax, [rbp + 130h]
    mov UWorldNotifyControlMessageHave_gen, rax   ; Generation
    mov UWorldNotifyControlMessageHave_conn, r15  ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageHave_conn
    mov rdx, UWorldNotifyControlMessageHave_guid
    mov r8, UWorldNotifyControlMessageHave_gen
    call MlnUWorldNotifyControlMessageHave_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageHave endp

extern MlnUWorldNotifyControlMessageJoin_Handler:proc
MlnUWorldNotifyControlMessageJoin proc
    lea rax, [rsp + 88h]
    mov UWorldNotifyControlMessageJoin_id, rax    ; UniqueId, [rsp + 78h]
    lea rax, [rbp - 60h]
    mov UWorldNotifyControlMessageJoin_err, rax   ; SpawnError
    mov UWorldNotifyControlMessageJoin_conn, r15  ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageJoin_conn
    mov rdx, UWorldNotifyControlMessageJoin_id
    mov r8, UWorldNotifyControlMessageJoin_err
    call MlnUWorldNotifyControlMessageJoin_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageJoin endp

extern MlnUWorldNotifyControlMessageUnload_Handler:proc
MlnUWorldNotifyControlMessageUnload proc
    lea rax, [rsp + 50h]
    mov UWorldNotifyControlMessageUnload_guid, rax  ; Guid, [rsp + 40h]
    mov UWorldNotifyControlMessageUnload_conn, r15  ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageUnload_conn
    mov rdx, UWorldNotifyControlMessageUnload_guid
    call MlnUWorldNotifyControlMessageUnload_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageUnload endp

extern MlnUWorldNotifyControlMessageUnknown19_Handler:proc
MlnUWorldNotifyControlMessageUnknown19 proc
    mov rax, [rbp + 130h]
    mov UWorldNotifyControlMessageUnknown19_unkcrc, rax  ; Crc
    mov UWorldNotifyControlMessageUnknown19_bunch, rbx   ; Bunch
    mov UWorldNotifyControlMessageUnknown19_conn, r15    ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageUnknown19_conn
    mov rdx, UWorldNotifyControlMessageUnknown19_unkcrc
    mov r8, UWorldNotifyControlMessageUnknown19_bunch
    call MlnUWorldNotifyControlMessageUnknown19_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageUnknown19 endp

extern MlnUWorldNotifyControlMessageUnknown21_Handler:proc
MlnUWorldNotifyControlMessageUnknown21 proc
    lea rax, [rsp + 70h]
    mov UWorldNotifyControlMessageUnknown21_guid, rax   ; Guid, [rsp + 60h]
    mov rax, [rbp + 130h]
    mov UWorldNotifyControlMessageUnknown21_flag, rax   ; Flag
    mov UWorldNotifyControlMessageUnknown21_bunch, rbx  ; Bunch
    mov UWorldNotifyControlMessageUnknown21_conn, r15   ; Connection

    storeall
    mov rbp, rsp
    initstack 20h

    mov rcx, UWorldNotifyControlMessageUnknown21_conn
    mov rdx, UWorldNotifyControlMessageUnknown21_guid
    mov r8, UWorldNotifyControlMessageUnknown21_flag
    mov r9, UWorldNotifyControlMessageUnknown21_bunch
    call MlnUWorldNotifyControlMessageUnknown21_Handler

    mov rsp, rbp
    restoreall
    ret
MlnUWorldNotifyControlMessageUnknown21 endp

end
