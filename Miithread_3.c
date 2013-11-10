/***************************************
Project:	Multi thread DOS_OS program
Author:		Miibotree
Time:		2013.11
***************************************/
#include <stdio.h>
#include <dos.h>
#include <string.h>
#include <stdlib.h>

#define null 		0
#define NTCB 		10
#define BUFFER_SIZE 1
#define NTEXT		1024
#define NBUF		10

#define GET_INDOS 	 0x34
#define GET_CRIT_ERR 0x5d06

/*thread state*/
#define FINISHED 	0
#define RUNNING 	1
#define READY		2
#define BLOCKED		3

/*message buffer*/
struct buffer
{
	int sender;
	int size;
	char text[NTEXT];
	struct buffer *next;
};

typedef struct
{
	int value;
	struct TCB *wq; /*thread block quene head node*/
}semaphore;

/*TCB structs*/
struct TCB{
	unsigned char far* stack;
	unsigned 	ss;
	unsigned 	sp;
	char 		state;
	char		name[30];
	struct TCB  *next;
	struct buffer *mq;	/*recv thread message quene head point*/
	semaphore mutex;	/*recv thread message quene mutex signal*/
	semaphore sm;		/*recv thread message quene count signal*/
}tcb[NTCB];

/*stack structs*/
struct int_regs{
	unsigned BP, DI, SI, DS, ES, DX, CX, BX, AX, IP, CS, Flags, Off, Seg;
};

typedef int (far *codeptr)(void);

void InitDos(void);
void initTcb();
void InitFreebuf(void);
int DosBusy(void);
void tcb_state(void);
int create(char *name, codeptr code, int stack);
void over();
void destroy();
int find();

void P(semaphore *sem);
void V(semaphore *sem);
void block(struct TCB **qp);
void wakeup_first(struct TCB **qp);

void interrupt (*old_int8)(void);
void interrupt new_int8(void);
void interrupt my_swtch(void);

void f1(void);
void f2(void);
void f3(void);
void f4(void);
void producer(void);
void consumer(void);
void send_message_proc();
void recv_message_proc();


struct buffer *getbuff_from_idle(void);
void insert(struct buffer **mq, struct buffer *buff);
void send(char *receiver, char *a, int size);
struct buffer *remov(struct buffer **mq, int sender);
int receive(char *sender, char *b);

char far *indos_ptr = 0;
char far *crit_err_ptr = 0;

int current = 1, timecount = 0;
int TL = 1;
int n = 0;
int g_cp_buffer = 0;

struct buffer *freebuf = NULL;

semaphore mutex 	 = {1, NULL};
semaphore fillCount  = {0, NULL};
semaphore emptyCount = {BUFFER_SIZE, NULL};
semaphore mutexb 	 = {1, NULL};			/*access the quene is mutually*/
semaphore mutexfb	 = {1, NULL};
semaphore sfb		 = {NBUF, NULL};		/*for the idle buffer quene*/

int main()
{
	/*InitDos get INDOS flag & srvious error flag address*/
	InitDos();
	/*TCB array init*/
	initTcb();
	InitFreebuf();

	old_int8 = getvect(8);

	/*Create 0# thread*/
	strcpy(tcb[0].name, "main");
	tcb[0].state = RUNNING;
	current = 0;
	tcb[0].ss = _SS;
	tcb[0].sp = _SP;

	/*   print a && b   */
	/*
	create("f1", (codeptr)f1, 1024);
	create("f2", (codeptr)f2, 1024);
	*/

	/*  mutex   */
	/*
	create("f3", (codeptr)f3, 1024);
	create("f4", (codeptr)f4, 1024);
	*/

	/*  producer_consumer problem   */
	/*
	create("producer", (codeptr)producer, 1024);
	create("consumer", (codeptr)consumer, 1024);
	*/

	create("send_message_proc", (codeptr)send_message_proc, 1024);
	create("recv_message_proc", (codeptr)recv_message_proc, 1024);

	tcb_state();

	/*start multi threads*/

	/*set a new alarm break deal proc
 	  it main contains my_swtch proc*/
	setvect(8, new_int8);
	my_swtch();

	while(!finished());

	tcb[0].state   = FINISHED;
	setvect(8, old_int8);
	tcb_state();
	printf("\nMulti_task system terminated. \n");
	return 0;
}

