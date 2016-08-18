CFLAGS := -Wall -W -std=c99 -g

all: cucu-zpu

test: cucu-dummy-test cucu-x86-test

cucu-dummy: cucu-dummy.o
cucu-dummy.o: cucu.c gen-dummy/gen.c
	$(CC) -c $< -DGEN=\"gen-dummy/gen.c\" -o $@
cucu-dummy-test: cucu-dummy
	python gen-dummy/test.py

cucu-zpu: cucu-zpu.o
cucu-zpu.o: cucu.c gen-zpu/gen.c
	$(CC) -c $< -DGEN=\"gen-zpu/gen.c\" -o $@

cucu-x86: cucu-x86.o
cucu-x86.o: cucu.c gen-x86/gen.c
	$(CC) -c $< -DGEN=\"gen-x86/gen.c\" -o $@
cucu-x86-test: cucu-x86
	sh gen-x86/test.sh

clean:
	rm -f cucu-dummy
	rm -f cucu-x86
	rm -f cucu-zpu
	rm -f *.o

.PHONY: all
