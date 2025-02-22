#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_USERS 50
#define MAX_MSG_SIZE 256
#define MAX_WORDS 50

typedef struct
{
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_MSG_SIZE];
    int modifyingGroup;
    int is_ban;
} Message;

char filtered_words[MAX_WORDS][MAX_MSG_SIZE];
int NumFilter = 0;
int violations[MAX_USERS][MAX_USERS] = {0};
int removed_users[MAX_USERS][MAX_USERS] = {0};
int NotBanned[MAX_USERS][MAX_USERS] = {0};

// To load filtered words from the file given
void LoadFilteredWords(int testcase)
{
    char filePath[256];
    snprintf(filePath, sizeof(filePath), "testcase_%d/filtered_words.txt", testcase);

    FILE *file = fopen(filePath, "r");
    if (!file)
    {
        perror("Error opening filtered words file");
        exit(EXIT_FAILURE);
    }

    while (fgets(filtered_words[NumFilter], sizeof(filtered_words[NumFilter]), file))
    {
        strtok(filtered_words[NumFilter], "\n");
        NumFilter++;
        if (NumFilter >= MAX_WORDS)
            break;
    }
    fclose(file);
}

void to_lowercase(char *str)
{
    for (int i = 0; str[i]; i++)
    {
        str[i] = tolower(str[i]);
    }
}

int count_violations(const char *message)
{
    int violation_count = 0;
    char lowered_message[MAX_MSG_SIZE];
    strncpy(lowered_message, message, MAX_MSG_SIZE);
    lowered_message[MAX_MSG_SIZE - 1] = '\0';
    to_lowercase(lowered_message);

    for (int i = 0; i < NumFilter; i++)
    {
        char lowered_word[MAX_MSG_SIZE];
        strncpy(lowered_word, filtered_words[i], MAX_MSG_SIZE);
        lowered_word[MAX_MSG_SIZE - 1] = '\0';
        to_lowercase(lowered_word);

        if (strstr(lowered_message, lowered_word) != NULL)
        {
            violation_count++;
        }
    }
    return violation_count;
}

void ReadInputFile(int testcase, int *mod_key, int *threshold)
{
    char filePath[256];
    snprintf(filePath, sizeof(filePath), "testcase_%d/input.txt", testcase);

    FILE *file = fopen(filePath, "r");
    if (!file)
    {
        perror("Error opening input.txt");
        exit(EXIT_FAILURE);
    }

    int num_groups, val_key, app_key;
    fscanf(file, "%d %d %d %d %d", &num_groups, &val_key, &app_key, mod_key, threshold);
    fclose(file);
}


// Marks the msgs received from the groups.c file as banned or not banned based on the no. of violations.
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <testcase_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int testcase = atoi(argv[1]);
    int mod_key, threshold;

    ReadInputFile(testcase, &mod_key, &threshold);
    LoadFilteredWords(testcase);

    int msgid = msgget(mod_key, 0666 | IPC_CREAT);
    if (msgid == -1)
    {
        perror("Error creating/moderator message queue");
        exit(EXIT_FAILURE);
    }

    char command[100];
    snprintf(command, sizeof(command), "ipcrm -q %d", msgid);

    while (1)
    {
        Message msg;

        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(msg.mtype), 0, 0) == -1)
        {
            perror("Error receiving message from group");
            continue;
        }

        int user_id = msg.user;
        int group_id = msg.modifyingGroup;
        int violation_count = count_violations(msg.mtext);

        violations[group_id][user_id] += violation_count;

        printf("Message from group %d user %d: '%s' has %d violation(s)\n",
               group_id, user_id, msg.mtext, violations[group_id][user_id]);

        NotBanned[group_id][user_id] = 0;
        if (violations[group_id][user_id] >= threshold && !removed_users[group_id][user_id])
        {
            printf("**User %d from group %d has been removed due to %d violations.**\n",
                   user_id, group_id, violations[group_id][user_id]);

            removed_users[group_id][user_id] = 1;

            Message remove_msg;
            remove_msg.mtype = 100 + group_id;
            remove_msg.modifyingGroup = group_id;
            remove_msg.user = user_id;
            remove_msg.is_ban = 1;

            if (msgsnd(msgid, &remove_msg, sizeof(remove_msg) - sizeof(remove_msg.mtype), 0) == -1)
            {
                perror("Error sending remove message to group");
            }
            else
            {
                printf("Successfully sent remove message: %d of group %d\n", remove_msg.user, remove_msg.modifyingGroup);
            }
        }
        else if (violations[group_id][user_id] < threshold && !NotBanned[group_id][user_id])
        {
            NotBanned[group_id][user_id] = 1;
            Message remove_msg;
            remove_msg.mtype = 100 + group_id;
            remove_msg.modifyingGroup = group_id;
            remove_msg.user = user_id;
            remove_msg.is_ban = 0;
            if (msgsnd(msgid, &remove_msg, sizeof(remove_msg) - sizeof(remove_msg.mtype), 0) == -1)
            {
                perror("Error sending remove message to group");
            }
            else
            {
                NotBanned[group_id][user_id] = 1;
                printf("Successfully sent not banned message for user: %d of group %d\n", remove_msg.user, remove_msg.modifyingGroup);
            }
        }
    }
    
    return 0;
}
