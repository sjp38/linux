obj-m := ksdemo.o

ksdemo.ko: ksdemo.c
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load: ksdemo.ko
	sudo insmod $^

unload: ksdemo.ko
	sudo rmmod $^