void InitDos(void)
{
	union REGS regs;
	struct SREGS segregs;

	regs.h.ah = GET_INDOS;
	intdosx(&regs, &regs, &segregs);
	indos_ptr = MK_FP(segregs.es, regs.x.bx);

	if (_osmajor < 3)
		crit_err_ptr = indos_ptr + 1;
	else if (_osmajor == 3 && _osminor == 0)
		crit_err_ptr = indos_ptr - 1;
	else
	{
		regs.x.ax = GET_CRIT_ERR;
		intdosx(&regs, &regs, &segregs);
		crit_err_ptr = MK_FP(segregs.ds, regs.x.si);
	}
}

int DosBusy(void)
{
	if (indos_ptr && crit_err_ptr)
		return (*indos_ptr || *crit_err_ptr);
	else
		return (-1);
}

void initTcb()
{
	int id;
	for (id = 0; id < NTCB; id++)
	{
		tcb[id].stack = NULL;
		tcb[id].ss = 0;
		tcb[id].sp = 0;
		tcb[id].state = FINISHED;
		tcb[id].name[0] = '\0';
		tcb[id].mq = NULL;
		tcb[id].mutex.value = 1;
		tcb[id].mutex.wq = NULL;
		tcb[id].sm.value = 0;
		tcb[id].sm.wq = NULL;
	}
}

void InitFreebuf(void)
{
	int i;
	struct buffer *temp = (struct buffer *)malloc(sizeof(struct buffer));
	freebuf = temp;
	for (i = 1; i < NBUF; i++)
	{
		freebuf->next = (struct buffer *)malloc(sizeof(struct buffer));
		freebuf = freebuf->next;
	}
	freebuf->next = NULL;
	freebuf = temp;
}

int create(char *name, codeptr code, int stack)
{
	/*init thread TCB*/
	int id;
	struct int_regs *thread_regs;

	/*find avail tcb id*/
	for (id = 1; id < NTCB; id++)
	{
		if (tcb[id].state == FINISHED)
			break;
	}
	if (id == NTCB)
	{
		printf("thread create failed\n");
		return -1;
	}

	else
	{
		/*create private stack for new thread*/
		tcb[id].stack = (unsigned char*)malloc(stack);

		thread_regs = (struct int_regs *)(tcb[id].stack + stack);
		thread_regs--;

		/*init TCB*/
		tcb[id].ss = FP_SEG(thread_regs);
		tcb[id].sp = FP_OFF(thread_regs);
		tcb[id].state = READY;
		strcpy(tcb[id].name, name);

		/*init registers*/
		thread_regs->CS = FP_SEG(code);
		thread_regs->IP = FP_OFF(code);
		thread_regs->DS = _DS;
		thread_regs->ES = _DS;
		thread_regs->Flags = 0x200;
		thread_regs->Seg = FP_SEG(over);
		thread_regs->Off = FP_OFF(over);

		return id;
	}
}

void over()
{
	destroy(current);

	/*CPU control*/
	my_swtch();
}

void destroy(int id)
{
	disable();
	free(tcb[id].stack);
	tcb[id].stack = NULL;
	tcb[id].state = FINISHED;
	/*tcb[id].name[0] = '\0';*/
	enable();
}

void f1(void)
{
	int i, j, k;
	for(i = 0; i < 20; i++)
	{
		putchar('a');
		for (j = 0; j < 1000; j++)
			for (k = 0; k < 10000; k++);
	}
	printf("\nf1 function end\n");
}

void f2(void)
{
	long i, j, k;
	for (i = 0; i < 1; i++)
	{
		putchar('b');
		for (j = 0; j < 1000; j++)
			for (k = 0; k < 10000; k++);
	}
	printf("\nf2 function end\n");
}

void f3()
{
	int p;
	int i = 0, j = 0, k = 0;
	for (i = 0; i < 10; i++)
	{
		P(&mutex);

		p = n;


		for  (j = 0; j < 1000; j++)
		    for (k = 0; k < 1000; k++);

		p = p + 1;


		for (j = 0; j < 10000; j++)
		    for (k = 0; k < 10000; k++);


		n = p;

		V(&mutex);

		printf("n=%d\n", n);
	}
}

void f4()
{
	int p;
	int i = 0, j = 0, k = 0;
	for (i = 0; i < 10; i++)
	{
		P(&mutex);

		p = n;
		for (j = 0; j < 10000; j++)
		    for (k = 0; k < 10000; k++);

		p = p + 1;

		for (j = 0; j < 10000; j++)
		    for (k = 0; k < 10000; k++);

		n = p;

		V(&mutex);

		printf("n=%d\n", n);
	}
}

