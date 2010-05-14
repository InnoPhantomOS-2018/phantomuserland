/**
 *
 * Phantom OS
 *
 * Copyright (C) 2005-2009 Dmitry Zavalishin, dz@dz.ru
 *
 * Kernel ready: it is a question.
 * Preliminary: no
 *
 *
**/

#define DEBUG_MSG_PREFIX "pager"
#include "debug_ext.h"
#define debug_level_flow 6
#define debug_level_error 10
#define debug_level_info 10


/**
 *
 * TODO REVIEW for kernel
 **/
//---------------------------------------------------------------------------

#define ERRS 0


#include <phantom_libc.h>


#include <kernel/vm.h>
#include <threads.h>

#include <i386/trap.h>
#include <i386/proc_reg.h>



#include "vm_map.h"
#include "dpc.h"
#include "snap_sync.h"

#include "i386/cpu.h"
#include "i386/mmx.h"

#define hal_panic panic
#define volatile /* */






static int DEBUG_MARK = 0;
static int SNAP_DEBUG = 1;
static int SNAP_STEPS_DEBUG = 1;
static int SNAP_LISTS_DEBUG = 0;
static int FAULT_DEBUG = 0;
static int PAGING_DEBUG = 0;
static int COW_DEBUG = 0;
static int PAGEOUT_DEBUG = 0;








// We do atomic works with this mutex taken.
// If something long happens with ne page we
// set
static hal_mutex_t vm_map_mutex;

//vm_map_impl   vm_map;
//spinlock    vm_page_lock;
//static hal_mutex_t vm_page_mutex;


static void vm_map_deferred_disk_alloc_thread(void);
static void vm_map_lazy_pageout_thread(void);


static void page_clear_engine_init(void);
static void page_clear_engine_clear_page(phys_page_t p);




static hal_cond_t      deferred_alloc_thread_sleep;



// for each copy of system this address can't change - we
// keep it in a pagefile.

static void *              vm_map_start_of_virtual_address_space;
static unsigned long       vm_map_vm_page_count;             // how many pages VM has

static vm_page *           vm_map_map;                       // array of pages
static vm_page *           vm_map_map_end;                   // a byte after map

static int last_snap_is_done = 0;


void                vm_map_wait_for_finish() { while(!last_snap_is_done) hal_sleep_msec(100); }
void *              vm_map_get_object_address_space_start() { return vm_map_start_of_virtual_address_space; }


//---------------------------------------------------------------------------




//static void process_deferred_allocations(void *arg);
//static volatile pager_io_request *deferred_allocations = 0; // memory
//dpc_request deferred_allocation_dpc;

//static volatile vm_page *deferred_disk_allocations = 0;

unsigned char      phantom_vm_generation; // system's current generation number

// generation is cycling, do special comparison
static int __inline__ page_generation_is_less( unsigned char page_gen )
{
    // take wraparound in account
    if( phantom_vm_generation & 0x80 ) // upper bit? Move both down a half
        return (page_gen - 0x80) < (phantom_vm_generation - 0x80);
    else
        return page_gen < phantom_vm_generation;
}






static void    page_fault( vm_page *p, int  is_writing );

static void
vm_map_page_fault_handler( void *address, int  write, int ip )
{
    // BUG! TODO! Stack growth? Object space growth?
    long addr = (unsigned int) address;

    addr -= (unsigned int)vm_map_start_of_virtual_address_space;
    /*
    if( addr < 0 || addr >= (vm_map_vm_page_count*__MEM_PAGE) )
    {
        dump_ds
        hal_panic("fault address 0x%X is outside of object space, IP 0x%X", address, ip);
    }
    */

    int pageno = addr / __MEM_PAGE;

    vm_page *vmp = vm_map_map + pageno;

    if(FAULT_DEBUG) hal_printf("fault 0x%X pgno %d\n", addr, pageno );

    hal_mutex_lock(&vm_map_mutex);

    // Someone is taking care of ths page, hands off.
    // It is supposed that later faults are of the same nature
    // and will be covered by first one's processing.
    // If not - we will get fault back.
    if( vmp->flag_dont_touch || vmp->flag_busy )
    {
        hal_mutex_unlock(&vm_map_mutex);
        hal_mutex_lock(&vmp->lock);
        while( vmp->flag_dont_touch || vmp->flag_busy )
        {
            if(FAULT_DEBUG) hal_printf("donttouch/busy 0x%X\n", vmp->virt_addr );
            hal_cond_wait(&vmp->done,&vmp->lock);
        }
        hal_mutex_unlock(&vmp->lock);
        return;
    }

    vmp->flag_busy = 1;
    page_fault( vmp, write );
    hal_mutex_unlock(&vm_map_mutex);
    hal_mutex_lock(&vmp->lock);
    vmp->flag_busy = 0;
    // We have to do this only if we opened mutex in page_fault(). Track that later.
    hal_cond_broadcast(&vmp->done); // Will do no harm.
    hal_mutex_unlock(&vmp->lock);

}


/*
 * Trap handler. All we (currently) care about is page faults. Everything
 * else is passed through.
 */
int
vm_map_page_fault_trap_handler(struct trap_state *ts)
{
    if (ts->trapno == T_DEBUG)
    {
        //hal_printf("\n\n\n --- debug trap at EIP=0x%X --- \n\n\n", ts->eip );
        return 0;
    }

    if (ts->trapno == T_PAGE_FAULT) {
        //int enabled = hal_save_sti();

        ts->cr2 = get_cr2();

        {
            long addr = (unsigned int) ts->cr2;

            addr -= (unsigned int)vm_map_start_of_virtual_address_space;

            if( addr < 0 || addr >= (vm_map_vm_page_count*__MEM_PAGE) )
            {
                dump_ss(ts);
                hal_panic("fault address 0x%X is outside of object space, IP 0x%X", (unsigned int) ts->cr2, ts->eip);
            }
        }

        vm_map_page_fault_handler( (void *)ts->cr2, ts->err & T_PF_WRITE, ts->eip );
        /*
         if (enabled)			cli();
         */

        return 0;
    }

    /*
     * Not a page fault. Pass it through to the application as
     * a signal. If signal handling is not enabled, a trap dump
     * will be generated.
     */
    panic("Not a page fault in vm_map_page_fault_trap_handler\n");
}














