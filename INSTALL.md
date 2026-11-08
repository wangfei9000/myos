# 安装 myos

这份说明把 `/home/edy/wangfei/myos` 做成一块可以启动的磁盘镜像。镜像第一个扇区是自己的引导程序，从 LBA 1 读出内核，跳到 `_start`。不使用 GRUB。

主机是 Ubuntu 24.04，已有 `gcc`、`ld`、`qemu-system-x86_64`。

## 1. 进入源码目录

```bash
cd /home/edy/wangfei/myos
```

后面的命令都在这个目录执行。

## 2. 编译内核并生成磁盘镜像

```bash
make img
```

这一步生成：

| 文件 | 用途 |
| --- | --- |
| `boot.elf` | 带符号的内核，供 QEMU `-kernel` 和检查 Multiboot 头 |
| `kernel.bin` | 内核的原始二进制，装入物理地址 `0x100000` |
| `mbr.bin` | 512 字节引导扇区，结尾是 `0x55AA` |
| `myos.img` | 整块磁盘。LBA 0 是 `mbr.bin`，LBA 1 起是 `kernel.bin` |

引导扇区在实模式里用 BIOS 扩展读盘，把内核拷到 `0x100000`，打开保护模式后跳到 `_start`。内核自己再切到长模式。`.bss` 由 `_start` 清零，不依赖引导程序。

## 3. 检查 Multiboot 头

```bash
make check
```

成功时没有输出，退出码为 0。这条只说明 `boot.elf` 仍能被 QEMU 的 `-kernel` 识别。从磁盘启动不使用这个头。

## 4. 从磁盘镜像启动

```bash
make boot
```

实际执行的是：

```bash
qemu-system-x86_64 -drive file=myos.img,format=raw,if=ide -display curses -serial file:serial.log -no-reboot
```

这台机器没有图形界面。`-display curses` 把 VGA 文字画在当前终端里。串口写到 `serial.log`。

看到的菜单是：

```text
menu
1  help
2  run thread_a
3  run thread_b
4  power off
press q while a thread runs to return
```

| 键 | 作用 |
| --- | --- |
| `1` | 显示帮助 |
| `2` | 运行 `thread_a`，连续输出 `A` |
| `3` | 运行 `thread_b`，连续输出 `B` |
| `q` | 线程运行时回到主菜单 |
| `4` | 关机。QEMU 会退出 |

curses 接管键盘后，Ctrl-C 不能退出。按 `4`，或按 Esc 再按 2，进入 QEMU 监视器后输入 `quit`。

`make run` 仍用 `qemu -kernel boot32.elf` 直接载入 ELF，不经过磁盘扇区。日常改代码可以用它。确认引导扇区时用 `make boot`。

## 5. 确认 U 盘的设备名

插入 U 盘，执行：

```bash
lsblk -o NAME,SIZE,TYPE,TRAN,MODEL,MOUNTPOINT
```

找 `TRAN` 为 `usb` 的那块整盘。例如 `/dev/sdb`，不要写成 `/dev/sdb1`。盘符每次插入都可能变化，以这次 `lsblk` 为准。

## 6. 写入 U 盘

下面的命令会删掉目标盘上的全部分区、文件和原有系统。把 `sdb` 换成第 5 步看到的 USB 整盘名。

```bash
sudo umount /dev/sdb1
sudo dd if=myos.img of=/dev/sdb bs=4M conv=fsync status=progress
sync
```

`umount` 若提示没有挂载，可以忽略。`dd` 结束前不要拔盘。

## 7. 从 U 盘启动电脑

1. 把 U 盘插到要启动的那台机器上。
2. 开机时按该机器的启动菜单键，常见的是 F12、F10 或 Esc。
3. 选择这个 U 盘的传统 BIOS 或 CSM 项。
4. 屏幕上应出现第 4 步那个 `menu`。

引导程序使用 BIOS 的 `int 0x13` 读盘，所以要走 BIOS 或 CSM，不能走纯 UEFI。画面写在 VGA 文字内存 `0xB8000`。键盘读的是 PS/2 端口 `0x60`。真机上按 `4` 只会停在 `power off`，端口 `0x604` 是 QEMU 的关机口，电源要手动关。
