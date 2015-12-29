#include "sh61.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

typedef struct zombies zombies;
struct zombies {
    int count;
    int *pids;
};

static void reap_zombies(zombies* z) {
    for (int i = 0; i < z->count; i++) {
        int status;
        waitpid(z->pids[i], &status, 0);
        
    }
}

// Struct redirect

typedef struct redirect redirect;
struct redirect {
    int direction;
    int redirect_fd;
    int new_fd;
    char* filename;
};

// struct command
//    Data structure describing a command. Add your own stuff.

typedef struct command command;
struct command {
    int argc;      // number of arguments
    char** argv;   // arguments, terminated by NULL
    pid_t pid;     // process ID running this command, -1 if none
    int status;
    int wait;
    int fd_in;
    int fd_out;
    int rd_count;
    redirect* rd_in;
    redirect* rd_out;
    redirect* rd_err;
    int cd;
};

typedef struct pipeline pipeline;
struct pipeline {
    command* command;
    pipeline* pipe_in;
    pipeline* pipe_out;
    int pipefd[2];
};

typedef struct conditional conditional;
struct conditional {
    int cond_argc;
    pipeline** cond_argv;
    int continue_if;
};

typedef struct command_list command_list;
struct command_list {
    int cargc;
    conditional** cargv;
    int background;  
};

typedef struct list_group list_group;
struct list_group {
    int largc;
    command_list** largv;
};

// redirect struct alloc

static redirect* rd_alloc(void) {
    redirect* rd = (redirect*) malloc(sizeof(redirect));
    rd->direction = 0;
    rd->redirect_fd = -1;
    rd->new_fd = -1;
    rd->filename = NULL;
    
    return rd;
}

static void handle_rd (command* c) {
    if (c->rd_in) {
        c->rd_in->new_fd = open(c->rd_in->filename, O_RDONLY);
        if (c->rd_in->new_fd == -1) {
            perror(strerror(errno));
            exit(1);
        }   
        dup2(c->rd_in->new_fd, c->rd_in->redirect_fd);
        close(c->rd_in->new_fd);
    }
    if (c->rd_out) {
        c->rd_out->new_fd = open(c->rd_out->filename, O_WRONLY | O_CREAT | O_TRUNC, 00777);
        if (c->rd_out->new_fd == -1) {
            perror(strerror(errno));
            exit(1);
        }   
        dup2(c->rd_out->new_fd, c->rd_out->redirect_fd);
        close(c->rd_out->new_fd);
    }
    if (c->rd_err) {
        c->rd_err->new_fd = open(c->rd_err->filename, O_WRONLY | O_CREAT | O_TRUNC, 00777);
        if (c->rd_err->new_fd == -1) {
            perror(strerror(errno));
            exit(1);
        }   
        dup2(c->rd_err->new_fd, c->rd_err->redirect_fd);
        close(c->rd_err->new_fd);
    }       
}

static void redirect_free(command* c) {
    if (c->rd_in) {
        free(c->rd_in);
    }
    if (c->rd_out) {
        free(c->rd_out);
    }
    if (c->rd_err) {
        free(c->rd_err);
    }
}

// command_alloc()
//    Allocate and return a new command structure.

static command* command_alloc(void) {
    command* c = (command*) malloc(sizeof(command));
    c->argc = 0;
    c->argv = NULL;
    c->pid = -1;
    c->status = 0;
    c->wait = 1;
    c->fd_in = -1;
    c->fd_out = -1;
    c->rd_count = 0;
    c->rd_in = NULL;
    c->rd_out = NULL;
    c->rd_err = NULL;
    c->cd = 0;

    return c;
}

// command_free(c)
//    Free command structure `c`, including all its words.

static void command_free(command* c) {
    for (int i = 0; i < c->argc; ++i)
        free(c->argv[i]);
    redirect_free(c);
    free(c->argv);
    free(c);
}

// command_append_arg(c, word)
//    Add `word` as an argument to command `c`. This increments `c->argc`
//    and augments `c->argv`.

static void command_append_arg(command* c, char* word) {
    c->argv = (char**) realloc(c->argv, sizeof(char*) * (c->argc + 2));
    c->argv[c->argc] = word;
    c->argv[c->argc + 1] = NULL;
    ++c->argc;
}