void interrupt new_int8(void)
{
	int result = 0;

	(*old_int8)();
	timecount++;
	if (timecount == TL)
	{
		result = DosBusy();
		if (result == 0)	/*dos is not busy*/
			my_swtch();
		else if(result == 1)	/*dos is busy*/
			return;
	}
}

int find()
{
	int id;
	for (id = current + 1; id < NTCB; id++)
	{
		if (tcb[id].state == READY)
			return id;
	}

	for (id = 1; id <= current; id++)
	{
		if (tcb[id].state == READY)
			return id;
	}
	return -1;
}

void interrupt my_swtch(void)
{
	int i = 0;

	disable();
	tcb[current].ss = _SS;
	tcb[current].sp = _SP;
	if ( tcb[current].state == RUNNING )
		tcb[current].state = READY;

	i = find();
	if(i < 0)
		i = 0;
	/*here maybe the problem #0 */
	/*if ( !(i == 0 && tcb[1].state == FINISHED && tcb[2].state == FINISHED ))*/

	_SS = tcb[i].ss;
	_SP = tcb[i].sp;

	tcb[i].state = RUNNING;

	current = i;
	timecount = 0;

	enable();
}

int finished(void)
{
	int i = 1, j = 0, k = 0;

	for (i = 1; i < NTCB; i++)
	{
		if (tcb[i].state != FINISHED)
		{
			for(j = 0; j < 10000; j++)
			    for (k = 0; k < 10000; k++);
			return 0;
		}
	}
	return 1;
}


void tcb_state()
{
	int i = 0;
	for (i = 0; i < NTCB; i++)
	{
		printf("thread %d's name is %s\t", i, tcb[i].name);
		switch(tcb[i].state)
		{
		 case FINISHED: printf("finished\n");break;
		 case RUNNING:	printf("running\n");break;
		 case READY:    printf("ready\n");break;
		 case BLOCKED:  printf("blocked\n");break;
		 default:       printf("unknown\n");break;

		}
	}
}


void block(struct TCB **qp)
{
	struct TCB *tcbp;
	tcb[current].state = BLOCKED;
	if((*qp == NULL)) /*block quene is null*/
		(*qp) = &tcb[current];
	else
	{
		tcbp = (*qp);
		while(tcbp->next != NULL)
			tcbp = tcbp->next;
		tcbp->next = &tcb[current];
	}
	tcb[current].next = NULL;
	my_swtch();
}

void wakeup_first(struct TCB **qp)
{
	struct TCB *tcbp;
	if ((*qp) == NULL)
		return ;
	tcbp = *qp;
	(*qp) = (*qp)->next;

	tcbp->state = READY;
	tcbp->next  = NULL;
}

void P(semaphore *sem)
{
	struct TCB **qp;
	disable();
	sem->value = sem->value - 1;
	if (sem->value < 0)
	{
		qp = &(sem->wq);
		block(qp);
	}
	enable();
}

void V(semaphore *sem)
{
	struct TCB **qp;
	disable();
	qp = &(sem->wq);
	sem->value = sem->value + 1;
	if (sem->value <= 0)
		wakeup_first(qp);
	enable();
}

void producer(void)
{
	int i = 0;
	for (i = 1; i <= 10; i++)
	{
		/*wait for the buffer is null*/
		P(&emptyCount);
		P(&mutex);
		g_cp_buffer = i;
		printf("producer put the data %d into the buffer\n", i);
		V(&mutex);
		V(&fillCount);
	}


}

void consumer(void)
{
	int flag = 1;
	while(flag)
	{
		P(&fillCount);
		P(&mutex);
		printf("consumer get the data %d from the buffer\n", g_cp_buffer);
		if (g_cp_buffer == 10)
		{
		    flag = 0;
		}

		V(&mutex);
		V(&emptyCount);
	}
}

/*get a buffer from the global struct freebuf*/
struct buffer *getbuff_from_idle(void)
{
	struct buffer *buff;
	buff = freebuf;
	freebuf = freebuf->next;
	return(buff);
}

/* insert a struct buffer into the quene *mq */
void insert(struct buffer **mq, struct buffer *buff)
{
	struct buffer *temp;
	if (buff == NULL)	/*buff is NULL*/
		return;
	buff->next = NULL;
	if (*mq == NULL)
		*mq = buff;
	else
	{
		temp = *mq;		/*insert buffer into the tail of the *mq*/
		while(temp->next != NULL)
			temp = temp->next;
		temp->next = buff;
	}
}


