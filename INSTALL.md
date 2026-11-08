# 安装 myos

这份说明把 `/home/edy/wangfei/myos` 编译成可启动映像，先在 QEMU 里确认菜单，再写入 U 盘，从真机启动。

内核文件是 `boot32.elf`。它带 Multiboot 头，入口在 32 位保护模式的 `_start`。BIOS 不能直接执行这个 ELF，U 盘里要有 GRUB，由 GRUB 把 CPU 交到 `_start`。

主机是 Ubuntu 24.04，已有 `gcc`、`ld`、`grub-mkrescue`、`qemu-system-x86_64`。做到第 4 步后二选一：方案 1 用 `grub-mkrescue` 做 ISO，方案 2 做一块虚拟硬盘，再整盘写入 U 盘。`mtools` 只在方案 1 里被 `grub-mkrescue` 内部使用，方案 2 不需要它。

## 1. 进入源码目录

```bash
cd /home/edy/wangfei/myos
```

后面的命令都在这个目录执行。

## 2. 编译内核

```bash
make boot32.elf
```

这一步生成两个文件：

| 文件 | 用途 |
| --- | --- |
| `boot.elf` | 64 位 ELF，给 `grub-file` 检查 Multiboot 头 |
| `boot32.elf` | 同一个内核装进 32 位 ELF。QEMU 的 `-kernel` 和 GRUB 都使用它 |

`objcopy` 只改容器格式。`entry64` 之后的机器码仍然是 64 位。

## 3. 检查 Multiboot 头

```bash
make check
```

成功时没有输出，退出码为 0。失败就不要继续做 U 盘，GRUB 会拒绝加载。

## 4. 先在 QEMU 里启动

```bash
make run
```

实际执行的是：

```bash
qemu-system-x86_64 -kernel boot32.elf -display curses -serial file:serial.log -no-reboot
```

这台机器没有图形界面，不能用 GTK 窗口。`-display curses` 把 VGA 文字画在当前终端里。串口写到 `serial.log`，避免和 curses 抢同一个屏幕。

看到的菜单是：

```text
menu
1  help
2  run thread_a
3  run thread_b
4  power off
press q while a thread runs to return
```

按键：

| 键 | 作用 |
| --- | --- |
| `1` | 显示帮助 |
| `2` | 运行 `thread_a`，连续输出 `A` |
| `3` | 运行 `thread_b`，连续输出 `B` |
| `q` | 线程运行时回到主菜单 |
| `4` | 关机。QEMU 会退出，`make run` 结束 |

线程跑起来以后，第 0 行保持 `press q to return`，下面刷字母。先按 `q` 回到菜单，再按 `4`。

curses 接管键盘后，Ctrl-C 不能退出。菜单里的 `4` 可以。也可以按 Esc，再按 2，进入 QEMU 监视器，输入 `quit` 回车。

`make clean` 会删掉 `serial.log`。要留日志就在清理前把文件拷走。

第 4 步的菜单能出现后，再用下面两套方案之一做启动盘。

## 方案 1：用 grub-mkrescue 做 ISO

这套方案把内核打进一张混合 ISO。`grub-mkrescue` 内部调用 `mtools` 往镜像里的 FAT 小盘拷文件，所以要先装这两个包。不想装 `mtools` 时，直接做方案 2。

### 1. 安装做 ISO 的工具

```bash
sudo apt install xorriso mtools
```

如果这一步报某个包 `not installable`，先确认 `/etc/apt/sources.list.d/` 里有 Ubuntu 24.04 的 `noble main` 二进制源，再执行 `sudo apt-get update`。`libpixman`、`libfdt`、`seabios` 这一类包只在 `noble main` 里，不在 `noble-updates` 里。

### 2. 制作 GRUB 启动镜像

```bash
mkdir -p isodir/boot/grub
cp boot32.elf isodir/boot/myos.elf
cat > isodir/boot/grub/grub.cfg << 'EOF'
set timeout=3
menuentry "myos" {
    multiboot /boot/myos.elf
    boot
}
EOF
grub-mkrescue -o myos.iso isodir
```

`grub.cfg` 里的 `multiboot` 告诉 GRUB：按 Multiboot 规范加载 `/boot/myos.elf`，EAX 放入魔数 `0x2BADB002`，EBX 放入信息结构地址，然后跳到 `_start`。

成功时当前目录出现 `myos.iso`。`grub-mkrescue` 如果提示找不到 `xorriso`，回到方案 1 的第 1 步安装。

### 3. 用 QEMU 试这张镜像

写 U 盘之前先确认镜像自己能启动：

```bash
qemu-system-x86_64 -cdrom myos.iso -display curses -serial file:serial.log -no-reboot
```

GRUB 菜单出现后选择 `myos`，应进入和第 4 步相同的菜单。按 `4` 退出。

这一步不过，就不要写入 U 盘。通过后跳到「确认 U 盘的设备名」，写入时使用 `myos.iso`。

## 方案 2：虚拟硬盘

这套方案先在一个普通文件里做出完整硬盘：分区表、ext4、GRUB 和内核都在这个文件里。最后用 `dd` 把整个文件写到 U 盘，等于把这块虚拟硬盘原样拷到 U 盘上。不使用 `mtools`，也不使用 `xorriso`。

内核用 `multiboot` 加载，不是 Linux 的 `linux` / `initrd`。不需要 ext4 里再放 initramfs。

### 1. 建立虚拟硬盘并分区

