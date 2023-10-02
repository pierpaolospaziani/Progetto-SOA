obj-m += singlefilefs.o
singlefilefs-objs += singlefilefs_src.o file.o dir.o

all:
	gcc singlefilemakefs.c -o singlefilemakefs
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm singlefilemakefs

insmod:
	insmod singlefilefs.ko

rmmod:
	rmmod singlefilefs

create-fs:
	dd bs=4096 count=10 if=/dev/zero of=image
	./singlefilemakefs image
	mkdir mount

clean-fs:
	rm -f image
	rm -rf mount
	
mount-fs:
	mount -o loop -t singlefilefs image ./mount/

umount-fs:
	umount ./mount