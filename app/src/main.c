#include <zephyr/kernel.h>

int main(void)
{
	int count = 0;

	while (1) {
		printk("Hello from zephyr-pump on xiao_esp32c6: %d\n", count++);
		k_msleep(1000);
	}
	return 0;
}
