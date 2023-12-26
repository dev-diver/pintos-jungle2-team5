/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "vm/file.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	
	// printf("page_kva %p page->va %p\n", page->frame->kva, page->va);
	if(pml4_is_dirty(thread_current()->pml4, page->va)){
		// printf("dirty page\n");
		off_t ofs = file_page -> ofs;
		size_t zero_bytes = file_page->zero_bytes;
		off_t writeback_size = zero_bytes ? PGSIZE - zero_bytes : PGSIZE;
		file_write_at(file_page->file, page->frame->kva, 
			writeback_size, ofs);
		pml4_set_dirty(thread_current()->pml4,page->va,false);
		pml4_clear_page(thread_current()->pml4,page->va);
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		int fd, off_t ofs) {
	
	//에러처리
	void *va = pg_round_down(addr);
	if(length == 0 || addr == 0 || va != addr 
		|| fd == 0 || fd == 1){
		return NULL;
	}

	struct file *file = file_reopen(thread_current()->fdt[fd]); //close해도 file 살아있게
	size_t read_bytes = file_length(file);

	if(read_bytes <= 0 || read_bytes - ofs <= 0){
		return NULL;
	}
	read_bytes-=ofs;

	//가상주소가 잡혀있는 페이지인지 확인
	// printf("read bytes : %d\n", read_bytes);
	int pages = (read_bytes / PGSIZE) + 1 ;
	// printf("page size before alloc : %d\n", pages);
	for(int i = 0; i < pages; i++){
		if(spt_find_page(&thread_current()->spt, va + i * PGSIZE) != NULL){
			return NULL;
		}
	}

	//페이지들에 맵핑
	while (read_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct seg_arg *args = calloc(1, sizeof(struct seg_arg));
		if(args == NULL){
			PANIC("Memory allocation failed\n");
			return false;
		}

		args->file = file;
		args->ofs = ofs;
		args->page_read_bytes = page_read_bytes;
		args->page_zero_bytes = page_zero_bytes;
		args->total_page = pages; //munmap을 위해 기록
		void *aux = args;
		if(!vm_alloc_page_with_initializer(VM_FILE, va, writable, lazy_load, aux)){
			exit(-1);
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		va += PGSIZE;
		ofs += page_read_bytes;
	}
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct page *page = spt_find_page(&thread_current()->spt, addr);
	if(!page){
		printf("not found \n");
		exit(-1);
	}

	void *va;
	struct file_page *file_page = &page->file; 
	int total_page = file_page->total_page;
	// printf("total_page_size : %d\n", total_page);
	for(int i = 0; i < total_page; i++){
		va = addr + i * PGSIZE;
		// printf("destroy page %d va %p\n", i, va);
		page = spt_find_page(&thread_current()->spt, va);
		if(!page){
			printf("cannot destroy page \n");
			exit(-1);
		}
		spt_remove_page(&thread_current()->spt, page);
	}
}

static bool
lazy_load(struct page *page, void *aux) {
	
	struct seg_arg *args = (struct seg_arg *)aux;
	struct file *file = args->file;
	off_t ofs = args->ofs;
	size_t page_read_bytes = args->page_read_bytes;
	size_t page_zero_bytes = args->page_zero_bytes;
	int total_page = args->total_page;

	file_seek(file, ofs);
	uint8_t *kpage = page->frame->kva;

	/* Load this page. */
	if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
		palloc_free_page (kpage); //frame 관리
		vm_dealloc_page(page);//page할당해제?
		return false;
	}
	memset (kpage + page_read_bytes, 0, page_zero_bytes);

	struct file_page *file_page = &page->file;
	file_page->file = file;
	file_page->total_page = total_page;
	file_page->ofs = ofs;
	file_page->zero_bytes = page_zero_bytes;

	free(aux);
	
	return true;
}
