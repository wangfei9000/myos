# boot.S

`boot.S` 在 32 位保护模式里执行，分页是关的。它清掉 `.bss`，装上自己的 GDT 和低 1GB 页表，打开长模式，再跳到 64 位的 `entry64` 去调用 `kmain`。文件前半段的 `.set` 只是给汇编器定常数，CPU 不会执行它们。

## Multiboot 头

```text
	.align 4
multiboot_header:
	.long MB_MAGIC
	.long 0
	.long -(MB_MAGIC)
```

`.align 4` 把后面的数据对齐到 4 字节。三行 `.long` 是 Multiboot 头：魔数 `0x1BADB002`、flags 为 0、以及两者的相反数当校验和。三项加起来是 0。QEMU 的 `-kernel` 靠这 12 字节认出内核。这三行是数据，执行流不会走进来。

## 入口

```text
_start:
	cli
	cld
```

`cli` 清掉 `EFLAGS.IF`，后面的准备阶段不再响应可屏蔽中断。`cld` 清掉方向标志，后面的 `stosb`、`stosl` 会让 `EDI` 往高地址走。

```text
	movl %eax, %esi
	movl %ebx, %ebp
	movl $__bss_start, %edi
	movl $_end, %ecx
	subl %edi, %ecx
	xorl %eax, %eax
	rep stosb
	movl %esi, multiboot_magic
	movl %ebp, multiboot_info_phys
```

进来时 `EAX`、`EBX` 还装着引导信息。`-kernel` 时 `EAX` 是 `0x2BADB002`，`EBX` 指向 Multiboot 信息。先把它们放进 `ESI` 和 `EBP`，因为马上要改 `EAX`，后面的 `cpuid` 也会改 `EBX`。

`EDI` 指到 `__bss_start`，`ECX` 先装 `_end`，再减去起点，得到要清的字节数。`xorl %eax, %eax` 把 `EAX` 变成 0。`rep stosb` 从 `ES:EDI` 起写 `ECX` 个 0，清掉未初始化数据、页表和启动栈。此时 `ES` 的基址是 0，所以这个地址就是物理地址。

最后两行把先前保住的魔数和信息地址写进 `multiboot_magic`、`multiboot_info_phys`。磁盘启动没有这个魔数，`pmm_init` 看到后按 128MB 计算内存。

## 换上自己的 GDT 和栈

```text
	lgdt gdt_ptr
	movw $SEL_DATA, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movl $stack_top, %esp
```

`lgdt gdt_ptr` 把下面那张 GDT 的界限和地址装进 `GDTR`。`SEL_DATA` 是 `0x10`，也就是 GDT 里第三项。`AX` 再写入 `DS`、`ES`、`SS`，三个段都变成基址 0、界限 4GB 的数据段。`ESP` 指到 `stack_top`，栈从这个地址向下长。

## 检查 CPU 有没有长模式

```text
	movl $0x80000000, %eax
	cpuid
	cmpl $0x80000001, %eax
	jb no_long_mode
	movl $0x80000001, %eax
	cpuid
	btl $29, %edx
	jnc no_long_mode
```

`EAX` 设成 `0x80000000` 再执行 `cpuid`，返回值是 CPU 支持的最高扩展叶号。`cmpl` 拿它和 `0x80000001` 比，`jb` 在最高叶号更小时跳到 `no_long_mode`。

支持这个叶时，再把 `EAX` 设成 `0x80000001` 执行一次 `cpuid`。长模式标志在 `EDX` 第 29 位。`btl $29, %edx` 把这一位复制到进位标志，`jnc` 在该位为 0 时跳走。

```text
no_long_mode:
	movw $0x4f4e, 0xb8000
	movw $0x4f4f, 0xb8002
	hlt
	jmp no_long_mode
```

`0xB8000` 是文字显存的第一个字符。`0x4f4e` 是红底白字的 `N`，`0x4f4f` 是旁边的 `O`。`hlt` 停住 CPU，`jmp` 再绕回来，于是一直停在这里。

## 建低 1GB 页表

```text
	movl $pml4, %edi
	xorl %eax, %eax
	movl $(3 * 4096 / 4), %ecx
	rep stosl
```

`EDI` 指到 `pml4`。`EAX` 为 0。`ECX` 是 `3072`，也就是三页共 12KB 里的双字个数。`rep stosl` 把 `pml4`、`pdpt`、`pd` 这三页全部写成 0。

```text
	movl $pml4, %edi
	movl $pdpt, %eax
	orl $(PTE_PRESENT | PTE_WRITE), %eax
	movl %eax, (%edi)

	movl $pdpt, %edi
	movl $pd, %eax
	orl $(PTE_PRESENT | PTE_WRITE), %eax
	movl %eax, (%edi)
```

