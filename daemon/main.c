#include <llarp.h>
#include <stdio.h>
#include <string.h>

struct llarp_main {
  struct llarp_alloc mem;
  struct llarp_router *router;
  struct llarp_threadpool *worker;
  struct llarp_threadpool *netio;
  struct llarp_logic *logic;
  struct llarp_config *config;
  struct llarp_nodedb *nodedb;
  struct llarp_ev_loop *mainloop;
  char nodedb_dir[256];
  int exitcode;
};

void iter_main_config(struct llarp_config_iterator *itr, const char *section,
                      const char *key, const char *val) {
  struct llarp_main *m = (struct llarp_main *)itr->user;

  if (!strcmp(section, "router")) {
    if (!strcmp(key, "threads")) {
      int workers = atoi(val);
      if (workers > 0 && m->worker == NULL) {
        m->worker = llarp_init_threadpool(workers);
      }
    }
  }
  if (!strcmp(section, "netdb")) {
    if (!strcmp(key, "dir")) {
      strncpy(m->nodedb_dir, val, sizeof(m->nodedb_dir));
    }
  }
}

static void progress() {
  printf(".");
  fflush(stdout);
}

int shutdown_llarp(struct llarp_main *m) {
  printf("Shutting down ");
  progress();
  if(m->router)
    llarp_stop_router(m->router);
  
  progress();
  if(m->mainloop)
    llarp_ev_loop_stop(m->mainloop);

  progress();
  if(m->netio)
  {
    llarp_threadpool_stop(m->netio);
    llarp_threadpool_join(m->netio);
  }
  
  progress();
  if(m->worker)
    llarp_threadpool_stop(m->worker);
  
  progress();
  
  if(m->worker)
    llarp_threadpool_join(m->worker);
  
  progress();
  if (m->logic) llarp_logic_stop(m->logic);
  
  progress();
  llarp_free_router(&m->router);
  
  progress();
  llarp_free_config(&m->config);
  
  progress();
  llarp_ev_loop_free(&m->mainloop);
  
  progress();
  llarp_free_threadpool(&m->worker);
  
  progress();
  
  llarp_free_logic(&m->logic);
  progress();
  
  printf("\n");
  fflush(stdout);
  return m->exitcode;
}

struct llarp_main llarp = {
  {0,0,0},
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  {0},
  1
};

void run_netio(void * user)
{
  struct llarp_ev_loop * loop = user;
  llarp_ev_loop_run(loop);
}

int main(int argc, char *argv[]) {
  const char *conffname = "daemon.ini";
  if (argc > 1) conffname = argv[1];
  llarp_mem_jemalloc(&llarp.mem);
  struct llarp_alloc * mem = &llarp.mem;
  llarp_new_config(&llarp.config);
  llarp_ev_loop_alloc(&llarp.mainloop);
  printf("%s loading config file %s\n", LLARP_VERSION, conffname);
  if (!llarp_load_config(llarp.config, conffname)) {
    struct llarp_config_iterator iter;
    iter.user = &llarp;
    iter.visit = iter_main_config;
    llarp_config_iter(llarp.config, &iter);

    llarp.nodedb = llarp_nodedb_new(mem);
    
    if (llarp.nodedb_dir[0]) {
      llarp.nodedb_dir[sizeof(llarp.nodedb_dir) - 1] = 0;
      char *dir = llarp.nodedb_dir;
      if (llarp_nodedb_ensure_dir(dir)) {
        // ensure worker thread pool
        if (!llarp.worker) llarp.worker = llarp_init_threadpool(2);
        // ensire net io thread
        llarp.netio = llarp_init_threadpool(1);
        
        llarp.router = llarp_init_router(mem, llarp.worker, llarp.mainloop);

        if (llarp_configure_router(llarp.router, llarp.config)) {
          
          llarp.logic = llarp_init_logic(mem);
          printf("starting router\n");
          llarp_run_router(llarp.router, llarp.logic);
          // run io loop
          struct llarp_thread_job netjob = {
            .user = llarp.mainloop,
            .work = &run_netio
          };
          llarp_threadpool_queue_job(llarp.netio, netjob);
          printf("running\n");
          llarp.exitcode = 0;
          llarp_logic_mainloop(llarp.logic);
        } else
          printf("Failed to configure router\n");
      } else
        printf("failed to initialize nodedb at %s\n", dir);
    } else
      printf("no nodedb defined\n");
    
    return shutdown_llarp(&llarp);
  } else
    printf("Failed to load config %s\n", conffname);

  return 1;
}
