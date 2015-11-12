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
static size_t orig_arglen;
static char *orig_argv = NULL;

void setproctitle(const char *title) {
  if (argstart == NULL) {
    return;
  }
  if (orig_argv == NULL) {
    orig_argv = (char *) malloc(orig_arglen);
    memcpy(orig_argv, argstart, orig_arglen);
  }
  memset(argstart,0,maxarglen);
  strncpy(argstart,title,maxarglen);
  argstart[maxarglen-1] = '\0';
  prctl(PR_SET_NAME,(long)title,0,0,0);
}

void resetproctitle() {
  if (orig_argv == NULL) {
    return;
  }
  memset(argstart,0,maxarglen);
  memcpy(argstart,orig_argv,orig_arglen);
  if (orig_arglen > 1) {
    prctl(PR_SET_NAME,(long)orig_argv,0,0,0);
  }
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

static int (*real_setenv) (const char *, const char *, int);
int setenv(const char *key, const char *value, int overwrite) {
  if (!strncmp(key,"SETPROCTITLE",13) && (strlen(value) > 0)) {
    setproctitle(value);
    return 0;
  }
  return real_setenv(key,value,overwrite);
}

static int (*real_unsetenv) (const char *);
int unsetenv(const char *key) {
  if (!strncmp(key,"SETPROCTITLE",13)) {
    resetproctitle();
    return 0;
  }
  return real_unsetenv(key);
}

static int fake_main(int argc, char **argv, char **envp) {
  char **p = argv;
  dlclose(libc_handle);

  argstart = *argv;
  while (*p != NULL) {
    maxarglen += strlen(*p++) + 1;
  }
  orig_arglen = maxarglen;

  if ( (argstart + maxarglen) == envp[0] ) {
    int i,j,len;
    char **tmp; 
    char *p;

    for (i = 0; envp[i] != NULL; ++i);
    tmp = calloc(i, sizeof(char *));
    j = 0;

    for (i = 0; envp[i] != NULL; ++i) {
      if (!strncmp(envp[i],"SETPROCTITLE_PADDING=",21)) {
	continue;
      }
      tmp[j++] = strdup(envp[i]);
    }
    
    /* shuffle everything towards the end, free up space at the front for longer proctitles! */
    if (i > j) {
      --i; --j;
      p = envp[i] + strlen(envp[i]);
      for (; i > j; --i) {
	envp[i] = 0;
      }
      for (; j >= 0; --j) {
	len = strlen(tmp[j]);
	p -= len;
	strncpy(p,tmp[j],len);
	free(tmp[j]);
	p[len] = 0;
	envp[j] = p;
	--p;
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
  real_setenv = dlsym(libc_handle,"setenv");
  real_unsetenv = dlsym(libc_handle,"unsetenv");

  return real_libc_start_main(fake_main, argc, ubp_av, init, fini, rtld_fini, stack_end);
}
