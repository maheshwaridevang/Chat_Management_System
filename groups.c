#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>

#define MAX_MSG_SIZE 256
#define MAX_USERS 50
#define MAX_TEXT_SIZE 256
#define MAX_PATH_SIZE 256

// Message structure for communication with validation and moderator
typedef struct {
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_TEXT_SIZE];
    int modifyingGroup;
} Message;

// Structure for messages sent over pipes (timestamp and text together)
typedef struct {
    int timestamp;
    char message[MAX_TEXT_SIZE];
} PipeMessage;

// Structure to store a received message (for sorting)
typedef struct {
    int timestamp;
    int user_id;
    char message[MAX_TEXT_SIZE];
} UserStoredMessage;

// Structure to store per-user data for a group
typedef struct {
    int user_id;                   // Extracted from user_X_Y.txt
    char user_file[MAX_PATH_SIZE]; // e.g., "users/user_1_0.txt"
    int pipe_fd[2];                // Pipe for communication with the user process
    int active;                    // 1 if still active, 0 if finished sending
} UserData;

// Sends a message to validation.out.
// If msgsnd fails due to the queue being removed (EINVAL or EIDRM), ignore the error.
void send_message_to_validation(int msgid, int mtype, int group_id, int user, int timestamp, const char *text) {
    Message msg;
    msg.mtype = mtype;
    msg.modifyingGroup = group_id;
    msg.user = user;
    msg.timestamp = timestamp;
    strncpy(msg.mtext, text, MAX_TEXT_SIZE);
    
    if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(msg.mtype), 0) == -1) {
        if (errno != EINVAL && errno != EIDRM) {
            perror("Error sending message to validation");
        }
    }
}

// Sends a message to moderator.out.
// If msgsnd fails due to the queue being removed, ignore the error.
void send_message_to_moderator(int mod_msgid, int group_id, int user, const char *text) {
    Message mod_msg;
    mod_msg.mtype = 1;  // Fixed mtype for moderator messages
    mod_msg.modifyingGroup = group_id;
    mod_msg.user = user;
    strncpy(mod_msg.mtext, text, MAX_TEXT_SIZE);
    
    if (msgsnd(mod_msgid, &mod_msg, sizeof(mod_msg) - sizeof(mod_msg.mtype), 0) == -1) {
        if (errno != EINVAL && errno != EIDRM) {
            perror("Error sending message to moderator");
        }
    }
}

// User process: reads its file and writes each message as one PipeMessage record.
void handle_user_process(int group_id, int user_id, const char *user_file, int write_pipe, int testcase) {
    char inputFile[MAX_PATH_SIZE];
    snprintf(inputFile, sizeof(inputFile), "testcase_%d/%s", testcase, user_file);
    FILE *file = fopen(inputFile, "r");
    if (!file) {
        perror("Error opening user file");
        exit(EXIT_FAILURE);
    }
    
    PipeMessage pm;
    char buffer[MAX_TEXT_SIZE];
    while (fgets(buffer, sizeof(buffer), file)) {
        sscanf(buffer, "%d %[^\n]", &pm.timestamp, pm.message);
        if (write(write_pipe, &pm, sizeof(PipeMessage)) != sizeof(PipeMessage)) {
            perror("Error writing to pipe");
        }
        sleep(1);  // Simulate message delay
    }
    
    fclose(file);
    close(write_pipe);
    exit(0);
}

// Initialize the group by reading the group file and setting up a pipe for each user.
void initialize_group(int group_id, const char *group_file, UserData users[], int *num_users, int testcase) {
    char inputFile[MAX_PATH_SIZE];
    snprintf(inputFile, sizeof(inputFile), "testcase_%d/%s", testcase, group_file);
    
    FILE *file = fopen(inputFile, "r");
    if (!file) {
        perror("Error opening group file");
        exit(EXIT_FAILURE);
    }
    
    fscanf(file, "%d", num_users);
    for (int i = 0; i < *num_users; i++) {
        char user_file[MAX_PATH_SIZE];
        fscanf(file, "%s", user_file);
        // Extract user_id from filename (assumes format user_X_Y.txt)
        char *underscore = strrchr(user_file, '_');
        users[i].user_id = atoi(underscore + 1);
        strncpy(users[i].user_file, user_file, MAX_PATH_SIZE);
        if (pipe(users[i].pipe_fd) == -1) {
            perror("Error creating pipe");
            exit(EXIT_FAILURE);
        }
        // Set the read end to non-blocking
        int flags = fcntl(users[i].pipe_fd[0], F_GETFL, 0);
        fcntl(users[i].pipe_fd[0], F_SETFL, flags | O_NONBLOCK);
        users[i].active = 1;
    }
    fclose(file);
}

// Comparison function for qsort: sort by timestamp; if equal, by user_id.
int compare_timestamps(const void *a, const void *b) {
    UserStoredMessage *msgA = (UserStoredMessage *)a;
    UserStoredMessage *msgB = (UserStoredMessage *)b;
    if (msgA->timestamp == msgB->timestamp) {
        return msgA->user_id - msgB->user_id;
    }
    return msgA->timestamp - msgB->timestamp;
}