static pipeline* pipeline_alloc(void) {
    pipeline* pipe = (pipeline*) malloc(sizeof(pipeline));
    pipe->command = NULL;
    pipe->pipe_in = NULL;
    pipe->pipe_out = NULL;
    pipe->pipefd[0] = -1;
    pipe->pipefd[1] = -1;
    return pipe;
}

static void pipeline_free(pipeline* pipe) {
    command_free(pipe->command);
    free(pipe);
}

static void set_pipe_command(pipeline* pipe, command* c) {
    pipe->command = c;
}

static conditional* conditional_alloc(void) {
    conditional* cond = (conditional*) malloc(sizeof(conditional));
    cond->cond_argc = 0;
    cond->cond_argv = NULL;
    cond->continue_if = 0;
    
    return cond;
}

static void conditional_free(conditional* cond) {
    for (int i = 0; i < cond->cond_argc; ++i)
        pipeline_free(cond->cond_argv[i]);
    free(cond->cond_argv);
    free(cond);
}

static void conditional_append_arg(conditional* cond, pipeline* pipe) {
    cond->cond_argv = (pipeline**) realloc(cond->cond_argv, sizeof(pipeline*) * (cond->cond_argc + 2));
    cond->cond_argv[cond->cond_argc] = pipe;
    cond->cond_argv[cond->cond_argc + 1] = NULL;
    ++cond->cond_argc;
}

static command_list* command_list_alloc(void) {
    command_list* list = (command_list*) malloc(sizeof(command_list));
    list->cargc = 0;
    list->cargv = NULL;
    list->background = 0;
    return list;
}

static void command_list_append_arg (command_list* list, conditional* cond) {
    list->cargv = (conditional**) realloc(list->cargv, sizeof(conditional*) * (list->cargc + 2));
    list->cargv[list->cargc] = cond;
    list->cargv[list->cargc + 1] = NULL;
    ++list->cargc;
}

static void command_list_free(command_list* list) {
    for (int i = 0 ; i != list->cargc; ++i) {
        conditional_free(list->cargv[i]);
    }
    free(list->cargv);
    free(list);
}

static list_group* list_group_alloc(void) {
    list_group* group = (list_group*) malloc(sizeof(list_group));
    group->largc = 0;
    group->largv = NULL;
    return group;
}

static void list_group_append_lst (list_group* group, command_list* list) {
    group->largv = (command_list**) realloc(group->largv, sizeof(command_list*) * (group->largc + 2));
    group->largv[group->largc] = list;
    group->largv[group->largc + 1] = NULL;
    ++group->largc;
}

static void list_group_free(list_group* group) {
    for (int i = 0; i != group->largc; ++i) {
        command_list_free(group->largv[i]);
    }
    free(group->largv);
    free(group);
}

// GLOBAL VARIABLES for handling zombies and interrupts

zombies z;

int interrupted = 0;
int cmd_running = 0;
int clear_cmd = 0;

void interrupt_handler(int signum) {
    (void) signum;
    if (cmd_running) {
        interrupted = 1;
    } else {
        clear_cmd = 1;
    }
}


// COMMAND EVALUATION

// start_command(c, pgid)
//    Start the single command indicated by `c`. Sets `c->pid` to the child
//    process running the command, and returns `c->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: The child process should be in the process group `pgid`, or
//       its own process group (if `pgid == 0`). To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t start_command(command* c, pid_t pgid) {
    pid_t pid = -1;
    const char *file;
    char **argv;
    
    if(c->cd) {
        setpgid(0, pgid);
        
        // save the original stdout and stderr in the case of
        // cd fail
        int saved_stdout;
        int saved_stderr;
        saved_stdout = dup(STDOUT_FILENO);
        saved_stderr = dup(STDERR_FILENO);
        
        if (c->rd_count != 0) {
            handle_rd(c);
        }
        c->status = chdir(c->argv[1]);
        if (c->status == -1) {
            c->status = 1;
            perror(strerror(errno));
        }
        
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
        
        c->pid = getpid();
        pid = c->pid;
        return pid;
    }
    
    pid = fork();
    
    if (pid != 0) {
        c->pid = getpid();
        setpgid(c->pid, pgid);
        return pid;
    } else {
        
        setpgid(pid, pgid);
        z.count++;
        z.pids = (int*) realloc(z.pids, sizeof(int) * z.count);
        z.pids[z.count - 1] = pid;
        
        // Redirect overrides pipe
        if (c->rd_count != 0) {
            handle_rd(c);
        }
        if (c->fd_in != -1 && c->rd_in == NULL) {
           dup2(c->fd_in, STDIN_FILENO);
           close(c->fd_in);
        }
        if (c->fd_out != -1 && c->rd_out == NULL) {
            dup2(c->fd_out, STDOUT_FILENO);
            close(c->fd_out);
        }
        
        file = (const char*) c->argv[0];
        argv = &(c->argv[0]);
        c->pid = pid;
        execvp(file, argv);
    }
    
    return pid;
}


