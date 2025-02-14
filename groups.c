#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <fcntl.h>

#define MAX_USERS 50
#define MAX_MSG_SIZE 256

typedef struct {
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_MSG_SIZE];
    int modifyingGroup;
} Message;

void process_user_messages(const char *userFile, int userID, int write_pipe) {
    FILE *file = fopen(userFile, "r");
    if (!file) {
        perror("Error opening user file");
        exit(EXIT_FAILURE);
    }

    char buffer[MAX_MSG_SIZE];
    while (fgets(buffer, MAX_MSG_SIZE, file)) {
        write(write_pipe, buffer, strlen(buffer) + 1);
        sleep(1);  // Simulating message delay
    }
    fclose(file);
    close(write_pipe);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <group_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Error opening group file");
        exit(EXIT_FAILURE);
    }

    int num_users;
    fscanf(file, "%d", &num_users);
    char userFiles[num_users][256];

    for (int i = 0; i < num_users; i++) {
        fscanf(file, "%s", userFiles[i]);
    }
    fclose(file);

    int pipes[num_users][2];
    for (int i = 0; i < num_users; i++) {
        pipe(pipes[i]);
        if (fork() == 0) {
            close(pipes[i][0]);
            process_user_messages(userFiles[i], i, pipes[i][1]);
            exit(0);
        }
        close(pipes[i][1]);
    }

    // Read messages from pipes
    for (int i = 0; i < num_users; i++) {
        char buffer[MAX_MSG_SIZE];
        while (read(pipes[i][0], buffer, MAX_MSG_SIZE) > 0) {
            printf("Group received: %s\n", buffer);
        }
        close(pipes[i][0]);
    }

    return 0;
}