void send(char *receiver, char *a, int size)
{
	struct buffer *buff;
	int i, id = -1;
	disable();
	for (i = 0; i < NTCB; i++)
	{
	/*if receiver thread is not exist, then return*/
		if (strcmp(receiver, tcb[i].name) == 0)
		{
			id = i;
			break;
		}
	}
	if (id == -1)
	{
		printf("Error: Receiver not exist!\n");
		enable();
		return;
	}
	/*get the idle message buffer*/
	P(&sfb);/*?what is the sfb for?*/
	P(&mutexfb);
	buff = getbuff_from_idle();
	V(&mutexfb);
	/*fill the message buffer info.*/
	buff->sender = current;
	buff->size = size;
	buff->next = NULL;
	for (i = 0; i < buff->size; i++, a++)
	{
		buff->text[i] = *a;
	}
	P(&tcb[id].mutex);

	/*insert buff into the receiver's quene*/
	insert(&(tcb[id].mq), buff);
	V(&tcb[id].mutex);
	V(&tcb[id].sm);

	enable();
}


struct buffer *remov(struct buffer **mq, int sender)
{
	struct buffer *temp, *find_buffer;
	temp = *mq;
	/*if is the head buffer*/
	if (temp->sender == sender)
	{
		find_buffer = temp;
		temp = temp->next;
		return (find_buffer);
	}
	else
	{
		while(temp->next != NULL)
		{
			/*sender represents where the message comes from*/
			if (temp->next->sender == sender)
			{
				find_buffer = temp->next;
				temp->next = temp->next->next;
				return (find_buffer);
			}
			else
			{
				temp = temp->next;
			}
		}
	}
	return 0;
}

/*argv sender means where the message comes from*/
int receive(char *sender, char *b)
{
	int i = 0, id = -1, size;
	struct buffer *get_buffer = NULL, *buff = NULL;

	disable();

	/*find the sender TCB*/
	for (i = 0; i < NTCB; i++)
	{
		if(strcmp(sender, tcb[i].name) == 0)
		{
			id = i;
			break;
		}
	}
	if (id == -1)
	{
		printf("Error: Sender not exist\n");
		enable();
		return;
	}

	/*judge message quene idle?*/

	P(&tcb[current].sm);
	P(&tcb[current].mutex);
	get_buffer = remov(&(tcb[current].mq), id);
	V(&tcb[current].mutex);

	if (get_buffer == NULL)
	{
		V(&tcb[current].sm);
		enable();
		return 0;
	}
	size = get_buffer->size;
	for (i = 0; i < get_buffer->size; i++,b++)
		*b = get_buffer->text[i];

	/*reinitial the buff and put it into the global buff: freebuf*/
	get_buffer->sender = -1;
	get_buffer->size = 0;
	get_buffer->text[0] = '\0';
	P(&mutexfb);
	insert(freebuf, get_buffer);
	V(&mutexfb);
	V(&sfb);
	enable();
	return size;
}

void send_message_proc()
{
	int i, j, k;
	char message[10] = "message";
	for (i = 0; i < 6; i++)
	{
		message[7] = i + '0';
		message[8] = '\0';
		send("recv_message_proc", message, strlen(message));
		printf("message %s has been sent.\n", message);
		if (i % 2 == 1)
			printf("\n");

		/*let CPU patch*/
		for (j = 0; j < 10000; j++)
		    for (k = 0; k < 10000; k++);
	}
	receive("recv_message_proc", message);
	if (message[0] == 'O' && message[1] == 'K')
		printf("All the message have been received, Successful!\n");
	else
		printf("Not all the message have been received, Failed!\n");
}

void recv_message_proc()
{
	int i, j, k;
	int size;
	char message[10];
	printf("\n");
	for (i = 0; i < 6; i++)
	{
		/*while loop until receive the message*/
		while( (size = receive("send_message_proc", message)) == 0 )
		{
			;
		}
		message[size] = '\0';
		printf("message %s has been received\n", message);
		if (i % 2 == 1)
			printf("\n");
		for (j = 0; j < 10000; j++)
		    for (k = 0; k < 10000; k++);
	}
	strcpy(message, "OK");
	send("send_message_proc", message, strlen(message));
}
