#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

#include <dirent.h>

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "print current working directory path"},
  {cmd_cd, "cd", "change current working directory to argument path"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}


/* Print current working directory */
int cmd_pwd(unused struct tokens *tokens) {
  char buffer[80];
  getcwd(buffer, sizeof(buffer));
  fprintf(stdout, "%s\n", buffer);
  return 1;
}

/* Change current working directory to token */
int cmd_cd(unused struct tokens *tokens) {
  char *cddir = tokens_get_token(tokens, 1);
  chdir(cddir);
  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

typedef struct parsed_path
{
  char **path_arr;
  size_t path_num;
} parsed_path_t;


parsed_path_t parse_path() {
  char *path_sys = getenv("PATH");
  char *path_str = malloc(strlen(path_sys)+1);
  strcpy(path_str, path_sys);
  char **path_arr;
  char delimiter = ':';
  size_t idx = 0;
  int path_num = 0;
  char c;

  while ((c = path_str[idx]) != '\0')
  {
    if (c == delimiter) {
      path_num++;
      path_str[idx] = '\0';
    }
    idx++;
  }
  path_num++;

  path_arr = malloc(path_num * sizeof(*path_arr));
  
  size_t pidx;
  idx = 0;
  for (size_t path_num_cur = 0; path_num_cur != path_num; path_num_cur++) {
    pidx = idx;
    path_arr[path_num_cur] = path_str + pidx;
    while ((c = path_str[idx++]) != '\0') {}
  }
  
  parsed_path_t ppt = {
    .path_arr = path_arr,
    .path_num = path_num
  };
  return ppt;
}


char * match_path(char *cmd, parsed_path_t ppt) {
  char *path;
  char *path_cmd = (char *) malloc(4096);
  memset(path_cmd, 0, 4096);
  DIR *dp;
  struct dirent *dent;
  for (int idx = 0; idx != ppt.path_num; ++idx) {
    path = ppt.path_arr[idx];
    if ((dp = opendir(path)) != NULL) {
      while ((dent = readdir(dp)) != NULL){
        if (strcmp(dent->d_name, cmd) == 0) {
          strcat(path_cmd, path);
          strcat(path_cmd, "/");
          strcat(path_cmd, cmd);
          closedir(dp);
          return path_cmd;
        }
      }
      closedir(dp);
    }
  }
  return path_cmd;
}


#define IO_IN 1
#define IO_OUT -1
#define IO_NO 0

typedef struct parsed_args
{
  char ***args_arr;
  size_t *args_len;
  int *io_idxs;
  int *io_tags;
  size_t proc_num;
} parsed_args_t;


size_t count_proc_num(struct tokens *tokens) {
  size_t tk_len = tokens_get_length(tokens);
  size_t proc_num = 1;
  char *tmp_token;

  for (size_t i=0; i!=tk_len; ++i) {
    tmp_token = tokens_get_token(tokens, i);
    if (strcmp(tmp_token, "|") == 0) {
      proc_num++;
    }
  }
  
  return proc_num;
}

parsed_args_t parse_args(struct tokens *tokens) {
  size_t tk_len = tokens_get_length(tokens);
  size_t proc_num = count_proc_num(tokens);

  char *tmp_token;
  size_t idx = 0;
  size_t args_len[proc_num];
  char **args_arr[proc_num];
  int io_idxs[proc_num];
  int io_tags[proc_num];
  for (size_t i=0; i!= proc_num; ++i) {
    args_len[i] = 0;
    io_tags[i] = IO_NO;
    io_idxs[i] = -1;
    size_t j;

    for (j=idx; j!=tk_len; ++j) {
      args_len[i]++;
      tmp_token = tokens_get_token(tokens, j);
      if (strcmp(tmp_token, "|") == 0) {
        break;
      }
    }

    args_arr[i] = malloc(args_len[i] * sizeof(char *));
    for (j=idx; j!=tk_len; ++j) {
      tmp_token = tokens_get_token(tokens, j);
      if (strcmp(tmp_token, "|") == 0) {
        break;
      } else {
        args_arr[i][j-idx] = tmp_token;
      }

      if (strcmp(tmp_token, ">") == 0) {
        io_tags[i] = IO_OUT;
        io_idxs[i] = j-idx;
      } else if (strcmp(tmp_token, "<") == 0) {
        io_tags[i] = IO_IN;
        io_idxs[i] = j-idx;
      }
    }
    idx = j+1;
  }

  parsed_args_t pat = {
    .args_arr = args_arr,
    .args_len = args_len,
    .io_idxs = io_idxs,
    .io_tags = io_tags,
    .proc_num = proc_num
  };

  return pat;
}



void execute_cmd(size_t argc, char **args, int io_idx, int io_tag) {
  tcsetpgrp(0, getpgid(getpid()));
  printf("cmd pid: %d, cmd pgid: %d cmd foreground pgid: %d\n", getpid(), getpgid(getpid()), tcgetpgrp(0));

  if (argc == 0) {
    exit(0);
  }

  char *cmd = args[0];
  parsed_path_t ppt = parse_path();
  char *path_cmd = match_path(cmd, ppt);
  size_t limit = (io_idx < 0 ? argc : io_idx);
  char *cmd_args[limit+1];
  cmd_args[0] = path_cmd;
  for (size_t i = 1; i<limit; ++i) {
    cmd_args[i] = args[i];
  }
  cmd_args[limit] = NULL;

  if (io_tag == IO_NO) {} 
  else {
    int fd; // file-descriptor
    if (io_tag == IO_IN) {
      if ((fd = open(args[limit + 1], O_RDONLY, 0)) < 0) {
        perror("cound not open the input file");
        return;
      }
      dup2(fd, STDIN_FILENO);
    } else if (io_tag == IO_OUT) {
      if ((fd = creat(args[limit + 1], 0644)) < 0) {
        perror("cound not open the output file");
        return;
      }
      dup2(fd, STDOUT_FILENO);
    }
    close(fd);
  }
  execv(path_cmd, cmd_args);
  exit(-1); // If execute do not exit, then the process need exit with -1
}


void execute(struct tokens *tokens) {
  setpgid(getpid(), getpid());
  tcsetpgrp(0, getpid());
  signal(SIGINT, SIG_DFL);  // ctrl+C
  signal(SIGQUIT, SIG_DFL); // ctrl+'\'
  signal(SIGTSTP, SIG_DFL); // ctrl+Z
  signal(SIGCONT, SIG_DFL);
  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
  printf("cmd pid: %d, cmd pgid: %d cmd foreground pgid: %d\n", getpid(), getpgid(getpid()), tcgetpgrp(0));

  char *cmd = tokens_get_token(tokens, 0);
  if (cmd == NULL) {
    exit(0);
  }

  parsed_args_t pat = parse_args(tokens);
  size_t proc_num = pat.proc_num;

  for (size_t proc_idx = 0; proc_idx != proc_num; ++proc_idx) {
    if (proc_idx == proc_num - 1) {
      // In tail of pipes
      execute_cmd(pat.args_len[proc_idx], pat.args_arr[proc_idx],
                  pat.io_idxs[proc_idx], pat.io_tags[proc_idx]);
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) perror("pipe fail");
    int rfd = pipefd[0];
    int wfd = pipefd[1];
    pid_t pid;
    pid = fork();

    if (pid < 0) {
      perror("fork fail");
    } else if (pid == 0) {
      // Child Process
      close(rfd);
      dup2(wfd, STDOUT_FILENO);
      close(wfd);
      execute_cmd(pat.args_len[proc_idx], pat.args_arr[proc_idx],
                  pat.io_idxs[proc_idx], pat.io_tags[proc_idx]);
    } else {
      // Parent Process
      close(wfd);
      dup2(rfd, STDIN_FILENO);
      close(rfd);
    }
  }
  // Child Process Exits, Auto Free Memory.
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  pid_t shell_pid = getpid();
  setpgid(shell_pid, shell_pid);
  tcsetpgrp(0, shell_pid);
  signal(SIGINT, SIG_IGN);  // ctrl+C
  signal(SIGQUIT, SIG_IGN); // ctrl+'\'
  signal(SIGTSTP, SIG_IGN); // ctrl+Z
  signal(SIGCONT, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  
  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);
  printf("shell pid: %d, shell pgid: %d shell foreground pgid: %d\n", shell_pid, getpgid(shell_pid), tcgetpgrp(0));

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));
    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      int status;
      pid_t pid = fork();
      if (pid == 0) {
        execute(tokens);
      } else if (pid < 0) {
        perror("fork fail");
      } else {
        wait(&status);
      } 
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);

    tcsetpgrp(0, shell_pid);
    printf("shell pid: %d, shell pgid: %d shell foreground pgid: %d\n", getpid(), getpgid(getpid()), tcgetpgrp(0));
  }

  return 0;
}
