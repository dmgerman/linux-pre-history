#ifndef __SPARC64_IPC_H__
#define __SPARC64_IPC_H__

/* 
 * These are used to wrap system calls on the sparc.
 *
 * See arch/sparc64/kernel/sys_sparc.c for ugly details..
 */
/* XXX 32-bit binary compatability... */
struct ipc_kludge {
	struct msgbuf *msgp;
	long msgtyp;
};

#define SEMOP		 1
#define SEMGET		 2
#define SEMCTL		 3
#define MSGSND		11
#define MSGRCV		12
#define MSGGET		13
#define MSGCTL		14
#define SHMAT		21
#define SHMDT		22
#define SHMGET		23
#define SHMCTL		24

#define IPCCALL(version,op)	((version)<<16 | (op))

#endif
