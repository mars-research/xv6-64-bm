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
  pid = fork();
  if(pid==0){
    struct msg m;
    recv(0,&m);
    for(unsigned long long i = 0;i<ITERS-1ul;i++){
      send_recv(0, &m);
    }
    send(0, &m);
    exit();
  }else{
    struct msg m;
    sleep(100);
    unsigned long long time = rdtsc();
    for(unsigned long long i = 0;i<ITERS;i++){
      cr3_test();
    }
    time = rdtsc() - time;
    time /= ITERS;
    printf(1, "reload time - %d\n",time);

    unsigned long long time1 = rdtsc();
    for(unsigned long long i = 0;i<ITERS;i++){
      send_recv(0, &m);
    }
    time1 = rdtsc() - time1;
    time1 /= ITERS;
    printf(1, "ipc time - %d\n",time1);

    unsigned long long time2 = rdtsc();
    for(unsigned long long i = 0;i<ITERS;i++){
      null_call();
    }
    time2 = rdtsc() - time2;
    time2 /= ITERS;
    printf(1, "null_call time - %d\n",time2);

    unsigned long long time3 = rdtsc();
    cr3_kernel(ITERS);

    time3 = rdtsc() - time3;
    time3 /= ITERS;
    printf(1, "cr3_kernel time - %d\n",time3);

    printf(1, "delta - %d\n",time1 - time);
    wait();
    //exit();
  }
  printf(1, "uptime - %d\n", send(0, 90));
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
