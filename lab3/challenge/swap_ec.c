#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_ec.h>
#include <list.h>
#include <memlayout.h>

/*
Step 1: Scan the frame buffer from the current position.
        The first frame encountered with (u=0, m=0) is selected for replacement
        During this scan, make no change to the use bit.
Step 2: If step 1 fails, scan again, looking for the frame with (u=0; m=1).
        The first such frame encountered is selected for replacement.
        During this scan, set the use bit to 0 on each frame that is passed
Step 3: If step 2 fails, the pointer should have returned to its original position and all of the frames in the set will have a use bit of 0.
        Repeat step 1 and, if necessary, step 2.
        This time, a frame will be found for the replacement
 */

struct Page pra_page_head;
pde_t *pgdir;
/*
 * (2) _ec_init_mm: init pra_list_head and let  mm->sm_priv point to the addr of pra_list_head.
 *              Now, From the memory control struct mm_struct, we can access EC PRA
 */
static int
_ec_init_mm(struct mm_struct *mm)
{
    pra_page_head.ref = 0;
    list_init(&(pra_page_head.pra_page_link));
    mm->sm_priv = &(pra_page_head.pra_page_link);
    //cprintf(" mm->sm_priv %x in ec_init_mm\n",mm->sm_priv);
    return 0;
}
/*
 * (3)_ec_map_swappable: According EC PRA, we should link the most recent arrival page at the back of pra_list_head qeueue
 */
static int
_ec_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    list_entry_t *entry=&(page->pra_page_link);

    assert(entry != NULL && head != NULL);
    //record the page access situlation
    list_add_before(head, entry);

    page->pra_vaddr = addr;
    pgdir = mm->pgdir;
    return 0;
}
/*
 *  (4)_ec_swap_out_victim: According EC PRA, we should unlink the  earliest arrival page in front of pra_list_head qeueue,
 *                            then set the addr of addr of this page to ptr_page.
 */
static int
_ec_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
    list_entry_t *current =(list_entry_t*) mm->sm_priv;
    assert(current != NULL);
    assert(in_tick==0);

    /* Select the victim */
    list_entry_t *record = current;
    struct Page *page = NULL;
    int i;
    for (i = 0; i < 2; ++i) {
        /* Step 1 */
        current = record;
        while (1) {
            page = le2page(current, pra_page_link);
            if (page->ref != 0) {
                pte_t *ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
                if ((*ptep & (PTE_A | PTE_D)) == 0) {
                    goto find_victim;
                }
            }

            current = list_next(current);
            if (current == record) {
                break;
            }
        }
        /* Step 2 */
        current = record;
        while (1) {
            page = le2page(current, pra_page_link);
            if (page->ref != 0) {
                pte_t *ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
                if ((*ptep & PTE_A) == 0) {
                    goto find_victim;
                } else {
                    *ptep ^= PTE_A;
                }
            }

            current = list_next(current);
            if (current == record) {
                break;
            }
        }
    }
find_victim:
    mm->sm_priv = list_next(current);
    *ptr_page = le2page(current, pra_page_link);
    list_del(current);
    return 0;
}

void outputList() {
    list_entry_t *head = &(pra_page_head.pra_page_link);
    list_entry_t *now;
    struct Page *page = le2page(head, pra_page_link);
    cprintf("%p", page->pra_vaddr);
    pte_t *ptep;
    for (now = list_next(head); now != head; now = list_next(now)) {
        page = le2page(now, pra_page_link);
        ptep = get_pte(pgdir, page->pra_vaddr, 0);
        cprintf(" -> %p %d %d", page->pra_vaddr, !!(*ptep & PTE_A), !!(*ptep & PTE_D));
    }
    cprintf("\n");
}

void readPage(int i) {
    unsigned char *p = 0x1000 * i;
    cprintf("read Virt Page %p in ec_check_swap\n", p);
    unsigned char c = *p;
    outputList();
}

void writePage(int i) {
    unsigned char *p = 0x1000 * i;
    cprintf("write Virt Page %p in ec_check_swap\n", p);
    *p = i;
    outputList();
}

static int
_ec_check_swap(void) {
    int t = 0;
    outputList();
    readPage(5);
    assert(pgfault_num == 5);
    readPage(1);
    assert(pgfault_num == 6);
    readPage(3);
    assert(pgfault_num == 6);
    readPage(2);
    assert(pgfault_num == 7);
    readPage(3);
    assert(pgfault_num == 7);
    writePage(1);
    assert(pgfault_num == 7);
    readPage(4);
    assert(pgfault_num == 8);
    writePage(3);
    assert(pgfault_num == 9);
    writePage(4);
    assert(pgfault_num == 9);
    return 0;
}


static int
_ec_init(void)
{
    return 0;
}

static int
_ec_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_ec_tick_event(struct mm_struct *mm)
{ return 0; }


struct swap_manager swap_manager_ec =
{
     .name            = "ec swap manager",
     .init            = &_ec_init,
     .init_mm         = &_ec_init_mm,
     .tick_event      = &_ec_tick_event,
     .map_swappable   = &_ec_map_swappable,
     .set_unswappable = &_ec_set_unswappable,
     .swap_out_victim = &_ec_swap_out_victim,
     .check_swap      = &_ec_check_swap,
};
