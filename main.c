/*
 * Authors: Adam Hultman, Dave
 * Description: The objective of this project is to design a custom Deadline-Driven Scheduler (DDS) to dynamically manage
 * 				tasks which have hard execution deadlines.
 *
 * FreeRTOS Task List:
 *		1. Deadline-Driven Scheduler
 *			Implements the EDF algorithm and controls the priorities of user-defined F-tasks from an actively-
 *			managed list of DD-Tasks.
 *			Note: 	-Highest priority
 *					-Normally suspended unless call is made to one of its interface functions:
 *			1. release_dd_task (create)
 *			2. complete_dd_task (delete)
 *			3. get_active_dd_task_list
 *			4. get_completed_dd_task_list
 *			5. get_overdue_dd_task_list
 *		2. User-Defined Tasks
 *			Contains the actual deadline-sensitive application code written by the user.
 *		3. Deadline-Driven Task Generator
 *			Periodically creates DD-Tasks that need to be scheduled by the DD Scheduler.
 *		4. Monitor Task
 *			F-Task to extract information from the DDS and report scheduling information.
 *
 * DD Task List:
 * 		1. Active Task List
 * 			A list of DD-Tasks which the DDS currently needs to schedule.
 *		2. Completed Task List
 *			A list of DD-Tasks which have completed execution before their deadlines.
 * 			Note: Only needed for debugging purposes. Takes up space.
 *		3. Overdue Task List
 *			A list of DD-Tasks which have missed their deadlines.
 */

/*--Includes--------------------------------------------------*/

/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "stm32f4_discovery.h"
/* Kernel includes. */
#include "stm32f4xx.h"
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"

/*--Defines--------------------------------------------------*/

/* Comment out unused Bench defines. All values are in milliseconds */
// Test Bench #1
//#define T1_EXEC_TIME	95
//#define T1_PERIOD		500
//#define T2_EXEC_TIME	150
//#define T2_PERIOD		500
//#define T3_EXEC_TIME	250
//#define T3_PERIOD		750
// Test Bench #2
#define T1_EXEC_TIME	95
#define T1_PERIOD		250
#define T2_EXEC_TIME	150
#define T2_PERIOD		500
#define T3_EXEC_TIME	250
#define T3_PERIOD		750
// Test Bench #3
//#define T1_EXEC_TIME	100
//#define T1_PERIOD		500
//#define T2_EXEC_TIME	200
//#define T2_PERIOD		500
//#define T3_EXEC_TIME	200
//#define T3_PERIOD		500

// Monitor Task Timing
#define PERIOD_MONITOR 		pdMS_TO_TICKS(1500)

// Priorities
#define PRIORITY_SCH 		4
#define PRIORITY_GEN 		3
#define PRIORITY_MON 		3
#define PRIORITY_HIGH 		2
#define PRIORITY_MIN 		1

/*--Types-&-Structs------------------------------------------*/

// Describes task_type for task generation
typedef enum task_type {
	PERIODIC,
	APERIODIC
} task_type;

// Describes types of messaged sent to DDS
typedef enum message_type {
	CREATE,
	DELETE,
	ACTIVE,
	COMPLETED,
	OVERDUE
} message_type;

// Bench task data structure
typedef struct bench_task {
	TaskHandle_t 	t_handle;
	uint32_t 		task_id;
	uint16_t		period;
	uint16_t		execution_time;
} bench_task;

// Deadline-Driven Task data structure
typedef struct dd_task {
	TaskHandle_t 	t_handle;
	task_type 		type;
	uint32_t 		task_id;
	uint32_t 		release_time;
	uint16_t		execution_time;
	uint32_t 		absolute_deadline;
	uint32_t 		completion_time;
} dd_task;

typedef struct dd_task_node {
	dd_task*		 		task;
	struct dd_task_node*	next;
} dd_task_node;

// Singly-linked List data structure for dd_tasks
typedef struct dd_task_list {
	dd_task_node* 	head;
	uint32_t		size;
} dd_task_list;

