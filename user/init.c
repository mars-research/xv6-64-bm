// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };
struct msg{
  unsigned long long regs[8];
};
#define ITERS 1000000ul
__attribute((always_inline)) unsigned long long rdtsc(){
  unsigned long long lo, hi;
  asm volatile( "rdtsc" : "=a" (lo), "=d" (hi) ); 
  return( lo | (hi << 32) );
}
int
main(void)
{
  int pid, wpid;
  
  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  
  dup(0);  // stdout
  dup(0);  // stderr

  for(int i = 0; i < 10; i++){
    pid = fork();
    if(pid == 0){
      struct msg m;
      recv(0,&m);
      for(unsigned long long i = 0; i<ITERS-1ul; i++){
        send_recv(0, &m);
      }
      send(0, &m);
      exit();
    }else{

      struct msg m;
      sleep(100);

      unsigned long long ipc_time = rdtsc();
      for(unsigned long long i = 0; i<ITERS; i++){
        send_recv(0, &m);
      }
      ipc_time = rdtsc() - ipc_time;
      ipc_time /= ITERS;
      printf(1, "ipc time - %d\n",ipc_time);

      unsigned long long null_call_time = rdtsc();
      for(unsigned long long i = 0; i<ITERS; i++){
        null_call();
      }
      null_call_time = rdtsc() - null_call_time;
      null_call_time /= ITERS;
      printf(1, "null_call time - %d\n",null_call_time);

      unsigned long long cr3_kernel_time = rdtsc();
      cr3_kernel(ITERS);
      cr3_kernel_time = rdtsc() - cr3_kernel_time;
      cr3_kernel_time /= ITERS;
      printf(1, "cr3_kernel time - %d\n",cr3_kernel_time);

      cr3_test();

      //printf(1, "ipc/reload time delta - %d\n",ipc_time - reload_time);
      //printf(1, "ipc/sum of parts delta - %d\n", ipc_time - (2*(reload_time-null_call_time) + 2*null_call_time));


      /*for(int i = 0; i < 128; i++){
        set_size(i);
        printf(1, "touch %d pages:", i);

        unsigned long long start, end;
        start = rdtsc();
        for(unsigned long long i = 0; i < ITERS; i++){
          cr3_test();
        }
        //printf(1, "after test loop\n");
        end = rdtsc();

        printf(1, "overhead of cr3_reload across %d runs: average cycles %d\n",
              ITERS, (unsigned long)(end-start)/ITERS);
      }*/

      wait();
      //exit();
    }
  
    printf(1, "uptime - %d\n", send(0, 90));
  }
  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
