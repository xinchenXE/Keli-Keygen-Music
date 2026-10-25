; =====================================================================
;  mixer.asm - hand-written 32-bit Paula engine
;  4 voices, 32.32 phase accumulator, linear interpolation, stereo bleed
;  Replaces paula.c in the smallest build.
; =====================================================================

bits 32
default rel

global _paulaInit, _paulaClose, _paulaSetMasterVolume, _paulaSetStereoSeparation
global _paulaSetPeriod, _paulaSetVolume, _paulaSetLength, _paulaSetData
global _paulaSetDMACON, _paulaMixSamples, _paulaTogglePause
global _resetAudioDithering, _paulaOutputSamples, _audio

%define V_PTR    0
%define V_LEN    4
%define V_POS    8
%define V_PHASE  12
%define V_INC    20
%define V_VOL    28
%define V_ACT    32
%define V_SIZE   36

section .text

_paulaInit:
    mov eax, 1
    ret

_paulaClose:
_paulaTogglePause:
_resetAudioDithering:
_paulaOutputSamples:
_paulaSetMasterVolume:
    ret

_paulaSetStereoSeparation:
    mov eax, [esp+4]
    mov [sep], eax
    ret

; void paulaSetPeriod(int ch, uint16 period)
;   inc = (3546895 << 32) / (44100 * period)       (32.32 bytes per sample)
_paulaSetPeriod:
    mov edx, [esp+8]
    test edx, edx
    jnz .nz
    mov edx, 65535
.nz:
    cmp edx, 113
    jae .ok
    mov edx, 113
.ok:
    imul edx, 44100
    mov ecx, edx                ; divisor
    mov eax, 0
    mov edx, 3546895
    div ecx                     ; edx:eax / ecx
    mov ecx, [esp+4]
    imul ecx, V_SIZE
    mov [voices + ecx + V_INC], eax
    mov dword [voices + ecx + V_INC + 4], 0
    ret

; void paulaSetVolume(int ch, uint16 vol)
_paulaSetVolume:
    mov edx, [esp+8]
    and edx, 127
    cmp edx, 64
    jbe .ok
    mov edx, 64
.ok:
    mov ecx, [esp+4]
    imul ecx, V_SIZE
    mov [voices + ecx + V_VOL], edx
    ret

; void paulaSetLength(int ch, uint16 len)
_paulaSetLength:
    mov ecx, [esp+4]
    imul ecx, V_SIZE
    movzx edx, word [esp+8]
    test edx, edx
    jnz .ok
    mov edx, 1
.ok:
    mov [voices + ecx + V_LEN], edx
    mov dword [voices + ecx + V_POS], 0
    ret

; void paulaSetData(int ch, const int8 *src)
_paulaSetData:
    mov ecx, [esp+4]
    imul ecx, V_SIZE
    mov edx, [esp+8]
    test edx, edx
    jnz .hp
    lea edx, [nullsample]
.hp:
    mov [voices + ecx + V_PTR], edx
    mov dword [voices + ecx + V_POS], 0
    mov dword [voices + ecx + V_PHASE], 0
    mov dword [voices + ecx + V_PHASE + 4], 0
    ret

; void paulaSetDMACON(uint16 bits)
_paulaSetDMACON:
    movzx eax, word [esp+4]
    mov edx, eax
    and edx, 0x8000
    xor ecx, ecx
.loop:
    test al, 1
    jz .next
    test edx, edx
    jz .off
    cmp dword [voices + ecx + V_PTR], 0
    jne .hp
    mov dword [voices + ecx + V_PTR], nullsample
.hp:
    mov dword [voices + ecx + V_ACT], 1
    mov dword [voices + ecx + V_PHASE], 0
    mov dword [voices + ecx + V_PHASE + 4], 0
    jmp .next
.off:
    mov dword [voices + ecx + V_ACT], 0
.next:
    shr eax, 1
    add ecx, V_SIZE
    cmp ecx, 4 * V_SIZE
    jb .loop
    ret

; ---------------------------------------------------------------------
; void paulaMixSamples(int16 *out, uint32 n)
;   locals: left -16  right -20  voice -24  count -28  scratch -32
; ---------------------------------------------------------------------
_paulaMixSamples:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 40

    mov edi, [ebp+8]
    mov eax, [ebp+12]
    test eax, eax
    jz .done
    mov [ebp-28], eax

.sample:
    mov dword [ebp-16], 0
    mov dword [ebp-20], 0
    mov dword [ebp-24], 0
    mov esi, voices

.voice:
    cmp dword [esi+V_ACT], 0
    je .vnext

    mov edx, [esi+V_PTR]
    mov ebx, [esi+V_POS]
    mov ecx, [esi+V_PHASE]
    shr ecx, 16
    and ecx, 0xFFFF             ; frac

    movsx eax, byte [edx+ebx]   ; s0
    inc ebx
    cmp ebx, [esi+V_LEN]
    jb .nw
    xor ebx, ebx
.nw:
    movsx ebx, byte [edx+ebx]   ; s1
    sub ebx, eax
    imul ebx, ecx
    sar ebx, 16
    add eax, ebx
    imul eax, [esi+V_VOL]

    mov ecx, [ebp-24]
    cmp ecx, 1
    je .r
    cmp ecx, 2
    je .r
    add [ebp-16], eax
    jmp .adv
.r:
    add [ebp-20], eax

.adv:
    mov eax, [esi+V_PHASE]
    add eax, [esi+V_INC]
    mov [esi+V_PHASE], eax
    mov eax, [esi+V_PHASE+4]
    adc eax, [esi+V_INC+4]
    mov [esi+V_PHASE+4], eax
    test eax, eax
    jz .np
    sub dword [esi+V_PHASE+4], 1
    mov ebx, [esi+V_POS]
    inc ebx
    cmp ebx, [esi+V_LEN]
    jb .sp
    xor ebx, ebx
.sp:
    mov [esi+V_POS], ebx
.np:

.vnext:
    add esi, V_SIZE
    inc dword [ebp-24]
    cmp dword [ebp-24], 4
    jb .voice

    ; --- stereo bleed ---
    mov eax, [ebp-16]
    mov ecx, [ebp-20]
    mov edx, [sep]
    test edx, edx
    jz .clamp
    mov ebx, ecx
    sub ebx, eax
    imul ebx, edx
    mov eax, ebx
    cdq
    mov ebx, 100
    idiv ebx
    add eax, [ebp-16]           ; left = l + (r-l)*sep/100
    mov [ebp-32], eax

    mov eax, [ebp-16]
    mov ecx, [ebp-20]
    mov edx, [sep]
    mov ebx, eax
    sub ebx, ecx
    imul ebx, edx
    mov eax, ebx
    cdq
    mov ebx, 100
    idiv ebx
    add eax, ecx
    mov ecx, eax                ; right
    mov eax, [ebp-32]

.clamp:
    cmp eax, 32767
    jle .c1
    mov eax, 32767
.c1:
    cmp eax, -32768
    jge .c2
    mov eax, -32768
.c2:
    cmp ecx, 32767
    jle .c3
    mov ecx, 32767
.c3:
    cmp ecx, -32768
    jge .c4
    mov ecx, -32768
.c4:
    mov [edi], ax
    mov [edi+2], cx
    add edi, 4

    dec dword [ebp-28]
    jnz .sample

.done:
    add esp, 40
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .data
align 8
_audio:
    dd 0, 44100, 256, 20, 0, 883
    dq 0
    dq 0

nullsample:
    times 1024 db 0

section .bss
voices:  resb 4 * V_SIZE
sep:     resd 1
