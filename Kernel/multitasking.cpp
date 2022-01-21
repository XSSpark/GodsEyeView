#include "multitasking.hpp"
#include "Hardware/interrupts.hpp"

Task::Task(char* task_name, uint32_t eip, int priv)
{
    if (strlen(task_name) > 20)
        task_name = "Unknown";

    memcpy(name, task_name, strlen(task_name));
    name[strlen(task_name)] = '\0';

    cpustate = (cpu_state*)(stack + 4096 - sizeof(cpu_state));
    cpustate->eax = 0;
    cpustate->ebx = 0;
    cpustate->ecx = 0;
    cpustate->edx = 0;
    cpustate->esi = 0;
    cpustate->edi = 0;
    cpustate->ebp = 0;
    cpustate->eip = eip;
    cpustate->cs = g_gdt->code_segment_selector();
    cpustate->eflags = 0x202;

    memset(arguments, 0, 100);
    memset(execfile, 0, 20);
    memset(stdin_buffer, 0, 200);

    execute = 0;
    state = 0;
    privelege = priv;
    pid = ++g_lpid;
}

void Task::executable(executable_t exec)
{
    is_executable = true;
    loaded_executable = exec;
    cpustate->eip = exec.eip;
}

int8_t Task::notify(int signal)
{
    switch (signal) {
    case SIG_ILL:
        suicide(SIG_ILL);
        break;
    case SIG_TERM:
        suicide(SIG_TERM);
        break;
    case SIG_SEGV:
        suicide(SIG_SEGV);
        break;
    }
    return 0;
}

void Task::suicide(int error_code)
{
    state = 1;
}

Task::~Task()
{
}

TaskManager::TaskManager(GDT* gdt)
{
    g_gdt = gdt;
    active = this;
    num_tasks = 0;
    current_task = -1;
}

TaskManager* TM = 0;
TaskManager::~TaskManager()
{
}

bool TaskManager::add_task(Task* task)
{
    if (num_tasks >= 256)
        return false;
    is_running = 0;
    tasks[num_tasks++] = task;
    is_running = 1;
    return true;
}

bool TaskManager::append_tasks(int count, ...)
{
    va_list list;
    va_start(list, count);

    for (int i = 0; i < count; i++)
        add_task(va_arg(list, Task*));
    va_end(list);
    return true;
}

void TaskManager::kill()
{
    tasks[current_task]->suicide(SIG_TERM);
}

int8_t TaskManager::send_signal(int pid, int sig)
{
    for (int i = 0; i < num_tasks; i++)
        if (tasks[i]->pid == pid)
            if (tasks[i]->privelege <= tasks[current_task]->privelege)
                return tasks[i]->notify(sig);
    return -1;
}

void TaskManager::append_stdin(char key)
{
    if (is_reading_stdin)
        kprintf("%c", key);

    size_t length = strlen(tasks[current_task]->stdin_buffer);
    if (key == '\b') {
        tasks[current_task]->stdin_buffer[length - 1] = 0;
    } else {
        tasks[current_task]->stdin_buffer[length] = key;
        tasks[current_task]->stdin_buffer[length + 1] = 0;
    }
}

void TaskManager::reset_stdin()
{
    memset(tasks[current_task]->stdin_buffer, 0, 200);
}

uint32_t TaskManager::read_stdin(char* buffer, uint32_t length)
{
    if (!length)
        return 0;

    uint32_t size = 0;
    reset_stdin();
    is_reading_stdin = true;

    while (true) {
        size = strlen(get_stdin());

        if (TM->get_stdin()[size - 1] == 10) {
            strncpy(buffer, get_stdin(), length);
            buffer[size - 1] = 0;
            reset_stdin();
            break;
        }
    }

    is_reading_stdin = false;
    return size - 1;
}

void TaskManager::sleep(uint32_t ticks)
{
    tasks[current_task]->sleeping = current_ticks + ticks;
}

int TaskManager::spawn(char* file, char* args)
{
    int fd = VFS::open(file);
    int size = VFS::size(fd);

    if (fd < 0)
        return -1;

    uint8_t* elfdata = (uint8_t*)kmalloc(size);
    VFS::read(fd, elfdata);
    VFS::close(fd);

    executable_t exec = Loader::load->exec(elfdata);
    kfree(elfdata);

    if (!exec.valid)
        return -1;

    Task* child = new Task(file, 0);
    strcpy(child->execfile, file);
    child->executable(exec);
    child->is_child = true;

    strcpy(child->arguments, args);
    strcpy((char*)child->cpustate->ebx, (char*)child->arguments);
    add_task(child);

    return child->pid;
}

void TaskManager::kill_zombie_tasks()
{
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i]->state == 1) {
            klog("Zombie process '%s' killed", tasks[i]->name);
            if (tasks[i]->is_child)
                free(tasks[i]);
            delete_element(i, num_tasks, tasks);
            num_tasks--;
        }
    }
}

cpu_state* TaskManager::schedule(cpu_state* cpustate)
{
    current_ticks++;
    if (current_task >= 0)
        tasks[current_task]->cpustate = cpustate;

    if (++current_task >= num_tasks)
        current_task = 0;

    if (tasks[current_task]->sleeping != 0) {
        if (current_ticks >= tasks[current_task]->sleeping) {
            tasks[current_task]->sleeping = 0;
        } else {
            current_task++;
            if (++current_task >= num_tasks)
                current_task = 0;
        }
    }

    kill_zombie_tasks();
    if (current_task >= num_tasks)
        current_task = 0;

    if ((num_tasks <= 0) || (is_running == 0))
        return cpustate;

    return tasks[current_task]->cpustate;
}
