# 系统调用（system call）

## 用户调用过程

User: 用户调用 -> ... -> usys.S

最终通过 `ecall` 切换到特权模式

usys.S 代码

```riscv
kernel_func:
 li a7, SYS_kernel_func
 ecall
 ret
```

## ecall 跳到了哪里？

ecall 跳转时会关中断。

调转到 `stvec` 寄存器指向的指令：`trampoline.S` 的 usertrap 处。
做好相关处理后跳转至 `trap.c` 中的 `usertrap()` 函数执行。

## usertrap 大体过程

1. 将 `stvec` 设置为 `kernelvec`，此时如果有中断，则走内核中断流程。

2. 检查 `scause == 8` 后开中断，走系统调用：`syscall()`

3. 通过 `usertrapret()` 返回。

## usertrapret() 大致流程

1. 关中断。
2. 将 `stvec` 重新设为 `trampoline` 中的 `uservec` 处。
3. 恢复栈等上下文信息，`x |= SSTAUTS_SPIE` 在 `sret` 后打开中断。
4. 执行 `trampoline` 中 `userret` 处的代码：恢复上下文相关信息，将返回值存至用户的 a0 寄存器，`sret` 返回调用处。