// Messages in the DDS Message Queue
typedef struct message {
	message_type 	type;
	dd_task*			task;
} message;

/*--Prototypes-----------------------------------------------*/

// F-Tasks
static void DD_Monitor_Task ( void *pvParameters );
static void DD_Generator_Task( void *pvParameters );
static void DD_Scheduler_Task( void *pvParameters );

void User_Defined_Task_1( void *pvParameters );
void User_Defined_Task_2( void *pvParameters );
void User_Defined_Task_3( void *pvParameters );

// Timers
static void Task_1_Callback(xTimerHandle);
static void Task_2_Callback(xTimerHandle);
static void Task_3_Callback(xTimerHandle);

dd_task_list* get_active_dd_task_list(void);
dd_task_list* get_completed_dd_task_list(void);
dd_task_list* get_overdue_dd_task_list(void);

// DDS Interface Functions
void create_dd_task(
	TaskHandle_t t_handle,
	task_type type,
	uint32_t task_id,
	uint32_t release_time,
	uint32_t absolute_deadline
	);
void delete_dd_task(uint32_t task_id);

// Linked List Helpers
void insert_task_ll(dd_task_list*, dd_task*);
void remove_task_ll(dd_task_list*, dd_task*);
void insert_sorted_task_ll(dd_task_list*, dd_task*);

/*--Tasks---------------------------------------------------*/

// F-Tasks
TaskHandle_t DD_Monitor_Task_Handle;
TaskHandle_t DD_Generator_Task_Handle;
TaskHandle_t DD_Scheduler_Task_Handle;

// User defined task handles
TaskHandle_t Task_1_Handle = NULL;
TaskHandle_t Task_2_Handle = NULL;
TaskHandle_t Task_3_Handle = NULL;

/*--Timers---------------------------------------------------*/

xTimerHandle Task_1_Timer = 0;
xTimerHandle Task_2_Timer = 0;
xTimerHandle Task_3_Timer = 0;

/*--Queues---------------------------------------------------*/

xQueueHandle DDS_Message_Queue 	= 0;
xQueueHandle DDS_Reply_Queue 	= 0;
xQueueHandle Task_Create_Queue 	= 0;

/*--Test Bench--------------------------------------------------*/

// Task Bench Data Array
bench_task* bench_array;

/*--Functions------------------------------------------------*/

