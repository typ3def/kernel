#include <kernel/paging.h>

#include <stdint.h>
#include <string.h>

#include <kernel/isr.h>
#include <kernel/kernel.h>
#include <kernel/tty.h>

#define INDEX_FROM_BIT(a) (a/(8*4))
#define OFFSET_FROM_BIT(a) (a%(8*4))

PageDirectory* kernel_directory = 0;
PageDirectory* current_directory = 0;

uint32_t* frames;
uint32_t nframes;

extern uint32_t placement_address;

static void set_frame(uint32_t frame_addr)
{
    uint32_t frame = frame_addr/0x1000;
    uint32_t idx = INDEX_FROM_BIT(frame);
    uint32_t off = OFFSET_FROM_BIT(frame);
    frames[idx] |= (0x1 << off);
}

static void clear_frame(uint32_t frame_addr)
{
    uint32_t frame = frame_addr/0x1000;
    uint32_t idx = INDEX_FROM_BIT(frame);
    uint32_t off = OFFSET_FROM_BIT(frame);
    frames[idx] &= ~(0x1 << off);
}

__attribute__((__unused__)) static uint32_t test_frame(uint32_t frame_addr)
{
    uint32_t frame = frame_addr/0x1000;
    uint32_t idx = INDEX_FROM_BIT(frame);
    uint32_t off = OFFSET_FROM_BIT(frame);
    return (frames[idx] & (0x1 << off));
}

static uint32_t first_frame()
{
    uint32_t i, j;
    for (i = 0; i < INDEX_FROM_BIT(nframes); i++) {
        if (frames[i] != 0xFFFFFFFF) {
            for (j = 0; j < 32; j++) {
                uint32_t toTest = 0x1 << j;
                if (!(frames[i]&toTest))
                    return i * 4 * 8 + j;
            }
        }
    }
	return 0;
}

void alloc_frame(Page* page, int is_kernel, int is_writeable)
{
    if (page->frame != 0) {
        return;
    } else {
        uint32_t idx = first_frame();
        if (idx == (uint32_t)-1) {
            // PANIC! no free frames!!
        }
        set_frame(idx*0x1000);
        page->present = 1;
        page->rw = (is_writeable)?1:0;
        page->user = (is_kernel)?0:1;
        page->frame = idx;
    }
}

void free_frame(Page *page)
{
    uint32_t frame;
    if (!(frame=page->frame)) {
        return;
    } else {
        clear_frame(frame);
        page->frame = 0x0;
    }
}

void init_paging()
{
    uint32_t mem_end_page = 0x1000000;
    
    nframes = mem_end_page / 0x1000;
    frames = (uint32_t*)kmalloc(INDEX_FROM_BIT(nframes));
    memset(frames, 0, INDEX_FROM_BIT(nframes));
    
    kernel_directory = (PageDirectory*)kmalloc_a(sizeof(PageDirectory));
    current_directory = kernel_directory;

    unsigned i = 0;
    while (i < placement_address) {
        alloc_frame( get_page(i, 1, kernel_directory), 0, 0);
        i += 0x1000;
    }
    register_interrupt_handler(14, page_fault);

    switch_page_directory(kernel_directory); // Paging enabled :)
}

void switch_page_directory(PageDirectory *dir)
{
    current_directory = dir;
    asm volatile("mov %0, %%cr3":: "r"(&dir->tables_physical));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0": "=r"(cr0));
    cr0 |= 0x80000000; // Enable paging!
    asm volatile("mov %0, %%cr0":: "r"(cr0));
}

Page *get_page(uint32_t address, int make, PageDirectory *dir)
{
    address /= 0x1000;
    uint32_t table_idx = address / 1024;
    if (dir->tables[table_idx]) {
        return &dir->tables[table_idx]->pages[address%1024];
    } else if(make) {
        uint32_t tmp;
        dir->tables[table_idx] = (PageTable*)kmalloc_ap(sizeof(PageTable), &tmp);
        dir->tables_physical[table_idx] = tmp | 0x7; // PRESENT, RW, US.
        return &dir->tables[table_idx]->pages[address%1024];
    } else {
        return 0;
    }
}


void page_fault(Registers regs)
{
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));
    
    int present   = !(regs.err_code & 0x1); // Page not present
    int rw = regs.err_code & 0x2;           // Write operation?
    int us = regs.err_code & 0x4;           // Processor was in user-mode?
    int reserved = regs.err_code & 0x8;     // Overwritten CPU-reserved bits of page entry?
    __attribute__((__unused__)) int id = regs.err_code & 0x10;          // Caused by an instruction fetch?

    terminal_writestr("Page fault! ( ");
    if (present) {terminal_writestr("present ");}
    if (rw) {terminal_writestr("read-only ");}
    if (us) {terminal_writestr("user-mode ");}
    if (reserved) {terminal_writestr("reserved ");}
    terminal_writestr(") at 0x");
    terminal_write_hex(faulting_address);
    terminal_writestr("\n");
    PANIC("Page fault");
}