void
vm_page_init( vm_page *me, void *my_vaddr)
{
    // BUG? Page generation number will be zero?
    memset( me, 0, sizeof(vm_page) );
    me->virt_addr = my_vaddr;
    //hal_cond_init(&me->sleepStone, "VM PG");
    hal_cond_init(&me->done, "VM PG");
    hal_mutex_init(&me->lock, "VM PG" );
}




void
vm_map_init(unsigned long page_count)
{
    page_clear_engine_init();

    vm_map_vm_page_count = page_count;

    int mapsize = vm_map_vm_page_count*sizeof(vm_page);

    vm_map_map = (vm_page *)malloc( mapsize );
    memset( vm_map_map, 0, mapsize );

    vm_map_map_end = vm_map_map + page_count;

    vm_map_start_of_virtual_address_space = (void *)hal_object_space_address();

    hal_mutex_init(&vm_map_mutex, "VM Map");

    hal_mutex_lock(&vm_map_mutex);

    int np;
    for( np = 0; np < page_count; np++ )
        vm_page_init( &vm_map_map[np], ((char *)vm_map_start_of_virtual_address_space) + (__MEM_PAGE * np) );

    if(pager_superblock_ptr()->last_snap == 0 )
    {
        hal_printf("\n\nNo pagelist to load\n\n");
        //hal_panic("vmem load: no pagelist!");
    }
    else
    {

        hal_printf("Loading pagelist from %d...", pager_superblock_ptr()->last_snap);

        pagelist loader;
        pagelist_init( &loader, pager_superblock_ptr()->last_snap, 0, DISK_STRUCT_MAGIC_SNAP_LIST );

        pagelist_seek(&loader);

        for( np = 0; np < page_count; np++ )
        {
            if( !pagelist_read_seq(&loader, &vm_map_map[np].prev_page) )
            {
                printf("Incomplete pagelist\n");
                //panic("Incomplete pagelist\n");
                break;
            }

            // Zero page means we have no data fr this block and it must be zero
            vm_map_map[np].flag_have_prev = (vm_map_map[np].prev_page != 0);
        }

        pagelist_finish( &loader );
    }

    //dpc_request_init(&deferred_allocation_dpc,process_deferred_allocations);

    hal_init_object_vmem(vm_map_start_of_virtual_address_space);

    hal_cond_init(&deferred_alloc_thread_sleep, "Deferred");

    hal_mutex_unlock(&vm_map_mutex);

#if 1
    hal_start_kernel_thread(vm_map_deferred_disk_alloc_thread);
    hal_start_kernel_thread(vm_map_lazy_pageout_thread);
#endif
    // Ok, everything is ready now. Turn on pagefaults handling
    phantom_trap_handlers[T_PAGE_FAULT] = vm_map_page_fault_trap_handler;


}


static int  stop_deferred_disk_alloc_thread =  0;
static int  stop_lazy_pageout_thread        =  0;

void vm_map_finish(void)
{
    stop_lazy_pageout_thread = 1;
    // stop_deferred_disk_alloc_thread is asserted in lazy pageout thread
    // after the last snapshot
}









static int  is_unmapped( volatile vm_page *p ) { return !p->flag_phys_mem; }
static int  is_mapped( volatile vm_page *p ) { return p->flag_phys_mem != 0; }

void vm_map_for_unmapped( vmem_page_func_t func ) {     vm_map_do_for( func, is_unmapped ); }
void vm_map_for_mapped( vmem_page_func_t func ) {     vm_map_do_for( func, is_mapped ); }



// -------------
// Global snapshot related variables
int     is_in_snapshot_process = 0;
//int     snapshot_swapout_queue_size = 0;
// -------------



/*
 // called under the lock!
 static void snap_clean_page( vm_page *p )
 {
 if(p->flag_snapped )        hal_panic("snap_clean got snapped page");
 if(p->flag_phys_dirty)      hal_panic("snap_clean got dirty page");
 if(p->flag_have_make)       hal_panic("snap_clean got flag_have_make set");

 // well, now page can have no current if it did not changed since last snap
 if(!p->flag_have_curr)
 {
 // in this case we do nothing. new snap will get it from previous one
 p->flag_snapped = 1;
 return;
 }

 p->make_page = p->curr_page;
 p->flag_have_make = 1;
 p->flag_have_curr = 0;
 p->flag_snapped = 1;
 }
 */



//---------------------------------------------------------------------------
//       vm_page helpers - CALL UNDER MUTEX!!!
//---------------------------------------------------------------------------

/*
void
vm_page_req_deferred_disk_alloc(vm_page *me)
{
    if(me->flag_dfda_active) return;
    me->flag_dfda_active = 1;

    me->dfda_next = (vm_page *)deferred_disk_allocations;
    deferred_disk_allocations = (volatile vm_page *)me;

    hal_printf("req deferred disk alloc 0x%X\n", me->virt_addr );

    // wake allocator
    //hal_wakeup(&deferred_disk_allocations);
    hal_cond_broadcast( &deferred_alloc_thread_sleep );
}*/

// Called under the lock

void
vm_page_req_pageout(vm_page *me)
{
    extern void pageout_callback( pager_io_request *req, int  write );

    if(PAGING_DEBUG) hal_printf("req pageout 0x%X: ", me->virt_addr );

#if ERRS
#error check me->flag_dont_touch and me->flag_busy?
#endif

    if(me->flag_pager_io_busy)
    {
        if(PAGING_DEBUG) hal_printf("already on pager\n" );
        return;
    }

    if(!me->flag_phys_mem)
    {
        if(PAGING_DEBUG) hal_printf("no phys mem\n" );
        return;
    }
    if(!me->flag_phys_dirty)
    {
        if(PAGING_DEBUG) hal_printf("not dirty\n" );
        return;
    }

    if(me->flag_dont_touch)
    {
        if(1||PAGING_DEBUG) hal_printf("flag_dont_touch - skip pgout\n" );
        return;
    }

    if(!me->flag_have_curr)
    {
        if(PAGING_DEBUG) hal_printf("no curr disk page\n" );
        //return;

        // Ask them to allocate us some disk space.
        if(PAGING_DEBUG) hal_printf("ask disk block 0x%X\n", me->virt_addr );

        if( !pager_alloc_page(&me->curr_page) )
        {
            hal_panic("can't alloc disk page in req pageout");
        }
        me->flag_have_curr = 1;
        if(PAGING_DEBUG) hal_printf("got disk block for 0x%X\n", me->virt_addr );
    }

    hal_page_control(me->phys_addr, me->virt_addr, page_map, page_ro );

    me->flag_phys_protect = 1;
    me->flag_dont_touch = 1;

    me->flag_pager_io_busy = 1;
    //me->pager_io.virt_addr = me->virt_addr;
    me->pager_io.phys_page = me->phys_addr;
    me->pager_io.pager_callback = pageout_callback;
#if ERRS
#error Not sure we allways can write to curr page!
#endif
    me->pager_io.disk_page = me->curr_page;

    if(PAGEOUT_DEBUG||PAGING_DEBUG) hal_printf("really req pageout\n" );
    pager_enqueue_for_pageout( /*(pager_io_request*)*/&me->pager_io );

    // Following lines added to prevent hang due to some races.
    // BUG: Races must be fixed, surely...
#if 0
    while(me->flag_pager_io_busy)
    {
        // races! pager does not take vm_map_mutex before modifying flag_pager_io_busy
        //hal_cond_wait(&me->sleepStone,&vm_map_mutex);
        hal_mutex_unlock(&vm_map_mutex);
        hal_sleep_msec( 1 ); // TODO: yield?
        hal_mutex_lock(&vm_map_mutex);
    }
    if(PAGING_DEBUG) hal_printf("pageout done\n" );
#endif
}


