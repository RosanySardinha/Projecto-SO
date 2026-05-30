
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <sthread.h>
#include <sthread_user.h>
#include <sthread_ctx.h>
#include <sthread_time_slice.h>
#include "queue.h"

#define MAX_PRIORITY 15 
#define QUANTUM_BASE 5

 // Guarda o estado, prioridade, quantum e contexto de cada tarefa no sistema.
 
struct _sthread {
    sthread_ctx_t *saved_ctx;         
    sthread_start_func_t start_routine_ptr; 
    long wake_time;                   
    int join_tid;                     
    void* join_ret;                   
    void* args;                       
    int tid;                          
    int priority;                     
    int time_slice;                   
    struct _sthread *next;            
};

 // Matriz de filas de prioridade do escalonador O(1) controlada por um bitmap.
 
typedef struct {
    unsigned int bitmap;                       
    struct _sthread *queue[MAX_PRIORITY];      
    struct _sthread *tail[MAX_PRIORITY];       
} priority_array_t;

static priority_array_t array_a;
static priority_array_t array_b;
static priority_array_t *active_array;         
static priority_array_t *expired_array;        

static queue_t *dead_thr_list;        
static queue_t *sleep_thr_list;       
static queue_t *join_thr_list;        
static queue_t *zombie_thr_list;      

static struct _sthread *active_thr;   
static int tid_gen;                   
static long Clock;                    

#define CLOCK_TICK 10000              


  //Retira a tarefa pronta de maior prioridade usando o bitmap em tempo O(1).
 
struct _sthread* dequeue_next_active(void) {
    if (active_array->bitmap == 0) return NULL;
    
    int highest_prio = __builtin_ctz(active_array->bitmap);
    
    struct _sthread *thread = active_array->queue[highest_prio];
    if (thread != NULL) {
        active_array->queue[highest_prio] = thread->next;
        if (active_array->queue[highest_prio] == NULL) {
            active_array->tail[highest_prio] = NULL;
            active_array->bitmap &= ~(1 << highest_prio); 
        }
        thread->next = NULL;
    }
    return thread;
}

 // Insere uma tarefa no fim da fila correspondente Ã  sua prioridade e liga o bit.
 
void enqueue_thread(priority_array_t *array, struct _sthread *thread) {
    int prio = thread->priority;
    thread->next = NULL;
    
    if (array->queue[prio] == NULL) {
        array->queue[prio] = thread;
        array->bitmap |= (1 << prio); 
    } else {
        array->tail[prio]->next = thread;
    }
    array->tail[prio] = thread;
}

 // Procura se uma thread com determinado ID existe dentro da matriz de prioridades.
 
static int is_thread_in_array(priority_array_t *array, int tid) {
    for (int i = 0; i < MAX_PRIORITY; i++) {
        struct _sthread *curr = array->queue[i];
        while (curr != NULL) {
            if (curr->tid == tid) return 1;
            curr = curr->next;
        }
    }
    return 0;
}

void sthread_user_exit(void *ret);
void sthread_user_free(struct _sthread *thread);

 // FunÃ§Ã£o auxiliar que inicia a tarefa e garante a chamada ao Exit quando ela terminar.
 
void sthread_aux_start(void) {
    splx(LOW);
    active_thr->start_routine_ptr(active_thr->args);
    sthread_user_exit((void*)0);
}

void sthread_user_dispatcher(void);

 // Inicializa as listas do sistema, as matrizes do escalonador e a thread principal.
 
void sthread_user_init(void) {
    active_array = &array_a;
    expired_array = &array_b; 
    active_array->bitmap = 0;
    expired_array->bitmap = 0;

    for (int i = 0; i < MAX_PRIORITY; i++) {
        active_array->queue[i] = active_array->tail[i] = NULL;
        expired_array->queue[i] = expired_array->tail[i] = NULL;
    }

    dead_thr_list = create_queue();
    sleep_thr_list = create_queue();
    join_thr_list = create_queue();
    zombie_thr_list = create_queue();
    tid_gen = 1;

    struct _sthread *main_thread = malloc(sizeof(struct _sthread));
    main_thread->start_routine_ptr = NULL;
    main_thread->args = NULL;
    main_thread->saved_ctx = sthread_new_blank_ctx();
    main_thread->wake_time = 0;
    main_thread->join_tid = 0;
    main_thread->join_ret = NULL;
    main_thread->priority = 5; 
    main_thread->time_slice = QUANTUM_BASE;
    main_thread->next = NULL;
    main_thread->tid = tid_gen++;
    
    active_thr = main_thread;
    Clock = 1;
    
    sthread_time_slices_init(sthread_user_dispatcher, CLOCK_TICK);
}
 // Aloca uma nova thread, configura sua prioridade inicial e a insere nas Ativas.
 
