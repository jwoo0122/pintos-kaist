/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"

static struct list frame_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		struct page *spt_page_for_user_process = malloc(sizeof(struct page));
		
		switch (VM_TYPE(type)) {
			case VM_ANON:
				uninit_new(spt_page_for_user_process, upage, init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new(spt_page_for_user_process, upage, init, type, aux, file_backed_initializer);
				break;
		}

		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		spt_page_for_user_process->is_writable = writable;
		return spt_insert_page(spt, spt_page_for_user_process);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	if (!list_empty(&spt->sptable_list)) {
		struct list_elem *e;
		
		for (e = list_front(&spt->sptable_list); e != list_end(&spt->sptable_list); e = list_next(e)) {
			struct page *_page = list_entry(e, struct page, spt_elem);
			
			if (_page->va == pg_round_down(va)) {
				return _page;
			}
		}
	}

	return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	list_push_back(&spt->sptable_list, &page->spt_elem);
	return true;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	 // FIXME: get victim, maybe we need frame priority... For now just pop from table
	struct list_elem *e = list_pop_front(&frame_table);
	victim = list_entry(e, struct frame, frt_elem);

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	
	void* candidate_virtual_address = palloc_get_page(PAL_USER);
	
	if (candidate_virtual_address == NULL) {
		// USER POOL IS FULL
		frame = vm_evict_frame(); // evict always success
	} else {
		frame->kva = candidate_virtual_address;
	}
	
	// FIXME: is it correct to doing like this?
	list_push_back(&frame_table, &frame->frt_elem);
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	struct thread *t = thread_current();
	bool alloc_result = vm_alloc_page(VM_ANON | VM_MARKER_0, t->stack_page_end - PGSIZE, 1);
	
	if (alloc_result) {
		t->stack_page_end -= PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	
	if (is_kernel_vaddr(addr)) {
		return false;
	}

	if (not_present) {
		struct thread *t = thread_current();
		void* current_rsp = user ? f->rsp : t->current_rsp;
		
		if (!vm_claim_page(addr)) {
			// x86 Push occurs fault 8 bytes below the rsp, so check the addr is 8bytes under.
			bool is_out_of_stack = current_rsp - 8 <= addr;
			bool is_over_1MB_stack = USER_STACK - 1000000 > addr;
			bool is_in_stack = USER_STACK >= addr;

			if (is_out_of_stack && !is_over_1MB_stack && is_in_stack) {
				vm_stack_growth(addr);
				return true;
			}
			
			return false;
		}

		return true;
	}
	
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct thread *t = thread_current();
	struct page *page = spt_find_page(&t->spt, va);
	
	if (page == NULL) {
		return 0;
	}
	
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	bool result = ((pml4_get_page (t->pml4, page->va) == NULL) && pml4_set_page (t->pml4, page->va, frame->kva, page->is_writable));

	if (!result) {
		return 0;
	}

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	list_init(&spt->sptable_list);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED) {
	// NOTE: current thread is child, containing dst table. src is from parent interrupt frame.
	
	if (!list_empty(&src->sptable_list)) {
		struct list_elem *e;
		
		for (e = list_front(&src->sptable_list); e != list_end(&src->sptable_list); e = list_next(e))  {
			struct page *src_p = list_entry(e, struct page, spt_elem);
			
			// Copy
			void* src_upage = src_p->va;
			bool writable = src_p->is_writable;
			
			// Candidate type, also if uninit (anon, file_backed...)
			enum vm_type src_p_type = page_get_type(src_p);
			
			// if page is uninit
			vm_initializer *init = src_p->uninit.init;
			void *aux = src_p->uninit.aux;

			if (src_p->operations->type == VM_UNINIT) {
				// Uninitialized pages
				bool alloc_with_init_result = vm_alloc_page_with_initializer(src_p_type, src_upage, writable, init, aux);
				
				if (!alloc_with_init_result)
					goto err;
			} else {
				// Already initialized pages, no need to use init
				bool alloc_result = vm_alloc_page(src_p_type, src_upage, writable);

				if (!alloc_result)
					goto err;

				// Claim the page, cause they're already claimed. (Not uninit)
				bool claim_result = vm_claim_page(src_upage);

				if (!claim_result)
					goto err;

				// Cause we already allocated the page, src_upage must be in dst.
				struct page *dst_p = spt_find_page(dst, src_upage);
				memcpy(dst_p->frame->kva, src_p->frame->kva, PGSIZE);
			}
		}
	}

	return true;
	
err:
	return false;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	if (!list_empty(&spt->sptable_list)) {
		struct list_elem *e = list_front(&spt->sptable_list);

		while (e != list_end(&spt->sptable_list)) {
			struct page *_page = list_entry(e, struct page, spt_elem);
			
			ASSERT(_page != NULL);

			e = list_remove(&_page->spt_elem);
			vm_dealloc_page(_page);
		}
	}
}