// run_list(c)
//    Run the command list starting at `c`.
//
//    PART 1: Start the single command `c` with `start_command`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in run_list (or in helper functions!).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Choose a process group for each pipeline.
//       - Call `set_foreground(pgid)` before waiting for the pipeline.
//       - Call `set_foreground(0)` once the pipeline is complete.
//       - Cancel the list when you detect interruption.

void run_list(command_list* list) {
    pid_t pid = -1;
    pid_t pgid = 1;
    int status = 0;
    
    conditional* cond;
    pipeline* pipeline;
    command* c;
    
    for (int i = 0; i < list->cargc; i++) {
        status = 0;
        cond = list->cargv[i];
        set_foreground(pgid);
        for (int j = 0; j < cond->cond_argc; j++) {
            pipeline = cond->cond_argv[j];
            c = pipeline->command;            
            
            // Setup pipeline fd's and pointers if we have no redirect
            if (pipeline->pipe_out != NULL && c->rd_out == NULL) {
                pipe(pipeline->pipefd);
                c->fd_out = pipeline->pipefd[1];
            }
            
            if (pipeline->pipe_in != NULL && c->rd_in == NULL) {
                c->fd_in = pipeline->pipe_in->pipefd[0];
            }
            
            // If the command was interrupted then do not execute
            if (!interrupted) {
                pid = start_command(c, pgid);
            }
            
            // Wait for child process
            if (pid != 0 && c->wait) {
                waitpid(pid, &(c->status), 0);

            }
            
            // Close any file descriptors left open
            if (c->rd_count != 0) {
                if (c->rd_in && c->rd_in->new_fd != -1) {
                    close(c->rd_in->new_fd);
                }
                if (c->rd_out && c->rd_out->new_fd != -1) {
                    close(c->rd_out->new_fd);
                }
                if (c->rd_err && c->rd_err->new_fd != -1) {
                    close(c->rd_err->new_fd);
                }
            }
            
            if (c->fd_in != -1 && c->rd_in == NULL) {
                close(c->fd_in);
            }
            if (c->fd_out != -1 && c->rd_out == NULL) {
                close(c->fd_out);
            }  

            if(WIFEXITED(c->status) && c->cd == 0) {
                status =  WEXITSTATUS(c->status);
            } else if (WIFEXITED(c->status) != 0 && WTERMSIG(c->status) == SIGINT) {
                interrupted = 1;
            }else if (c->cd) {
                status = c->status;
            }
            
                
        }
        
        set_foreground(0);
        pgid++;
        // Here is where we skip the next command if the condition was not correct
        if ((status == 0) && (cond->continue_if != 0)) {
            i = i + 1;
        }
        if ((status != 0) && (cond->continue_if == 0)) {
            i = i + 1;
        }   
    }
}


// eval_line(c)
//    Parse the command list in `s` and run it via `run_list`.

