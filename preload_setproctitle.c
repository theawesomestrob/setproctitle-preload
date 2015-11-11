#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <unistd.h>

extern char **environ;
static char *argstart = NULL;
static size_t maxarglen;

void setproctitle(char *title) {
  if (argstart == NULL) {
    return;
  }
  memset(argstart,0,maxarglen);
  strncpy(argstart,title,maxarglen);
  argstart[maxarglen-1] = '\0';
  prctl(PR_SET_NAME,(long)title,0,0,0);
}

static int (*real_main) (int, char **, char **);
static void *libc_handle;

static int (*real_putenv) (char *);
int putenv(char *string) {
  if (!strncmp(string,"SETPROCTITLE=",13) && (strlen(string) > 13)) {
    setproctitle( (char *) (string+13) );
    return 0;
  }
  return real_putenv(string);
}

static int fake_main(int argc, char **argv, char **envp) {
  char **p = argv;
  dlclose(libc_handle);

  argstart = *argv;
  while (*p != NULL) {
    maxarglen += strlen(*p++) + 1;
  }
  if ( (argstart + maxarglen) == envp[0] ) {
    int i;
    for (i = 0; envp[i] != NULL; ++i) {
      if (!strncmp(envp[i],"SETPROCTITLE_PADDING=",21)) {
	break;
      }
    }
    if (envp[i] != NULL) {
      int len; char *p;
      p = envp[i] + strlen(envp[i]) - 1;
      for (; i > 0; --i) {
	len = strlen(envp[i-1]);
	p -= len;
	strncpy(p,envp[i-1],len);
	p[len] = '\0';
	envp[i] = p--;
      }
      for (i = 0; envp[i] != NULL; ++i) {
	envp[i] = envp[i+1];
      }
      p = argstart + maxarglen;
      maxarglen = envp[0] - argstart;
      memset(p,'\0',envp[0]-p);
      environ = envp;
    }
  }

  return real_main(argc, argv, envp);
}

int __libc_start_main(int (*main) (int, char **, char **),
		      int argc, char **ubp_av, void (*init) (void),
		      void (*fini) (void), void (*rtld_fini) (void),
		      void (*stack_end)) {
  int (*real_libc_start_main) (int (*main) (int, char **, char **), int argc,
	       char **ubp_av, void (*init) (void),
	       void (*fini) (void), void (*rtld_fini) (void),
	       void (*stack_end));

  libc_handle = dlopen("libc.so.6", RTLD_NOW);
  if (!libc_handle) {
    _exit(1);
  }
  real_libc_start_main = dlsym(libc_handle, "__libc_start_main");
  if (!real_libc_start_main) {
    _exit(1);
  }
  real_main = main;
  real_putenv = dlsym(libc_handle,"putenv");

  return real_libc_start_main(fake_main, argc, ubp_av, init, fini, rtld_fini, stack_end);
}