int main(void)
{
	bench_array = malloc(3*sizeof(bench_task));

	// Create DDS Message Queue
	DDS_Message_Queue 	= xQueueCreate( 20, sizeof( message ) );
	DDS_Reply_Queue 	= xQueueCreate( 5, sizeof( dd_task_list** ) );
	Task_Create_Queue 	= xQueueCreate( 20, sizeof( uint16_t ) );

	// Check for queue creation errors
	if( DDS_Message_Queue == NULL || DDS_Reply_Queue == NULL || Task_Create_Queue == NULL )
	{
		printf("Queue create error\n");
		return 1;
	}

	// Add to the registry, for the benefit of kernel aware debugging.
	vQueueAddToRegistry( DDS_Message_Queue, "DDS_Messages" );
	vQueueAddToRegistry( DDS_Reply_Queue, "DDS_Replies" );
	vQueueAddToRegistry( Task_Create_Queue, "Task_Create" );

	// Create F-Tasks
	xTaskCreate( DD_Monitor_Task, "Monitor", configMINIMAL_STACK_SIZE, NULL, PRIORITY_MON, &DD_Monitor_Task_Handle );
	xTaskCreate( DD_Scheduler_Task, "Scheduler", configMINIMAL_STACK_SIZE, NULL, PRIORITY_SCH, &DD_Scheduler_Task_Handle );

	// Create user defined tasks
	xTaskCreate(User_Defined_Task_1, "UD_T1", configMINIMAL_STACK_SIZE, NULL, PRIORITY_MIN, &Task_1_Handle);
	vTaskSuspend(Task_1_Handle);
	xTaskCreate(User_Defined_Task_2, "UD_T2", configMINIMAL_STACK_SIZE, NULL, PRIORITY_MIN, &Task_2_Handle);
	vTaskSuspend(Task_2_Handle);
	xTaskCreate(User_Defined_Task_3, "UD_T3", configMINIMAL_STACK_SIZE, NULL, PRIORITY_MIN, &Task_3_Handle);
	vTaskSuspend(Task_3_Handle);

	xTaskCreate( DD_Generator_Task, "Generator", configMINIMAL_STACK_SIZE, NULL, PRIORITY_GEN, &DD_Generator_Task_Handle );

	// Create Timers
	Task_1_Timer = xTimerCreate(
		"T_1_TMR", 							/* name */
		pdMS_TO_TICKS(T1_PERIOD), 			/* period */
		pdFALSE, 							/* auto-reload */
		(void*)0, 							/* timer ID */
		Task_1_Callback			 			/* callback */
		);
	Task_2_Timer = xTimerCreate(
		"T_2_TMR", 							/* name */
		pdMS_TO_TICKS(T2_PERIOD), 			/* period */
		pdFALSE, 							/* auto-reload */
		(void*)0, 							/* timer ID */
		Task_2_Callback			 			/* callback */
		);
	Task_3_Timer = xTimerCreate(
		"T_3_TMR", 							/* name */
		pdMS_TO_TICKS(T3_PERIOD), 			/* period */
		pdFALSE, 							/* auto-reload */
		(void*)0, 							/* timer ID */
		Task_3_Callback			 			/* callback */
		);

	// Check for timer creation errors
	if( Task_1_Timer == NULL || Task_2_Timer == NULL || Task_3_Timer == NULL )
	{
		printf("Timer create error\n");
		return 1;
	}

	bench_array[0].t_handle = 			Task_1_Handle;
	bench_array[0].task_id = 			1;
	bench_array[0].period = 			T1_PERIOD;
	bench_array[0].execution_time = 	T1_EXEC_TIME;

	bench_array[1].t_handle = 			Task_2_Handle;
	bench_array[1].task_id = 			2;
	bench_array[1].period = 			T2_PERIOD;
	bench_array[1].execution_time = 	T2_EXEC_TIME;

	bench_array[2].t_handle = 			Task_3_Handle;
	bench_array[2].task_id = 			3;
	bench_array[2].period = 			T3_PERIOD;
	bench_array[2].execution_time = 	T3_EXEC_TIME;

	// Start Timers for user defined tasks
	if ( !(xTimerStart(Task_1_Timer, 0) && xTimerStart(Task_2_Timer, 0) &&	xTimerStart(Task_3_Timer, 0)) ) {
		printf("TimerStart Error, pdFAIL encountered\n");
		return 1;
	}

	// Start tasks initially
	xQueueSend(Task_Create_Queue, &bench_array[0].task_id, 0);
	xQueueSend(Task_Create_Queue, &bench_array[1].task_id, 0);
	xQueueSend(Task_Create_Queue, &bench_array[2].task_id, 0);

	// Start tasks
	vTaskStartScheduler();

	while(1);
}

/*--Auxiliary-F-Task-----------------------------------------*/

static void DD_Monitor_Task( void* pvParameters )
{
	TickType_t currentTick;

	while(1)
	{
		// Delay Task for X time, for debugging purposes
		vTaskDelay(PERIOD_MONITOR);

		currentTick = xTaskGetTickCount();
		printf("\n      \t   Monitor Task: \t\t  %u\n", currentTick);

		// get all three task lists
		dd_task_list* activeTasks = get_active_dd_task_list();
		dd_task_list* completedTasks = get_completed_dd_task_list();
		dd_task_list* overdueTask = get_overdue_dd_task_list();

		// print the sizes of the task lists
		printf("\nActive Tasks:\n");
		printf("\tSize: %u\n", activeTasks->size);
		printf("\nCompleted Tasks:\n");
		printf("\tSize: %u\n", completedTasks->size);
		printf("\nOverdue Tasks:\n");
		printf("\tSize: %u\n", overdueTask->size);
		printf("\n");

	}
}

