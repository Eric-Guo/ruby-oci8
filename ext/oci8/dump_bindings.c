#ifdef RUBY_EXTCONF_H
#include "oci8.h"
#endif

#if defined __APPLE__ || defined __linux__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plthook.h"
#include <dlfcn.h>

typedef struct mem_map mem_map_t;
static int mem_map_init(mem_map_t *mm);
static int mem_map_get(mem_map_t *mm, const char **name, void **addr);

#ifdef __linux__
#include <link.h>

#define SOEXT "so"

struct mem_map {
    struct link_map *map;
};

static int mem_map_init(mem_map_t *mm)
{
    mm->map = _r_debug.r_map;
    return 0;
}

static int mem_map_get(mem_map_t *mm, const char **name, void **addr)
{
    while (mm->map != NULL) {
        struct link_map *map = mm->map;
        mm->map = map->l_next;
        if (map->l_name[0] == '/') {
            *name = map->l_name;
            *addr = (void*)map->l_addr;
            return 0;
        }
    }
    return -1;
}
#endif

#ifdef __APPLE__
#include <mach/mach.h>

#define SOEXT "dylib"

struct mem_map {
    mach_port_t task;
    vm_address_t addr;
    void *prev_fbase;
};

static int mem_map_init(mem_map_t *mm)
{
    mach_port_t task = 
    mm->task = mach_task_self();
    mm->addr = 0;
    mm->prev_fbase = NULL;
    return 0;
}

static int mem_map_get(mem_map_t *mm, const char **name, void **addr)
{
    vm_size_t size;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    memory_object_name_t object = 0;

    while (vm_region_64(mm->task, &mm->addr, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &info_count, &object) == KERN_SUCCESS) {
        Dl_info info;
        if (dladdr((void*)mm->addr, &info)) {
            if (mm->prev_fbase != info.dli_fbase) {
                mm->prev_fbase = info.dli_fbase;
                *name = info.dli_fname;
                *addr = info.dli_fbase;
                mm->addr += size;
                return 0;
            }
        }
        mm->addr += size;
    }
    return -1;
}
#endif

static int is_oracle_lib(const char *name)
{
    static const char * const oracle_libs[] = {
        "/libclntshcore." SOEXT,
        "/libnnz." SOEXT,
        "/libociicus." SOEXT,
        "/libociei." SOEXT,
        "/libclntsh." SOEXT,
        NULL,
    };

    for (int i = 0; oracle_libs[i] != NULL; i++) {
        if (strstr(name, oracle_libs[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static void dump_bindings_in_lib(const char *name, void *addr)
{
    plthook_t *ph;
    if (plthook_open_by_address(&ph, addr) != 0) {
        fprintf(stderr, "   plthook_open error: %s\n", plthook_error());
        return;
    }
    unsigned int pos = 0;
    plthook_entry_t entry;
    while (plthook_enum_entry(ph, &pos, &entry) == 0) {
        Dl_info info;
        if (dladdr(*entry.addr, &info)) {
            size_t addend = (size_t)*entry.addr - (info.dli_sname ? (size_t)info.dli_saddr : (size_t)info.dli_fbase);
#ifdef __APPLE__
            fprintf(stderr, "%s:%s+%x -> %s:%s+%zx\n",
                name, entry.name, entry.addend,
                info.dli_fname, info.dli_sname, addend);
#else
            fprintf(stderr, "%s:%s -> %s:%s+%zx\n",
                name, entry.name,
                info.dli_fname, info.dli_sname, addend);
#endif
        }
    }
    plthook_close(ph);
}

#define HAVE_OCI8_DUMP_BINDINGS
static void oci8_dump_bindings(void)
{
    mem_map_t mm;
    const char *name;
    void *addr;

    if (mem_map_init(&mm) != 0) {
        return;
    }
    while (mem_map_get(&mm, &name, &addr) == 0) {
        if (is_oracle_lib(name)) {
            dump_bindings_in_lib(name, addr);
        }
    }
}

#ifdef RUBY_EXTCONF_H
static VALUE oci8_s_dump_bindings(VALUE klass)
{
    oci8_dump_bindings();
    return Qnil;
}
#endif // RUBY_EXTCONF_H
#endif // defined __APPLE__ || defined __linux__

#ifdef RUBY_EXTCONF_H
void Init_oci8_dump_bindings(VALUE cOCI8)
{
#ifdef HAVE_OCI8_DUMP_BINDINGS
    rb_define_singleton_method_nodoc(cOCI8, "dump_bindings", oci8_s_dump_bindings, 0);
#endif
}
#endif

#ifdef DUMP_BINDIGINGS_MAIN
void OCIClientVersion(int *, int *, int *, int *, int *);

int main(int argc, char **argv)
{
    void *func = OCIClientVersion;
    oci8_dump_bindings();
    return 0;
}
#endif
