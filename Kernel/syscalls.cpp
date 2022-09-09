#include "syscalls.hpp"

Syscalls::Syscalls(InterruptManager* interrupt_manager, uint8_t interrupt_number)
    : InterruptHandler(interrupt_manager, interrupt_number + interrupt_manager->get_hardware_interrupt_offset())
{
}

Syscalls::~Syscalls()
{
}

void Syscalls::sys_exit()
{
    TM->kill();
}

int Syscalls::sys_read(int fd, void* data, int length)
{
    if (length <= 0)
        return 0;

    int size = 0;

    switch (fd) {
    case 0:
        size = TM->tty()->task_read_stdin((uint8_t*)data, length);
        break;

    case 1:
        size = TM->tty()->read_stdout((uint8_t*)data, length);
        break;

    case DEV_MOUSE_FD:
        size = MouseDriver::active->mouse_event((mouse_event_t*)data);
        break;

    case DEV_KEYBOARD_FD:
        size = KeyboardDriver::active->keyboard_event((keyboard_event_t*)data);
        break;

    case DEV_DISPLAY_FD:
        ((uint32_t*)data)[0] = Vesa::active->get_framebuffer();
        size = sizeof(uint32_t);
        break;

    case DEV_AUDIO_FD:
        ((uint32_t*)data)[0] = AUDIO->current_audio_position();
        size = sizeof(uint32_t);
        break;

    default:
        size = VFS->read(fd, (uint8_t*)data, length);
        break;
    }

    TM->test_poll();
    return size;
}

int Syscalls::sys_write(int fd, void* data, int length)
{
    if (length <= 0)
        return -1;

    switch (fd) {
    case 0:
        TM->tty()->write_stdin((uint8_t*)data, length);
        break;

    case 1:
        TM->tty()->write_stdout((uint8_t*)data, length);
        break;

    case DEV_AUDIO_FD:
        AUDIO->write((uint8_t*)data, length);
        break;

    default:
        /* FIXME: Cannot overwrite existing file */
        VFS->write(fd, (uint8_t*)data, length);
        break;
    }

    TM->test_poll();
    return length;
}

int Syscalls::sys_open(char* file, int flags)
{
    return VFS->open(file, flags);
}

int Syscalls::sys_close(int fd)
{
    return VFS->close(fd);
}

int Syscalls::sys_waitpid(int pid)
{
    return TM->waitpid(pid);
}

int Syscalls::sys_chdir(char* dir)
{
    return TM->task()->chdir(dir);
}

uint32_t Syscalls::sys_time()
{
    return CMOS::active->timestamp();
}

int Syscalls::sys_stat(char* file, struct stat* buffer)
{
    int fd = VFS->open(file);
    int status = sys_fstat(fd, buffer);
    VFS->close(fd);
    return status;
}

int Syscalls::sys_fstat(int fd, struct stat* buffer)
{
    buffer->st_uid = VFS->uid(fd);
    buffer->st_gid = VFS->gid(fd);
    buffer->st_size = VFS->size(fd);
    return (buffer->st_size == -1) ? -1 : 0;
}

int Syscalls::sys_getpid()
{
    return TM->task()->get_pid();
}

int Syscalls::sys_setsid()
{
    return TM->task()->setsid();
}

void Syscalls::sys_reboot(int cmd)
{
    switch (cmd) {
    case 1:
        outbw(0x604, 0x2000);
        outbw(0x4004, 0x3400);
        shutdown();
        return;

    default:
        uint8_t reboot = 0x02;
        while (reboot & 0x02)
            reboot = inb(0x64);
        outb(0x64, 0xFE);
        return;
    }
}

int Syscalls::sys_nice(int inc)
{
    return TM->task()->nice(inc);
}

int8_t Syscalls::sys_kill(int pid, int sig)
{
    return TM->send_signal(pid, sig);
}

uint32_t Syscalls::sys_mmap(void* addr, size_t length)
{
    uint32_t address = PMM->allocate_pages(length);
    addr = (!addr) ? (void*)address : addr;
    TM->task()->process_mmap({ (uint32_t)addr, address, length });
    return address;
}

int Syscalls::sys_munmap(void* addr, size_t length)
{
    uint32_t physical_address = Paging::virtual_to_physical((uint32_t)addr);
    TM->task()->process_munmap({ (uint32_t)addr, physical_address, length });
    return PMM->free_pages((uint32_t)addr, length);
}

