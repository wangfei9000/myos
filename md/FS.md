# 用户程序

用户程序各自一个 C 文件，和内核链进同一个 `boot.elf`。`kmain` 用一张表引用这些函数。菜单从这张表画出。不读磁盘，不使用 MYFS。

改一个用户程序后要重新链接内核。程序和菜单共用内核栈，跑在内核态。按 `q` 从函数返回，菜单再画一次。

## 文件

| 文件 | 导出的函数 | 菜单 |
| --- | --- | --- |
| `user/help.c` | `help_main` | `1  help` |
| `user/thread_a.c` | `thread_a_main` | `2  run thread_a` |
| `user/thread_b.c` | `thread_b_main` | `3  run thread_b` |

三个函数不能都叫 `main`。它们进同一个 ELF，符号名必须各不相同。声明写在 `kernel.h`。

这些函数没有参数。它们和内核是同一份代码，直接调用 `vga_putc`、`serial_putc`、`kbd_poll`。

## kmain 里的表

```text
struct item {
    char key;
    const char *title;
    void (*fn)(void);
};

static const struct item items[] = {
    { '1', "help", help_main },
    { '2', "run thread_a", thread_a_main },
    { '3', "run thread_b", thread_b_main },
};
```

`show_menu` 按这张表逐行输出，最后固定加一行 `4  power off`。关机仍写端口 `0x604`，留在 `kmain.c`。

`menu` 等到一个键。键是 `4` 就关机。否则在表里找相同的 `key`，调用 `items[i].fn()`。没有对应项就重新画菜单。

## 编译

`user/help.c`、`user/thread_a.c`、`user/thread_b.c` 加入 `Makefile` 的 `SRCS`，用和内核相同的编译选项。链接脚本不用改，这三个 `.o` 跟着 `kmain.o` 进 `boot.elf`。

拿掉 `fs.c`、`user/hdr.S`、`user/prog.ld`、`tools/packmyfs.py`、`tools/stampprog.py`。`myos.img` 回到「引导扇区 + `kernel.bin`」。`make run` 用 `-kernel`，菜单不依赖 `-drive`。

再加一个程序时：新建 `user/foo.c`，导出 `foo_main`，在 `kernel.h` 声明，在 `items` 里加一行，把这个 `.c` 加进 `SRCS`。

## 不做的事

不从磁盘读程序，不保留 `PROG` 头，不把程序装到 `0x1000000`。不给每个程序单独的栈、页表或任务。

下面是先前的磁盘方案，这一步不实现。

# 简单文件系统

内核由磁盘第一个扇区读入，不经过 GRUB。`mbr.S` 把 `kernel.bin` 从 LBA 1 拷到 `0x100000` 后跳到 `_start`。用户程序不链进这个文件。改一个用户程序时，只改磁盘上的文件系统，不重新编译内核。

用户程序和 `thread_a` 一样跑在内核态。没有用户态，没有系统调用指令。内核把程序读到固定地址，当成普通函数调用。程序返回后，菜单重新画出来。

这一版只读，一次只跑一个程序。文件在 Linux 上用打包工具写进磁盘镜像。

## 磁盘布局

`myos.img` 现在没有分区表。

| LBA | 内容 | 谁来读 |
| --- | --- | --- |
| 0 | `mbr.bin`，512 字节引导程序 | BIOS |
| 1 起 | `kernel.bin` | 引导扇区 |
| 内核之后 | 下面定义的 MYFS | myos |

MYFS 的扇区号从它自己的起点算起。起点放在内核最后一个扇区的下一个 LBA，具体数字由打包工具根据 `kernel.bin` 的长度计算。myos 不解析 ext4。

QEMU 的 `-drive file=myos.img,format=raw` 把这块盘接到主 ATA。内核用 PIO 方式读端口 `0x1F0`，一次 512 字节，不使用磁盘中断。真机上的 U 盘不走这个端口，这一版先在 QEMU 里读盘。

## MYFS 布局

扇区大小 512 字节。数字都是小端。分区的 LBA 0 是超级块，从 LBA 1 开始连续 8 个扇区是目录，文件内容从 LBA 9 开始。目录占 8 × 512 / 64 = 64 个项。

超级块只使用前 16 字节，其余填 0：

| 偏移 | 类型 | 内容 |
| --- | --- | --- |
| 0 | 4 字节 | 魔数 `MYFS` |
| 4 | uint32 | 版本，现在是 1 |
| 8 | uint32 | 文件个数 |
| 12 | uint32 | 扇区字节数，固定 512 |

目录项 64 字节：

