# mbr.S

`mbr.S` 是磁盘第一个扇区。BIOS 把它载入 `0x7C00`，CPU 此时是 16 位实模式。它打开 A20，从 LBA 1 起把 `kernel.bin` 读进来，拷到 `0x100000`，再进入 32 位保护模式，跳到内核的 `_start`。

`KERNEL_SECTORS` 和 `KERNEL_ENTRY` 由 `Makefile` 在编译时传入。前者是 `kernel.bin` 占多少扇区，后者是 `boot.elf` 的入口地址。

## 入口

```text
_start:
	cli
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw $0x7c00, %sp
	movb %dl, drive
	lgdt gdt_desc
```

`cli` 关掉可屏蔽中断。`xorw %ax, %ax` 得到 0，再写入 `DS`、`ES`、`SS`。实模式的段基址是段寄存器乘 16，所以这三个段的基址都是 0，后面的地址就是物理地址。

`SP` 设成 `0x7C00`。栈从引导扇区下面向下长，不会盖住扇区里的代码。

BIOS 把启动盘号放在 `DL`。`movb %dl, drive` 把它存下来，后面读盘还要用。`lgdt gdt_desc` 装入这张扇区末尾的 GDT。GDT 在 1MB 以下，这时还没开 A20 也能读到。

## 打开 A20

```text
	inb $0x92, %al
	orb $2, %al
	outb %al, $0x92
```

从端口 `0x92` 读出一个字节，把第 1 位置 1，再写回去。这一位打开 A20。不打开的话，第 20 根地址线被固定成 0，`0x100000` 会绕回地址 0，内核拷不到 1MB。

## 确认 BIOS 能按 LBA 读盘

```text
	movb $0x41, %ah
	movw $0x55aa, %bx
	movb drive, %dl
	int $0x13
	jc fail
	cmpw $0xaa55, %bx
	jne fail
```

`AH` 是 `0x41`，这是 INT 13h 的扩展读盘安装检查。`BX` 必须是 `0x55AA`，`DL` 是驱动器号。`int $0x13` 调用 BIOS。

进位标志为 1 表示调用失败，`jc fail` 跳去打印 `E`。成功时 BIOS 把 `BX` 改成 `0xAA55`。`cmpw` 和 `jne` 发现不是这个值也去 `fail`。

## 按块把内核读到 0x8000

```text
	movl $1, lba
	movl $0x100000, dest
	movw $KERNEL_SECTORS, left
```

`lba` 从 1 开始，因为 LBA 0 是这个引导扇区自己。`dest` 是内核要放到的物理地址 `0x100000`。`left` 是还没读的扇区数。

```text
load_loop:
	cmpw $0, left
	je enter_pm
	movw left, %ax
	cmpw $32, %ax
	jbe have_count
	movw $32, %ax
have_count:
	movw %ax, chunk
	movw %ax, dap_count
	movw $0x8000, dap_off
	movw $0, dap_seg
	movl lba, %eax
	movl %eax, dap_lba
	movl $0, dap_lba_hi
	movb $0x42, %ah
	movw $dap, %si
	movb drive, %dl
	int $0x13
	jc fail
```

`left` 已经是 0 就去 `enter_pm`，读盘结束。否则把剩余扇区数放进 `AX`，和 32 比较。大于 32 就只读 32 个扇区。32 个扇区是 16KB，临时缓冲区放在 `0x8000`，读完不会越过 1MB。

`chunk` 和 `dap_count` 都写成这次要读的扇区数。`dap_off` 是 `0x8000`，`dap_seg` 是 0，所以 BIOS 把数据放到物理地址 `0x8000`。`dap_lba` 是当前起始 LBA，高 32 位写 0。

`AH` 设成 `0x42`，这是扩展读扇区。`SI` 指向下面的 DAP 包，`DL` 是驱动器号。`int $0x13` 读盘，失败则进位并跳到 `fail`。

```text
	call copy_chunk
	movzwl chunk, %eax
	addl %eax, lba
	shll $9, %eax
	addl %eax, dest
	movw chunk, %ax
	subw %ax, left
	jmp load_loop
```

`call copy_chunk` 把刚读到 `0x8000` 的数据拷到 `dest`。返回后仍是实模式。

`movzwl chunk, %eax` 把本次扇区数零扩展到 `EAX`。加到 `lba` 上，下一块从后面的扇区继续。`shll $9, %eax` 乘以 512，因为左移 9 位就是乘 512。这个字节数加到 `dest`，下一块写到更高的物理地址。然后从 `left` 里减去本次扇区数，跳回 `load_loop`。

## 在保护模式里搬一块，再回到实模式

实模式不能直接写 1MB 以上的平坦地址，所以每拷一块都短暂进入保护模式。

```text
copy_chunk:
	cli
	movl %cr0, %eax
	orl $1, %eax
	movl %eax, %cr0
	ljmp $0x08, $copy32
```