void pageout_callback( pager_io_request *req, int write )
{
    assert(write);
    // BUG!?
    //hal_mutex_lock(&vm_map_mutex); // BUG - can we wait in callback?

    // Called after we asked for pageout

    vm_page *vmp = (vm_page *)req;
    if(PAGEOUT_DEBUG||PAGING_DEBUG) hal_printf("pageout callback 0x%X\n", vmp->virt_addr );

    if(!vmp->flag_pager_io_busy) hal_panic("pager_io not busy?");

    //if(!write) hal_panic("pageout_callback after read!?");
    req->pager_callback = 0;

    hal_mutex_lock(&vmp->lock);
    vmp->flag_phys_dirty = 0; // just saved out, we're clean
    vmp->flag_pager_io_busy = 0;
    vmp->flag_dont_touch = 0; // let them run!

    hal_cond_broadcast(&vmp->done);
    hal_mutex_unlock(&vmp->lock);
}




//---------------------------------------------------------------------------
//       Page Fault
//---------------------------------------------------------------------------

void snapper_COW_callback( pager_io_request *req, int  write );

// this code will do fault-time snapshot work
// returns 1 if caller must return now
static int
page_fault_snap_aid( vm_page *p, int  page_is_read_enabled, int  is_writing  )
{
    // 1. We are called only for user's write access attempt!
    assert(is_writing);

    // Sanity check
    //if( p->flag_have_make ) hal_panic("page_fault_snap_aid - have make page??");
    //assert(!p->flag_have_make);

    // Is it OK?
    if(p->flag_have_make)
        return 0; // No special snap treatment needed, or, maybe, already done

    if( !p->flag_phys_mem )
    {
        // Since we're not backed, have only actual copy on disk.
        // this copy will go to snapshot, but we'll read it in
        // for process to go on. And later will swap out to a separate
        // disk page

        if(SNAP_DEBUG) hal_printf("snapaid !physmem 0x%X\n", p->virt_addr );

        // after we will return 0 caller will alloc phys page.

        // where precisely on disk?
        if( p->flag_have_curr )
        {
            p->make_page = p->curr_page;
            p->flag_have_curr = 0;
            p->flag_have_make = 1;
            p->generation = phantom_vm_generation;
            return 0; // Do standard write fault processing
        }

        if( p->flag_have_prev )
        {
            // do nothing, just mark we're up to date, since
            // page did not change since previous snap. This situation
            // has to be taken care about in snap finalization.
            // Code there must move page to const block, possibly.
            // (A block that keeps pages that do not change for
            // generations.)
            p->generation = phantom_vm_generation;
            return 0; // Do standard write fault processing
        }

        // BUG? Can we just let him go? We definitely shall not write him?
        p->make_page = 0;
        p->flag_have_make = 1;
        p->generation = phantom_vm_generation;
        return 0; // Do standard write fault processing

        //hal_panic("page_fault_snap_aid - unmapped unbacked page");
        // NO - done ater return! activate swapin and put thread sleeping
    }

    // we're in RAM, but still need one more RAM page


    /* TODO: We can start pageout even if no new RAM page is available */

    if(COW_DEBUG||SNAP_DEBUG) hal_printf("snapaid COW 0x%X\n", p->virt_addr );

    physaddr_t  new_phys;
    if( hal_alloc_phys_page(&new_phys) )
        panic("out of phys mem, no deferred alloc");

    hal_copy_page_v2p( new_phys, p->virt_addr );


    assert(!p->flag_pager_io_busy);

    p->flag_pager_io_busy = 1;

    // pass new page to pager
    p->pager_io.phys_page = new_phys; // kvtophys( new_addr );
    p->pager_io.pager_callback = snapper_COW_callback;

    // current disk page will come to snap - activate swapout now


    if( p->flag_have_curr )
    {
        assert(!p->flag_have_make);

        p->make_page = p->curr_page;
        p->flag_have_curr = 0;
        p->flag_have_make = 1;
        // BUG - or should we update generation on pageout completion?
        p->generation = phantom_vm_generation;
    }
    else
    {
        //hal_panic("no curr disk page on in-snap mem write pagefault");
        if( !pager_alloc_page(&(p->make_page)) )
            hal_panic("can't alloc disk page in req pageout");
        p->flag_have_make = 1;
    }

    // start pageout
    if(SNAP_DEBUG) hal_printf("req pageout fast");
    p->pager_io.disk_page = p->make_page;

    hal_page_control(p->phys_addr, p->virt_addr, page_map, page_rw );
    p->flag_phys_protect = 0;

    p->access_count++;
    p->flag_phys_dirty = 1; // we'll be dirty after return from trap
    // Done!

    assert(p->flag_phys_mem);

    p->flag_dont_touch = 1; // Long op on page, will be reset in COW cllback

    // NB! Must be last - it does not check mutex - we
    // must finish all the modifications before starting
    pager_enqueue_for_pageout_fast( &(p->pager_io) );

    return 1; // Don't do standard write fault processing
}





