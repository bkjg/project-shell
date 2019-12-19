#include <readline/readline.h>
#include <readline/history.h>

//#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static sigjmp_buf loop_env;

static void sigint_handler(int sig) {
  siglongjmp(loop_env, sig);
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */
#ifdef DEBUG
  safe_printf("SHELL: do_redir - TODO\n");
#endif
  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
<<<<<<< HEAD
    mode = token[i];
    if (separator_p(mode)) {
=======
    if (separator_p(token[i])) {
>>>>>>> b5eb13d... Add implementation of pipeline
      token[n] = token[i];
      n++;
    } else if (string_p(mode)) {
      token[n] = token[i];
      n++;
    } else {
      assert(i + 1 < ntokens);
      if (mode == T_INPUT) {
        *inputp = open(token[i + 1], O_RDONLY, 0);
        i++;
        assert(*inputp >= 0); 
      } else if (mode == T_OUTPUT) {
        *outputp = open(token[i + 1], O_CREAT | O_WRONLY | O_TRUNC, 0666);
        i++;
        assert(*outputp >= 0); 
      } else if (mode == T_APPEND) {
        *outputp = open(token[i + 1], O_CREAT | O_WRONLY | O_APPEND, 0666);
        i++;
        assert(*outputp >= 0); 
      } else {
        /* BANG operator */
      }
    }
  }

  for (int i = n; i < ntokens; ++i) {
    token[i] = T_NULL;
  }

  token[n] = NULL;

  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

#ifdef DEBUG
  safe_printf("SHELL: do_job\n");
#endif

  ntokens = do_redir(token, ntokens, &input, &output);

  if ((exitcode = builtin_command(token)) >= 0)
    return exitcode;

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef DEBUG
  safe_printf("SHELL: do_job - TODO\n");
#endif
  pid_t pid = Fork();

  if (pid == 0) {  
    Sigprocmask(SIG_SETMASK, &mask, NULL);
    //Setpgid(getpid(), getpid());

    if (input != -1) {
      Dup2(input, STDIN_FILENO);
      close(input);
    }

    if (output != -1) {
      Dup2(output, STDOUT_FILENO);
      close(output);
    }

    external_command(token);
  }

  Setpgid(pid, getpid());
  int j = addjob(getpgid(pid), bg);
  addproc(j, pid, token);

  if (!bg) {
<<<<<<< HEAD
    exitcode = monitorjob(&mask);
=======
    while (true) {
      monitorjob(&mask);
      (void) jobstate(j, &exitcode);
      if (exitcode < 0) {
        Sigsuspend(&mask);
      } else {
        break;
      }
    }
>>>>>>> b5eb13d... Add implementation of pipeline
  }

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens) {
  ntokens = do_redir(token, ntokens, &input, &output);
#ifdef DEBUG
  printf("SHELL: do_stage\n");
#endif
  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();

  if (pid == 0) {
    Sigprocmask(SIG_SETMASK, mask, NULL);
    Setpgid(getpid(), pgid);

    if (input != -1) {
      Dup2(input, STDIN_FILENO);
      close(input);
    }

    if (output != -1) {
      Dup2(output, STDOUT_FILENO);
      close(output);
    }

    if (builtin_command(token) >= 0) {
      exit(EXIT_SUCCESS);
    }

    external_command(token);
  }

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
#ifdef DEBUG
  printf("SHELL: do_pipeline\n");
#endif
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;
  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
  pid = Fork();
  exitcode = -1;
  pid_t pid1, pid2;
  if (pid == 0) {
    //Sigprocmask(SIG_SETMASK, &mask, NULL);
    pgid = getpid();
    Setpgid(getpid(), pgid);

    size_t size; 
    for (int i = 0; i < ntokens; ++i) {
      if (token[i] == T_PIPE) {
        token[i] = T_NULL;
        size = i;
        break;
      }
    }

    sigset_t old;
    pid1 = do_stage(pgid, &mask, input, output, token, size);
    int j = addjob(pgid, bg);
    addproc(j, getpid(), token);
    addproc(j, pid1, token);

    Sigsuspend(&mask);
    printf("First child returned\n");
    Sigprocmask(SIG_SETMASK, &mask, NULL);

    //Waitpid(pid1, NULL, NULL);

    Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);
    pid2 = do_stage(pgid, &mask, next_input, input, token + size + 1, ntokens - size - 1);
    //Kill(pid2, SIGKILL);
    //Waitpid(pid2, NULL, NULL);
    printf("YAY\n");
    //Sigprocmask(SIG_SETMASK, &mask, &old);
    //printf("Second child returned %d\n", sigismember(&old, SIGCHLD));
    addproc(j, pid2, token + size + 1);
    printf("status: %d\n", jobstate(j, NULL));
    printf("bg = %d, %d\n", bg, j);
    Sigsuspend(&mask);
    Sigprocmask(SIG_SETMASK, &mask, NULL);
  }

  if (!bg) {
    //printf("Go to monitorjob\n");
    monitorjob(&mask);
  }

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
#ifdef DEBUG
  printf("SHELL: is_pipeline (implemented)\n");
#endif
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
#ifdef DEBUG
  printf("SHELL: eval (implemented)\n");
#endif
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

int main(int argc, char *argv[]) {
  rl_initialize();

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  initjobs();

  Signal(SIGINT, sigint_handler);
  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  char *line;
  while (true) {
    if (!sigsetjmp(loop_env, 1)) {
      line = readline("# ");
    } else {
      msg("\n");
      continue;
    }

    if (line == NULL)
      break;

    if (strlen(line)) {
      add_history(line);
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