`cli` 再次关中断。读出 `CR0`，把第 0 位 `PE` 置 1，写回。CPU 进入保护模式，但 `CS` 还是实模式的段。`ljmp $0x08, $copy32` 用 GDT 第 2 项重新装载 `CS`。那一项是 32 位代码段，基址 0，从 `copy32` 开始按 32 位指令执行。

```text
	.code32
copy32:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movl $0x8000, %esi
	movl dest, %edi
	movzwl chunk, %ecx
	shll $7, %ecx
	cld
	rep movsl
	ljmp $0x18, $copy16
```

`.code32` 只告诉汇编器按 32 位编码。`DS` 和 `ES` 装入选择子 `0x10`，也就是基址 0、界限 4GB 的数据段。

`ESI` 是源地址 `0x8000`，`EDI` 是目标 `dest`。`ECX` 先装扇区数，再左移 7 位。一个扇区 512 字节，一次 `movsl` 搬 4 字节，512 除以 4 等于 128，也就是左移 7 位。`cld` 让 `movsl` 往高地址搬。`rep movsl` 把这一块从 `0x8000` 拷到 `dest`。

`ljmp $0x18, $copy16` 跳进 GDT 第 4 项。那是 16 位代码段，`D=0`。这一跳之后指令按 16 位译码，但 `PE` 还是 1，仍然在保护模式里。

```text
	.code16
copy16:
	movl %cr0, %eax
	andb $0xfe, %al
	movl %eax, %cr0
	ljmp $0x0000, $copy_real
copy_real:
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	ret
```

读出 `CR0`，`andb $0xfe, %al` 清掉低字节第 0 位，也就是清掉 `PE`，再写回。CPU 回到实模式。`ljmp $0x0000, $copy_real` 把 `CS` 重新装成段 0。

`DS`、`ES`、`SS` 也清成 0。`ret` 回到 `load_loop` 里 `call copy_chunk` 的下一条。

## 内核读完后进入保护模式

```text
enter_pm:
	cli
	movl %cr0, %eax
	orl $1, %eax
	movl %eax, %cr0
	ljmp $0x08, $pm_entry

	.code32
pm_entry:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movl $0x7c00, %esp
	movl $KERNEL_ENTRY, %eax
	jmp *%eax
```

和 `copy_chunk` 一样先置 `PE`，再远跳到 32 位代码段。`DS`、`ES`、`SS` 设成 `0x10`，`ESP` 设成 `0x7C00`。

`KERNEL_ENTRY` 是内核 `_start` 的地址。`movl` 把它放进 `EAX`，`jmp *%eax` 跳过去。这时还是 32 位保护模式、分页关闭，正好是 `boot.S` 的 `_start` 要求的状态。

## 失败

```text
	.code16
fail:
	movb $0x0e, %ah
	movb $'E', %al
	int $0x10
	hlt
	jmp fail
```

`AH` 是 `0x0E`，INT 10h 的电传打字功能。`AL` 是字符 `E`。`int $0x10` 把它显示到屏幕上。然后 `hlt`，`jmp` 再绕回来。

## 扇区里的变量和 DAP

这些字节跟在代码后面，整个扇区只有 512 字节，所以它们和代码在同一段里。

```text
drive:
	.byte 0
lba:
	.long 1
dest:
	.long 0x100000
chunk:
	.word 0
left:
	.word 0
```

`drive` 保存 BIOS 传来的驱动器号。`lba` 和 `dest` 的初值与入口里写的一样。`chunk` 是当前这一块的扇区数，`left` 是剩余扇区数。

```text
dap:
	.byte 0x10, 0
dap_count:
	.word 0
dap_off:
	.word 0x8000
dap_seg:
	.word 0
dap_lba:
	.long 0
dap_lba_hi:
	.long 0
```

这是 INT 13h `AH=0x42` 要的 16 字节磁盘地址包。第 1 字节 `0x10` 是包的长度，第 2 字节保留为 0。后面依次是扇区数、缓冲区偏移、缓冲区段、LBA 低 32 位、LBA 高 32 位。入口每次读盘前会改扇区数和 LBA。

## GDT

```text
gdt:
	.quad 0
	.quad 0x00cf9a000000ffff
	.quad 0x00cf92000000ffff
	.quad 0x00009a000000ffff
gdt_end:

gdt_desc:
	.word gdt_end - gdt - 1
	.long gdt
```

四项，每项 8 字节。第一项全 0，是空描述符。第二项选择子 `0x08`，是 32 位代码段：存在、特权级 0、可执行可读，粒度 4KB，`D=1`，界限盖住 4GB。第三项选择子 `0x10`，是对应的可写数据段。第四项选择子 `0x18`，是 16 位代码段：界限 64KB，`D=0`，用来从保护模式回到实模式。

`gdt_desc` 是 `lgdt` 要读的 6 字节。`.word` 是表的字节数减 1。`.long` 是 `gdt` 的线性地址，链接后落在 `0x7C00` 附近。

```text
	.org 510
	.word 0xaa55
```

`.org 510` 把位置补到扇区的第 510 字节。最后两个字节是 `0xAA55`，BIOS 靠它承认这是启动扇区。
