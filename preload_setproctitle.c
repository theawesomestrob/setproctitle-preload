#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <unistd.h>

extern char **environ;

static char *argv_start = NULL;
static size_t argv_maxlen = 0;

static char *orig_title = NULL;

void setproctitle(const char *title) {
  if (title == NULL || argv_start == NULL || argv_maxlen < 1) {
    return;
  }
  memset(argv_start,0,argv_maxlen);
  strncpy(argv_start,title,argv_maxlen);
  argv_start[argv_maxlen-1] = '\0';
  if (strlen(title) > 15) {
    // if title is >15 chars it'll get truncated by prctl; so let's chop it 
    // nicely at the last space char before the limit and add an ellipsis
    char *shortname = strndup(title,16);
    char *p;
    shortname[12] = '\0';
    if ((p = strrchr(shortname,' ')) != NULL) {
      ++p;
    }
    else {
      p = shortname + strlen(shortname);
    }
    // we truncated shortname at index 12, so we've definitely got space to
    // add 4 chars ('...' and the trailing null) without going out of bounds
    strcpy(p,"...");
    p[3] = '\0';
    prctl(PR_SET_NAME,(long)shortname,0,0,0);
    free(shortname);
  }
  else {
    prctl(PR_SET_NAME,(long)title,0,0,0);
  }
}

static int (*real_main) (int, char **, char **);
static void *libc_handle;

static int (*real_putenv) (char *);
int putenv(char *string) {
  if (!strncmp(string,"SETPROCTITLE=",13) && (strlen(string) > 13)) {
    setproctitle( (char *) (string+13) );
  }
  return real_putenv(string);
}

static int (*real_setenv) (const char *, const char *, int);
int setenv(const char *key, const char *value, int overwrite) {
  if (!strncmp(key,"SETPROCTITLE",13) && (strlen(value) > 0)) {
    setproctitle(value);
  }
  return real_setenv(key,value,overwrite);
}

static int (*real_unsetenv) (const char *);
int unsetenv(const char *key) {
  if (!strncmp(key,"SETPROCTITLE",13)) {
    setproctitle(orig_title);
  }
  return real_unsetenv(key);
}

static int fake_main(int argc, char **argv, char **envp) {
  char **new_argv = NULL;
  char *p = NULL;
  int i, envc;
  size_t l;
  dlclose(libc_handle);

  // remember where argv starts, find the max length we can play with, then 
  // copy argv and the environment to somewhere else before calling the real
  // main()


  for (envc = 0; envp[envc] != NULL; ++envc) 
    ;  

  // TODO: handle calloc failure (?!)
  new_argv = calloc(argc+1,sizeof(char *));
  environ = calloc(envc+1, sizeof(char *));

  argv_start = argv[0];
  argv_maxlen = 0;

  // TODO: handle strdup failure (?!)
  for (i = 0; i < argc; ++i) {
    new_argv[i] = strdup(argv[i]);
    if ((p == NULL) || (p + 1 == argv[i])) {
      p = argv[i] + strlen(argv[i]);
    }
  }
  new_argv[i] = NULL;

  // copy all args into a single string, so we can reset the title later
  l = p - argv_start + 1;
  orig_title = malloc(l);
  memset(orig_title,'\0',l);
  for (i = 0; i < argc; ++i) {
    strcat(orig_title,argv[i]);
    strcat(orig_title," ");
  }
  orig_title[l-1] = '\0';

  // TODO: handle strdup failure (?!)
  for (i = 0; i < envc; ++i) {
    environ[i] = strdup(envp[i]);
    if (p + 1 == envp[i]) {
      p = envp[i] + strlen(envp[i]);
    }
  }
  environ[i] = NULL;

  argv_maxlen = p - argv_start;  
  
  // TODO: does this actually achieve anything?
  for (i = 1; i < argc; ++i) {
    argv[i] = NULL;
  }

  // set our title: use the env var if it's set, otherwise the original argv
  p = getenv("SETPROCTITLE");
  if (p == NULL) {
    p = orig_title;
  }
  setproctitle(p);

  return real_main(argc, new_argv, environ);
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