/*-----------------------------------------------------------*/

static void DD_Generator_Task( void *pvParameters )
{
	uint32_t task_number = 0;
	uint32_t absolute_deadline = 0;

	// listens for task gen message queue message
	while(1)
	{
		// receives a message
		if ( xQueueReceive(Task_Create_Queue, &task_number, portMAX_DELAY) )
		{
			// Calculate absolute deadline
			uint32_t release_time = xTaskGetTickCount();
			absolute_deadline = release_time + pdMS_TO_TICKS( bench_array[task_number-1].period );

			// Call create() for whichever timer callback the message was from (1, 2, 3)
			// Support APERIODIC tasks
			create_dd_task(bench_array[task_number-1].t_handle, PERIODIC, task_number, release_time, absolute_deadline);
		}
	}
}

/*-----------------------------------------------------------*/

static void DD_Scheduler_Task( void *pvParameters )
{
	dd_task* 		curr_task;
	message 		current_msg;
	uint32_t		current_time = 0;

	dd_task_list* active_ll = malloc(sizeof(dd_task_list));
	active_ll->size = 0;
	active_ll->head = NULL;

	dd_task_list* completed_ll = malloc(sizeof(dd_task_list));
	completed_ll->size = 0;
	completed_ll->head = NULL;

	dd_task_list* overdue_ll = malloc(sizeof(dd_task_list));
	overdue_ll->size = 0;
	overdue_ll->head = NULL;

	// Test variables for debugging
	uint32_t active_size;
	uint32_t completed_size;
	uint32_t overdue_size;

	while(1)
	{
		// Test variables for debugging
		active_size = active_ll->size;
		completed_size = completed_ll->size;
		overdue_size = overdue_ll->size;

		// Receive from DDS_Message queue
		if ( xQueueReceive(DDS_Message_Queue, &current_msg, portMAX_DELAY) )
		{
			current_time = xTaskGetTickCount();
			curr_task = active_ll->head->task;

			// Check if current task is overdue
			if ( active_ll->size > 0 )
			{
				if ( curr_task->absolute_deadline < current_time )
				{
					remove_task_ll( active_ll, curr_task );
					insert_task_ll(overdue_ll, curr_task );
					curr_task = active_ll->head->task;
				}
			}

			// Process received message
			switch (current_msg.type) {
				case CREATE:
					// Check if task will be overdue
					if (current_msg.task->absolute_deadline < current_time + current_msg.task->execution_time)
					{
						// overdue, add to overdue_ll
						insert_task_ll(overdue_ll, current_msg.task);
					}
					// Not overdue, add to active_ll
					else
					{
						if ( active_ll->size == 0 )
						{
							insert_sorted_task_ll(active_ll, current_msg.task);
							curr_task = active_ll->head->task;
						}
						else
						{
							insert_sorted_task_ll(active_ll, current_msg.task);
						}

						// Check that a task is running
						if ( active_ll->size > 0 )
						{
							// Check if task pre-empts current task
							if ( current_msg.task->absolute_deadline < curr_task->absolute_deadline )
							{
								vTaskPrioritySet(curr_task->t_handle, PRIORITY_MIN);
								vTaskSuspend(curr_task->t_handle);
							}
						}
					}
					break;
				case DELETE:
					// Remove from active_ll
					vTaskSuspend( curr_task->t_handle );
					remove_task_ll(active_ll, curr_task);
					vTaskPrioritySet(curr_task->t_handle, PRIORITY_MIN);
					curr_task->completion_time = xTaskGetTickCount();
					insert_task_ll(completed_ll, curr_task);
					printf("\nTask %u\nReleased:\t%u\nCompleted:\t%u\n",
							curr_task->task_id,
							curr_task->release_time,
							curr_task->completion_time);
					break;
				case ACTIVE:
					// return active LL size
					xQueueSend(DDS_Reply_Queue, &active_ll, 0);
					break;
				case COMPLETED:
					// return completed LL size
					xQueueSend(DDS_Reply_Queue, &completed_ll, 0);
					break;
				case OVERDUE:
					// return overdue LL size
					xQueueSend(DDS_Reply_Queue, &overdue_ll, 0);
				default:
					break;
			}

			if ( active_ll->size > 0 )
			{
				// Set head of active LL to highest priority task
				vTaskPrioritySet(active_ll->head->task->t_handle, PRIORITY_HIGH);

				// Resume highest priority user task
				vTaskResume(active_ll->head->task->t_handle);
			}
		}
	}
}

