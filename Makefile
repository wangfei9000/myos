# make        编译 boot.elf，user/ 里的程序一起链进来
# make img    生成引导扇区加内核的 myos.img
# make boot   从磁盘启动

CC := gcc
LD := ld
CFLAGS := -m64 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone \
	-mgeneral-regs-only -fno-asynchronous-unwind-tables -Wall -Wextra -c -O1 -I.
LDFLAGS := -m elf_x86_64 -z max-page-size=0x1000 -z noexecstack --build-id=none -T linker.ld

SRCS := kmain.c pmm.c task.c idt.c pic.c serial.c vga.c kbd.c \
	user/help.c user/thread_a.c user/thread_b.c
ASMS := boot.S swtch.S trap.S
OBJS := $(SRCS:.c=.o) $(ASMS:.S=.o)

.PHONY: all check run img boot clean

all: boot.elf

%.o: %.c kernel.h
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -o $@ $<

boot.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

check: boot.elf
	grub-file --is-x86-multiboot boot.elf

# QEMU 的 -kernel 只接受 ELF32。机器码仍是切到长模式后的 64 位代码。
boot32.elf: boot.elf
	objcopy -O elf32-i386 boot.elf boot32.elf

# 当前环境没有 DISPLAY，GTK 窗口会初始化失败。curses 把 VGA 文字画在终端里。
# 串口写到 serial.log，避免和 curses 抢同一个终端。
run: boot32.elf
	qemu-system-x86_64 -kernel boot32.elf -display curses -serial file:serial.log -no-reboot

kernel.bin: boot.elf
	objcopy -O binary boot.elf kernel.bin

mbr.o: mbr.S boot.elf kernel.bin
	entry=$$(readelf -h boot.elf | awk '/Entry point/ {print $$4}'); \
	sectors=$$(( ($$(wc -c < kernel.bin) + 511) / 512 )); \
	$(CC) -m32 -ffreestanding -c -DKERNEL_ENTRY=$$entry -DKERNEL_SECTORS=$$sectors -o mbr.o mbr.S

mbr.bin: mbr.o mbr.ld
	$(LD) -m elf_i386 -T mbr.ld -o mbr.elf mbr.o
	objcopy -O binary mbr.elf mbr.bin

# LBA 0 是引导扇区，LBA 1 起是内核。用户程序已经在内核里。
myos.img: mbr.bin kernel.bin
	dd if=/dev/zero of=myos.img bs=512 count=$$(( $$(wc -c < kernel.bin) / 512 + 2 )) status=none
	dd if=mbr.bin of=myos.img conv=notrunc status=none
	dd if=kernel.bin of=myos.img bs=512 seek=1 conv=notrunc status=none

img: myos.img

boot: myos.img
	qemu-system-x86_64 -drive file=myos.img,format=raw,if=ide -display curses -serial file:serial.log -no-reboot

clean:
	rm -f $(OBJS) boot.elf boot32.elf kernel.bin mbr.o mbr.elf mbr.bin myos.img serial.log \
		user/*.elf user/*.bin fs.o
