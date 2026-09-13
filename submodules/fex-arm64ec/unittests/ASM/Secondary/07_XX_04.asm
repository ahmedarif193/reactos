%ifdef CONFIG
{
  "RegData": {
    "RAX": "0x0000000080050033",
    "RBX": "0x4142434480050033",
    "RCX": "0x4142434445460033",
    "RDX": "0x4142434445460033",
    "RDI": "0x0000000080050033",
    "RSP": "0x0000000080050033",
    "RBP": "0x0000000080050033",
    "R8":  "0x4142434445460033",
    "R9":  "0x4142434445460033",
    "R10": "0x4142434445460033"
  }
}
%endif

mov rax, 0x4142434445464748
mov rbx, 0x4142434445464748
mov rcx, 0x4142434445464748
mov rdx, 0x4142434445464748
mov rsi, 0xe000_0000
mov [rsi], rdx

mov rdi, 0x4142434445464748
mov rsp, 0x4142434445464748
mov rbp, 0x4142434445464748
mov r8, 0x4142434445464748
mov r9, 0x4142434445464748
mov r10, 0x4142434445464748

; NASM 3.x selects the no-REX.W alias for reg64 and omits the reg16
; operand-size prefix. Encode REX.W cases explicitly and force o16 so this
; test exercises the requested operand sizes, independently of that alias.
db 0x48, 0x0f, 0x01, 0xe0 ; smsw rax
smsw ebx
o16 smsw cx

smsw [rsi]
mov rdx, [rsi]

; REX.W takes precedence over the operand-size override prefix.
db 0x66, 0x48, 0x0f, 0x01, 0xe7 ; o16 smsw rdi
db 0xf3, 0x48, 0x0f, 0x01, 0xe4 ; repe smsw rsp
db 0xf2, 0x48, 0x0f, 0x01, 0xe5 ; repne smsw rbp

db 0x66
o16 smsw r8w
repe o16 smsw r9w
repne o16 smsw r10w

hlt