// Handle the group process: repeatedly poll each user's pipe in non-blocking mode.
// As soon as the active user count falls below 2, terminate the group immediately.
void handle_group_process(int group_id, int num_users, UserData users[], int val_msgid, int mod_msgid) {
    UserStoredMessage user_messages[1000];
    int message_count = 0;
    int active_users = num_users;
    int done = 0;
    
    // Poll until active user count drops below 2.
    while (active_users >= 2 && !done) {
        int progress = 0;
        for (int i = 0; i < num_users; i++) {
            if (!users[i].active)
                continue;
            PipeMessage pm;
            ssize_t bytes_read = read(users[i].pipe_fd[0], &pm, sizeof(PipeMessage));
            if (bytes_read == sizeof(PipeMessage)) {
                // Forward to moderator immediately.
                send_message_to_moderator(mod_msgid, group_id, users[i].user_id, pm.message);
                // Buffer for sorting.
                user_messages[message_count].timestamp = pm.timestamp;
                user_messages[message_count].user_id = users[i].user_id;
                strncpy(user_messages[message_count].message, pm.message, MAX_TEXT_SIZE);
                message_count++;
                progress = 1;
            } else if (bytes_read == 0) {
                // EOF: mark user as finished.
                if (users[i].active) {
                    printf("User %d removed from group %d after completing its messages.\n", users[i].user_id, group_id);
                    users[i].active = 0;
                    active_users--;
                    if (active_users < 2) {
                        done = 1;
                        break;
                    }
                }
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("Error reading from pipe");
                }
            }
        }
        if (!progress && !done) {
            usleep(100000); // Sleep 100ms if no progress.
        }
        // Check if all users are finished.
        int all_finished = 1;
        for (int i = 0; i < num_users; i++) {
            if (users[i].active) {
                all_finished = 0;
                break;
            }
        }
        if (all_finished)
            done = 1;
    }
    
    // If active users dropped below 2, terminate immediately.
    if (active_users < 2) {
        printf("Active users in group %d dropped below 2. Terminating group.\n", group_id);
        send_message_to_validation(val_msgid, 3, group_id, 0, 0, "");
        return;
    }
    
    // Otherwise, drain remaining messages from active users.
    for (int i = 0; i < num_users; i++) {
        while (users[i].active) {
            PipeMessage pm;
            ssize_t bytes_read = read(users[i].pipe_fd[0], &pm, sizeof(PipeMessage));
            if (bytes_read == sizeof(PipeMessage)) {
                send_message_to_moderator(mod_msgid, group_id, users[i].user_id, pm.message);
                user_messages[message_count].timestamp = pm.timestamp;
                user_messages[message_count].user_id = users[i].user_id;
                strncpy(user_messages[message_count].message, pm.message, MAX_TEXT_SIZE);
                message_count++;
            } else {
                break;
            }
        }
        close(users[i].pipe_fd[0]);
    }
    
    // Sort the buffered messages.
    qsort(user_messages, message_count, sizeof(UserStoredMessage), compare_timestamps);
    
    // Debug print: display sorted messages.
    printf("Final sorted messages for group %d:\n", group_id);
    for (int i = 0; i < message_count; i++) {
        printf("User: %d, Timestamp: %d, Message: %s\n",
               user_messages[i].user_id,
               user_messages[i].timestamp,
               user_messages[i].message);
    }
    
    // Forward the sorted messages to validation.
    for (int i = 0; i < message_count; i++) {
        send_message_to_validation(val_msgid, 30 + group_id, group_id,
                                   user_messages[i].user_id,
                                   user_messages[i].timestamp,
                                   user_messages[i].message);
    }
    
    // Send termination message.
    send_message_to_validation(val_msgid, 3, group_id, 0, 0, "");
}

// Main function for the group process.
int main(int argc, char *argv[]) {
    if (argc != 8) {
        fprintf(stderr, "Usage: %s <group_file> <group_id> <app_key> <mod_key> <val_key> <threshold> <testcase>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    char *group_file = argv[1];
    int group_id = atoi(argv[2]);
    int app_key = atoi(argv[3]);
    int mod_key = atoi(argv[4]);
    int val_key = atoi(argv[5]);
    int threshold = atoi(argv[6]);  // Not used here; removals due to violations handled by moderator.
    int testcase = atoi(argv[7]);
    
    int val_msgid = msgget(val_key, 0666 | IPC_CREAT);
    int mod_msgid = msgget(mod_key, 0666 | IPC_CREAT);
    
    UserData users[MAX_USERS];
    int num_users = 0;
    
    initialize_group(group_id, group_file, users, &num_users, testcase);
    send_message_to_validation(val_msgid, 1, group_id, 0, 0, "");
    
    for (int i = 0; i < num_users; i++) {
        if (fork() == 0) { // Child: user process.
            close(users[i].pipe_fd[0]);
            handle_user_process(group_id, users[i].user_id, users[i].user_file, users[i].pipe_fd[1], testcase);
        } else {
            close(users[i].pipe_fd[1]);
            send_message_to_validation(val_msgid, 2, group_id, users[i].user_id, 0, "");
        }
    }
    
    handle_group_process(group_id, num_users, users, val_msgid, mod_msgid);
    
    return 0;
}
