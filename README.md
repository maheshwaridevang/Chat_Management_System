# 🛡️ Chat Management and Moderation System

A POSIX-compliant C-based system simulating real-time group chat moderation using process creation and inter-process communication (IPC) mechanisms like message queues and pipes.

> 📚 Developed as part of CS-F372 Operating Systems coursework at BITS Pilani.

---

## 🚀 Overview

This system simulates a secure and moderated chat environment under a fictional authoritarian regime. It involves users chatting in groups, where all messages are checked for filtered content, and violators are automatically banned. The system is structured into modular processes to simulate real-world IPC concepts.

---

## 🧩 Components

- **`app.c`**: Launches and manages group processes.
- **`groups.c`**: Handles group logic and user process creation. Manages message flow to moderator and validation.
- **`moderator.c`**: Scans messages for filtered words, tracks violations, and bans users.
- **`validation.out`**: Provided by instructors. Validates message sequence, user bans, and group termination logic.

---

## 🏗️ Architecture

```plaintext
                        +-----------------+
                        |  validation.out |
                        +--------^--------+
                                 |
        +--------+        +-----+-----+        +----------+
        | app.out|------->| groups.out|<------>|moderator |
        +--------+        +-----^-----+        +----------+
                                 |
                         +-------+--------+
                         |   user_X_Y.txt  |
                         +----------------+
```

- **Pipes**: Used between group processes and their user processes.
- **Message Queues**:
  - `groups.c` ↔ `moderator.c`
  - `groups.c` ↔ `validation.out`
  - `groups.c` ↔ `app.c`

---

## 📂 Directory Structure

```
Chat_Management_System/
├── app.c
├── groups.c
├── moderator.c
├── validation.out                # Provided executable
├── testcase_X/                   # Input folder for test X
│   ├── input.txt
│   ├── filtered_words.txt
│   ├── groups/
│   │   └── group_X.txt
│   └── users/
│       └── user_X_Y.txt
├── README.md
```

---

## 🛠️ Compilation

```bash
gcc -o app.out app.c
gcc -o groups.out groups.c
gcc -o moderator.out moderator.c
chmod +x validation.out
```

---

## ▶️ Execution Instructions

Open three terminals for running the system in this order:

1. **Validation Script**
   ```bash
   ./validation.out X
   ```
2. **Moderator Process**
   ```bash
   ./moderator.out X
   ```
3. **Main Application**
   ```bash
   ./app.out X
   ```

> Replace `X` with the test case number (e.g., 0, 1, 2...).

---

## 📥 Input Format

### `input.txt`
```plaintext
<number_of_groups>
<validation_msgq_key>
<app_groups_msgq_key>
<groups_moderator_msgq_key>
<violation_threshold>
<group_file_path_1>
...
<group_file_path_n>
```

### `group_X.txt`
```plaintext
<number_of_users>
<path_to_user_file_1>
<path_to_user_file_2>
...
```

### `user_X_Y.txt`
Each line contains:
```plaintext
<timestamp> <message>
```

### `filtered_words.txt`
A list of filtered words, one per line.

---

## 📌 Features

- ✅ Fork-based group and user process creation.
- ✅ Pipe communication between user ↔ group.
- ✅ Real-time violation tracking using case-insensitive substring matching.
- ✅ Automatic banning of users exceeding violation thresholds.
- ✅ Graceful group termination when users < 2.
- ✅ Ordered message validation via `validation.out`.

---

## 📈 Sample Output

```plaintext
User 3 from group 1 has been removed due to 6 violations.
All users terminated. Exiting group process 1.
```

---

## ⚠️ Constraints

- Max Groups: 30  
- Max Users per Group: 50  
- Max Filtered Words: 50  
- Max Word Length: 20  
- Max Message Length: 256 characters  
- Max Timestamp: 2147000000  

---

