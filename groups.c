#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <limits.h>  // For INT_MAX

#define MAX_MSG_SIZE 256
#define MAX_USERS 50
#define MAX_TEXT_SIZE 256
#define MAX_PATH_SIZE 256

// Message structure for communication
typedef struct {
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_TEXT_SIZE];
    int modifyingGroup;
} Message;

// Message structure for storing user messages
typedef struct {
    int timestamp;
    int user_id;
    char message[MAX_TEXT_SIZE];
} UserMessage;

// Structure to store user data
typedef struct {
    int user_id;             // Actual user ID (Y) from user_X_Y.txt
    char user_file[MAX_PATH_SIZE]; // File path for the user file
    int pipe_fd[2];          // Pipe for communication with the user
    int is_banned;           // 1 if user is banned, 0 otherwise
} UserData;

void close_unused_pipe_ends(int pipefd[2], int to_close) {
    close(pipefd[to_close]); // to_close = 0 (close read), to_close = 1 (close write)
}

// Send a message to validation.out
void send_message_to_validation(int msgid, int mtype, int group_id, int user, int timestamp, const char *text) {
    Message msg;
    msg.mtype = mtype;
    msg.modifyingGroup = group_id;
    msg.user = user;
    msg.timestamp = timestamp;
    strncpy(msg.mtext, text, MAX_TEXT_SIZE);

    if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(msg.mtype), 0) == -1) {
        perror("Error sending message to validation");
    }
}

// Send a message to moderator.c
void send_message_to_moderator(int mod_msgid, int group_id, int user, const char *text) {
    Message mod_msg;
    mod_msg.mtype = 1;  // Can be changed to appropriate type if needed
    mod_msg.modifyingGroup = group_id;
    mod_msg.user = user;
    strncpy(mod_msg.mtext, text, MAX_TEXT_SIZE);

    if (msgsnd(mod_msgid, &mod_msg, sizeof(mod_msg) - sizeof(mod_msg.mtype), 0) == -1) {
        perror("Error sending message to moderator");
    }
}

// Check if the moderator has banned a user
int check_for_user_ban(int group_id, int user_id, int mod_msgid) {
    Message mod_msg;

    // Try to receive a message from the moderator (non-blocking)
    ssize_t msg_size = msgrcv(mod_msgid, &mod_msg, sizeof(mod_msg) - sizeof(mod_msg.mtype), 1, IPC_NOWAIT);
    if (msg_size == -1) {
        // No message from the moderator, assume user is not banned
        return 0;
    }

    // Check if the moderator is requesting the removal of this user
    if (mod_msg.modifyingGroup == group_id && mod_msg.user == user_id) {
        return 1;  // User should be banned
    }

    return 0;  // User should not be banned
}

// Handle the user process: read user_X_Y.txt and send messages via the pipe
void handle_user_process(int group_id, int user_id, const char *user_file, int write_pipe, int testcase) {
    char inputFile[MAX_PATH_SIZE];
    snprintf(inputFile, sizeof(inputFile), "testcase_%d/%s", testcase, user_file);
    FILE *file = fopen(inputFile, "r");
    if (!file) {
        perror("Error opening user file");
        exit(EXIT_FAILURE);
    }

    char buffer[MAX_TEXT_SIZE];
    while (fgets(buffer, sizeof(buffer), file)) {
        int timestamp;
        char message[MAX_TEXT_SIZE];

        // Extract timestamp and message from the user file
        sscanf(buffer, "%d %[^\n]", &timestamp, message);  // Read timestamp and message

        // Write the timestamp and message to the pipe
        write(write_pipe, &timestamp, sizeof(timestamp));
        write(write_pipe, message, strlen(message) + 1);  // Send message to the group via pipe
    }

    fclose(file);
    close(write_pipe);  // Close the write end after sending all messages
    exit(0);  // Terminate user process
}

// Initialize the group by reading the group file and creating pipes for each user
void initialize_group(int group_id, const char *group_file, UserData users[], int *num_users, int testcase) {
    char inputFile[MAX_PATH_SIZE];
    snprintf(inputFile, sizeof(inputFile), "testcase_%d/%s", testcase, group_file);  // Construct the path to input.txt

    FILE *file = fopen(inputFile, "r");

    if (!file) {
        perror("Error opening group file");
        exit(EXIT_FAILURE);
    }

    fscanf(file, "%d", num_users);  // Read number of users

    for (int i = 0; i < *num_users; i++) {
        char user_file[MAX_PATH_SIZE];
        fscanf(file, "%s", user_file);  // Read the user file path

        // Extract user ID (Y) from user_X_Y.txt
        char *underscore = strrchr(user_file, '_');
        users[i].user_id = atoi(underscore + 1);  // Get the user ID (Y)

        // Store the user file path
        strncpy(users[i].user_file, user_file, MAX_PATH_SIZE);

        // Initialize pipes
        if (pipe(users[i].pipe_fd) == -1) {
            perror("Error creating pipe");
            exit(EXIT_FAILURE);
        }

        users[i].is_banned = 0;  // Initialize as not banned
    }

    fclose(file);
}

