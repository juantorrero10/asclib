bits 64



global _asm_ctx_switch_64

section .text
; Saves current rbp/rsp into *save_rbp / *save_rsp,
;  then switches to new_rbp/new_rsp and jumps to jmp_address.
;void _asm_ctx_switch_64(
;    ctx_t* new,
;    ctx_t* old
;);

; rdi = new_ctx
; rsi = old_ctx
_asm_ctx_switch_64: 
    mov [rsi+0], rsp       ; save old rsp
    mov [rsi+8], rbp
    mov [rsi+16], rbx
    mov [rsi+24], r12
    mov [rsi+32], r13
    mov [rsi+40], r14
    mov [rsi+48], r15

    mov rsp, [rdi+0]       ; load new rsp
    mov rbp, [rdi+8]
    mov rbx, [rdi+16]
    mov r12, [rdi+24]
    mov r13, [rdi+32]
    mov r14, [rdi+40]
    mov r15, [rdi+48]
    ret


section .note.GNU-stack noalloc noexec nowrite progbits