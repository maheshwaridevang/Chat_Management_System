#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>

#define MAX_GROUPS 30

// Updated Message structure for communication between app and group processes
struct msg_buffer {
    long mtype;      // Message type (use group ID as the type)
    int group_id;    // Group ID
    char status[100]; // Additional status message (optional)
};

// Function to spawn group processes
void spawn_groups(int num_groups, char groupFiles[][256], int app_key, int mod_key, int val_key, int threshold, int msgid,int testcase) {
    pid_t pids[num_groups];

    for (int i = 0; i < num_groups; i++) {
         char *underscore = strrchr(groupFiles[i], '_');
         char *dot = strrchr(groupFiles[i], '.');
         int group_id = atoi(underscore + 1);  // Extract group ID from file name
        pids[i] = fork();
        if (pids[i] == 0) { // Child process (Group)
            // Construct command-line arguments for groups.out
            char group_id_str[10];
            snprintf(group_id_str, sizeof(group_id), "%d", group_id);  // Group ID as argument

            char app_key_str[10], mod_key_str[10], val_key_str[10], threshold_str[10],testcase_str[10];
            snprintf(app_key_str, sizeof(app_key_str), "%d", app_key);      // App key
            snprintf(mod_key_str, sizeof(mod_key_str), "%d", mod_key);      // Moderator key
            snprintf(val_key_str, sizeof(val_key_str), "%d", val_key);      // Validation key
            snprintf(threshold_str, sizeof(threshold_str), "%d", threshold); // Violation threshold
            snprintf(testcase_str, sizeof(testcase_str), "%d", testcase);

            execl("./groups.out", "groups.out", groupFiles[i], group_id_str, app_key_str, mod_key_str, val_key_str, threshold_str,testcase_str, NULL);
            perror("Error executing group process");
            exit(EXIT_FAILURE);
        }
    }

    // Track and wait for group terminations
    int active_groups = num_groups;
    struct msg_buffer message;
    while (active_groups > 0) {
        // Receive message from group process when it terminates
        if (msgrcv(msgid, &message, sizeof(message) - sizeof(long), 0, 0) == -1) {
            perror("Error receiving message from group");
            exit(EXIT_FAILURE);
        }

        printf("All users terminated. Exiting group process %d. Status: %s\n", message.group_id, message.status);
        active_groups--;
    }

    // Cleanup the message queue
    // msgctl(msgid, IPC_RMID, NULL);
}

// Main function
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <testcase_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int testcase = atoi(argv[1]); // ✅ Get the test case number
    char inputFile[256];
    sprintf(inputFile, "testcase_%d/input.txt", testcase); // ✅ Correct file path

    FILE *file = fopen(inputFile, "r");
    if (!file) {
        perror("Error opening input file");
        exit(EXIT_FAILURE);
    }
    
    // Read input from input.txt
    int num_groups, val_key, app_key, mod_key, threshold;
    fscanf(file, "%d %d %d %d %d", &num_groups, &val_key, &app_key, &mod_key, &threshold);

    char groupFiles[num_groups][256];
    for (int i = 0; i < num_groups; i++) {
        fscanf(file, "%s", groupFiles[i]);  // Reading non-contiguous group file paths
    }
    fclose(file);

    printf("Spawning %d groups...\n", num_groups);

    // Create message queue for communication between app and groups
    int msgid = msgget(app_key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("Error creating message queue");
        exit(EXIT_FAILURE);
    }

    // Spawn group processes
    spawn_groups(num_groups, groupFiles, app_key, mod_key, val_key, threshold, msgid,testcase);

    printf("All groups terminated. Exiting app process.\n");

    // ✅ Ensure queue is only removed after all groups are done
    sleep(2);  // Wait to make sure all messages are received
    msgctl(msgid, IPC_RMID, NULL);
    return 0;
}