// Comparison function for qsort() to sort messages by timestamp
int compare_timestamps(const void *a, const void *b) {
    UserMessage *msgA = (UserMessage *)a;
    UserMessage *msgB = (UserMessage *)b;
    return msgA->timestamp - msgB->timestamp;
}

// Handle the group process: manage communication with users, moderator, and validation
void handle_group_process(int group_id, int num_users, UserData users[], int val_msgid, int mod_msgid) {
    UserMessage user_messages[1000];  // Store up to 1000 messages
    int message_count = 0;            // Track the number of messages

    int active_users = num_users;

    while (active_users > 0) {
        for (int i = 0; i < num_users; i++) {
            if (users[i].is_banned) {
                continue;  // Skip banned users
            }

            int timestamp;
            char message[MAX_TEXT_SIZE];
            // Read from each user's pipe
            ssize_t bytes_read = read(users[i].pipe_fd[0], &timestamp, sizeof(timestamp));  // Read timestamp
            if (bytes_read > 0) {
                read(users[i].pipe_fd[0], message, sizeof(message));  // Read the message

                // Store the message with timestamp and actual user_id
                user_messages[message_count].timestamp = timestamp;
                user_messages[message_count].user_id = users[i].user_id;  // Use the actual user ID (Y)
                strncpy(user_messages[message_count].message, message, MAX_TEXT_SIZE);
                message_count++;

                // Send message to validation and moderator
                send_message_to_validation(val_msgid, 30 + group_id, group_id, users[i].user_id, timestamp, message);
                send_message_to_moderator(mod_msgid, group_id, users[i].user_id, message);

                // Check if the moderator has banned the user
                if (check_for_user_ban(group_id, users[i].user_id, mod_msgid)) {
                    printf("User %d from group %d has been removed.\n", users[i].user_id, group_id);
                    close(users[i].pipe_fd[0]);  // Close the user's pipe
                    users[i].is_banned = 1;      // Mark the user as banned
                    active_users--;
                }
            } else if (bytes_read == 0) {
                // User has sent all messages, close the pipe
                close(users[i].pipe_fd[0]);
                active_users--;
            }
        }

        if (active_users < 2) {
            // Less than 2 active users, terminate group
            send_message_to_validation(val_msgid, 3, group_id, 0, 0, "");  // mtype = 3, indicates group termination
            break;
        }
    }

    // Sort messages by timestamp before sending them in the correct order
    qsort(user_messages, message_count, sizeof(UserMessage), compare_timestamps);

    // Send sorted messages to validation and moderator (already done above)
}

// Main function for group process
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
    int threshold = atoi(argv[6]);
    int testcase = atoi(argv[7]);

    // Create message queues for communication with validation and moderator
    int val_msgid = msgget(val_key, 0666 | IPC_CREAT);
    int mod_msgid = msgget(mod_key, 0666 | IPC_CREAT);

    UserData users[MAX_USERS];
    int num_users = 0;

    // Initialize group and create pipes for each user
    initialize_group(group_id, group_file, users, &num_users, testcase);
    send_message_to_validation(val_msgid, 1, group_id, 0, 0, "");  // mtype = 1, indicates group creation

    // Create user processes
    for (int i = 0; i < num_users; i++) {
        if (fork() == 0) {
            // Close unused pipe ends for the user process
            close_unused_pipe_ends(users[i].pipe_fd, 0);  // Close read end
            handle_user_process(group_id, users[i].user_id, users[i].user_file, users[i].pipe_fd[1], testcase);  // Pass the user file path
        } else {
            // Parent process closes the write end of the pipe
            close_unused_pipe_ends(users[i].pipe_fd, 1);  // Close write end
            send_message_to_validation(val_msgid, 2, group_id, users[i].user_id, 0, "");  // mtype = 2 indicates user creation

        }
    }

    // Handle group process (manage communication between users, validation, and moderator)
    handle_group_process(group_id, num_users, users, val_msgid, mod_msgid);

    return 0;
}