sthread_t sthread_user_create(sthread_start_func_t start_routine, void *arg) {
    struct _sthread *new_thread = (struct _sthread*)malloc(sizeof(struct _sthread));
    sthread_ctx_start_func_t func = sthread_aux_start;
    
    new_thread->args = arg;
    new_thread->start_routine_ptr = start_routine;
    new_thread->wake_time = 0;
    new_thread->join_tid = 0;
    new_thread->join_ret = NULL;
    new_thread->saved_ctx = sthread_new_ctx(func); 
    new_thread->next = NULL;
    new_thread->priority = 5;          
    new_thread->time_slice = QUANTUM_BASE; 

    splx(HIGH); 
    new_thread->tid = tid_gen++;
    enqueue_thread(active_array, new_thread); 
    splx(LOW);
    return new_thread;
}
 // Finaliza a thread atual, acorda quem dependia dela no Join e chama o escalonador.
 
void sthread_user_exit(void *ret) {
   splx(HIGH);
   int is_zombie = 1;

   queue_t *tmp_queue = create_queue();   
   while (!queue_is_empty(join_thr_list)) {
      struct _sthread *thread = queue_remove(join_thr_list);
      printf("Test join list: join_tid=%d, active->tid=%d\n", thread->join_tid, active_thr->tid);
      
      if (thread->join_tid == active_thr->tid) {
         thread->join_ret = ret;
         enqueue_thread(active_array, thread); 
         is_zombie = 0;
      } else {
         queue_insert(tmp_queue, thread);
      }
   }
   delete_queue(join_thr_list);
   join_thr_list = tmp_queue;
 
   if (is_zombie) {
      queue_insert(zombie_thr_list, active_thr);
   } else {
      queue_insert(dead_thr_list, active_thr);
   }

   if (active_array->bitmap == 0 && expired_array->bitmap == 0) {  
      delete_queue(dead_thr_list);
      delete_queue(sleep_thr_list);
      delete_queue(join_thr_list);
      delete_queue(zombie_thr_list);
      sthread_user_free(active_thr);
      printf("A fila em execucao esta vazia!\n");
      exit(0);
   }
  
   struct _sthread *old_thr = active_thr;
   active_thr = dequeue_next_active();
   if (active_thr == NULL) { 
       priority_array_t *temp = active_array;
       active_array = expired_array;
       expired_array = temp;
       active_thr = dequeue_next_active();
   }
   sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
   splx(LOW);
}
 //Suspende a execuÃ§Ã£o da thread atual atÃ© que a thread alvo termine.
 
int sthread_user_join(sthread_t thread, void **value_ptr) {
   splx(HIGH);
   int found = 0;
   queue_t *tmp_queue = create_queue();
   
   while (!queue_is_empty(zombie_thr_list)) {
      struct _sthread *zthread = queue_remove(zombie_thr_list);
      if (thread->tid == zthread->tid) {
         if (value_ptr != NULL) *value_ptr = thread->join_ret; 
         queue_insert(dead_thr_list, thread);
         found = 1;
      } else {
         queue_insert(tmp_queue, zthread);
      }
   }
   delete_queue(zombie_thr_list);
   zombie_thr_list = tmp_queue;

   if (found) { splx(LOW); return 0; }

   if (active_thr->tid == thread->tid) found = 1;
   if (!found) found = is_thread_in_array(active_array, thread->tid);
   if (!found) found = is_thread_in_array(expired_array, thread->tid);

   queue_element_t *qe = NULL;
   if (!found && sleep_thr_list != NULL) {
       qe = sleep_thr_list->first;
       while (qe != NULL) {
          if (qe->thread->tid == thread->tid) { found = 1; break; }
          qe = qe->next;
       }
   }

   if (!found) { splx(LOW); return -1; }

   active_thr->join_tid = thread->tid;
   struct _sthread *old_thr = active_thr;
   queue_insert(join_thr_list, old_thr);
   
   active_thr = dequeue_next_active();
   if (active_thr == NULL) {
       priority_array_t *temp = active_array;
       active_array = expired_array;
       expired_array = temp;
       active_thr = dequeue_next_active();
   }
   printf("Active is 0:%d\n", (active_thr == NULL));
   printf("Old is 0:%d\n", (old_thr == NULL));
   sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
   
   if (value_ptr != NULL) *value_ptr = thread->join_ret;
   splx(LOW);
   return 0;
}


 // Bloqueia a thread atual por um determinado perÃ­odo de tempo (ticks do relÃ³gio).
 