void snapper_COW_callback( pager_io_request *req, int  write )
{
    // We are not taking mutex here because p->flag_dont_touch is true
    // and we are in the DPC thread, so can't (and souldn't) wait
    //hal_mutex_lock(&vm_map_mutex); // BUG! Can we lock here? We will put somebody like DPC thread in sleep

    // Called after we copied write-attempted page to side-allocated
    // storage and kicked it to be paged out to a snapshot.

    vm_page *vmp = (vm_page *)req;
    if(COW_DEBUG||SNAP_DEBUG) hal_printf("COW callback 0x%X\n", vmp->virt_addr );

    assert(vmp->flag_dont_touch);
    assert(vmp->flag_pager_io_busy);
    assert(write);
    //if(!vmp->flag_pager_io_busy) hal_panic("COW: pager_io not busy?");
    //if(!write) hal_panic("snapper_COW_callback after read!?");
    req->pager_callback = 0;

    // release memory used to hold page.

    //hal_mutex_unlock(&vm_map_mutex); // BUG - see above, can we wait in callback?
    hal_free_phys_page(req->phys_page);

    // TODO can we sleep here? who calls us? DPC? We'll stop DPC thread then?
    // TODO have thread pool in DPC?
    hal_mutex_lock(&vmp->lock);
    vmp->flag_pager_io_busy = 0;
    vmp->flag_dont_touch = 0; // let them run!
    hal_cond_broadcast(&vmp->done);
    hal_mutex_unlock(&vmp->lock);
}







static void
pagein_callback( pager_io_request *p, int  pageout )
{
    //hal_cli(); // BUG: can we sleep in callback?
    //hal_mutex_lock(&vm_map_mutex);

    vm_page *vmp = (vm_page *)p;
    if(PAGING_DEBUG) hal_printf("pagein callback 0x%X\n", vmp->virt_addr );

    assert(vmp->flag_pager_io_busy);
    assert(!pageout);
    //if(!vmp->flag_pager_io_busy) hal_panic("pager_io not busy in pagein callback?");
    //if(pageout) hal_panic("pageout came to pahein callback");


    p->pager_callback = 0;


    hal_page_control( vmp->phys_addr, vmp->virt_addr, page_map, page_ro );
    vmp->flag_phys_protect = 1;
    vmp->flag_phys_dirty = 0;

    hal_mutex_lock(&vmp->lock);
    vmp->flag_pager_io_busy = 0;
    //vmp->flag_dont_touch = 0; // let them run!
    hal_cond_broadcast(&vmp->done); // wakeup threads waiting for page
    hal_mutex_unlock(&vmp->lock);

    //hal_sti();
    //hal_mutex_unlock(&vm_map_mutex);

}



// Mutex is taken!
// Process for memory read faults
static void
page_fault_read( vm_page *p )
{
    //int  page_is_read_enabled = (p->flag_phys_mem) && (!p->flag_phys_protect);

    // just read from mapped, but locked for statistics page
    // it is the same for snapshot and normal operation mode

    // NB! Can it happen at all? Do we need to check CPU ppage access flag instead?
    if( /*(!page_is_read_enabled ) && */ p->flag_phys_mem )
    {
        // counter strike :)
        p->access_count++;
        p->flag_phys_protect = 1;
        hal_page_control( p->phys_addr, p->virt_addr, page_map, page_ro );
        return;
    }

    // read from unmapped page occured
    // it is the same for snapshot and normal operation mode

    /*if( p->flag_phys_mem )
        hal_panic("page has phys mem on read access fault - ??");*/


    if(FAULT_DEBUG) hal_printf("unmapped read 0x%X\n", p->virt_addr );
    // counter strike :)
    p->access_count++; // well, we shouldnt, but what the hell...

    // pagein is already in progress, so...
    while(p->pager_io.flag_pagein)
    {
        // Frankly, it must not be possible - if pagein is active, then
        // page is mapped...
#if 0
        hal_cond_wait(&p->sleepStone,&vm_map_mutex);
#else
        hal_mutex_unlock(&vm_map_mutex);
        hal_sleep_msec( 1 ); // TODO: yield?
        hal_mutex_lock(&vm_map_mutex);
#endif
        // we'll just return, and if page is there -
        // nothing will happen. If not - another fault will come.
        return;
    }

    // Allocate phys mem
    {
        phys_page_t newp;

        // TODO: on alloc fail just wake up pageout thread and sleep on
        // some 'mem avail' event, then retry.
        while(hal_alloc_phys_page(&newp))
        {

            hal_panic("deferred alloc not implemented!");
            // TODO request some pageout works, etc
            //request_more_ram = 1;
            //hal_cond_wait(&ramSleepStone,&vm_map_mutex);
        }

        p->phys_addr = newp;
        p->flag_phys_mem = 1;
        p->flag_phys_dirty = 0;
        p->flag_phys_protect = 1; // read access.
        hal_page_control( p->phys_addr, p->virt_addr, page_map, page_ro );
    }

    // Allright, decide where to read from
    if(p->flag_pager_io_busy) hal_panic("pager_io busy?");

    // if we have current and want just read, we are paging it in
    // in any state
    if     ( p->flag_have_curr )    p->pager_io.disk_page   = p->curr_page;
    else if( p->flag_have_make )    p->pager_io.disk_page   = p->make_page;
    else if( p->flag_have_prev )    p->pager_io.disk_page   = p->prev_page;
    else
    {
        // They're trying to read from unallocated page.
        // BUG! In fact, we are better send them an exception
        // because it looks like an error. But for now we'll just give
        // 'em an empty page.

        page_clear_engine_clear_page(p->phys_addr);
        p->flag_phys_dirty = 0;
        p->flag_phys_protect = 1; // read access - see below.

        hal_page_control( p->phys_addr, p->virt_addr, page_map, page_ro );
        return;
    }

    // HACK! We set have make and make_page = 0 on unused page
    if(p->flag_have_make && p->make_page == 0 && !p->flag_have_curr && !p->flag_have_prev )
    {
        page_clear_engine_clear_page(p->phys_addr);
        p->flag_phys_dirty = 0;
        p->flag_phys_protect = 1; // read access - see below.

        hal_page_control( p->phys_addr, p->virt_addr, page_map, page_ro );
        return;
    }

    if(FAULT_DEBUG) hal_printf("start pagein 0x%X\n", p->virt_addr );

    p->flag_pager_io_busy = 1;
    // start pagein, passing wakeup for this thread callback
    //p->pager_io.virt_addr = p->virt_addr;
    p->pager_io.phys_page = p->phys_addr;
    p->pager_io.pager_callback = pagein_callback;

    pager_enqueue_for_pagein( &(p->pager_io) );

#if 0
    while(p->flag_pager_io_busy)
    {
        hal_cond_wait(&p->sleepStone,&vm_map_mutex);
    }
#else
    while(p->flag_pager_io_busy)
    {
        // races! pager does not take vm_map_mutex before modifying flag_pager_io_busy
        //hal_cond_wait(&me->sleepStone,&vm_map_mutex);
        hal_mutex_unlock(&vm_map_mutex);
        hal_sleep_msec( 1 ); // TODO: yield?
        hal_mutex_lock(&vm_map_mutex);
    }
#endif
}






