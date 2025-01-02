# Custom Deadline-Driven Scheduler (DDS)

## Project Overview
This project implements a **custom Deadline-Driven Scheduler (DDS)** to dynamically manage tasks with hard execution deadlines. The scheduler utilizes the **Earliest Deadline First (EDF)** algorithm to ensure efficient task prioritization and scheduling.

---

## Features
- **Deadline-Driven Scheduler**:
  - Implements the EDF algorithm to dynamically manage task priorities.
  - Manages user-defined "DD-Tasks" through the following interface functions:
    ```c
    release_dd_task        // Create a new DD-Task
    complete_dd_task       // Mark a DD-Task as completed
    get_active_dd_task_list    // Retrieve the list of currently active DD-Tasks
    get_completed_dd_task_list // Retrieve the list of completed DD-Tasks
    get_overdue_dd_task_list   // Retrieve the list of overdue DD-Tasks
    ```
  - Operates at the highest priority and remains suspended unless invoked via an interface function.

- **User-Defined Tasks**:
  - Application code for deadline-sensitive tasks defined by the user.

- **Deadline-Driven Task Generator**:
  - Periodically generates new DD-Tasks for the scheduler.

- **Monitor Task**:
  - Reports scheduling information, extracting details from the DDS.

---

## Task Lists
1. **Active Task List**:
   - DD-Tasks currently scheduled for execution.

2. **Completed Task List**:
   - DD-Tasks that have successfully completed before their deadlines.
   - Primarily used for debugging.

3. **Overdue Task List**:
   - DD-Tasks that have missed their deadlines.

---

This DDS system enables efficient management of hard real-time tasks and provides debugging tools for monitoring performance and scheduling efficiency.
