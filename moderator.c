#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>

#define MAX_WORDS 50
#define MAX_MSG_SIZE 256

typedef struct {
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_MSG_SIZE];
    int modifyingGroup;
} Message;

char filtered_words[MAX_WORDS][20];
int num_filtered = 0;

void load_filtered_words(int testcase) {
    char filePath[256];
    sprintf(filePath, "testcase_%d/filtered_words.txt", testcase); // ✅ Corrected file path

    FILE *file = fopen(filePath, "r");
    if (!file) {
        perror("Error opening filtered words file");
        exit(EXIT_FAILURE);
    }

    while (fgets(filtered_words[num_filtered], sizeof(filtered_words[num_filtered]), file)) {
        strtok(filtered_words[num_filtered], "\n"); // ✅ Remove newline character
        num_filtered++;
    }
    fclose(file);
}

int count_violations(const char *message) {
    int violations = 0;
    for (int i = 0; i < num_filtered; i++) {
        if (strstr(message, filtered_words[i])) { // ✅ Case-sensitive substring check
            violations++;
        }
    }
    return violations;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <testcase_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int testcase = atoi(argv[1]); // ✅ Get test case number from command-line argument
    load_filtered_words(testcase); // ✅ Load words from correct path

    int queue_id = msgget(9131, 0666 | IPC_CREAT);
    if (queue_id == -1) {
        perror("Error creating message queue");
        exit(EXIT_FAILURE);
    }

    while (1) {
        Message msg;
        if (msgrcv(queue_id, &msg, sizeof(msg) - sizeof(long), 0, 0) == -1) {
            perror("Error receiving message");
            continue;
        }

        int violations = count_violations(msg.mtext);
        printf("User %d in Group %d has %d violations\n", msg.user, msg.modifyingGroup, violations);
    }

    return 0;
}