int sthread_user_sleep(int time) {
   splx(HIGH);
   long num_ticks = 10 * time / CLOCK_TICK;
   if (num_ticks == 0) { splx(LOW); return 0; }
   
   active_thr->wake_time = Clock + num_ticks;
   queue_insert(sleep_thr_list, active_thr); 
   
   struct _sthread *old_thr = active_thr;
   active_thr = dequeue_next_active();
   if (active_thr == NULL) {
       priority_array_t *temp = active_array;
       active_array = expired_array;
       expired_array = temp;
       active_thr = dequeue_next_active();
   }
   sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
   splx(LOW);
   return 0;
}
 
 // Despachante ativado por sinal de hardware. Controla o quantum e acorda tarefas em sleep.
 
void sthread_user_dispatcher(void) {
   splx(HIGH);
   Clock++;

   queue_t *tmp_queue = create_queue();   
   while (!queue_is_empty(sleep_thr_list)) {
      struct _sthread *thread = queue_remove(sleep_thr_list);
      if (thread->wake_time <= Clock) {
         thread->wake_time = 0;
         enqueue_thread(active_array, thread); 
         
         if (thread->priority < active_thr->priority) {
             struct _sthread *old_thr = active_thr;
             enqueue_thread(active_array, old_thr);
             active_thr = dequeue_next_active();
             sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
         }
      } else {
         queue_insert(tmp_queue, thread);
      }
   }
   delete_queue(sleep_thr_list);
   sleep_thr_list = tmp_queue;

   active_thr->time_slice--;
   if (active_thr->time_slice <= 0) { 
       struct _sthread *old_thr = active_thr;
       
       old_thr->time_slice = QUANTUM_BASE; 
       enqueue_thread(expired_array, old_thr); 
       
       if (active_array->bitmap == 0) { 
           priority_array_t *temp = active_array;
           active_array = expired_array;
           expired_array = temp;
       }
       
       active_thr = dequeue_next_active();
       sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
   }
   splx(LOW);
}


 //Libera voluntariamente o processador colocando a thread de volta na lista de Ativas.
 
void sthread_user_yield(void) {
  splx(HIGH);
  struct _sthread *old_thr = active_thr;
  enqueue_thread(active_array, old_thr); 
  
  active_thr = dequeue_next_active();
  if (active_thr == NULL) {
      priority_array_t *temp = active_array;
      active_array = expired_array;
      expired_array = temp;
      active_thr = dequeue_next_active();
  }
  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
  splx(LOW);
}


 // Estrutura de dados para o bloqueio de exclusÃ£o mÃºtua simples (Mutex).
 
struct _sthread_mutex {
  lock_t l;                   
  struct _sthread *thr;       
  queue_t* queue;             
};

sthread_mutex_t sthread_user_mutex_init() {
  sthread_mutex_t lock = malloc(sizeof(struct _sthread_mutex));
  if(!lock){ printf("Error in creating mutex\n"); return 0; }
  lock->l = 0; lock->thr = NULL; lock->queue = create_queue();
  return lock;
}

void sthread_user_mutex_free(sthread_mutex_t lock) {
  delete_queue(lock->queue); free(lock);
}

 // Tranca o recurso crÃ­tico ou bloqueia a thread na fila se ele jÃ¡ estiver ocupado.
 
void sthread_user_mutex_lock(sthread_mutex_t lock) {
  while(atomic_test_and_set(&(lock->l))) {} 
  if(lock->thr == NULL){
    lock->thr = active_thr; atomic_clear(&(lock->l));
  } else {
    queue_insert(lock->queue, active_thr); atomic_clear(&(lock->l));
    splx(HIGH);
    struct _sthread *old_thr = active_thr;
    active_thr = dequeue_next_active();
    if (active_thr == NULL) {
        priority_array_t *temp = active_array; active_array = expired_array;
        expired_array = temp; active_thr = dequeue_next_active();
    }
    sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx); splx(LOW);
  }
}

 //Libera a trava do recurso e transfere para a prÃ³xima tarefa aguardando na fila.
 