// Mutex is taken!
// Process for memory write faults
static void
page_fault_write( vm_page *p )
{
    if(FAULT_DEBUG) hal_printf("write 0x%X\n", p->virt_addr );
    // we're here if it was write (and, possibly, page is not mapped)

    // Can it happen here with flag_busy taken?
    if(p->pager_io.flag_pagein || p->pager_io.flag_pageout)
    {
        if(FAULT_DEBUG) hal_printf("write nosnap go asleep on pager io 0x%X\n", p->virt_addr );
#if 0
        hal_cond_wait(&p->sleepStone,&vm_map_mutex);
#else
#if ERRS
#error Why do we check here internal pagers flags?
#endif
        while( p->flag_pager_io_busy || p->pager_io.flag_pagein || p->pager_io.flag_pageout )
        {
            hal_mutex_unlock(&vm_map_mutex);
            hal_sleep_msec( 1 ); // TODO: yield?
            hal_mutex_lock(&vm_map_mutex);
        }
#endif

        return;
    }

    int  page_is_read_enabled = (p->flag_phys_mem) && (!p->flag_phys_protect);

    // we have to aid snapping of this page
    if( is_in_snapshot_process && page_generation_is_less( p->generation ) )
        if( page_fault_snap_aid( p, page_is_read_enabled, 1  ) )
            return;

    if(FAULT_DEBUG) hal_printf("get page to write 0x%X\n", p->virt_addr );

    // we are not snapping or already did all we need for snap.
    // Now get us a page to write to if we still have no!

    if(p->flag_phys_mem)
    {
        if(FAULT_DEBUG) hal_printf("have physmem 0x%X\n", p->virt_addr );
        if(p->flag_phys_protect)
        {
            hal_page_control( p->phys_addr, p->virt_addr, page_map, page_rw );
            p->flag_phys_protect = 0;
            p->access_count++;
            p->flag_phys_dirty = 1; // we'll be dirty after return from trap
            if(FAULT_DEBUG) hal_printf("unprotect to write 0x%X\n", p->virt_addr );
        }
        return;
    }

    //if(p->flag_io_phys_mem) panic("still have flag_io_phys_mem");

    // Get us some phys mem

    if(0||FAULT_DEBUG) hal_printf("get physmem 0x%X\n", p->virt_addr );


    // try to alloc phys page. if cant - kick allocator to find free space
    // (this will result in swapout, usually), then schedule dpc and put
    // thread to sleep
    phys_page_t newp;

    while(hal_alloc_phys_page(&newp))
    {

        hal_panic("deferred alloc not implemented!");
        // TODO request some pageout works, etc
        //request_more_ram = 1;

        //hal_cond_wait(&ramSleepStone,&vm_map_mutex); // NB! Mutex must be locked!

    }

    // got some - now pagein.

    p->phys_addr = newp;
    hal_page_control( p->phys_addr, p->virt_addr, page_map, page_rw );
    p->flag_phys_mem = 1;
    p->flag_phys_dirty = 1; // or set it after pagein?
    p->flag_phys_protect = 0; // pager has to write there - or can it anyway?

    if(p->flag_pager_io_busy) hal_panic("pager_io busy?");
    p->flag_pager_io_busy = 1;

    if     ( p->flag_have_curr ) p->pager_io.disk_page = p->curr_page;
    else if( p->flag_have_make ) p->pager_io.disk_page = p->make_page;
    else if( p->flag_have_prev ) p->pager_io.disk_page = p->prev_page;
    else
    {
        // Just clear page here as it is new

        p->flag_pager_io_busy = 0; // need not
        //memset( p->virt_addr, 0, __MEM_PAGE /*BUG!*/ );
        //#warning use page clear engine
        page_clear_engine_clear_page(p->phys_addr);


#if 0 // Why?
        // Ask them to allocate us some disk space.
        hal_printf("ask disk block 0x%X\n", p->virt_addr );
        if( pager_interrupt_alloc_page(&p->curr_page) )
        {
            p->flag_have_curr = 1;
            hal_printf("got disk block 0x%X\n", p->virt_addr );
            return; // got it
        }

        vm_page_req_deferred_disk_alloc(p);
#endif
        return;
    }

    if(FAULT_DEBUG) hal_printf("req pagein 0x%X\n", p->virt_addr );

    //p->pager_io.virt_addr = p->virt_addr;
    p->pager_io.phys_page = p->phys_addr;
    p->pager_io.pager_callback = pagein_callback;

    pager_enqueue_for_pagein( /*(pager_io_request*)*/&(p->pager_io) );

#if 0
    while(p->flag_pager_io_busy)
    {
        hal_cond_wait(&p->sleepStone,&vm_map_mutex);
    }
#else

    while(p->flag_pager_io_busy)
    {
        // races! pager does not take vm_map_mutex before modifying flag_pager_io_busy
        //hal_cond_wait(&me->sleepStone,&vm_map_mutex);
        hal_mutex_unlock(&vm_map_mutex);
        hal_sleep_msec( 1 ); // TODO: yield?
        hal_mutex_lock(&vm_map_mutex);
    }

#endif

}












// Mutex is taken!
//
// We will possibly reenable with sti(), or maybe not.
// Caller must enable ints himself. (Or, for USA readers, herself.)
void
page_fault( vm_page *p, int  is_writing )
{
    if( is_writing )    page_fault_write( p );
    else                page_fault_read( p );
}






