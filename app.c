#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_GROUPS 30

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

    int num_groups, val_key, app_key, mod_key, threshold;
    fscanf(file, "%d %d %d %d %d", &num_groups, &val_key, &app_key, &mod_key, &threshold);

    char groupFiles[num_groups][256];
    for (int i = 0; i < num_groups; i++) {
        fscanf(file, "%s", groupFiles[i]);
    }
    fclose(file);

    printf("Spawning %d groups...\n", num_groups);
    pid_t pids[num_groups];

    for (int i = 0; i < num_groups; i++) {
        char groupFilePath[256];
        sprintf(groupFilePath, "testcase_%d/%s", testcase, groupFiles[i]); // ✅ Corrected path

        // ✅ Check if the group file exists before executing the process
        FILE *groupFile = fopen(groupFilePath, "r");
        if (!groupFile) {
            perror("Error opening group file");
            continue; // Skip this group and move to the next one
        }
        fclose(groupFile);

        pids[i] = fork();
        if (pids[i] == 0) { // Child process
            execl("./groups.out", "groups.out", groupFilePath, NULL);
            perror("Error executing group process");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for all groups to finish
    for (int i = 0; i < num_groups; i++) {
        waitpid(pids[i], NULL, 0);
    }

    printf("All groups terminated. Exiting app process.\n");
    return 0;
}