第一段写 `PML4[0]`：值是 `pdpt` 的地址，低两位是 Present 和 Writable，所以这一项指向下一张表，并且可写。第二段同样写 `PDPT[0]`，让它指向 `pd`。页表地址按 4KB 对齐，低 12 位本来是 0，`orl` 只是把标志填进去。

```text
	movl $pd, %edi
	movl $(PTE_PRESENT | PTE_WRITE | PTE_PS), %eax
	movl $512, %ecx
fill_pd:
	movl %eax, (%edi)
	addl $0x200000, %eax
	addl $8, %edi
	decl %ecx
	jnz fill_pd
```

`EDI` 指到页目录 `pd`。`EAX` 一开始是 `0x83`：Present、Writable、PS。PS 表示这一项直接描述 2MB 页，不再往下分。`ECX` 是 512。

循环里 `movl %eax, (%edi)` 写下这一项的低 4 字节，高 4 字节刚才已经清成 0。`addl $0x200000, %eax` 让下一页的物理地址增加 2MB。`addl $8, %edi` 前进到下一项，页目录项是 8 字节。`decl` 和 `jnz` 做满 512 次。512 乘 2MB 等于 1GB，虚拟地址和物理地址相同。

## 打开长模式

```text
	movl %cr4, %eax
	orl $CR4_PAE, %eax
	movl %eax, %cr4

	movl $pml4, %eax
	movl %eax, %cr3

	movl $MSR_EFER, %ecx
	rdmsr
	orl $EFER_LME, %eax
	wrmsr
```

`CR4` 读出来，第 5 位置 1，再写回。这一位是 PAE，4 级页表要用它。

`CR3` 写成 `pml4` 的地址。分页打开后，CPU 从这里找第一张表。

`ECX` 装入 `0xC0000080`，这是 `EFER` 的编号。`rdmsr` 把这个寄存器读进 `EDX:EAX`。`orl` 把 `EAX` 第 8 位 `LME` 置 1，`wrmsr` 把 `EDX:EAX` 写回去。`EDX` 没改，高 32 位保持原值。

```text
	movl $CR0_STATE, %eax
	movl %eax, %cr0
	ljmpl $SEL_CODE64, $entry64
```

`CR0_STATE` 是 `0x80050033`。写进 `CR0` 时第 31 位 `PG` 打开分页，第 0 位 `PE` 保持保护模式，另外还带着 `MP`、`ET`、`NE`、`WP`、`AM`。写完这一条，CPU 已经按长模式查页表，但当前 `CS` 仍是 32 位代码段，指令还按 32 位译码。

`ljmpl $SEL_CODE64, $entry64` 做一次远跳转。`SEL_CODE64` 是 `0x08`，描述符里 `L=1`、`D=0`。从 `entry64` 开始，指令按 64 位译码。

## 64 位入口

```text
	.code64
entry64:
	xorl %eax, %eax
	movl %eax, %ds
	movl %eax, %es
	movl %eax, %ss
	movl $stack_top, %esp
	call kmain
1:
	hlt
	jmp 1b
```

`.code64` 只告诉汇编器：下面按 64 位编码。`xorl %eax, %eax` 得到 0，同时把 `RAX` 的高 32 位也清掉。三个 `movl` 把 `DS`、`ES`、`SS` 设成空选择子。64 位寻址不再用这些段的基址。`movl $stack_top, %esp` 同样把高 32 位清掉，`RSP` 就是 `stack_top`。

`call kmain` 进入 C。`menu` 不返回，所以正常走不到下面。万一返回了，`hlt` 停机，`jmp 1b` 跳回标号 `1`，一直停着。

## 文件后部的数据

```text
multiboot_magic:
	.long 0
multiboot_info_phys:
	.long 0
```

这两个 4 字节变量初始为 0。`_start` 清完 `.bss` 之后把引导信息写进来。

```text
gdt:
	.quad 0
	.quad 0x00af9a000000ffff
	.quad 0x00cf92000000ffff
gdt_end:

gdt_ptr:
	.word gdt_end - gdt - 1
	.long gdt
```

GDT 有三项，每项 8 字节。第一项全 0，是空描述符。第二项选择子 `0x08`，是 64 位代码段：存在、特权级 0、可执行可读，粒度 4KB，`L=1`、`D=0`。第三项选择子 `0x10`，是可写数据段，基址 0，界限盖住 4GB。

`gdt_ptr` 是 `lgdt` 要读的 6 字节。`.word` 是表的字节数减 1。`.long` 是 `gdt` 的线性地址。

最后一行 `.section .note.GNU-stack` 是空的链接器注记，用来标记栈不可执行。CPU 不执行它。