int Syscalls::sys_fchown(int fd, uint32_t owner, uint32_t group)
{
    if ((fd == 0) || (fd == 1) && (owner == 1))
        return TM->task()->become_tty_master();

    /* TODO: Not implemented */
    return -1;
}

int Syscalls::sys_uname(utsname* buffer)
{
    strcpy(buffer->sysname, "GevOS");
    strcpy(buffer->release, "0.1.0");
    return 0;
}

int Syscalls::sys_sleep(int sec)
{
    TM->sleep(sec * 18);
    TM->yield();
    return 0;
}

int Syscalls::sys_spawn(char* file, char** args)
{
    return TM->spawn(file, args);
}

/* TODO: Add timespec and sigmask */
int Syscalls::sys_poll(pollfd* fds, uint32_t nfds)
{
    return TM->task()->poll(fds, nfds);
}

/* TODO: Replace with getdents */
int Syscalls::sys_listdir(char* dirname, fs_entry_t* entries, uint32_t count)
{
    return VFS->listdir(dirname, entries, count);
}

int Syscalls::sys_osinfo(struct osinfo* buffer)
{
    PMM->stat(&buffer->free_pages, &buffer->used_pages);
    buffer->procs = TM->task_count();
    buffer->uptime = CMOS::active->timestamp() - CMOS::active->get_uptime();
    return 0;
}

void Syscalls::sys_getcwd(char* buffer)
{
    TM->task()->cwd(buffer);
}

int Syscalls::sys_mkfifo(char* pathname, int mode)
{
    return VFS->open_fifo(pathname, mode | O_CREAT);
}

uint32_t Syscalls::interrupt(uint32_t esp)
{
    cpu_state* cpu = (cpu_state*)esp;
    IRQ::activate();

    switch (cpu->eax) {
    case 1:
        sys_exit();
        break;

    case 3:
        cpu->eax = sys_read((int)cpu->ebx, (void*)cpu->ecx, (int)cpu->edx);
        break;

    case 4:
        cpu->eax = sys_write((int)cpu->ebx, (void*)cpu->ecx, (int)cpu->edx);
        break;

    case 5:
        cpu->eax = sys_open((char*)cpu->ebx, (int)cpu->ecx);
        break;

    case 6:
        cpu->eax = sys_close((int)cpu->ebx);
        break;

    case 7:
        cpu->eax = sys_waitpid((int)cpu->ebx);
        break;

    case 12:
        cpu->eax = sys_chdir((char*)cpu->ebx);
        break;

    case 13:
        cpu->eax = sys_time();
        break;

    case 18:
        cpu->eax = sys_stat((char*)cpu->ebx, (struct stat*)cpu->ecx);
        break;

    case 20:
        cpu->eax = sys_getpid();
        break;

    case 28:
        cpu->eax = sys_fstat((int)cpu->ebx, (struct stat*)cpu->ecx);
        break;

    case 34:
        cpu->eax = sys_nice((int)cpu->ebx);
        break;

    case 37:
        cpu->eax = sys_kill((int)cpu->ebx, (int)cpu->ecx);
        break;

    case 66:
        cpu->eax = sys_setsid();
        break;

    case 88:
        sys_reboot((int)cpu->ebx);
        break;

    case 90:
        cpu->eax = sys_mmap((void*)cpu->ebx, (size_t)cpu->ecx);
        break;

    case 91:
        cpu->eax = sys_munmap((void*)cpu->ebx, (size_t)cpu->ecx);
        break;

    case 95:
        cpu->eax = sys_fchown((int)cpu->ebx, (uint32_t)cpu->ecx, (uint32_t)cpu->edx);
        break;

    case 109:
        cpu->eax = sys_uname((utsname*)cpu->ebx);
        break;

    case 162:
        cpu->eax = sys_sleep((uint32_t)cpu->ebx);
        break;

    case 168:
        cpu->eax = sys_poll((pollfd*)cpu->ebx, (uint32_t)cpu->ecx);
        break;

    case 183:
        sys_getcwd((char*)cpu->ebx);
        break;

    case 400:
        cpu->eax = sys_mkfifo((char*)cpu->ebx, (int)cpu->ecx);
        break;

    case 401:
        cpu->eax = sys_spawn((char*)cpu->ebx, (char**)cpu->ecx);
        break;

    case 402:
        cpu->eax = sys_listdir((char*)cpu->ebx, (fs_entry_t*)cpu->ecx, (uint32_t)cpu->edx);
        break;

    case 403:
        cpu->eax = sys_osinfo((struct osinfo*)cpu->ebx);
        break;
    }

    return esp;
}
