#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <errno.h>

#define MAX_GROUPS 30

typedef struct
{
    long mtype;
    int group_id;
    char status[100];
}msg_buffer;

// this creates multiple groups and creates separate processes for each group
void GroupFormation(int num_groups, char groupFiles[][256], int app_key, int mod_key, int val_key, int threshold, int msgid, int testcase)
{
    pid_t pids[num_groups];

    for (int i = 0; i < num_groups; i++)
    {
        char *underscore = strrchr(groupFiles[i], '_');
        int group_id = atoi(underscore + 1);
        pids[i] = fork();
        if (pids[i] == 0)
        {
            char group_id_str[10];
            snprintf(group_id_str, sizeof(group_id_str), "%d", group_id);

            char app_key_str[10], mod_key_str[10], val_key_str[10], threshold_str[10], testcase_str[10];
            snprintf(app_key_str, sizeof(app_key_str), "%d", app_key);
            snprintf(mod_key_str, sizeof(mod_key_str), "%d", mod_key);
            snprintf(val_key_str, sizeof(val_key_str), "%d", val_key);
            snprintf(threshold_str, sizeof(threshold_str), "%d", threshold);
            snprintf(testcase_str, sizeof(testcase_str), "%d", testcase);

            execl("./groups.out", "groups.out", groupFiles[i], group_id_str, app_key_str, mod_key_str, val_key_str, threshold_str, testcase_str, NULL);
            perror("Error executing group process");
            exit(EXIT_FAILURE);
        }
    }

    int active_groups = num_groups;
    msg_buffer message;
    while (active_groups > 0)
    {
        if (msgrcv(msgid, &message, sizeof(message) - sizeof(long), 0, 0) == -1)
        {
            if (errno == EIDRM)
            {
                break;
            }
            else
            {
                perror("Error receiving message from group");
                exit(EXIT_FAILURE);
            }
        }
        printf("All users terminated. Exiting group process %d. Status: inactive\n", message.group_id);
        active_groups--;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <testcase_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int testcase = atoi(argv[1]);
    char inputFile[256];
    sprintf(inputFile, "testcase_%d/input.txt", testcase);

    FILE *file = fopen(inputFile, "r");
    if (!file)
    {
        perror("Error opening input file");
        exit(EXIT_FAILURE);
    }

    int num_groups, val_key, app_key, mod_key, threshold;
    fscanf(file, "%d %d %d %d %d", &num_groups, &val_key, &app_key, &mod_key, &threshold);

    char groupFiles[num_groups][256];
    for (int i = 0; i < num_groups; i++)
    {
        fscanf(file, "%s", groupFiles[i]);
    }
    fclose(file);

    printf("Spawning %d groups...\n", num_groups);

    int msgid = msgget(app_key, 0666);
    if (msgid == -1)
    {
        perror("Error creating message queue");
        exit(EXIT_FAILURE);
    }

    GroupFormation(num_groups, groupFiles, app_key, mod_key, val_key, threshold, msgid, testcase);

    printf("All groups terminated. Exiting app process.\n");
    msgctl(mod_key, IPC_RMID, NULL);
    return 0;
}
