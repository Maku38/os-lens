OUTPUT := .output
CLANG ?= clang
BPFTOOL ?= bpftool
LIBBPF_OBJ := /usr/lib/x86_64-linux-gnu/libbpf.a 
# Note: Adjust LIBBPF_OBJ path if needed (e.g. use -lbpf instead)

all: main

.output:
	mkdir -p .output

# 1. Generate vmlinux.h (Kernel types)
vmlinux:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

# 2. Compile BPF code to object file
main.bpf.o: .output vmlinux
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -I. -c main.bpf.c -o .output/main.bpf.o

# 3. Generate Skeleton Header (User space helper)
main.skel.h: main.bpf.o
	$(BPFTOOL) gen skeleton .output/main.bpf.o > main.skel.h

# 4. Compile User Space code
main: main.skel.h
	$(CLANG) -g -O2 -Wall -I. main.c -lbpf -lelf -lz -o main

clean:
	rm -rf .output main main.skel.h vmlinux.h