/*-----------------------------------------------------------*/

void User_Defined_Task_1( void *pvParameters )
{
	TickType_t execute_time 	= 0;
	TickType_t current_tick 	= 0;
	TickType_t previous_tick 	= 0;

	while(1)
	{
		current_tick = xTaskGetTickCount();
		execute_time = bench_array[0].execution_time;

		// Loop until task is completed
		while (0 < execute_time)
		{
			// Compare current and previous time, so task only progresses as ticks progress
			previous_tick = current_tick;
			current_tick = xTaskGetTickCount();
			if (current_tick != previous_tick)
			{
				execute_time--;
			}
		}

		// Send message to DD_Scheduler_Task to "delete" task, and suspend
		delete_dd_task(bench_array[0].task_id);
//		vTaskSuspend(NULL);
	}
}

/*-----------------------------------------------------------*/

void User_Defined_Task_2( void *pvParameters )
{
	TickType_t execute_time 	= 0;
	TickType_t current_tick 	= 0;
	TickType_t previous_tick 	= 0;

	while(1)
	{
		current_tick = xTaskGetTickCount();
		execute_time = bench_array[1].execution_time;

		// Loop until task is completed
		while (0 < execute_time)
		{
			// Compare current and previous time, so task only progresses as ticks progress
			previous_tick = current_tick;
			current_tick = xTaskGetTickCount();
			if (current_tick != previous_tick)
			{
				execute_time--;
			}
		}

		// send message to DD_Scheduler_Task to "delete" task
		delete_dd_task(bench_array[1].task_id);
//		vTaskSuspend(NULL);
	}
}

/*-----------------------------------------------------------*/

void User_Defined_Task_3( void *pvParameters )
{
	TickType_t execute_time 	= 0;
	TickType_t current_tick 	= 0;
	TickType_t previous_tick 	= 0;

	while(1)
	{
		current_tick = xTaskGetTickCount();
		execute_time = bench_array[2].execution_time;

		// Loop until task is completed
		while (0 < execute_time)
		{
			// Compare current and previous time, so task only progresses as ticks progress
			previous_tick = current_tick;
			current_tick = xTaskGetTickCount();
			if (current_tick != previous_tick)
			{
				execute_time--;
			}
		}

		// send message to DD_Scheduler_Task to "delete" task
		delete_dd_task(bench_array[2].task_id);
//		vTaskSuspend(NULL);
	}
}

/*--Timer-Callback-Functions---------------------------------*/

static void Task_1_Callback(TimerHandle_t pxTimer_Handle)
{
	uint16_t task_value = 1;

	xQueueSend(Task_Create_Queue, &task_value, 0);

	// check if periodic task
	if (bench_array[0].period > 0)
	{
		// periodic task, re-start timer
		xTimerStart(Task_1_Timer, 0);
	}
	vTaskResume(DD_Generator_Task_Handle);
}

