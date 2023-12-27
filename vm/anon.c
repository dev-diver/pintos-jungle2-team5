/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	printf("disk size: %d\n", disk_size(swap_disk));
	swap_table.map = bitmap_create(disk_size(swap_disk)); //내부에서 malloc 잡아줌.
	if(swap_table.map == NULL){
		PANIC("swap bitmap createion failed--disk is too large");
	}
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	//swap과 관련된 anon_page 설정
	anon_page->swapped_sectors = 0;
	for(int i = 0; i < SECTORS_PER_FRAME; i++){
		anon_page->disk_sector_no[i] = 0;
	}
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	disk_sector_t disk_sector_no;
	for(int i = 0; i < anon_page->swapped_sectors; i++){
		disk_sector_no = anon_page->disk_sector_no[i];
		disk_read(swap_disk, disk_sector_no, kva);
		bitmap_reset(swap_table.map, disk_sector_no);
	}
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	void *kva = page->frame->kva;

	// TODO: page의 유효 데이터에 따라 조정
	anon_page->swapped_sectors = SECTORS_PER_FRAME; 

	//현재 연속적으로 배치함
	size_t first_sector_no = bitmap_scan_and_flip(swap_table.map, 0, 
		anon_page->swapped_sectors, false);
	if(first_sector_no == BITMAP_ERROR){
		printf("not enough swap disk");
		exit(-1);
	}
	
	//swap 비연속으로 구현할 시 교체
	for(int i = 0; i < anon_page->swapped_sectors; i++){
		anon_page->disk_sector_no[i] = first_sector_no + i;
	}

	disk_sector_t disk_sector_no;
	for(int i = 0; i < anon_page->swapped_sectors; i++){
		disk_sector_no = anon_page->disk_sector_no[i];
		disk_write(swap_disk, disk_sector_no, kva);
		bitmap_mark(swap_table.map,disk_sector_no);
	}
	pml4_clear_page(page->pml4, page->va);
	bitmap_reset(frame_table.map, page->frame->frame_no);
	page->frame->page = NULL;
	page->frame = NULL;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if(page->frame){
		swap_out(page);
	}
}