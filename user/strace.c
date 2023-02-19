#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// strace mask command [args]
// strace
// runs the specified command until it exits.
// It intercepts and records the system calls which are called by a process during its
// execution.
// It should take one argument, an integer mask, whose bits specify which system
// calls to trace.

int
main(int argc, char *argv[])
{
  int i;
  char *command_args[argc-1];
  if(argc < 3){
    fprintf(2, "Usage: strace mask command [args]\n");
    
    exit(1);
  }

  //set tracemask of process
  
  //p->mask = argv[1];//pointer to incomplete class type "struct proc" is not allowed
   //call syscall trace
  trace(atoi(argv[1]));
  for (i = 2; i < argc; i++)
    {
        command_args[i - 2] = argv[i];
    }
  exec(command_args[0], command_args);
  fprintf(2, "exec %s failed\n", argv[2]);
  exit(0);
}
