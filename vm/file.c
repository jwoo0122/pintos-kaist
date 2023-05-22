/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "filesys/file.h"

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
	printf("[1]\n");
	struct file_page *file_page UNUSED = &page->file;
	printf("[2]\n");
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	/* Maybe file is closed. If file is closed we loose inode data. So reopen it surely */
	struct file *_file = file_reopen(file);
	int read_length = length > file_length(_file) ? file_length(_file) : length;
	
	void *addr_original = addr;
	
	while (read_length > 0) {
		struct args_for_lazy_load_segment *aux = malloc(sizeof(struct args_for_lazy_load_segment));
		
		int size_to_read = read_length > PGSIZE ? PGSIZE : read_length;
		
		aux->file = _file;
		aux->ofs = offset;
		aux->page_read_bytes = read_length;
		
		bool alloc_result = vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux);
		
		if (alloc_result) {
			offset += size_to_read;
			addr += PGSIZE;
			read_length -= size_to_read;
		} else {
			return NULL;
		}
	}
	
	return addr_original;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *t = thread_current();
	
	for (;;) {
		struct page *_page = spt_find_page(&t->spt, addr);
		
		if (_page == NULL) {
			/* End */
			return NULL;
		}
		
		if (pml4_is_dirty(t->pml4, _page->va)) {
			struct args_for_lazy_load_segment* aux = _page->uninit.aux;
			file_write_at(aux->file, addr, aux->page_read_bytes, aux->ofs);
			// clear
			pml4_set_dirty(t->pml4, _page->va, 0);
		}
		
		/* Clean up */
		pml4_clear_page(t->pml4, _page->va);
		
		// NEXT
		addr += PGSIZE;
	}
}