static void Task_2_Callback(TimerHandle_t pxTimer_Handle)
{
	uint16_t task_value = 2;

	xQueueSend(Task_Create_Queue, &task_value, 0);
	//vTaskResume(DD_Generator_Task_Handle);

	// check if periodic task
	if (bench_array[1].period > 0)
	{
		// periodic task, re-start timer
		xTimerStart(Task_2_Timer, 0);
	}
	vTaskResume(DD_Generator_Task_Handle);
}

static void Task_3_Callback(TimerHandle_t pxTimer_Handle)
{
	uint16_t task_value = 3;

	xQueueSend(Task_Create_Queue, &task_value, 0);
	//vTaskResume(DD_Generator_Task_Handle);

	// check if periodic task
	if (bench_array[2].period > 0)
	{
		// periodic task, re-start timer
		xTimerStart(Task_3_Timer, 0);
	}
	vTaskResume(DD_Generator_Task_Handle);
}

/*--Deadline-Driven-Scheduler-Interface-Functions------------*/

// This function receives all of the information necessary to create a new dd_task struct (excluding
// the release time and completion time). The struct is packaged as a message and sent to a queue
// for the DDS to receive.
// Message:
//		The DDS should assign a release time to the new task, add the DD-Task to the Active Task List, sort
//		the list by deadline, then set the priorities of the User-Defined Tasks accordingly.
void create_dd_task( TaskHandle_t t_handle, task_type type, uint32_t task_id, uint32_t release_time, uint32_t absolute_deadline)
{
	// Create dd_task
	dd_task* new_task 			= (dd_task *)malloc(sizeof(dd_task));
	new_task->t_handle			= t_handle;
	new_task->type				= type;
	new_task->task_id 			= task_id;
	new_task->release_time 		= release_time;
	new_task->execution_time	= bench_array[task_id - 1].execution_time;
	new_task->absolute_deadline	= absolute_deadline;
	new_task->completion_time	= 0;

	// Package dd_task into message
	message msg;
	msg.type = CREATE;
	msg.task = new_task;

	// Send message to DDS queue
	xQueueSendToBack(DDS_Message_Queue, &msg, 0);
}

// This function receives the ID of the DD-Task which has completed its execution. The ID is packaged
// as a message and sent to a queue for the DDS to receive.
// Message:
// 		The DDS should assign a completion time to the newly-completed DD-Task, remove the DD-task
// 		from the Active Task List and add it to the Completed Task List. The DDS should also sort the Active
// 		Task List by deadline and then set the priorities of the User-Defined Tasks accordingly.
void delete_dd_task(uint32_t task_id)
{
	// Package dd_task into message
	message msg;
	msg.type = DELETE;
	msg.task = NULL;
	// Send message to DDS queue
	xQueueSend(DDS_Message_Queue, &msg, portMAX_DELAY);
}

// This function sends a message to a queue requesting the  Active Task List from the DDS. Once a
// response is received from the DDS, the function returns the list.
// Message:
// 		The DDS should send the Active Task List to a queue.
dd_task_list* get_active_dd_task_list()
{
	dd_task_list* active_list;
	message msg;
	msg.type = ACTIVE;

	// send message to DDS queue requesting active list
	xQueueSend(DDS_Message_Queue, &msg, portMAX_DELAY);

	// wait for reply from DDS
	xQueueReceive(DDS_Reply_Queue, &active_list, portMAX_DELAY);

	return active_list;
}

// This function sends a message to a queue requesting the Completed Task List from the DDS. Once
// a response is received from the DDS, the function returns the list.
// Message:
// 		The DDS should send the Completed Task List to a queue.
dd_task_list* get_completed_dd_task_list()
{
	dd_task_list* complete_list;
	message msg;
	msg.type = COMPLETED;

	// send message to DDS queue requesting complete list
	xQueueSend(DDS_Message_Queue, &msg, portMAX_DELAY);

	// wait for reply from DDS
	xQueueReceive(DDS_Reply_Queue, &complete_list, portMAX_DELAY);

	return complete_list;
}

