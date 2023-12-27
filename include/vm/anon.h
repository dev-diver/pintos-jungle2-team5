#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"
struct page;
enum vm_type;

struct anon_page {
    int swapped_sectors; //swap된 sector의 수
    disk_sector_t disk_sector_no[4];
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
