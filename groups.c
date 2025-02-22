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

// structure for message queue
typedef struct
{
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_TEXT_SIZE];
    int modifyingGroup;
    int is_ban;
} Message;

typedef struct
{
    long mtype;
    int group_id;
    char status[100];
}app_buffer;

// Structure for messages sent over pipes
typedef struct
{
    int timestamp;
    char message[MAX_TEXT_SIZE];
} Pipes;

// structure for storing user messages for sorting
typedef struct
{
    int timestamp;
    int user_id;
    char message[MAX_TEXT_SIZE];
} StoredMsgs;

// structure to store user data
typedef struct
{
    int user_id;
    char user_file[MAX_PATH_SIZE];
    int pipe_fd[2];
    int active;
    int removal;
    int message_number;
} UserData;

void message_to_validation(int msgid, int mtype, int group_id, int user, int timestamp, const char *text)
{
    Message msg;
    msg.mtype = mtype;
    msg.modifyingGroup = group_id;
    msg.user = user;
    msg.timestamp = timestamp;
    strncpy(msg.mtext, text, MAX_TEXT_SIZE);

    if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(msg.mtype), 0) == -1)
    {
        if (errno != EINVAL && errno != EIDRM)
        {
            perror("Error sending message to validation");
        }
    }
}

void message_to_moderator(int mod_msgid, int group_id, int user, const char *text)
{
    Message mod_msg;
    mod_msg.mtype = 1;
    mod_msg.modifyingGroup = group_id;
    mod_msg.user = user;
    strncpy(mod_msg.mtext, text, MAX_TEXT_SIZE);

    if (msgsnd(mod_msgid, &mod_msg, sizeof(mod_msg) - sizeof(mod_msg.mtype), 0) == -1)
    {
        if (errno != EINVAL && errno != EIDRM)
        {
            perror("Error sending message to moderator");
        }
    }
}

// reads user file and writes each message as one Pipes record
void UserProcess(int group_id, int user_id, const char *user_file, int write_pipe, int testcase)
{
    char inputFile[MAX_PATH_SIZE];
    snprintf(inputFile, sizeof(inputFile), "testcase_%d/%s", testcase, user_file);
    FILE *file = fopen(inputFile, "r");
    if (!file)
    {
        perror("Error opening user file");
        exit(EXIT_FAILURE);
    }

    Pipes pm;
    char buffer[MAX_TEXT_SIZE];
    while (fgets(buffer, sizeof(buffer), file))
    {
        sscanf(buffer, "%d %[^\n]", &pm.timestamp, pm.message);
        if (write(write_pipe, &pm, sizeof(Pipes)) != sizeof(Pipes))
        {
            perror("Error writing to pipe");
        }
        sleep(1);
    }
    fclose(file);
    close(write_pipe);
    exit(0);
}

// reads the user file paths and initializes the users array
void InitializeGroup(int group_id, const char *group_file, UserData users[], int *num_users, int testcase)
{
    char inputFile[MAX_PATH_SIZE];
    snprintf(inputFile, sizeof(inputFile), "testcase_%d/%s", testcase, group_file);

    FILE *file = fopen(inputFile, "r");
    if (!file)
    {
        perror("Error opening group file");
        exit(EXIT_FAILURE);
    }

    fscanf(file, "%d", num_users);
    for (int i = 0; i < *num_users; i++)
    {
        char user_file[MAX_PATH_SIZE];
        fscanf(file, "%s", user_file);
        char *underscore = strrchr(user_file, '_');
        users[i].user_id = atoi(underscore + 1);
        strncpy(users[i].user_file, user_file, MAX_PATH_SIZE);
        if (pipe(users[i].pipe_fd) == -1)
        {
            perror("Error creating pipe");
            exit(EXIT_FAILURE);
        }
        int flags = fcntl(users[i].pipe_fd[0], F_GETFL, 0);
        fcntl(users[i].pipe_fd[0], F_SETFL, flags | O_NONBLOCK);
        users[i].active = 1;
    }
    fclose(file);
}

// for sorting compare timestamps
int compare_timestamps(const void *a, const void *b)
{
    StoredMsgs *msgA = (StoredMsgs *)a;
    StoredMsgs *msgB = (StoredMsgs *)b;
    if (msgA->timestamp == msgB->timestamp)
    {
        return msgA->user_id - msgB->user_id;
    }
    return msgA->timestamp - msgB->timestamp;
}