// we depend on do_for not modifying interrupts disable status
void
vm_map_do_for( vmem_page_func_t func, bool_vmem_page_func_t predicate )
{
    //if(do_lock)
    hal_mutex_lock(&vm_map_mutex);

    foreach_pause_counter   pcount;
    foreach_pause_counter_init( &pcount );

    vm_page *i;
    for( i = vm_map_map; i < vm_map_map_end; i++ )
    {
        if(foreach_pause(&pcount))
        {
            hal_mutex_unlock(&vm_map_mutex);
            // give them a chance
            hal_mutex_lock(&vm_map_mutex);
        }

        while( i->flag_busy )
        {
            hal_mutex_unlock(&vm_map_mutex);
            // give them a chance to unlock
            hal_sleep_msec(1);
            hal_mutex_lock(&vm_map_mutex);
        }
        if( predicate == 0 || predicate( i ) )
        {
            i->flag_busy = 1;
            func( i );
            hal_mutex_lock(&i->lock);
            i->flag_busy = 0;
            hal_cond_broadcast(&(i->done)); // Will do no harm.
            hal_mutex_unlock(&i->lock);
        }
    }

    hal_mutex_unlock(&vm_map_mutex);
}

void
vm_map_for_all( vmem_page_func_t func )
{
    vm_map_do_for( func, 0 );
}




//---------------------------------------------------------------------------
//       Snapshot
// (runs in pager thread)
//---------------------------------------------------------------------------

//
// called under the lock!
//
// goal is to protect all pages against write - actual snap is just that,
// saving to disk is to be done in page fault code or later in regular snapshot
// code
//
static void mark_for_snap( volatile vm_page *p )
{
    if(p->flag_have_make)       hal_panic("mark_for_snap got flag_have_make set");

    if(DEBUG_MARK) hal_printf("mark4snap 0x%x: ", p->virt_addr );

    if( !p->flag_phys_mem )
    {
        // since we're not backed, nothing to do - everything will either
        // be done during next page fault or in regular snapshot code
        if(DEBUG_MARK) hal_printf("!phys\n");
        return;
    }

    if( p->flag_phys_protect )
    {
        // just the same - page is already protected
        if(DEBUG_MARK) hal_printf("already ro\n");
        return;
    }



    if(DEBUG_MARK) hal_printf("set to ro\n");
    // ok, page is mapped, writeable: the real case.
    hal_page_control( p->phys_addr, p->virt_addr, page_map, page_ro );
    p->flag_phys_protect = 1;

    // do we need it here? Better do it before and/or after!
    //if(p->flag_phys_dirty)        vm_page_req_pageout(p);

}

static void kick_pageout(vm_page *p)
{
    static int cnt = 0;
    if(p->flag_phys_dirty)
    {
        //if(SNAP_DEBUG) hal_printf("V");
        vm_page_req_pageout(p);
        cnt++;
    }
    if(SNAP_DEBUG && 0 == (0xFFFFF & (int)p->virt_addr) )
    {
        hal_printf("0x%X (%d)\n", p->virt_addr, cnt );
        cnt = 0;
    }
}


// NB! Call with vm_map_mutex taken
static void finalize_snap( volatile vm_page *p )
{
    if(SNAP_DEBUG) hal_printf("finalize_snap 0x%X: ", p->virt_addr );

    if(p->flag_have_make)
    {
        if(SNAP_DEBUG) hal_printf("has make\n" );
        return;
    }

    if(p->flag_phys_dirty)
    {
        if(SNAP_DEBUG) hal_printf("touch\n" );

        hal_mutex_unlock(&vm_map_mutex);
        hal_mutex_lock(&p->lock);
        p->flag_busy = 0;
        hal_cond_broadcast(&(p->done)); // Will do no harm.
        hal_mutex_unlock(&p->lock);

        {
            // BUG! touch is in the races with other thread or CPU. Init COW without touching page data.
            // TODO SMP spinlock?
            int enabled = hal_save_cli();

            // touch it to make sure fault COW does the work
            char a = *((char *)(p->virt_addr));
            *((char *)(p->virt_addr)) = a;

            // TODO SMP spinlock unlock?
            if (enabled) hal_sti();
        }

        // BUG! Races! Wait for some real event from page fault code on THIS page

        hal_sleep_msec(1); // TODO yield?
        //while( p->flag_pager_io_busy )
        while( ! p->flag_have_make )
        {
            if(SNAP_DEBUG) hal_printf("w4make " );
            hal_sleep_msec(1); // TODO give up the rest of slice? Skip to the next page?
        }
        //hal_cli();
        hal_mutex_lock(&vm_map_mutex);
        p->flag_busy = 1;

        if(! p->flag_have_make )
            hal_panic("doesnt have make again!");

    }

    /*    if(p->flag_phys_dirty)
     {
     hal_sti();
     if( !p->flag_pager_io_busy )
     p->req_pageout();

     while( p->flag_pager_io_busy )
     hal_sleep_msec(0);
     hal_cli();
     }*/

    if(p->flag_phys_dirty && !(p->flag_have_make))
#if 1
        hal_panic("dirty not snapped after snap?!");
#else
        // Ask them to allocate us some disk space.
        hal_printf("ask disk block 0x%X\n", p->virt_addr );
        if( pager_interrupt_alloc_page(&p->curr_page) )
        {
            p->flag_have_curr = 1;
            if(SNAP_DEBUG) hal_printf("got disk block for 0x%X\n", p->virt_addr );
        }
        else
        {
            vm_page_req_deferred_disk_alloc(p);
            if(SNAP_DEBUG) hal_printf("sleep wait 4 deferred disk alloc %X\n", p->virt_addr );
            while(!p->flag_have_make)
                hal_cond_wait(&p->sleepStone,&vm_map_mutex);
            if(SNAP_DEBUG) hal_printf("got deferred disk alloc %X\n", p->virt_addr );
        }
#endif

    if(p->flag_have_curr)
    {
        p->make_page = p->curr_page;
        p->flag_have_curr = 0;
        p->flag_have_make = 1;
        p->generation = phantom_vm_generation;
        //hal_mutex_unlock(&vm_map_mutex);
        return;
    }

    if(p->flag_have_prev)
    {
        // NB! snap list writer will check out that make == prev
        p->make_page = p->prev_page;
        p->flag_have_make = 1;
        p->generation = phantom_vm_generation;
        //hal_mutex_unlock(&vm_map_mutex);
        return;
    }

    // either empty or nonexistent. How can we distinguish?
    p->make_page = 0; // NB! means page is not to be written or can be written as zeros
    p->flag_have_make = 1;
    p->generation = phantom_vm_generation;

    if(SNAP_DEBUG) hal_printf(" done, " );
    // fall out
    //hal_mutex_unlock(&vm_map_mutex);
}


pagelist *snap_saver = 0;

