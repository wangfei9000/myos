# make        编译 boot.elf
# make check  确认 Multiboot 头
# make run    QEMU 串口应交替出现 A 和 B

CC := gcc
LD := ld
CFLAGS := -m64 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone \
	-mgeneral-regs-only -fno-asynchronous-unwind-tables -Wall -Wextra -c -O1
LDFLAGS := -m elf_x86_64 -z max-page-size=0x1000 -z noexecstack --build-id=none -T linker.ld

SRCS := kmain.c pmm.c task.c idt.c pic.c serial.c
ASMS := boot.S swtch.S trap.S
OBJS := $(SRCS:.c=.o) $(ASMS:.S=.o)

.PHONY: all check run clean

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

run: boot32.elf
	qemu-system-x86_64 -kernel boot32.elf -serial stdio -display none -no-reboot

clean:
	rm -f $(OBJS) boot.elf boot32.elf
