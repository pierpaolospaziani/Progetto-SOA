obj-m += my_mod.o
my_mod-objs += my_module.o lib/scth.o

NBLOCKS := 6	# number of blocks (included superblock and inode)

KVERSION = $(shell uname -r)

all:
	gcc filesystem/singlefilemakefs.c -o filesystem/singlefilemakefs
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
	gcc test/test.c -o test/test -lpthread

clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	rm ./filesystem/singlefilemakefs
	rm ./test/test
	rmdir ./mount
	rm image

insmod:
	@if ! lsmod | grep -q the_usctm; then \
		echo "Module the_usctm is not loaded. Building and loading it..."; \
		$(MAKE) -C syscall-table; \
		make -C syscall-table insmod; \
	fi
	insmod my_mod.ko the_syscall_table=$$(cat /sys/module/the_usctm/parameters/sys_call_table_address)

rmmod:
	@if lsmod | grep -q the_usctm; then \
		echo "Removing module the_usctm..."; \
		rmmod the_usctm; \
	fi
	@if lsmod | grep -q my_mod; then \
		echo "Removing module my_module..."; \
		rmmod my_mod; \
	fi

create-fs:
	dd bs=4096 count=$(NBLOCKS) if=/dev/zero of=image
	./filesystem/singlefilemakefs image $(NBLOCKS)
	mkdir ./mount

mount-fs:
	mount -o loop -t singlefilefs image ./mount/

unmount-fs:
	umount ./mount/


# +=======+
# | USAGE |
# +=======+

# make
# sudo make insmod
# sudo make create-fs
# sudo make mount-fs
#
# sudo make unmount-fs
# sudo make rmmod
# make clean