void sthread_user_mutex_unlock(sthread_mutex_t lock) {
  if(lock->thr != active_thr){ printf("unlock without lock!\n"); return; }
  while(atomic_test_and_set(&(lock->l))) {}
  if(queue_is_empty(lock->queue)){
    lock->thr = NULL;
  } else {
    struct _sthread *woken_thr = queue_remove(lock->queue);
    lock->thr = woken_thr; enqueue_thread(active_array, woken_thr); 
    
    if (woken_thr->priority < active_thr->priority) { 
        splx(HIGH); struct _sthread *old_thr = active_thr;
        enqueue_thread(active_array, old_thr); active_thr = dequeue_next_active();
        sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx); splx(LOW);
    }
  }
  atomic_clear(&(lock->l));
}

 // Estrutura de Monitores para sincronizaÃ§Ã£o condicional complexa entre tarefas.
 
struct _sthread_mon {
 	sthread_mutex_t mutex;
	queue_t* queue;
};

sthread_mon_t sthread_user_monitor_init() {
  sthread_mon_t mon = malloc(sizeof(struct _sthread_mon));
  if(!mon){ printf("Error creating monitor\n"); return 0; }
  mon->mutex = sthread_user_mutex_init();
  if (mon->mutex == 0) { printf("WARNING: Failed to initialize internal monitor mutex!\n"); free(mon); return 0; }
  mon->queue = create_queue(); return mon;
}

void sthread_user_monitor_free(sthread_mon_t mon) {
  sthread_user_mutex_free(mon->mutex); delete_queue(mon->queue); free(mon);
}

void sthread_user_monitor_enter(sthread_mon_t mon) { sthread_user_mutex_lock(mon->mutex); }
void sthread_user_monitor_exit(sthread_mon_t mon) { sthread_user_mutex_unlock(mon->mutex); }


 // Libera o mutex interno e bloqueia a tarefa em uma fila condicional atÃ© ser acordada.
 
void sthread_user_monitor_wait(sthread_mon_t mon) {
  if(mon->mutex->thr != active_thr){ printf("monitor wait called outside monitor\n"); return; }
  struct _sthread *temp = active_thr; queue_insert(mon->queue, temp);
  sthread_user_mutex_unlock(mon->mutex);
  splx(HIGH);
  struct _sthread *old_thr = active_thr; active_thr = dequeue_next_active();
  if (active_thr == NULL) {
      priority_array_t *temp_arr = active_array; active_array = expired_array;
      expired_array = temp_arr; active_thr = dequeue_next_active();
  }
  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx); splx(LOW);
  sthread_user_mutex_lock(mon->mutex); 
}


 // Acorda uma tarefa bloqueada na fila condicional movendo-a para a fila do mutex.
 
void sthread_user_monitor_signal(sthread_mon_t mon) {
  if(mon->mutex->thr != active_thr){ printf("monitor signal called outside monitor\n"); return; }
  while(atomic_test_and_set(&(mon->mutex->l))) {}
  if(!queue_is_empty(mon->queue)){
    struct _sthread *temp = queue_remove(mon->queue); queue_insert(mon->mutex->queue, temp);
  }
  atomic_clear(&(mon->mutex->l));
}


 // Mantidas com avisos textuais apenas para garantir compatibilidade de testes cruzados.
 
sthread_mon_t sthread_dummy_monitor_init() { printf("WARNING: pthreads do not include monitors!\n"); return NULL; }
void sthread_dummy_monitor_free(sthread_mon_t mon) { printf("WARNING: pthreads do not include monitors!\n"); }
void sthread_dummy_monitor_enter(sthread_mon_t mon) { printf("WARNING: pthreads do not include monitors!\n"); }
void sthread_dummy_monitor_exit(sthread_mon_t mon) { printf("WARNING: pthreads do not include monitors!\n"); }
void sthread_dummy_monitor_wait(sthread_mon_t mon) { printf("WARNING: pthreads do not include monitors!\n"); }
void sthread_dummy_monitor_signal(sthread_mon_t mon) { printf("WARNING: pthreads do not include monitors!\n"); }


 // Libera a memÃ³ria fÃ­sica do contexto e da estrutura TCB da tarefa concluÃ­da.
 */
void sthread_user_free(struct _sthread *thread) { sthread_free_ctx(thread->saved_ctx); free(thread); }
