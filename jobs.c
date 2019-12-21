#include "shell.h"

//#define DEBUG 0

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;    /* 0 if slot is free */
  proc_t *proc;  /* array of processes running in as a job */
  int nproc;     /* number of processes */
  int state;     /* changes when live processes have same state */
  char *command; /* textual representation of command line */
} job_t;

static job_t *jobs = NULL; /* array of all jobs */
static int njobmax = 1;    /* number of slots in jobs array */
static int tty_fd = -1;    /* controlling terminal file descriptor */

static proc_t* findPid(job_t job, pid_t pid) {
  for (int i = 0; i < job.nproc; ++i) {
    if (job.proc[i].pid == pid) {
      return &job.proc[i];
    }
  }

  return NULL;
}

static int job_state(job_t job) {
  int state = FINISHED;
  for (int i = 0; i < job.nproc; ++i) {
    if (job.proc[i].state == RUNNING) {
      return RUNNING;
    } else if (job.proc[i].state == STOPPED) {
      state = STOPPED;
    }
  }

  return state;
}

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
#ifdef DEBUG
  printf("JOBS: sigchld_handler - TODO (implemented)\n");
#endif
  /* TODO: Chan ge state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    printf("pid = %d\n", pid);
    for (int i = 0; i < njobmax; ++i) {
      if (jobs[i].pgid != 0) {
        proc_t *proc = findPid(jobs[i], pid);
        if (proc == NULL) continue;
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
          proc->state = FINISHED;
          proc->exitcode = status;
        } else if (WIFSTOPPED(status)) {
          proc->state = STOPPED;
        } else if (WIFCONTINUED(status)) {
          proc->state = RUNNING;
        }

        jobs[i].state = job_state(jobs[i]);

        break;
      }
    }
  }

  //printf("End of sigchild\n");
  watchjobs(ALL);
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
#ifdef DEBUG
  printf("JOBS: exitcode (implemented)\n");
  printf("exitcode: %d\n", job->nproc);
#endif
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
#ifdef DEBUG
  printf("JOBS: allocjob (implemented)\n");
#endif
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  return njobmax++;
}

static int allocproc(int j) {
#ifdef DEBUG
  printf("JOBS: allocproc (implemented)\n");
#endif
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
#ifdef DEBUG
  printf("JOBS: addjob (implemented)\n");
#endif
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  return j;
}

static void deljob(job_t *job) {
#ifdef DEBUG
  printf("JOBS: deljob (implemented)\n");
#endif
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
#ifdef DEBUG
  printf("JOBS: movejob (implemented)\n");
#endif
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
#ifdef DEBUG
  printf("JOBS: mkcommand (implemented)\n");
#endif
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
#ifdef DEBUG
  printf("JOBS: addproc (implemented)\n");
#endif

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;
#ifdef DEBUG
  printf("JOBS: jobstate - TODO (implemented)\n");
#endif

  /* TODO: Handle case where job has finished. */
  if (state == FINISHED && job->pgid != 0) {
    *statusp = exitcode(job);
    deljob(job);
  }

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
#ifdef DEBUG
  printf("JOBS: jobcmd\n");
#endif
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
#ifdef DEBUG
  printf("JOBS: resumejob - TODO (implemented)\n");
#endif
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

  /* TODO: Continue stopped job. Possibly move job to foreground slot. */
  assert (j != FG);

  Signal(SIGCONT, sigchld_handler);

  Kill(-jobs[j].pgid, SIGCONT);
  jobs[j].state = RUNNING;

  /* foreground job */
  if (!bg) {
    movejob(j, FG);
    (void) monitorjob(mask);
  }

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
#ifdef DEBUG
  printf("JOBS: killjob - TODO (implemented)\n");
#endif
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
  Kill(-jobs[j].pgid, SIGTERM);

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
#ifdef DEBUG
  printf("JOBS: watchjobs - TODO (implemented)\n");
  printf("watchjobs %d %d\n", BG, njobmax);
#endif
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

    /* TODO: Report job number, state, command and exit code or signal. */
    int statusp;

#ifdef DEBUG
    printf("jobstate - which -> %d - j: %d - status: %d", which, j, jobs[j].state);
#endif
    if (jobstate(j, &statusp) == which || which == ALL) {
      msg("[%d] ", j);
      if (WIFEXITED(statusp)) {
        msg("exited, status=%d, command=%s\n", WEXITSTATUS(statusp), jobcmd(j));
      } else if (WIFSIGNALED(statusp)) {
        msg("killed by signal %d, command=%s\n", WTERMSIG(statusp), jobcmd(j));
      } else if (WIFSTOPPED(statusp)) {
        msg("stopped by signal %d, command=%s\n", WSTOPSIG(statusp), jobcmd(j));
      } else if (WIFCONTINUED(statusp)) {
        msg("continued, command=%s\n", jobcmd(j));
      }
    }
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode, state;
#ifdef DEBUG
  printf("SHELL: monitorjob - TODO\n");
#endif
  exitcode = -1;
  while (true) {
    state = jobstate(FG, &exitcode);
    printf("monitorjob\n");
    if (state == STOPPED) {
      Tcsetpgrp(tty_fd, getpgrp());
      movejob(FG, allocjob());
    } else if (state == FINISHED) {
      Tcsetpgrp(tty_fd, getpgrp());
    }

    if (exitcode < 0) {
      Sigsuspend(mask);
    } else {
      break;
    }
  }

  printf("End of monitorjob\n");
  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  Signal(SIGCHLD, sigchld_handler);
  jobs = calloc(sizeof(job_t), 1);
#ifdef DEBUG
  printf("JOBS: initjobs (implemented)\n");
#endif
  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFL, O_CLOEXEC);
  Tcsetpgrp(tty_fd, getpgrp());
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);
#ifdef DEBUG
  printf("JOBS: shutdownjobs - TODO (implemented?)\n");
#endif
  /* TODO: Kill remaining jobs and wait for them to finish. */
  for (int j = BG; j < njobmax; ++j) {
    if (jobs[j].state != FINISHED) {
      killjob(j);
      Waitpid(-jobs[j].pgid, NULL, 0);
    }
  }

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}
