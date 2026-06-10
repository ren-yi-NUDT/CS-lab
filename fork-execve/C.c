#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;

int main(){
    pid_t pid_a, pid_b;

    pid_a = fork();
    if (pid_a == 0){
        char *args[] = {"./A", NULL};
        execve("./A", args, environ);
        return 1;
    }
    else if (pid_a < 0){
        perror("fork A failed");
        return 1;
    }

    pid_b = fork();
    if (pid_b == 0){
        char *args[] = {"./B", NULL};
        execve("./B", args, environ);
        return 1;
    }
    else if (pid_b < 0){
        perror("fork B failed");
        return 1;
    }

    waitpid(pid_a, NULL, 0);
    waitpid(pid_b, NULL, 0);
    return 0;
}