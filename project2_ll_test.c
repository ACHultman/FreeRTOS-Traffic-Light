#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>

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
	char * 	t_handle;
	int 		task_id;
	int		period;
	int		execution_time;
} bench_task;

// Deadline-Driven Task data structure
typedef struct dd_task {
	char * 	t_handle;
	task_type 		type;
	int 		task_id;
	int 		release_time;
	int		execution_time;
	int 		absolute_deadline;
	int 		completion_time;
} dd_task;

typedef struct dd_task_node {
	dd_task 		task;
	struct dd_task_node*	next;
} dd_task_node;

// Singly-linked List data structure for dd_tasks
typedef struct dd_task_list {
	dd_task_node* 	head;
	int		size;
} dd_task_list;

// Messages in the DDS Message Queue
typedef struct message {
	message_type 	type;
	dd_task			task;
} message;

// Reply from the DDS to the Monitor Task
// Holds message type and a reference to the dd_task_list
typedef struct reply_message {
	message_type 	type;
	dd_task_list**	list;
} reply_message;

/*--Deadline-Driven-Scheduler-Linked-List-Functions----------*/

// TODO fix dd_task_list pointer param
void insert_task_ll(dd_task_list* task_ll , dd_task task)
{
	dd_task_node * node = NULL;
	node = (dd_task_node *)malloc(sizeof(dd_task_node));
	node->task = task;
	
	node->next = NULL;

	if (task_ll->head == NULL)
	{
		// empty list
		task_ll->head = node;
		task_ll->size++;
		return;
	}

	// make task new head
	node->next = task_ll->head;
	task_ll->head = node;
	task_ll->size++;
}

void remove_task_ll(dd_task_list* task_ll, dd_task task)
{
	dd_task_node * node = NULL;
	node = task_ll->head;
	dd_task_node * prev_node = NULL;

	while (node != NULL)
	{
		if (node->task.task_id == task.task_id)
		{
			// found task
			if (node == task_ll->head)
			{
				// task is head
				// make next node head
				task_ll->head = node->next;
				// free node from mem
				free(node);
				// decrement size
				task_ll->size--;
				return;
			}
			else
			{
				// task is not head
				// make prev node's next node the node after node
				prev_node->next = node->next;

				// free node from mem
				free(node);
				// decrement size
				task_ll->size--;
				return;
			}
		}

		prev_node = node;
		// move to next node
		node = node->next;
	}
}

void insert_sorted_task_ll(dd_task_list* task_ll, dd_task task)
{
	dd_task_node * node = NULL;
	node = (dd_task_node *)malloc(sizeof(dd_task_node));
	node->task = task;
	node->next = NULL;

	if (task_ll->head == NULL)
	{
		task_ll->head = node;
		task_ll->size++;
		return;
	}

	dd_task_node * temp = task_ll->head;

	if (task.absolute_deadline < temp->task.absolute_deadline)
	{
		// new task has a smaller (earlier) deadline than temp
		// make task new head
		node->next = temp;
		task_ll->head = node;
		task_ll->size++;
		return;
	}

	while (temp != NULL)
	{
		if (task.absolute_deadline < temp->task.absolute_deadline)
		{
			// insert new node before temp
			node->next = temp;
			break;
		}
		else if (temp->next == NULL)
		{
			// last node in list
			temp->next = node;
			break;
		}
		// move to next node
		temp = temp->next;
	}

	task_ll->size++;
}

void DD_Monitor_Task(dd_task_list** overdueTask, dd_task_list** completedTasks, dd_task_list** activeTasks)
{
    printf("=========================");
	printf("\nActive Tasks:\n");
	printf("\tSize: %d\n", (*activeTasks)->size);
//	printf("\tHead: %s\n", (*activeTasks)->head->task.t_handle);
	printf("\nCompleted Tasks:\n");
	printf("\tSize: %d\n", (*completedTasks)->size);
//	printf("\tHead: %s\n", (*completedTasks)->head->task.t_handle);
	printf("\nOverdue Tasks:\n");
	printf("\tSize: %d\n", (*overdueTask)->size);
	printf("\tHead: %s\n", (*overdueTask)->head->task.t_handle);
    printf("=========================");
}

int main()
{
    printf("Starting...\n");
    
    dd_task_list* active_ll = malloc(sizeof(dd_task_list));
	active_ll->size = 0;
	active_ll->head = NULL;

	dd_task_list* completed_ll = malloc(sizeof(dd_task_list));
	completed_ll->size = 0;
	completed_ll->head = NULL;

	dd_task_list* overdue_ll = malloc(sizeof(dd_task_list));
	overdue_ll->size = 0;
	overdue_ll->head = NULL;
	
	dd_task task_1;
	task_1.t_handle			= "1";
	task_1.type				= PERIODIC;
	task_1.task_id 			= 1;
	task_1.release_time 		= 0;
	task_1.execution_time		= 100;
	task_1.absolute_deadline	= 200;
	task_1.completion_time	= 0;
	
	dd_task task_2;
	task_2.t_handle			= "2";
	task_2.type				= PERIODIC;
	task_2.task_id 			= 2;
	task_2.release_time 		= 0;
	task_2.execution_time		= 100;
	task_2.absolute_deadline	= 100;
	task_2.completion_time	= 0;
	
	insert_sorted_task_ll(overdue_ll, task_2);
	insert_sorted_task_ll(overdue_ll, task_1);
	
	printf("\n%d\n", overdue_ll->size);
	DD_Monitor_Task(&overdue_ll, &completed_ll, &active_ll);
	
	remove_task_ll(overdue_ll, task_2);
	
	printf("\n%d\n", overdue_ll->size);
    DD_Monitor_Task(&overdue_ll, &completed_ll, &active_ll);
    
    return 0;
}