void eval_line(const char* s) {
    int type;
    char* token;

    //z = (zombies*) maalloc(sizeof(zombies));
    z.count = 0;
    z.pids = NULL;

    // build the command
    list_group* group = list_group_alloc();    
    command_list* list = command_list_alloc();
    conditional* cond = conditional_alloc(); 
    pipeline* pipe = pipeline_alloc();   
    command* c = command_alloc();
    redirect* rd;
    
    list_group_append_lst(group, list);    
    command_list_append_arg(list, cond);
    conditional_append_arg(cond, pipe);
    set_pipe_command(pipe, c);
    
    int new_pipe = 0;
    int new_cond = 0;
    int new_list = 0;
    
    while ((s = parse_shell_token(s, &type, &token)) != NULL) {
        
        if(new_list) {
            list = command_list_alloc();
            cond = conditional_alloc();
            pipe = pipeline_alloc();
            c = command_alloc();
            
            list_group_append_lst(group, list);          
            command_list_append_arg(list, cond);
            conditional_append_arg(cond, pipe);
            set_pipe_command(pipe, c);
            
            new_list = 0;
        }
        
        if(new_cond) {            
            cond = conditional_alloc();
            pipe = pipeline_alloc();
            c = command_alloc();
            
            command_list_append_arg(list, cond);
            conditional_append_arg(cond, pipe);
            set_pipe_command(pipe, c);
            
            new_cond = 0;
        }
        
        if(new_pipe) {
            pipeline* old_pipe = pipe;
            
            pipe = pipeline_alloc();
            c = command_alloc();
            
            conditional_append_arg(cond, pipe);
            set_pipe_command(pipe, c);
            
            old_pipe->pipe_out = pipe;
            pipe->pipe_in = old_pipe;  
            
            new_pipe = 0;          
        }
        
        if (type == TOKEN_REDIRECTION) {
            rd = rd_alloc();
            if (strcmp("<", token) == 0) {
                rd->direction = -1;
                rd->redirect_fd = STDIN_FILENO;
                c->rd_in = rd;               
            } else if (strcmp(">", token) == 0) {
                rd->direction = 1;
                rd->redirect_fd = STDOUT_FILENO;
                c->rd_out = rd;
            } else if (strcmp("2>", token) == 0) {
                rd->direction = 1;
                rd->redirect_fd = STDERR_FILENO;
                c->rd_err = rd;
            }
            c->rd_count++;
            s = parse_shell_token(s, &type, &rd->filename);
            continue;
        }
        
        if (type == TOKEN_BACKGROUND) {
            list->background = 1;
            new_list = 1;
            continue;
        }    
        
        if (type == TOKEN_SEQUENCE) {           
            new_list = 1;
            continue;
        } 
        
        if (type == TOKEN_AND) {
            cond->continue_if = 0;
            new_cond = 1;
            continue;
        }   
        
        if (type == TOKEN_OR) {
            cond->continue_if = 1;
            new_cond = 1;
            continue;
        }
        
        if (type == TOKEN_PIPE) {
            new_pipe = 1;
            continue;
        }
        
        if (strcmp(token, "cd") == 0) {
            c->cd = 1;
        }
    
        command_append_arg(c, token);
    }
    // execute the different lists of commands
    for(int i = 0; i < group->largc; i++) {        
        
        if(interrupted) {
            return;
        }
        
        int stop_process = 0;
        
        command_list* list = group->largv[i];
        
        if (list->background) {
            pid_t pid = fork();
            if (pid == 0) {
                // stop child process after this list
                stop_process = 1;
            } else {
                // skip this process as parent while child
                // runs it in the background
                continue;
            }
        }
        cmd_running = 1;
        run_list(list);
        
        if (stop_process) {
            // break after running this list as child
            break;
        }
    }
    cmd_running = 0;
    list_group_free(group);
}


int main(int argc, char* argv[]) {
    FILE* command_file = stdin;
    int quiet = 0;

    // Check for '-q' option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = 1;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            exit(1);
        }
    }

    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back
    //   into the foreground
    set_foreground(0);
    handle_signal(SIGTTOU, SIG_IGN);
    
    // Handler for interrupt signal
    handle_signal(SIGINT, interrupt_handler);

    char buf[BUFSIZ];
    int bufpos = 0;
    int needprompt = 1;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("sh61[%d]$ ", getpid());
            fflush(stdout);
            needprompt = 0;
        }
        
        cmd_running = 0;
        
        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == NULL) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file))
                    perror("sh61");
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            eval_line(buf);
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
        if (clear_cmd) {
            printf("\nsh61[%d]$ ", getpid());
            fflush(stdout);
            clear_cmd = 0;
            needprompt = 0;
        }
        
        if (interrupted) 
            interrupted = 0;
        
        reap_zombies(&z);
        
    }
    free(z.pids);
    
    return 0;
}