// This function sends a message to a queue requesting the Overdue Task List from the DDS. Once a
// response is received from the DDS, the function returns the list.
// Message:
// 		The DDS should send the Overdue Task List to a queue.
dd_task_list* get_overdue_dd_task_list()
{
	dd_task_list* overdue_list;
	message msg;
	msg.type = OVERDUE;

	// send message to DDS queue requesting overdue list
	xQueueSend(DDS_Message_Queue, &msg, portMAX_DELAY);

	// wait for reply from DDS
	xQueueReceive(DDS_Reply_Queue, &overdue_list, portMAX_DELAY);

	return overdue_list;
}

/*-----------------------------------------------------------*/

/*--Deadline-Driven-Scheduler-Linked-List-Functions----------*/

void insert_task_ll(dd_task_list* task_ll , dd_task* task)
{
	// Create a new node
	dd_task_node* node = NULL;
	node = (dd_task_node *)malloc(sizeof(dd_task_node));
	node->task = task;
	node->next = NULL;

	// Check if LinkedList is empty, insert as head
	if (task_ll->head == NULL)
	{
		task_ll->head = node;
	}
	// Insert node as head
	else
	{
		node->next = task_ll->head;
		task_ll->head = node;
	}
	task_ll->size++;
}

void remove_task_ll(dd_task_list* task_ll, dd_task* task)
{
	dd_task_node* curr_node = task_ll->head;
	dd_task_node* prev_node = NULL;

	// Iterate through list
	while (curr_node != NULL)
	{
		// Check if current node matches correct task
		if (curr_node->task->task_id == task->task_id)
		{
			// Check if node is head, if so, make next node head
			if (curr_node == task_ll->head)
			{
				task_ll->head = curr_node->next;
			}
			else
			{
				prev_node->next = curr_node->next;
			}
			// Free node in memory and decrement LinkedList
			free(curr_node);
			task_ll->size--;
			return;
		}
		// Change cursor to next node in the LinkedList
		prev_node = curr_node;
		curr_node = curr_node->next;
	}
}

// TODO: Do we need a Semaphore to prevent insertion and deletion operating simultaneously?
// Sorts by EDF (Earliest Deadline First)
void insert_sorted_task_ll(dd_task_list* task_ll, dd_task* task)
{
	// Create a new node
	dd_task_node* node = NULL;
	node = (dd_task_node *)malloc(sizeof(dd_task_node));
	node->task = task;
	node->next = NULL;

	// Increment list size
	task_ll->size++;

	// Check if LinkedList is empty, if so, insert and set head to node
	if (task_ll->head == NULL)
	{
		task_ll->head = node;
		return;
	}

	// Create a temporary cursor/previous node
	dd_task_node* curr = task_ll->head;
	dd_task_node* prev = NULL;

	// Iterate through list until appropriate EDF spot is found, then insert new task
	while (curr != NULL)
	{
		// Check if new task deadline is earlier than the current node, insert if so
		if (task->absolute_deadline < curr->task->absolute_deadline)
		{
			// Check if cursor is pointing to head, change head to node if so
			if (curr == task_ll->head)
			{
				node->next = curr;
				task_ll->head = node;
				return;
			}

			node->next = curr;
			prev->next = node;
			break;
		}
		// If end of list, append task
		else if (curr->next == NULL)
		{
			curr->next = node;
			break;
		}
		// Move cursor to next node, and change previous
		prev = curr;
		curr = curr->next;
	}
}

/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
	/* The malloc failed hook is enabled by setting
	configUSE_MALLOC_FAILED_HOOK to 1 in FreeRTOSConfig.h.
	Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}

/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( xTaskHandle pxTask, signed char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected.  pxCurrentTCB can be
	inspected in the debugger if the task name passed into this function is
	corrupt. */
	for( ;; );
}

/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
volatile size_t xFreeStackSpace;

	/* The idle task hook is enabled by setting configUSE_IDLE_HOOK to 1 in
	FreeRTOSConfig.h.
	This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amount of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}