```bash
qemu-img create -f raw myos.img 64M
parted myos.img mklabel msdos
parted myos.img mkpart primary ext4 1MiB 100%
```

64MB 足够放下 GRUB 和 `boot32.elf`。分区从 1MB 开始，前面这段空着，给 GRUB 的 BIOS 引导代码。不要对整个 `myos.img` 直接 `mkfs.ext4`，那样没有分区表，引导代码没有地方放。

### 2. 挂载分区并放入内核

```bash
sudo losetup -Pf --show myos.img
```

这条命令打印循环设备名，例如 `/dev/loop0`。下面把它写成 `/dev/loop0`。如果打印的是 `/dev/loop1`，就把后面命令里的 `loop0` 换成 `loop1`。`p1` 是刚才建立的第一个分区。

```bash
sudo mkfs.ext4 /dev/loop0p1
sudo mkdir -p /mnt/myos
sudo mount /dev/loop0p1 /mnt/myos
sudo mkdir -p /mnt/myos/boot/grub
sudo cp boot32.elf /mnt/myos/boot/myos.elf
```

### 3. 写 GRUB 配置

```bash
sudo tee /mnt/myos/boot/grub/grub.cfg >/dev/null << 'EOF'
set timeout=3
menuentry "myos" {
    insmod part_msdos
    insmod ext2
    set root='hd0,msdos1'
    multiboot /boot/myos.elf
    boot
}
EOF
```

`multiboot` 按 Multiboot 规范加载 `/boot/myos.elf`。EAX 放入魔数 `0x2BADB002`，EBX 放入信息结构地址，然后跳到 `_start`。`hd0,msdos1` 是这块虚拟硬盘的第一个分区。整盘写入 U 盘后，U 盘自己就是 `hd0`。

### 4. 把 GRUB 装进虚拟硬盘

```bash
sudo grub-install --target=i386-pc --boot-directory=/mnt/myos/boot /dev/loop0
```

`--target=i386-pc` 是传统 BIOS 引导。设备写整块循环盘 `/dev/loop0`，不要写成 `/dev/loop0p1`。引导代码装在分区前面的那 1MB 里，内核和配置留在分区中。

### 5. 卸下虚拟硬盘

```bash
sudo umount /mnt/myos
sudo losetup -d /dev/loop0
```

卸的时候仍然使用第 2 步打印出来的那个循环设备。

### 6. 用 QEMU 试这块虚拟硬盘

```bash
qemu-system-x86_64 -drive file=myos.img,format=raw -display curses -serial file:serial.log -no-reboot
```

GRUB 菜单出现后选择 `myos`，应进入第 4 步那个菜单。按 `4` 退出。这一步不过，就不要写入 U 盘。通过后，下面写入 U 盘时使用 `myos.img`。

## 5. 确认 U 盘的设备名

插入 U 盘，执行：

```bash
lsblk -o NAME,SIZE,TYPE,TRAN,MODEL,MOUNTPOINT
```

找 `TRAN` 为 `usb` 的那块整盘。例如：

```text
sda   256G  disk          ssd-model
sdb    16G  disk  usb     Ultra
sdb1   16G  part  usb     /media/edy/USB
```

这里应使用的名字是 `/dev/sdb`，不是 `/dev/sdb1`。`/dev/sdb1` 只是分区，写到分区上机器不能从这块 U 盘启动。

盘符每次插入都可能变化。以这次 `lsblk` 的输出为准，不要沿用旧的 `/dev/sdb`。

## 6. 写入 U 盘

下面的命令会删掉目标盘上的全部分区、文件和原有系统。把 `sdb` 换成第 5 步看到的 USB 整盘名。方案 1 写入 `myos.iso`，方案 2 写入 `myos.img`。两个文件都是整块启动盘，写的都是整个 U 盘。

方案 1：

```bash
sudo umount /dev/sdb1
sudo dd if=myos.iso of=/dev/sdb bs=4M conv=fsync status=progress
sync
```

方案 2：

```bash
sudo umount /dev/sdb1
sudo dd if=myos.img of=/dev/sdb bs=4M conv=fsync status=progress
sync
```

`umount` 若提示没有挂载，可以忽略。`conv=fsync` 用来等数据写进 U 盘，不要在它结束前拔盘。`dd` 结束后再执行一次 `sync`。

写完后 `lsblk` 里这块 U 盘的分区会变成镜像自带的布局，原来的 FAT 分区不再存在。

## 7. 从 U 盘启动电脑

1. 把 U 盘插到要启动的那台机器上。
2. 开机时按该机器的启动菜单键，常见的是 F12、F10 或 Esc。
3. 选择这个 U 盘。有两项时，选带 `UEFI` 字样之外的那项，或者 BIOS、Legacy、CSM。
4. 出现 GRUB 菜单后选择 `myos`。三秒内不按键也会自动进入。
5. 屏幕上应出现第 4 步那个 `menu`。

纯 UEFI、没有 CSM 的机器可能选出 U 盘，GRUB 也能加载内核，但屏幕仍是黑的。本内核把字符写在 VGA 文字内存 `0xB8000`，不写 UEFI 显存。要看到菜单，固件里需要打开 CSM，或选择传统 BIOS 方式启动这块 U 盘。

键盘读的是 PS/2 端口 `0x60`。台式机的 PS/2 键盘可以直接用。USB 键盘只有在固件把它模拟成 PS/2 时才能用，内核自己没有 USB 驱动。

真机上按 `4` 只会在屏幕上留下 `power off` 并停住。端口 `0x604` 是 QEMU 的关机口，真机电源要手动关。