static void save_snap( volatile vm_page *p )
{
    // TODO added for safety - remove or do in a more smart way?
    // HACK! We set have make and make_page = 0 on unused page
    if(! (p->flag_have_make && p->make_page == 0 && !p->flag_have_curr && !p->flag_have_prev) )
        vm_page_req_pageout(p);

    if(!p->flag_have_make)
        hal_panic("no flag_have_make after snap?!");

    //if(p->make_page == 0)        printf("mp0 ");

    pagelist_write_seq( snap_saver, p->make_page);
    if(SNAP_LISTS_DEBUG) hal_printf("pg %d, ", p->make_page);

    p->flag_have_make = 0;
}



void
do_snapshot()
{
    int			  enabled; // interrupts

    hal_printf("\nSnapshot start!\n");
    // prerequisites
    //
    // - no pages with flag_have_make can exist! check that?
    //

    // This pageout request is not nesessary, but makes snap to catch a more later state.
    // If we skip this pageout, a lot of pages will go to 'after snap' state.
    vm_map_for_all( kick_pageout ); // Try to pageout all of them - NOT IN LOCK!
    if(SNAP_STEPS_DEBUG) hal_printf("wait 4 pgout to settle\n");
    hal_sleep_msec(30000); // sleep for 10 sec - why 10?
    if(SNAP_STEPS_DEBUG) hal_printf("stop world\n");


    // MUST BE BEFORE hal_mutex_lock!
    phantom_snapper_wait_4_threads();

    if(SNAP_STEPS_DEBUG) printf("snap: threads stopped");

    //hal_cli();
    //hal_mutex_lock(&vm_map_mutex);
    enabled = hal_save_cli(); 

    // START!
    is_in_snapshot_process = 1;

    /* WRONG

    // --- Mark all vmem pages as not snapped
    vm_map_clear_snapped();

    // all the clean pages will go to snapshot directly
    // we're doing this step before swapping all the memory out to
    // make snapshot process a bit more smooth.
    vm_map_for_clean_not_snapped( snap_clean_page );

    // here we'll kick pageout for all unsnapped pages
    vm_map_for_not_snapped( pageout );

    // here we have to wait somehow for pageout to complete
    //while(snapshot_swapout_queue_size)
    //hal_sleep_msec(100); // sleep for 0.1 sec

    // again - all the clean pages will go to snapshot directly
    vm_map_for_clean_not_snapped( snap_clean_page );
    */

    // NB! Some pages are possibly have RAM page but have no actual
    // backing disk page. Give them one now! Just to make sure we have
    // disk space to complete. (What if we have no? Just die?)

    // first we'll kick pageout for all pages.
    //vm_map_for_all( pageout );

    // here we have to wait somehow for pageout to complete
    //while(snapshot_swapout_queue_size)
    //hal_sleep_msec(100); // sleep for 0.1 sec
    //hal_sleep_msec(1000); // sleep for 1 sec

    // hope that pageout made some free physical memory
    // in fact - there will be no really free memory, just
    // a lot (I hope!) of clean pages that we can treat as free later

    // Terrible and mighty step - ALL the pages will be marked
    // as not snapped and access to them will be locked here, so
    // that page faults will bring them to us on write attempts and we'll
    // make a copies (COW).

    // !!!! SnapShot !!!!

    hal_printf("\nSnapshot THE SNAP...");

    // TODO: we have top do more. such as stop oher CPUS, force VMs into the
    // special snap-friendly state, etc

    vm_map_for_all( mark_for_snap );
    phantom_vm_generation++;        // Now any page fault will work for us

    // dies with intr disabled
#if 0
hal_printf("save vmem copy...");
phantom_save_vmem();
#endif

    hal_printf("done THE SNAP\n");

    //hal_mutex_unlock(&vm_map_mutex);
    if(enabled) hal_sti();

    phantom_snapper_reenable_threads();

    // YES, YES, YES, Snap is nearly done.

    hal_sleep_msec(10000); // sleep for 100 msec - why 100?

    // Here we have to wait a little and start processing pages manually
    // because no one can be sure that all the pages will be accessed for
    // write in a short time.

    // This pageout request is needed - if I skip it, snaps are incomplete
    if(SNAP_STEPS_DEBUG) hal_printf("pgout\n");
    vm_map_for_all( kick_pageout ); // Try to pageout all of them - NOT IN LOCK!

    hal_sleep_msec(1000); // sleep for 100 msec - why 100?

    //if(SNAP_STEPS_DEBUG) hal_printf("snapshot go kick ass those lazy pages\n");
    //if(SNAP_DEBUG) getchar();

    // BUG! BUG! BUG! BUG! BUG! BUG! BUG! It must be earlier!
    //phantom_snapper_reenable_threads();
    //hal_sleep_msec(2000); // sleep for 100 msec - why 100?

    hal_printf("snapshot will finalize_snap\n");
    // scan nonsnapped pages, snap them manually (or just access to cause
    // page fault??)
    vm_map_for_all( finalize_snap );

    // BUG! BUG! BUG! BUG! BUG! BUG! BUG! It must be earlier!
    //phantom_snapper_reenable_threads();

    // now all the pages for snapshot are done. Now create
    // the disk data structure for them

    // TODO - free prev snap first!

    //long new_snap_head = 0;
    disk_page_no_t new_snap_head = 0;


    if(SNAP_STEPS_DEBUG) hal_printf("Creating primary pagelist root\n");
    if( !pager_alloc_page(&new_snap_head) ) panic("out of disk!");



    if(SNAP_STEPS_DEBUG) hal_printf("Creating pagelist...\n");
    //if(SNAP_DEBUG) getchar();

    {

        pagelist saver;
        pagelist_init( &saver, new_snap_head, 1, DISK_STRUCT_MAGIC_SNAP_LIST );

        pagelist_clear(&saver);
        snap_saver = &saver;
        vm_map_for_all( save_snap );
        snap_saver = 0;
        pagelist_flush(&saver);
        pagelist_finish(&saver);
    }

    // ok, now we have current snap and previous one. come fix the
    // superblock

    //long toFree = pager_superblock_ptr()->prev_snap;

    pager_superblock_ptr()->prev_snap = pager_superblock_ptr()->last_snap;
    pager_superblock_ptr()->last_snap = new_snap_head;
    pager_update_superblock();


    // TODO free list @toFree excep 4 pages used in actual two lists


    //#error not impl
    // and free pages of previous-previous snapshot that changed in this
    // snapshot and not used in previous. Release data structure of
    // previous-previous snapshot too

    //#error not impl
    // free journal part, which was created before this snap
    // was started


    // DONE!
    is_in_snapshot_process = 0;
    hal_printf("\nSnapshot done!\n");
    //if(SNAP_DEBUG) getchar();

    hal_sleep_msec(20000);
    hal_printf("\nSnapshot 10 sec more wait\n");
    hal_sleep_msec(10000);

}


