obj-m+=i2c.o

"all:
	@echo "* Compiling Project *"
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	gcc -c -o BMP180.o BMP180.c
	#make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNEL_DIR) M=$(PWD) modules

#bmp:
	#gcc -c -o BMP180.o BMP180.c

copy:
	@echo "* Copying to BBB *"
	sudo scp -r ../driver/ debian@192.168.6.2:./td3/

clean:
	@echo "* Cleaning Project*"
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm -f *.o
	#make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNEL_DIR) M=$(PWD) clean
