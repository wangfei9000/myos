# Makefile

这个 `Makefile` 做三件事：把源码编成 `boot.elf`，检查它有没有 Multiboot 头，再转成 QEMU 能加载的 `boot32.elf` 并启动。

直接运行 `make` 时，默认目标是 `all`，也就是生成 `boot.elf`。

## 变量

`CC` 和 `LD` 分别是 `gcc` 和 `ld`。

`SRCS` 是 6 个 C 文件，`ASMS` 是 3 个汇编文件。`OBJS` 把它们的后缀换成 `.o`，所以链接时用的是 `kmain.o`、`pmm.o`、`task.o`、`idt.o`、`pic.o`、`serial.o`、`boot.o`、`swtch.o`、`trap.o`。

`CFLAGS` 同时用于 C 和汇编：

| 选项 | 作用 |
| --- | --- |
| `-m64` | 生成 64 位代码。`boot.S` 里的 `.code32` 仍会让 `_start` 那段按 32 位编码 |
| `-ffreestanding` | 不依赖宿主 C 库 |
| `-fno-pie` | 地址由链接脚本固定，不生成位置无关代码 |
| `-fno-stack-protector` | 不加栈保护，内核里没有对应的检查函数 |
| `-mno-red-zone` | 不使用 `RSP` 下面的 128 字节。中断帧会压在当前栈顶 |
| `-mgeneral-regs-only` | 只用通用寄存器，不发出 SSE 指令。这里没有打开 `CR4.OSFXSR` |
| `-fno-asynchronous-unwind-tables` | 不生成异常展开表 |
| `-Wall -Wextra` | 打开常见警告 |
| `-c` | 只编译，不链接 |
| `-O1` | 轻度优化 |

`LDFLAGS` 用 `linker.ld` 做 64 位链接。`-z max-page-size=0x1000` 让装载段按 4KB 对齐，Multiboot 头才能留在文件前 8192 字节里。`-z noexecstack` 声明栈不可执行，`--build-id=none` 不插入构建编号。

## 规则

`.PHONY` 里的 `all`、`check`、`run`、`clean` 是动作名，不是要生成的文件。

```makefile
%.o: %.c kernel.h
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -o $@ $<

boot.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
```

`%.o: %.c kernel.h` 表示每个 `.c` 都编译成同名 `.o`，并且依赖 `kernel.h`。改了头文件，六个 C 文件都会重编。`$<` 是源文件，`$@` 是目标 `.o`。汇编规则同样用 `gcc` 调用汇编器，不依赖 `kernel.h`。

`boot.elf` 依赖全部 `.o` 和 `linker.ld`。其中一个变了，就重新链接。

`make check` 先保证 `boot.elf` 是新的，再运行 `grub-file --is-x86-multiboot`。头合法时没有输出，退出码为 0。

`boot32.elf` 用 `objcopy -O elf32-i386` 把同一个映像装进 32 位 ELF。QEMU 的 `-kernel` 只接受这种格式；`entry64` 之后的机器码仍然是 64 位。

`make run` 会先做出 `boot32.elf`，然后执行：

```text
qemu-system-x86_64 -kernel boot32.elf -display curses -serial file:serial.log -no-reboot
```

这台机器没有图形界面，`-display gtk` 会报 `gtk initialization failed`。`-display curses` 把写进 `0xB8000` 的文字画在当前终端里。COM1 写到 `serial.log`，不和 curses 抢终端。`-no-reboot` 让内核停机后 QEMU 也停下。退出用 Esc，再按 2，退出 curses；或者直接 Ctrl-C。

`make clean` 删掉全部 `.o`、`boot.elf` 和 `boot32.elf`。