//---------------------------------------------------------------------------
// Normal pageout housekeeping activity
// (runs in pager thread on a quite low prio)
//---------------------------------------------------------------------------

/*
 void
 do_pageout_housekeeping()
 {
 do_pageout_kickoff_dirty();
 do_pageout_alloc_disk_pages();
 }


 // scan through pages getting backing disk pages as needed
 void
 do_pageout_alloc_disk_pages()
 {
 #error not impl
 }


 // scan through pages and pageout some dirty ones - IDLE only!
 void
 do_pageout_kickoff_dirty()
 {
 #error not impl
 }
 */

//int  vm_map_last_snap_is_done = 0;

static int vm_regular_snaps_enabled = 0;

void vm_enable_regular_snaps() { vm_regular_snaps_enabled = 1; }


static void vm_map_lazy_pageout_thread(void)
{
    SHOW_FLOW0( 1, "Ready");

    hal_set_thread_name("LazySnap");


    vm_page *p = vm_map_map;

    int loop_count = 0;

    hal_mutex_lock(&vm_map_mutex);

    while(1)
    {
        hal_mutex_unlock(&vm_map_mutex);

        if( stop_lazy_pageout_thread )
        {
            do_snapshot();
            stop_deferred_disk_alloc_thread = 1;

            hal_cond_broadcast( &deferred_alloc_thread_sleep );
            // so that it will see the stop flag

            hal_exit_kernel_thread();
        }

        hal_sleep_msec( 100 ); // TODO: yield?
        p++;
        if( p >= vm_map_map_end ) p = vm_map_map;

        if( loop_count++ > 100 )
        {
            loop_count = 0;
            if( vm_regular_snaps_enabled )
            {
#if 1

#if 1
                do_snapshot();
#else
                printf("Total pageout\n");
                vm_map_for_all( kick_pageout ); // Try to pageout all of them - NOT IN LOCK!
                printf("Total pageout done\n");
#endif

#else
                phantom_snapper_wait_4_threads();
                hal_sleep_msec( 1000 );
                phantom_snapper_reenable_threads();
#endif
            }
        }


        //hal_cli();
        hal_mutex_lock(&vm_map_mutex);
continue;

        // too busy page?
        if( p->flag_busy )
            continue;
        //printf("nb ");
        if( p->flag_dont_touch )
            continue;
        //printf("ndt ");
        // has nothing to swap out?
        if( !(p->flag_phys_mem) )
            continue;
        //printf("hpm ");
        if( !(p->flag_phys_dirty) )
            continue;
        //printf("dty ");
        // nowhere to swap to?
        if( !(p->flag_have_curr) )
        {
            vm_page_req_deferred_disk_alloc(p);
            continue;
        }

        hal_printf("o");
        // allright, kick it!

        vm_page_req_pageout(p);
    }
}


static void vm_map_deferred_disk_alloc_thread(void)
{
    SHOW_FLOW0( 1, "Ready");

    hal_set_thread_name("DeferAlloc");

    while(1)
    {
        //hal_sleep( &deferred_disk_allocations );
        hal_mutex_lock(&vm_map_mutex); // TODO: this is for cond_wait to have something to unlock only
        hal_cond_wait( &deferred_alloc_thread_sleep, &vm_map_mutex );
        hal_mutex_unlock(&vm_map_mutex);

        // see lazy pageout shutdown
        if(stop_deferred_disk_alloc_thread)
        {
            last_snap_is_done = 1;
            hal_exit_kernel_thread();
        }

        SHOW_FLOW0( 1, "Active");

        //hal_sleep_msec( 100 );
        // sleep is to check for situation where disk mem is not available for a long time

        pager_refill_free();

        //hal_cli();  // TODO use sema!
        hal_mutex_lock(&vm_map_mutex);
#if 0
        while(deferred_disk_allocations)
        {
            //hal_printf("\ndefered disk alloc request\n");

            volatile vm_page *p = deferred_disk_allocations;
            // off the q!
            deferred_disk_allocations = deferred_disk_allocations->dfda_next;
            p->flag_dfda_active = 0; // used as flag that we are on q;

            if( p->flag_have_curr ) continue; // he has one.

            disk_page_no_t temp;
            if( pager_interrupt_alloc_page(&temp) )
            {

                //hal_printf("+");

                p->curr_page = temp;
                p->flag_have_curr = 1; // got it
                p->flag_phys_dirty = 1; // disk page contents are wrong
            }
            else
            {
                hal_printf("?");
                break; // to get to refill_free_reserve()
            }

        }
#endif

        hal_mutex_unlock(&vm_map_mutex);

    }
}





//---------------------------------------------------------------------------
// Page clear code
//---------------------------------------------------------------------------



static void * 		page_clear_vaddr; // Place in address space to map page to clear
static hal_spinlock_t	page_clear_lock;

// TODO idle time pre-clear to some queue
static void page_clear_engine_init(void)
{
    int ie = hal_save_cli();
    hal_spin_init(&page_clear_lock);
    hal_spin_lock(&page_clear_lock);

    if( hal_alloc_vaddress( &page_clear_vaddr, 1 ) )
        panic("page_clear_vaddr alloc failed");

    hal_spin_unlock(&page_clear_lock);
    if(ie) hal_sti();
}


// Clear phys page from some special vaddress
// TODO perCPU vaddress and no spinlock?
static void page_clear_engine_clear_page(phys_page_t p)
{
    int enabled = hal_save_cli();
    hal_spin_lock(&page_clear_lock);

    if(FAULT_DEBUG)
        hal_printf("page_clear_engine_clear_page( 0x%X )\n", p );

    hal_page_control( p, page_clear_vaddr, page_map, page_rw );

    // TODO use MMX clear code
    //memset( page_clear_vaddr, '#', __MEM_PAGE );
    memset( page_clear_vaddr, 0, __MEM_PAGE );

    // BUG! Broken!
    //fast_clear_page( page_clear_vaddr );

    hal_page_control( p, page_clear_vaddr, page_unmap, page_ro );

    hal_spin_unlock(&page_clear_lock);
    if (enabled) hal_sti();
}




