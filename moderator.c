#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_USERS 50
#define MAX_MSG_SIZE 256
#define MAX_WORDS 50

// Message structure for communication
typedef struct {
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_MSG_SIZE];
    int modifyingGroup;
} Message;

char filtered_words[MAX_WORDS][MAX_MSG_SIZE];
int num_filtered = 0;
int violations[MAX_USERS][MAX_USERS] = {0}; // Tracks violations per user per group

// Function to load filtered words from "filtered_words.txt"
void load_filtered_words(int testcase) {
    char filePath[256];
    // Corrected: open the filtered_words.txt file from the test case directory.
    snprintf(filePath, sizeof(filePath), "testcase_%d/filtered_words.txt", testcase);
    
    FILE *file = fopen(filePath, "r");
    if (!file) {
        perror("Error opening filtered words file");
        exit(EXIT_FAILURE);
    }
    
    while (fgets(filtered_words[num_filtered], sizeof(filtered_words[num_filtered]), file)) {
        strtok(filtered_words[num_filtered], "\n"); // Remove newline characters
        num_filtered++;
        if (num_filtered >= MAX_WORDS) break;
    }
    fclose(file);
}

// Function to convert string to lowercase (for case-insensitive matching)
void to_lowercase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower(str[i]);
    }
}

// Function to count violations in a message
int count_violations(const char *message) {
    int violation_count = 0;
    char lowered_message[MAX_MSG_SIZE];
    strncpy(lowered_message, message, MAX_MSG_SIZE);
    lowered_message[MAX_MSG_SIZE - 1] = '\0'; // Ensure null termination
    to_lowercase(lowered_message);

    // Check for each filtered word (case-insensitive)
    for (int i = 0; i < num_filtered; i++) {
        char lowered_word[MAX_MSG_SIZE];
        strncpy(lowered_word, filtered_words[i], MAX_MSG_SIZE);
        lowered_word[MAX_MSG_SIZE - 1] = '\0';
        to_lowercase(lowered_word);

        if (strstr(lowered_message, lowered_word) != NULL) {
            violation_count++;
        }
    }
    return violation_count;
}

// Function to read moderator key and threshold from "input.txt"
void read_input_file(int testcase, int *mod_key, int *threshold) {
    char filePath[256];
    snprintf(filePath, sizeof(filePath), "testcase_%d/input.txt", testcase);  // Path to input.txt

    FILE *file = fopen(filePath, "r");
    if (!file) {
        perror("Error opening input.txt");
        exit(EXIT_FAILURE);
    }

    int num_groups, val_key, app_key;
    fscanf(file, "%d %d %d %d %d", &num_groups, &val_key, &app_key, mod_key, threshold);

    fclose(file);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <testcase_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int testcase = atoi(argv[1]);  // Get the testcase number from command line
    int mod_key, threshold;

    // Read moderator key and threshold from input.txt
    read_input_file(testcase, &mod_key, &threshold);
    
    // Load filtered words from "filtered_words.txt" (now correctly)
    load_filtered_words(testcase);

    // Create or access the message queue for the moderator (receiving messages from groups)
    int msgid = msgget(mod_key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("Error creating/moderator message queue");
        exit(EXIT_FAILURE);
    }

    // Continuously monitor incoming messages from groups
    while (1) {
        Message msg;

        // Receive messages from the message queue (from groups)
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(msg.mtype), 0, 0) == -1) {
            perror("Error receiving message from group");
            continue;
        }

        // Count violations for the received message
        int user_id = msg.user;
        int group_id = msg.modifyingGroup;
        int violation_count = count_violations(msg.mtext);

        // Update the user's total violations
        violations[group_id][user_id] += violation_count;

        printf("Message from group %d user %d: '%s' has %d violation(s)\n",
               group_id, user_id, msg.mtext, violation_count);

        // Check if the user should be removed based on the violation threshold
        if (violations[group_id][user_id] >= threshold) {
            printf("User %d from group %d has been removed due to %d violations.\n",
                   user_id, group_id, violations[group_id][user_id]);

            // Send message back to group process to remove the user
            Message remove_msg;
            remove_msg.mtype = 2;  // Use a different mtype for user removal messages
            remove_msg.modifyingGroup = group_id;
            remove_msg.user = user_id;

            if (msgsnd(msgid, &remove_msg, sizeof(remove_msg) - sizeof(remove_msg.mtype), 0) == -1) {
                perror("Error sending remove message to group");
            }
        }
    }

    return 0;
}
