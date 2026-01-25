#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <dispatcher.h>
#include <log.h>
#include <task_control.h>



// Thread-local worker variable
TLS worker_t* tl_worker = NULL;


void create_stack_frame(size_t stack_size, TCB* t) {
    
    uint8_t* stack = malloc(stack_size);
    assert(stack);
    

    uint8_t* sp = stack + stack_size;

    // Align 16 bytes for ABI
    sp = (uint8_t*)((uintptr_t)sp & ~0xF);

    // Push fake rbp
    sp -= sizeof(void*);
    *(void**)sp = (void*)0xBF;

    // Push fake return address -> trampoline
    sp -= sizeof(void*);
    *(void**)sp = task_trampoline;

    t->stack = stack;
    t->ctx.rsp = sp;
    t->ctx.rbp = 0;
}

void print_ctx(ctx_t* c) {
    printf("CONTEXT:\n");
    char* names[] = {"rsp", "rbp", "rbx", "r12", "r13", "r14", "r14"};
    for (size_t i = 0; i < 7; i++)
    {
        printf("%s: %p", names[i], ((uint8_t**)c)[i]);
        if ((i == 1 || i == 0) && ((uint8_t**)c)[i] != 0) printf(" -> 0x%012lx", *((uint64_t*)((uint8_t**)c)[i]));
        printf("\n");
    }
    
}

#define STACK_SIZE (60 * 1024) // 60Kb

// Centralized dispatcher: Round robin
void* dispatcher(void* arg) {
    worker_t* w = (worker_t*)arg;
    assert(w);
    tl_worker = w;
    w->returned_array = calloc(w->n_tasks, sizeof(task_result));
    TCB* t = NULL;

    for (;;) {

        // Get next runnable task: Round robin
        int all_finished = 1;
        int found = 0;
        
        // size_t next = (size_t)w->current + 1;

#ifdef __DEBUG
        size_t buffer = w->current;
        char* names[] = {"UNITIALIZED", "RUNNING", "READY", "WAITING", "FINISHED"};
        for (size_t i = 0; i < w->n_tasks; i++)
        {
            INFO("%zu: %s", i, names[w->tasks[i].status]);
        }
#endif        
        
        for (size_t i = 0; i < w->n_tasks; i++)
        {
            int idx = (w->current + 1 + i) % w->n_tasks;
    
            if (w->tasks[i].status != FINISHED) all_finished = 0;

            if (STATUS_RUNNABLE(w->tasks[idx].status) && found == 0) {
                t = &w->tasks[idx];
                w->current = idx;
                found = 1;
            }
        }
        OKAY("found=%d, all_finished=%d, previous=%zu, current=%zu", found, all_finished, buffer, w->current);
        

        if (all_finished) {
            break; // we're done
        }
        
        if (!all_finished && !found) {
            //Deadlocked
            WARN("Deadlock in worker {%zu}", w->th);
            break;
        }

        if (t->status == UNITIALIZED) {
            // Create frame
            create_stack_frame(STACK_SIZE, t);
        }
        // Switch context.
        t->status = RUNNING;
        _asm_ctx_switch_64(
            &t->ctx,
            &w->sched_ctx
        );
    }
    return NULL;
} 
