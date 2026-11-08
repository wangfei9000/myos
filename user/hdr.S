/* PROG 头占文件前 16 字节。后两项由打包前的脚本按实际长度填上。 */
	.section .proghdr, "a"
	.ascii "PROG"
	.long 16
	.long 0
	.long 0
