// UCLA CS 111 Lab 1 command execution

// Copyright 2012-2014 Paul Eggert.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "command.h"
#include "command-internals.h"

#include <time.h>
#include <fcntl.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/resource.h>


#define NOT_USED(x) (void)(x)
#define true 1

FILE* stdlog;

typedef struct
{
  double finish_time;
  int finish_precision;

  double real_time;
  double user_time;
  double system_time;

  char** words;
  pid_t pid;
}
profile_proc_t;

int
prepare_profiling (char const *name)
{
  return open(name, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
}

void profile (const profile_proc_t* p)
{
  if (stdlog == NULL)
    return;

  flock(fileno(stdlog), LOCK_EX);

  char** w = p->words;

  fprintf(stdlog, "%0.9f ", p->finish_time);
  fprintf(stdlog, "%0.6f ", p->real_time);
  fprintf(stdlog, "%0.6f ", p->user_time);
  fprintf(stdlog, "%0.6f ", p->system_time);

  if (*w == NULL)
    fprintf(stdlog, "[%d]", (int) p->pid);

  while (*w != NULL)
    fprintf(stdlog, "%s ", *w++);
  fprintf(stdlog, "\n");

  fflush(stdlog);

  flock(fileno(stdlog), LOCK_UN);
}

int
command_status (command_t c)
{
  return c->status;
}

enum command_type command_type (command_t c)
{
  return c->type;
}

void
recursive_execute(command_t c, int inherited_input, int inherited_output);

void execute_simple(command_t c, int input_fd, int output_fd)
{
  profile_proc_t prof;

  struct timespec rtime1;
  struct timespec rtime2;
  struct timeval utime1;
  struct timeval utime2;
  struct timeval stime1;
  struct timeval stime2;
  struct timespec ftime;

  struct rusage resusage;

  getrusage(RUSAGE_CHILDREN, &resusage);
  clock_gettime(CLOCK_MONOTONIC, &rtime1);

  utime1 = resusage.ru_utime;
  stime1 = resusage.ru_stime;

  pid_t pid = fork();

  if (pid < 0)
    error(0, 0, "warning: fork() failed");

  if (pid == 0) // child
  {
    if (input_fd != -1)
      dup2(input_fd, fileno(stdin));
    if (output_fd != -1)
      dup2(output_fd, fileno(stdout));

    if (!strcmp(c->u.word[0], "exec"))
      execvp(c->u.word[1], c->u.word + 1);
    else
      execvp(c->u.word[0], c->u.word);

    perror("error: process spawn failed\n");
  }

  int status;

  if (waitpid(pid, &status, 0) < 0)
    perror("error: process join failed\n");

  getrusage(RUSAGE_CHILDREN, &resusage);
  clock_gettime(CLOCK_REALTIME, &ftime);
  clock_gettime(CLOCK_MONOTONIC, &rtime2);

  utime2 = resusage.ru_utime;
  stime2 = resusage.ru_stime;

  prof.finish_time = ftime.tv_sec + (double) (ftime.tv_nsec) / 100000000;

  prof.real_time = (rtime2.tv_sec - rtime1.tv_sec) + 
                (double) (rtime2.tv_nsec - rtime1.tv_nsec) / 100000000;
  prof.user_time = (utime2.tv_sec - utime1.tv_sec) + 
                (double) (utime2.tv_usec - utime1.tv_usec) / 100000;
  prof.system_time = (stime2.tv_sec - stime1.tv_sec) + 
                (double) (stime2.tv_usec - stime1.tv_usec) / 100000;

  prof.words = c->u.word;
  prof.pid = pid;

  profile(&prof);

  c->status = WEXITSTATUS(status);
}

void execute_while(command_t c, int input_fd, int output_fd)
{
  int cond_status = 1;

  recursive_execute(c->u.command[0], input_fd, output_fd);

  cond_status = c->u.command[0]->status;

  if (cond_status != 0)
    c->status = 0;

  while (cond_status == 0)
  {
    recursive_execute(c->u.command[1], input_fd, output_fd);
    c->status = c->u.command[1]->status;

    recursive_execute(c->u.command[0], input_fd, output_fd);
    cond_status = c->u.command[0]->status;
  }

}

void execute_until(command_t c, int input_fd, int output_fd)
{
  int cond_status = 0;

  recursive_execute(c->u.command[0], input_fd, output_fd);

  cond_status = c->u.command[0]->status;

  if (cond_status == 0)
    c->status = 1;

  while (cond_status != 0)
  {
    recursive_execute(c->u.command[1], input_fd, output_fd);
    c->status = c->u.command[1]->status;

    recursive_execute(c->u.command[0], input_fd, output_fd);
    cond_status = c->u.command[0]->status;
  }
}

void execute_subshell(command_t c, int input_fd, int output_fd)
{
  pid_t pid;
  int status;

  profile_proc_t prof;

  struct timespec rtime1;
  struct timespec rtime2;
  struct timeval utime1;
  struct timeval utime2;
  struct timeval stime1;
  struct timeval stime2;
  struct timespec ftime;

  struct rusage resusage;

  getrusage(RUSAGE_CHILDREN, &resusage);
  clock_gettime(CLOCK_MONOTONIC, &rtime1);

  utime1 = resusage.ru_utime;
  stime1 = resusage.ru_stime;

  if (!(pid = fork()))
  {
    recursive_execute(c->u.command[0], input_fd, output_fd);
    _exit(command_status(c->u.command[0]));
  }

  if (waitpid(pid, &status, 0) < 0)
    perror("error: process join failed\n");

  getrusage(RUSAGE_CHILDREN, &resusage);
  clock_gettime(CLOCK_REALTIME, &ftime);
  clock_gettime(CLOCK_MONOTONIC, &rtime2);

  utime2 = resusage.ru_utime;
  stime2 = resusage.ru_stime;

  prof.finish_time = ftime.tv_sec + (double) (ftime.tv_nsec) / 100000000;

  prof.real_time = (rtime2.tv_sec - rtime1.tv_sec) + 
                (double) (rtime2.tv_nsec - rtime1.tv_nsec) / 100000000;
  prof.user_time = (utime2.tv_sec - utime1.tv_sec) + 
                (double) (utime2.tv_usec - utime1.tv_usec) / 100000;
  prof.system_time = (stime2.tv_sec - stime1.tv_sec) + 
                (double) (stime2.tv_usec - stime1.tv_usec) / 100000;

  char* null = NULL;
  prof.words = &null;
  prof.pid = pid;

  profile(&prof);

  c->status = WEXITSTATUS(status);
}

void execute_sequence(command_t c, int input_fd, int output_fd)
{
  recursive_execute(c->u.command[0], input_fd, output_fd);
  
  recursive_execute(c->u.command[1], input_fd, output_fd);

  c->status = c->u.command[1]->status;
}

void execute_pipe(command_t c, int input_fd, int output_fd)
{
  int pipefd[2];
  pid_t pid;

  if (pipe(pipefd) == -1)
  {
    error(0, 0, "warning: could not create pipe");
    c->status = -1;
    return;
  }

  if (!(pid = fork()))
  {
    profile_proc_t prof;
    struct timespec rtime1;
    struct timespec rtime2;
    struct timeval utime1;
    struct timeval utime2;
    struct timeval stime1;
    struct timeval stime2;
    struct timespec ftime;
    struct rusage resusage;

    getrusage(RUSAGE_SELF, &resusage);
    clock_gettime(CLOCK_MONOTONIC, &rtime1);
    utime1 = resusage.ru_utime;
    stime1 = resusage.ru_stime;


    /* actual command part */
    close(pipefd[0]);
    recursive_execute(c->u.command[0], input_fd, pipefd[1]);
    /* end actual command part */

    getrusage(RUSAGE_SELF, &resusage);
    clock_gettime(CLOCK_REALTIME, &ftime);
    clock_gettime(CLOCK_MONOTONIC, &rtime2);

    utime2 = resusage.ru_utime;
    stime2 = resusage.ru_stime;

    prof.finish_time = ftime.tv_sec + (double) (ftime.tv_nsec) / 100000000;

    prof.real_time = (rtime2.tv_sec - rtime1.tv_sec) + 
                  (double) (rtime2.tv_nsec - rtime1.tv_nsec) / 100000000;
    prof.user_time = (utime2.tv_sec - utime1.tv_sec) + 
                  (double) (utime2.tv_usec - utime1.tv_usec);
    prof.system_time = (stime2.tv_sec - stime1.tv_sec) + 
                  (double) (stime2.tv_usec - stime1.tv_usec);

    char* null = NULL;
    prof.words = &null;
    prof.pid = getpid();

    profile(&prof);

    _exit(0);
  }

  close(pipefd[1]);
  recursive_execute(c->u.command[1], pipefd[0], output_fd);

  c->status = c->u.command[1]->status;
}

void execute_if(command_t c, int input_fd, int output_fd)
{
  recursive_execute(c->u.command[0], input_fd, output_fd);

  c->status = c->u.command[0]->status;

  if (c->u.command[0]->status == 0)
  {
    recursive_execute(c->u.command[1], input_fd, output_fd);
    c->status = command_status(c->u.command[1]);
  }

  else if (c->u.command[2] != NULL)
  {
    recursive_execute(c->u.command[2], input_fd, output_fd);
    c->status = command_status(c->u.command[2]);
  }
}

void
recursive_execute(command_t c, int inherited_input, int inherited_output)
{
  int input_fd = -1;
  int output_fd = -1;

  if (c->input != NULL)
    input_fd = open(c->input, O_RDONLY, 0644);
  else if (inherited_input > -1)
    input_fd = inherited_input;

  if (c->output != NULL)
    output_fd = open(c->output, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  else if (inherited_output > -1)
    output_fd = inherited_output;

  switch (command_type(c))
  {
    case IF_COMMAND:
      execute_if(c, input_fd, output_fd);
      break;

    case PIPE_COMMAND:
      execute_pipe(c, input_fd, output_fd);
      break;

    case SEQUENCE_COMMAND:
      execute_sequence(c, input_fd, output_fd);
      break;

    case SUBSHELL_COMMAND:
      execute_subshell(c, input_fd, output_fd);
      break;

    case UNTIL_COMMAND:
      execute_until(c, input_fd, output_fd);
      break;

    case WHILE_COMMAND:
      execute_while(c, input_fd, output_fd);
      break;

    case SIMPLE_COMMAND:
      execute_simple(c, input_fd, output_fd);
      break;
  }

  if (input_fd != -1 && inherited_input == -1)
    close(input_fd);
  if (output_fd != -1 && inherited_output == -1)
    close(output_fd);
}

void
execute_command (command_t c, int profiling)
{
  stdlog = fdopen(profiling, "w");

  recursive_execute(c, -1, -1);

  if (profiling > -1)
  {
    fflush(stdlog);
    flock(fileno(stdlog), LOCK_UN);
    close(profiling);
  }
}
