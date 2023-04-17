ifeq ($(shell uname -p),x86_64)
	SYSROOT = aarch64-linux-gnu
	CC = clang -target $(SYSROOT)
	CXX = clang++ -target $(SYSROOT)
	STRIP = /usr/$(SYSROOT)/bin/strip
	QEMU = qemu-aarch64 -L /usr/$(SYSROOT)
else
	STRIP = strip
	CC = clang
	CXX = clang++
endif
CCLD = $(CXX)
C_FLAGS = -std=gnu11
FLAGS = -I../libhooker/include/linux/ -I../libhooker/include/ -fPIC -g
FLAGS_LH = $(FLAGS) -O3 -fvisibility=hidden
CFLAGS_LH = $(FLAGS_LH) $(C_FLAGS)
CFLAGS = $(FLAGS) $(C_FLAGS)
CXXFLAGS_LH = $(FLAGS_LH)

all: libhooker libhooker-static libhooker.so libhooker.a

libhooker: libhooker.so test.o
	$(CCLD) test.o -L. -lhooker -ldl -o libhooker
	#$(STRIP) libhooker

libhooker-static: as-aarch64.o disas-aarch64.o funcHook.o writeMem.o shadowMem.o shadowMemCPP.o misc.o ldSyms.o test.o
	$(CCLD) $^ -ldl -o $@
	#$(STRIP) libhooker-static
	
libhooker.a: as-aarch64.o disas-aarch64.o funcHook.o writeMem.o shadowMem.o shadowMemCPP.o misc.o ldSyms.o
	ar rcs $@ $^

libhooker.so: as-aarch64.o disas-aarch64.o funcHook.o writeMem.o shadowMem.o shadowMemCPP.o misc.o ldSyms.o
	$(CC) -shared -Wl,-soname,libhooker.so -o libhooker.so $^
	$(STRIP) libhooker.so

as-aarch64.o: ../libhooker/as-aarch64/as-aarch64.c
	$(CC) $(CFLAGS_LH) -c $^

disas-aarch64.o: ../libhooker/disas-aarch64/disas-aarch64.c
	$(CC) $(CFLAGS_LH) -c $^

funcHook.o: ../libhooker/func/funcHook.c
	$(CC) $(CFLAGS_LH) -c $^

writeMem.o: ../libhooker/mem/writeMem-linux.c
	$(CC) $(CFLAGS_LH) -o writeMem.o -c $^

shadowMem.o: ../libhooker/mem/shadowMem.c
	$(CC) $(CFLAGS_LH) -o shadowMem.o -c $^

shadowMemCPP.o: ../libhooker/mem/shadowMem.cpp
	$(CXX) $(CXXFLAGS_LH) -o shadowMemCPP.o -c $^

misc.o: ../libhooker/misc/misc.c
	$(CC) $(CFLAGS_LH) -c $^

ldSyms.o: ../libhooker/ld.so/ldSyms.c
	$(CC) $(CFLAGS_LH) -c $^

test.o: test.c
	$(CC) $(CFLAGS) -c $^

runtest:
	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd) $(QEMU) ./libhooker

clean:
	rm -f libhooker libhooker-static libhooker.so libhooker.a *.o
