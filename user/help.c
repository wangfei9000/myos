#include "kapi.h"

/* 帮助页。按 q 回到菜单。 */
void main(struct kapi *api)
{
	api->puts("help\n");
	api->puts("1  show this page\n");
	api->puts("2  run thread_a, print A\n");
	api->puts("3  run thread_b, print B\n");
	api->puts("4  power off\n");
	api->puts("q  return to menu\n");
	for (;;) {
		if (api->poll() == 'q')
			return;
	}
}
