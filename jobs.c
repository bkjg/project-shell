#include "shell.h"

//include was added to save terminal parameters because processes like vim and watch change them
#include "termios.h"

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

  /* TODO: Chan ge state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
  bool status_changed = false;
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
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
          proc->exitcode = status;
        }

        int prev_state = jobs[i].state;
        jobs[i].state = job_state(jobs[i]);
        if (prev_state != jobs[i].state) {
          status_changed = true;
        }
        break;
      }
    }
  }

  if (status_changed) {
    watchjobs(ALL);
  }
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;

  if (bg) {
    msg("[%d] %d\n", j, pgid);
  }

  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
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

  /* TODO: Handle case where job has finished. */
  if (state == FINISHED && job->pgid != 0) {
    *statusp = exitcode(job);
    deljob(job);
  }

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

  /* TODO: Continue stopped job. Possibly move job to foreground slot. */
  assert (j != FG);

  if (jobs[j].state == STOPPED) {
    while (jobs[j].state != RUNNING) {
      Kill(-jobs[j].pgid, SIGCONT);
      Sigsuspend(mask);
    }
  }

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
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */

  if (jobs[j].state == STOPPED) {
    Kill(-jobs[j].pgid, SIGCONT);
  }
  Kill(-jobs[j].pgid, SIGTERM);
  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

    /* TODO: Report job number, state, command and exit code or signal. */
    int statusp;
    int state;

    state = jobs[j].state;
    if (state == which || which == ALL) {
      msg("[%d] ", j);
      statusp = exitcode(&jobs[j]);
      if (state == RUNNING) {
        if (statusp != -1 && WIFCONTINUED(statusp)) {
          msg("continue '%s'\n", jobcmd(j));
        } else {
          msg("running '%s'\n", jobcmd(j));
        }
      } else if (state == STOPPED) {
        msg("suspended '%s'\n", jobcmd(j));
      } else {
        if (WIFEXITED(statusp)) {
          msg("exited '%s', status=%d\n", jobcmd(j), WEXITSTATUS(statusp));
        } else if (WIFSIGNALED(statusp)) {
          msg("killed '%s' by signal %d\n", jobcmd(j), WTERMSIG(statusp));
        }
        deljob(&jobs[j]);
      }
    }
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int status, state;

/* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
  status = -1;
  struct termios ots;
  // save old terminal parameters, because processes like vim changes them
  tcgetattr(tty_fd, &ots);

  Tcsetpgrp(tty_fd, jobs[FG].pgid);
  sigset_t old_mask;
  Sigprocmask(SIG_SETMASK, mask, &old_mask);
  while ((state = jobs[FG].state) == RUNNING) {
    Sigsuspend(mask);
  }

  state = jobs[FG].state;
  Sigprocmask(SIG_SETMASK, &old_mask, NULL);
  if (state == STOPPED) {
    Tcsetpgrp(tty_fd, getpgrp());
    tcsetattr(tty_fd, TCSADRAIN, &ots);
    int j = allocjob();
    jobs[j].pgid = 0;
    jobs[j].state = STOPPED;
    movejob(FG, j);
    watchjobs(STOPPED);
  } else if (state == FINISHED) {
    Tcsetpgrp(tty_fd, getpgrp());
    //restore terminal parameters 
    tcsetattr(tty_fd, TCSADRAIN, &ots);
    status = exitcode(&jobs[FG]);
    watchjobs(FINISHED);
    deljob(&jobs[FG]);
  }

  return status;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  Signal(SIGCHLD, sigchld_handler);
  jobs = calloc(sizeof(job_t), 1);
  
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
  
  /* TODO: Kill remaining jobs and wait for them to finish. */
  for (int j = BG; j < njobmax; ++j) {
    if (jobs[j].state != FINISHED) {
      killjob(j);
    }
  }

  watchjobs(FINISHED);
  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}