| 偏移 | 类型 | 内容 |
| --- | --- | --- |
| 0 | 1 字节 | 菜单按键，例如 `'1'`、`'2'` |
| 1 | 31 字节 | 菜单上显示的文字，以 0 结尾 |
| 32 | uint32 | 文件起始 LBA，相对 MYFS 起点 |
| 36 | uint32 | 文件字节数 |
| 40 | uint32 | 标志。第 0 位为 1 表示可以执行 |
| 44 | uint32 | 保留，写 0 |

文件内容连续存放，文件之间按扇区向上对齐。不拆碎片，不做子目录，不做日志。内核只按这张表把整段读进内存。

打包工具在 Linux 上打开 `myos.img`，跳过 LBA 0 的引导扇区和后面的 `kernel.bin`，从 MYFS 起点重写超级块、目录和文件内容。`kernel.bin` 不动。

## 用户程序的文件格式

可执行文件开头 16 字节：

| 偏移 | 类型 | 内容 |
| --- | --- | --- |
| 0 | 4 字节 | 魔数 `PROG` |
| 4 | uint32 | 入口相对文件开头的偏移，通常是 16 |
| 8 | uint32 | 要装入内存的字节数，含这 16 字节头 |
| 12 | uint32 | 装入内容之后还要清零的字节数 |

头后面就是机器码和已初始化的数据。编译时用和内核相同的限制：`-ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -mgeneral-regs-only`。链接地址固定为 `0x1000000`，再用 `objcopy -O binary` 得到这个文件。

`0x1000000` 在已有的低 1GB 恒等映射里，并且远高于内核占用的大约 1MB。虚拟地址等于物理地址，加载就是一次拷贝。单个程序的装入长度加上清零长度不超过 1MB。

## 程序源码和菜单

`thread_a`、`thread_b` 和帮助页不再写在 `kmain.c` 里。每个要运行的程序是 `user/` 下一个独立的 C 文件，单独编译成 `PROG` 二进制，不链进内核。

```text
user/help.c       按键 1，只打印说明，按 q 返回
user/thread_a.c   按键 2，连续输出 A，按 q 返回
user/thread_b.c   按键 3，连续输出 B，按 q 返回
```

这些文件不能直接调用 `vga_putc`。它们只收到 `kapi` 指针，用 `api->putc`、`api->puts`、`api->poll`。改其中任何一个，只重新编译这个文件并重写 MYFS，不重新编译 `kernel.bin`。

菜单也从这张目录来，不在内核里写死几行字。`kmain` 读出全部目录项，按按键排序后逐行显示：

```text
menu
1  help
2  run thread_a
3  run thread_b
4  power off
```

前三行的文字来自目录项。`4  power off` 留在内核里，因为它要写 QEMU 的关机端口 `0x604`，不能做成一个可替换的用户程序。按下目录里存在的键，就加载对应文件并调用入口。程序 `return` 之后，再读一次目录，重新画菜单。这样增减一个程序，菜单行会跟着变。

读盘失败或超级块不是 `MYFS` 时，只显示 `4  power off`，避免没有磁盘也进不了关机。

## 内核怎么运行它

键盘轮询要认识目录里用到的数字键。现在是 `1` 到 `4`，扫描码分别是 `0x02` 到 `0x05`。`q` 的扫描码是 `0x10`，只在用户程序里使用，用来返回菜单。

运行时：

```text
从内核结束后的 LBA 读超级块，魔数必须是 MYFS，版本必须是 1
按目录项把选中的文件读到 0x1000000
检查 PROG 魔数
把入口之后的 bss 清零
调用入口，第一个参数是下面的 kapi
函数返回后重新画主菜单
```

入口就是一个 C 函数。程序和菜单共用当前栈。一次只加载一个程序，后一个程序覆盖 `0x1000000`。程序写坏内核内存时，整个系统停住。这是内核态运行的结果。

`kapi` 是程序能调用的全部内核服务。内核把这个结构的指针放在 `rdi` 里，符合 System V 调用约定。

```text
struct kapi {
    uint32_t version;                 /* 1 */
    void (*putc)(char c);             /* 屏幕输出一个字符 */
    void (*puts)(const char *s);
    void (*serial_putc)(char c);
    int (*poll)(void);                /* 没有键时返回 -1 */
};
```

程序里的写法：

```text
void main(struct kapi *api)
{
    api->puts("hello\n");
    for (;;) {
        if (api->poll() == 'q')
            return;
        api->putc('A');
    }
}
```

`return` 回到菜单。不增加退出系统调用。`version` 用来让以后追加的函数指针不影响旧程序。

`help`、`thread_a` 和 `thread_b` 放在 `user/` 里，菜单文字来自 MYFS 目录。内核只保留 `4 power off`。

## 这一版不做的事

不写文件，不删除文件，不建子目录。文件内容要更新时，在 Linux 上重新运行打包工具。

不读 ext4，不用磁盘中断，不支持 AHCI 和 USB 磁盘。不把程序做成独立任务，也不给每个程序单独的页表。
