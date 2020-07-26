#include "shell.h"
#include <glob.h>

typedef int (*func_t)(char **argv);

typedef struct {
  const char *name;
  func_t func;
} command_t;

/* expanding wildcard:
 * to expand wildcard use the linux struct glob_t */
static void expand_wildcard(char *command, char** pattern) {
  glob_t globbuf;

  /* find number of arguments */
  int argc = 0;
  while (pattern[argc] != T_NULL) {
    argc++;
  }

  /* find the number of options of command */
  int opt = 0;
  for (int i = 1; i < argc; ++i) {
    if (pattern[i][0] == '-') {
      opt++;
    }
  }

  /* reserve opt + 1 slots (for name of command and options */
  globbuf.gl_offs = opt + 1;

  /* extend first argument */
  if (argc - opt > 1) {
    glob(pattern[opt + 1], GLOB_DOOFFS , NULL, &globbuf);
  } else {
    return;
  }

  /* extend other arguments with append option */
  for (int i = opt + 2; i < argc; ++i) {
    glob(pattern[i], GLOB_DOOFFS | GLOB_APPEND, NULL, &globbuf);
  }


  /* save command and options to structure */
  for (int i = 0; i <= opt; ++i) {
    globbuf.gl_pathv[i] = pattern[i];
  }

  /* if there was anything to extend, do execve */
  if (globbuf.gl_pathc > 0) {
    execve(command, &globbuf.gl_pathv[0], environ);
  }
}

/* do_history added to display the history of commands
 * display the content of history file */
static int do_history(char **argv) {

  if (Fork() == 0) {
    const char *homedir = getenv("HOME");
    char *path = strndup(homedir, strlen(homedir));
    strapp(&path, "/.history");
    char *argvv[] = {"cat", path, NULL};
    external_command(argvv);
  }

  return 0;
}

static int do_quit(char **argv) {
  shutdownjobs();
  exit(EXIT_SUCCESS);
}

/*
 * Change current working directory.
 * 'cd' - change to $HOME
 * 'cd path' - change to provided path
 */
static int do_chdir(char **argv) {
  glob_t globbuf;
  globbuf.gl_offs = 0;
  char *path = argv[0];

  if (path == NULL) {
      path = getenv("HOME");
  }

  glob(path, GLOB_DOOFFS , NULL, &globbuf);

  if (globbuf.gl_pathc > 1) {
    msg("cd: Wrong numbers of arguments\n");
    return 1;
  }

  int rc = chdir(globbuf.gl_pathv[0]);
  if (rc < 0) {
    msg("cd: %s: %s\n", strerror(errno), path);
    return 1;
  }
  return 0;
}

/*
 * Displays all stopped or running jobs.
 */
static int do_jobs(char **argv) {
  watchjobs(ALL);
  return 0;
}

/*
 * Move running or stopped background job to foreground.
 * 'fg' choose highest numbered job
 * 'fg n' choose job number n
 */
static int do_fg(char **argv) {
  int j = argv[0] ? atoi(argv[0]) : -1;

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);
  if (!resumejob(j, FG, &mask)) {
    msg("fg: job not found: %s\n", argv[0]);
  }

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return 0;
}

/*
 * Make stopped background job running.
 * 'bg' choose highest numbered job
 * 'bg n' choose job number n
 */
static int do_bg(char **argv) {
  int j = argv[0] ? atoi(argv[0]) : -1;

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);
  if (!resumejob(j, BG, &mask)) {
    msg("bg: job not found: %s\n", argv[0]);
  }

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return 0;
}

/*
 * Make stopped background job running.
 * 'bg' choose highest numbered job
 * 'bg n' choose job number n
 */
static int do_kill(char **argv) {
  if (!argv[0]) {
    return -1;
  }

  if (*argv[0] != '%') {
    return -1;
  }

  int j = atoi(argv[0] + 1);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);
  if (!killjob(j)) {
    msg("kill: job not found: %s\n", argv[0]);
  }

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  return 0;
}

static command_t builtins[] = {
  {"quit", do_quit}, {"cd", do_chdir},  {"jobs", do_jobs}, {"fg", do_fg},
  {"bg", do_bg},     {"kill", do_kill}, {"history", do_history}, {NULL, NULL},
};

int builtin_command(char **argv) {
  for (command_t *cmd = builtins; cmd->name; cmd++) {
    if (strcmp(argv[0], cmd->name)) {
      continue;
    }
    return cmd->func(&argv[1]);
  }

  errno = ENOENT;
  return -1;
}


noreturn void external_command(char **argv) {
  const char *path = getenv("PATH");

  if (!index(argv[0], '/') && path) {
    /* For all paths in PATH construct an absolute path and execve it. */
    char* command;
    int pos;
    while ((pos = strcspn(path, ":")) > 0) {
      command = strndup(path, pos);
      strapp(&command, "/");
      strapp(&command, argv[0]);

      /* try to extend command , never return if succeeded */
      expand_wildcard(command, argv);
      (void) execve(command, argv, environ);
      path += (pos + 1);
      free(command);
    }
  } else {
    (void)execve(argv[0], argv, environ);
  }

  msg("%s: %s\n", argv[0], strerror(errno));
  exit(EXIT_FAILURE);
}
