#include "kernel.h"
#include "user/kapi.h"

struct dirent {
	char key;
	char title[31];
	uint32_t lba;
	uint32_t bytes;
	uint32_t flags;
	uint32_t reserved;
} __attribute__((packed));

struct prog_hdr {
	uint32_t magic;
	uint32_t entry_off;
	uint32_t image_bytes;
	uint32_t bss_bytes;
} __attribute__((packed));

#define LOAD_ADDR 0x1000000UL
#define LOAD_MAX (1UL << 20)

static uint32_t myfs_base;
static struct dirent ents[64];
static int nent;
static int ready;

static inline void outb(uint16_t port, uint8_t value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static inline uint16_t inw(uint16_t port)
{
	uint16_t value;
	__asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

/* 主 ATA，PIO 读一个 512 字节扇区。成功返回 0。 */
static int ata_read(uint32_t lba, void *buf)
{
	int saw_ready = 0;
	for (int i = 0; i < 1000000; i++) {
		uint8_t status = inb(0x1f7);
		if (status == 0xff)
			continue;
		if ((status & 0x80) == 0) {
			saw_ready = 1;
			break;
		}
	}
	if (!saw_ready)
		return -1;

	outb(0x1f6, (uint8_t)(0xe0 | ((lba >> 24) & 0x0f)));
	for (int i = 0; i < 4; i++)
		inb(0x1f7);
	outb(0x1f2, 1);
	outb(0x1f3, (uint8_t)lba);
	outb(0x1f4, (uint8_t)(lba >> 8));
	outb(0x1f5, (uint8_t)(lba >> 16));
	outb(0x1f7, 0x20);

	for (int i = 0; i < 1000000; i++) {
		uint8_t status = inb(0x1f7);
		if (status & 0x01)
			return -1;
		if ((status & 0x80) == 0 && (status & 0x08)) {
			uint16_t *dst = buf;
			for (int word = 0; word < 256; word++)
				dst[word] = inw(0x1f0);
			return 0;
		}
	}
	return -1;
}

/* 读引导扇区里记下的 MYFS 起点，再读超级块和目录。 */
int fs_init(void)
{
	uint8_t sector[512];
	ready = 0;
	nent = 0;
	if (ata_read(0, sector) != 0)
		return -1;

	uint32_t base = *(uint32_t *)(sector + 0x1b0);
	if (base < 2)
		return -1;
	if (ata_read(base, sector) != 0)
		return -1;
	if (sector[0] != 'M' || sector[1] != 'Y' || sector[2] != 'F' || sector[3] != 'S')
		return -1;
	uint32_t version = *(uint32_t *)(sector + 4);
	uint32_t count = *(uint32_t *)(sector + 8);
	if (version != 1 || count > 64)
		return -1;

	for (uint32_t i = 0; i < count; i++) {
		uint32_t off = i * 64;
		uint32_t sec = 1 + off / 512;
		uint32_t pos = off % 512;
		if (ata_read(base + sec, sector) != 0)
			return -1;
		ents[i] = *(struct dirent *)(sector + pos);
	}
	myfs_base = base;
	nent = (int)count;
	ready = 1;
	return 0;
}

int fs_count(void)
{
	return ready ? nent : 0;
}

char fs_key(int index)
{
	return ents[index].key;
}

const char *fs_title(int index)
{
	return ents[index].title;
}

/* 屏幕和串口都写，方便在终端和 serial.log 里看到同一份输出。 */
static void fs_puts(const char *s)
{
	vga_puts(s);
	serial_puts(s);
}

/* 把选中的 PROG 读到 0x1000000，清 bss，然后调用入口。 */
void fs_run(int index)
{
	struct dirent *file = &ents[index];
	if ((file->flags & 1) == 0 || file->bytes < 16 || file->bytes > LOAD_MAX)
		return;

	uint8_t *dst = (uint8_t *)LOAD_ADDR;
	uint32_t got = 0;
	while (got < file->bytes) {
		uint8_t sector[512];
		if (ata_read(myfs_base + file->lba + got / 512, sector) != 0)
			return;
		uint32_t n = 512;
		if (got + n > file->bytes)
			n = file->bytes - got;
		for (uint32_t i = 0; i < n; i++)
			dst[got + i] = sector[i];
		got += 512;
	}

	struct prog_hdr *hdr = (struct prog_hdr *)LOAD_ADDR;
	if (hdr->magic != 0x474f5250)
		return;
	if (hdr->entry_off >= hdr->image_bytes || hdr->image_bytes > file->bytes)
		return;
	if (hdr->image_bytes + hdr->bss_bytes > LOAD_MAX)
		return;
	for (uint32_t i = 0; i < hdr->bss_bytes; i++)
		dst[hdr->image_bytes + i] = 0;

	struct kapi api = {
		1,
		vga_putc,
		fs_puts,
		serial_putc,
		kbd_poll
	};
	void (*entry)(struct kapi *) = (void (*)(struct kapi *))(LOAD_ADDR + hdr->entry_off);
	entry(&api);
}
