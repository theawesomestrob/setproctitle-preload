#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <unistd.h>

#define ENV_SETPROCTITLE "SETPROCTITLE"
#define ENV_OPT_SHORTNAME_ELLIPSIS "SETPROCTITLE_ELLIPSIS"

extern char **environ;

static char *argv_start = NULL;
static size_t argv_maxlen = 0;

static char *orig_argv = NULL;

void setproctitle(const char *title) {
  if (title == NULL || argv_start == NULL || argv_maxlen < 1) {
    return;
  }
  memset(argv_start,0,argv_maxlen);
  strncpy(argv_start,title,argv_maxlen);
  argv_start[argv_maxlen-1] = '\0';
  if (strlen(title) > 15) {
    char *shortname = strndup(title,16);
    char *p;
    // if title is >15 chars it'll get truncated by prctl; so let's chop it 
    // nicely if we can
    if (getenv(ENV_OPT_SHORTNAME_ELLIPSIS)) {
      // cut at the last space char before the limit and add an ellipsis
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
    }
    else {
      // otherwise cut at the first space
      if ((p = strchr(shortname,' ')) != NULL) {
	*p = '\0';
      }
    }
    prctl(PR_SET_NAME,(long)shortname,0,0,0);
    free(shortname);
  }
  else {
    prctl(PR_SET_NAME,(long)title,0,0,0);
  }
}

static int (*real_putenv) (char *);
int putenv(char *string) {
  if (!strncmp(string,ENV_SETPROCTITLE "=",13) && (strlen(string) > 13)) {
    setproctitle( (char *) (string+13) );
  }
  return real_putenv(string);
}

static int (*real_setenv) (const char *, const char *, int);
int setenv(const char *key, const char *value, int overwrite) {
  if (!strncmp(key,ENV_SETPROCTITLE,13) && (strlen(value) > 0)) {
    setproctitle(value);
  }
  return real_setenv(key,value,overwrite);
}

static int (*real_unsetenv) (const char *);
int unsetenv(const char *key) {
  if (!strncmp(key,ENV_SETPROCTITLE,13)) {
    setproctitle(orig_argv);
  }
  return real_unsetenv(key);
}

static int (*real_main) (int, char **, char **);

static int fake_main(int argc, char **argv, char **envp) {
  char **new_argv = NULL;
  char *p = NULL;
  int i, envc;
  size_t l;

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
  orig_argv = malloc(l);
  memset(orig_argv,'\0',l);
  for (i = 0; i < argc; ++i) {
    strcat(orig_argv,argv[i]);
    strcat(orig_argv," ");
  }
  orig_argv[l-1] = '\0';

  // TODO: handle strdup failure (?!)
  for (i = 0; i < envc; ++i) {
    environ[i] = strdup(envp[i]);
    if (p + 1 == envp[i]) {
      p = envp[i] + strlen(envp[i]);
    }
  }
  environ[i] = NULL;

  argv_maxlen = p - argv_start;  
  
  // set our title: use the env var if it's set, otherwise the original argv
  p = getenv(ENV_SETPROCTITLE);
  if (p == NULL) {
    p = orig_argv;
  }
  setproctitle(p);

  return real_main(argc, new_argv, environ);
}

// hook the glibc entry point to the program's main()
int __libc_start_main(int (*main) (int, char **, char **), int argc, 
		      char **ubp_av, void (*init) (void), void (*fini) (void), 
		      void (*rtld_fini) (void), void (*stack_end)) {

  int (*real_libc_start_main) (int (*main) (int, char **, char **), int argc,
			       char **ubp_av, void (*init) (void),
			       void (*fini) (void), void (*rtld_fini) (void),
			       void (*stack_end));

  real_main = main;
  real_libc_start_main = dlsym(RTLD_NEXT, "__libc_start_main");
  real_putenv = dlsym(RTLD_NEXT,"putenv");
  real_setenv = dlsym(RTLD_NEXT,"setenv");
  real_unsetenv = dlsym(RTLD_NEXT,"unsetenv");

  return real_libc_start_main(fake_main, argc, ubp_av, init, fini, rtld_fini, stack_end);
}