//1. stores the user msgs in a buffer 
//2. sorts the user msgs according to timestamps
//3. sends the messages to validation and moderator
void GroupProcess(int group_id, int num_users, UserData users[], int val_msgid, int mod_msgid,int app_msgid)
{
    StoredMsgs user_messages[1000];
    int message_count = 0;
    int active_users = num_users;

    // Mark all users as initially active and not removed and keeping message count to zero
    for (int i = 0; i < num_users; i++)
    {
        users[i].active = 1;
        users[i].removal = 0;
        users[i].message_number = 0;
    }

    int done = 0;
    while (!done)
    {
        int progress = 0;

        for (int i = 0; i < num_users; i++)
        {
            if (!users[i].active)
                continue;

            Pipes pm;
            ssize_t bytes_read = read(users[i].pipe_fd[0], &pm, sizeof(Pipes));

            if (bytes_read == sizeof(Pipes))
            {
                // Buffer the message for sorting
                user_messages[message_count].timestamp = pm.timestamp;
                user_messages[message_count].user_id = users[i].user_id;
                strncpy(user_messages[message_count].message, pm.message, MAX_TEXT_SIZE);
                message_count++;
                progress = 1;
                users[i].message_number++;
            }
            else if (bytes_read == 0)
            {
                if (users[i].active)
                {
                    users[i].active = 0;
                    active_users--;
                }
            }
            else
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    perror("Error reading from pipe");
                }
            }
        }

        if (!progress)
        {
            usleep(100000);
        }

        int all_finished = 1;
        for (int i = 0; i < num_users; i++)
        {
            if (users[i].active)
            {
                all_finished = 0;
                break;
            }
        }
        if (all_finished)
        {
            done = 1;
        }
    }

    for (int i = 0; i < num_users; i++)
    {
        close(users[i].pipe_fd[0]);
    }

    qsort(user_messages, message_count, sizeof(StoredMsgs), compare_timestamps);

    active_users = num_users;

    for (int k = 0; k < num_users; k++)
    {
        users[k].active = 1;
    }
    for (int i = 0; i < message_count; i++)
    {

        if (active_users < 2)
        {
            printf("Active users in group %d dropped below 2. Terminating group.\n", group_id);
            break;
        }

        int flag = 0;
        for (int j = 0; j < num_users; j++)
        {
            if (user_messages[i].user_id == users[j].user_id)
            {
                if (!users[j].active)
                {
                    flag = 1;
                    break;
                }
            }
        }
        if (flag)
        {
            continue;
        }

        message_to_validation(val_msgid, 30 + group_id, group_id, user_messages[i].user_id, user_messages[i].timestamp, user_messages[i].message);
        message_to_moderator(mod_msgid, group_id, user_messages[i].user_id, user_messages[i].message);

        Message mod_msg;
        while (msgrcv(mod_msgid, &mod_msg, sizeof(mod_msg) - sizeof(mod_msg.mtype), 100 + group_id, 0) != -1)
        {
            int banned_user = mod_msg.user;
            int ban = mod_msg.is_ban;

            if (ban)
            {
                for (int j = 0; j < num_users; j++)
                {
                    if (users[j].user_id == banned_user && !users[j].removal)
                    {
                        printf("**Moderator banned user %d from group %d.**\n", banned_user, group_id);
                        users[j].active = 0;
                        users[j].removal = 1;
                        active_users--;
                        break;
                    }
                }
                break;
            }
            else
            {
                for (int j = 0; j < num_users; j++)
                {
                    if (users[j].user_id == user_messages[i].user_id)
                    {
                        users[j].message_number--;
                        if (users[j].message_number == 0)
                        {
                            printf("User %d has finished sending all messages. Marking as inactive.\n", users[j].user_id);
                            users[j].active = 0;
                            active_users--;
                        }
                        break;
                    }
                }
                break;
            }
        }
    }
    int violation_removals = 0;
    for (int i = 0; i < num_users; i++)
    {
        if (users[i].removal)
        {
            violation_removals++;
        }
    }

    message_to_validation(val_msgid, 3, group_id, violation_removals, 0, "Terminating group");
    app_buffer apmsg;
    apmsg.group_id=group_id;
    apmsg.mtype=30+group_id;
    if (msgsnd(app_msgid, &apmsg, sizeof(apmsg) - sizeof(apmsg.mtype), 0) == -1)
    {
        if (errno != EINVAL && errno != EIDRM)
        {
            perror("Error sending message to validation");
        }
    }
    
}

int main(int argc, char *argv[])
{
    if (argc != 8)
    {
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

    int val_msgid = msgget(val_key, 0666);
    int mod_msgid = msgget(mod_key, 0666);
    int app_msgid = msgget(app_key, 0666);

    UserData users[MAX_USERS];
    int num_users = 0;

    InitializeGroup(group_id, group_file, users, &num_users, testcase);
    message_to_validation(val_msgid, 1, group_id, 0, 0, "");

    for (int i = 0; i < num_users; i++)
    {
        if (fork() == 0) // child process
        {
            close(users[i].pipe_fd[0]);
           UserProcess(group_id, users[i].user_id, users[i].user_file, users[i].pipe_fd[1], testcase);
        }
        else // parent process
        {
            close(users[i].pipe_fd[1]);
            message_to_validation(val_msgid, 2, group_id, users[i].user_id, 0, "");
        }
    }

    GroupProcess(group_id, num_users, users, val_msgid, mod_msgid,app_msgid);
    msgctl(mod_key, IPC_RMID, NULL);

    return 0;
}
