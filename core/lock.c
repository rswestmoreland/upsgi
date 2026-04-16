#include "upsgi.h"

extern struct upsgi_server upsgi;

#define UPSGI_LOCK_ITEM_ENGINE_DEFAULT 0
#define UPSGI_LOCK_ITEM_ENGINE_FDLOCK 1

static void upsgi_set_thunder_lock_backend_state(int backend, char *reason, int has_owner_dead_recovery) {
	upsgi.thunder_lock_backend = backend;
	upsgi.thunder_lock_backend_reason = reason;
	upsgi.thunder_lock_backend_has_owner_dead_recovery = has_owner_dead_recovery;
}

static struct upsgi_lock_item *upsgi_register_lock(char *id, int rw) {

	struct upsgi_lock_item *uli = upsgi.registered_locks;
	if (!uli) {
		upsgi.registered_locks = upsgi_malloc_shared(sizeof(struct upsgi_lock_item));
		upsgi.registered_locks->id = id;
		upsgi.registered_locks->pid = 0;
		upsgi.registered_locks->lock_engine = 0;
		if (rw) {
			upsgi.registered_locks->lock_ptr = upsgi_malloc_shared(upsgi.rwlock_size);
		}
		else {
			upsgi.registered_locks->lock_ptr = upsgi_malloc_shared(upsgi.lock_size);
		}
		upsgi.registered_locks->rw = rw;
		upsgi.registered_locks->next = NULL;
		return upsgi.registered_locks;
	}

	while (uli) {
		if (!uli->next) {
			uli->next = upsgi_malloc_shared(sizeof(struct upsgi_lock_item));
			if (rw) {
				uli->next->lock_ptr = upsgi_malloc_shared(upsgi.rwlock_size);
			}
			else {
				uli->next->lock_ptr = upsgi_malloc_shared(upsgi.lock_size);
			}
			uli->next->id = id;
			uli->next->pid = 0;
			uli->next->lock_engine = 0;
			uli->next->rw = rw;
			uli->next->next = NULL;
			return uli->next;
		}
		uli = uli->next;
	}

	upsgi_log("*** DANGER: unable to allocate lock %s ***\n", id);
	exit(1);

}

static void upsgi_mutex_fatal(const char *op, struct upsgi_lock_item *uli, int ret) {
	const char *lock_id = "unknown";
	if (uli && uli->id) {
		lock_id = uli->id;
	}
	upsgi_log("mutex %s failed for lock %s: %s\n", op, lock_id, strerror(ret));
	exit(1);
}

#ifdef UPSGI_LOCK_USE_MUTEX

#ifdef OBSOLETE_LINUX_KERNEL
#undef EOWNERDEAD
#endif

#ifdef EOWNERDEAD
#define UPSGI_LOCK_ENGINE_NAME "pthread robust mutexes"
int upsgi_pthread_robust_mutexes_enabled = 1;
#else
#define UPSGI_LOCK_ENGINE_NAME "pthread mutexes"
#endif

#define UPSGI_LOCK_SIZE	sizeof(pthread_mutex_t)

#ifdef OBSOLETE_LINUX_KERNEL
#define UPSGI_RWLOCK_SIZE	sizeof(pthread_mutex_t)
#else
#define UPSGI_RWLOCK_SIZE	sizeof(pthread_rwlock_t)
#endif

#ifndef PTHREAD_PRIO_INHERIT
int pthread_mutexattr_setprotocol (pthread_mutexattr_t *__attr,
                                          int __protocol);
#define PTHREAD_PRIO_INHERIT 1
#endif

// REMEMBER lock must contains space for both pthread_mutex_t and pthread_mutexattr_t !!! 
struct upsgi_lock_item *upsgi_lock_fast_init(char *id) {

	pthread_mutexattr_t attr;

	struct upsgi_lock_item *uli = upsgi_register_lock(id, 0);

#ifdef EOWNERDEAD
retry:
#endif
	if (pthread_mutexattr_init(&attr)) {
		upsgi_log("unable to allocate mutexattr structure\n");
		exit(1);
	}

	if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) {
		upsgi_log("unable to share mutex\n");
		exit(1);
	}

#ifdef EOWNERDEAD
	if (upsgi_pthread_robust_mutexes_enabled) {
		int ret;
		if ((ret = pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT)) != 0) {
			switch (ret) {
			case ENOTSUP:
				// PTHREAD_PRIO_INHERIT will only prevent
				// priority inversion when SCHED_FIFO or
				// SCHED_RR is used, so this is non-fatal and
				// also currently unsupported on musl.
				break;
			default:
				upsgi_log("unable to set PTHREAD_PRIO_INHERIT\n");
				exit(1);
			}
		}
		if (pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST)) {
			upsgi_log("unable to make the mutex 'robust'\n");
			exit(1);
		}
	}
#endif

	if (pthread_mutex_init((pthread_mutex_t *) uli->lock_ptr, &attr)) {
#ifdef EOWNERDEAD
		if (upsgi_pthread_robust_mutexes_enabled) {
			upsgi_log("!!! it looks like your kernel does not support pthread robust mutexes !!!\n");
			upsgi_log("!!! falling back to standard pthread mutexes !!!\n");
			upsgi_pthread_robust_mutexes_enabled = 0;
			pthread_mutexattr_destroy(&attr);
			goto retry;
		}
#endif
		upsgi_log("unable to initialize mutex\n");
		exit(1);
	}

	pthread_mutexattr_destroy(&attr);

#ifdef EOWNERDEAD
	if (!upsgi_pthread_robust_mutexes_enabled) {
		uli->can_deadlock = 1;
	}
#else
	uli->can_deadlock = 1;
#endif

	return uli;
}

pid_t upsgi_lock_fast_check(struct upsgi_lock_item * uli) {
	if (uli->lock_engine == UPSGI_LOCK_ITEM_ENGINE_FDLOCK) {
		return upsgi_lock_fd_check(uli);
	}

	int ret = pthread_mutex_trylock((pthread_mutex_t *) uli->lock_ptr);
	if (ret == 0) {
		ret = pthread_mutex_unlock((pthread_mutex_t *) uli->lock_ptr);
		if (ret != 0) {
			upsgi_mutex_fatal("unlock", uli, ret);
		}
		return 0;
	}
#ifdef EOWNERDEAD
	if (ret == EOWNERDEAD) {
		upsgi_log("[deadlock-detector] a process holding a robust mutex died during lock check. recovering...\n");
		pthread_mutex_consistent((pthread_mutex_t *) uli->lock_ptr);
		ret = pthread_mutex_unlock((pthread_mutex_t *) uli->lock_ptr);
		if (ret != 0) {
			upsgi_mutex_fatal("unlock", uli, ret);
		}
		return 0;
	}
	if (ret == ENOTRECOVERABLE) {
		upsgi_mutex_fatal("trylock", uli, ret);
	}
#endif
	if (ret != EBUSY) {
		upsgi_mutex_fatal("trylock", uli, ret);
	}
	return uli->pid;
}

pid_t upsgi_rwlock_fast_check(struct upsgi_lock_item * uli) {
#ifdef OBSOLETE_LINUX_KERNEL
	return upsgi_lock_fast_check(uli);
#else

	if (pthread_rwlock_trywrlock((pthread_rwlock_t *) uli->lock_ptr) == 0) {
		pthread_rwlock_unlock((pthread_rwlock_t *) uli->lock_ptr);
		return 0;
	}
	return uli->pid;
#endif
}


void upsgi_lock_fast(struct upsgi_lock_item *uli) {
	if (uli->lock_engine == UPSGI_LOCK_ITEM_ENGINE_FDLOCK) {
		upsgi_lock_fd(uli);
		return;
	}

	int ret = pthread_mutex_lock((pthread_mutex_t *) uli->lock_ptr);
#ifdef EOWNERDEAD
	if (ret == EOWNERDEAD) {
		upsgi_log("[deadlock-detector] a process holding a robust mutex died. recovering...\n");
		pthread_mutex_consistent((pthread_mutex_t *) uli->lock_ptr);
	}
	else if (ret == ENOTRECOVERABLE) {
		upsgi_mutex_fatal("lock", uli, ret);
	}
	else if (ret != 0) {
		upsgi_mutex_fatal("lock", uli, ret);
	}
#else
	if (ret != 0) {
		upsgi_mutex_fatal("lock", uli, ret);
	}
#endif
	uli->pid = upsgi.mypid;
}

void upsgi_unlock_fast(struct upsgi_lock_item *uli) {
	if (uli->lock_engine == UPSGI_LOCK_ITEM_ENGINE_FDLOCK) {
		upsgi_unlock_fd(uli);
		return;
	}

	int ret = pthread_mutex_unlock((pthread_mutex_t *) uli->lock_ptr);
	if (ret != 0) {
		upsgi_mutex_fatal("unlock", uli, ret);
	}
	uli->pid = 0;

}

void upsgi_rlock_fast(struct upsgi_lock_item *uli) {
#ifdef OBSOLETE_LINUX_KERNEL
	upsgi_lock_fast(uli);
#else
	pthread_rwlock_rdlock((pthread_rwlock_t *) uli->lock_ptr);
	uli->pid = upsgi.mypid;
#endif
}

void upsgi_wlock_fast(struct upsgi_lock_item *uli) {
#ifdef OBSOLETE_LINUX_KERNEL
	upsgi_lock_fast(uli);
#else
	pthread_rwlock_wrlock((pthread_rwlock_t *) uli->lock_ptr);
	uli->pid = upsgi.mypid;
#endif
}

void upsgi_rwunlock_fast(struct upsgi_lock_item *uli) {
#ifdef OBSOLETE_LINUX_KERNEL
	upsgi_unlock_fast(uli);
#else
	pthread_rwlock_unlock((pthread_rwlock_t *) uli->lock_ptr);
	uli->pid = 0;
#endif
}

struct upsgi_lock_item *upsgi_rwlock_fast_init(char *id) {

#ifdef OBSOLETE_LINUX_KERNEL
	return upsgi_lock_fast_init(id);
#else

	pthread_rwlockattr_t attr;

	struct upsgi_lock_item *uli = upsgi_register_lock(id, 1);

	if (pthread_rwlockattr_init(&attr)) {
		upsgi_log("unable to allocate rwlock structure\n");
		exit(1);
	}

	if (pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) {
		upsgi_log("unable to share rwlock\n");
		exit(1);
	}

	if (pthread_rwlock_init((pthread_rwlock_t *) uli->lock_ptr, &attr)) {
		upsgi_log("unable to initialize rwlock\n");
		exit(1);
	}

	pthread_rwlockattr_destroy(&attr);

	uli->can_deadlock = 1;

	return uli;
#endif



}



#elif defined(UPSGI_LOCK_USE_UMTX)

/* Warning: FreeBSD is still not ready for process-shared UMTX */

#include <machine/atomic.h>
#include <sys/umtx.h>

#define UPSGI_LOCK_SIZE		sizeof(struct umtx)
#define UPSGI_RWLOCK_SIZE	sizeof(struct umtx)
#define UPSGI_LOCK_ENGINE_NAME	"FreeBSD umtx"

struct upsgi_lock_item *upsgi_rwlock_fast_init(char *id) {
	return upsgi_lock_fast_init(id);
}
void upsgi_rlock_fast(struct upsgi_lock_item *uli) {
	upsgi_lock_fast(uli);
}
void upsgi_wlock_fast(struct upsgi_lock_item *uli) {
	upsgi_lock_fast(uli);
}
void upsgi_rwunlock_fast(struct upsgi_lock_item *uli) {
	upsgi_unlock_fast(uli);
}

struct upsgi_lock_item *upsgi_lock_fast_init(char *id) {
	struct upsgi_lock_item *uli = upsgi_register_lock(id, 0);
	umtx_init((struct umtx *) uli->lock_ptr);
	return uli;
}

void upsgi_lock_fast(struct upsgi_lock_item *uli) {
	umtx_lock((struct umtx *) uli->lock_ptr, (u_long) getpid());
	uli->pid = upsgi.mypid;
}

void upsgi_unlock_fast(struct upsgi_lock_item *uli) {
	umtx_unlock((struct umtx *) uli->lock_ptr, (u_long) getpid());
	uli->pid = 0;
}

pid_t upsgi_lock_fast_check(struct upsgi_lock_item *uli) {
	if (umtx_trylock((struct umtx *) uli->lock_ptr, (u_long) getpid())) {
		umtx_unlock((struct umtx *) uli->lock_ptr, (u_long) getpid());
		return 0;
	}
	return uli->pid;
}

pid_t upsgi_rwlock_fast_check(struct upsgi_lock_item * uli) {
	return upsgi_lock_fast_check(uli);
}

#elif defined(UPSGI_LOCK_USE_POSIX_SEM)

#define UPSGI_LOCK_SIZE         sizeof(sem_t)
#define UPSGI_RWLOCK_SIZE       sizeof(sem_t)
#define UPSGI_LOCK_ENGINE_NAME  "POSIX semaphores"

#include <semaphore.h>

struct upsgi_lock_item *upsgi_lock_fast_init(char *id) {
	struct upsgi_lock_item *uli = upsgi_register_lock(id, 0);
	sem_init((sem_t *) uli->lock_ptr, 1, 1);
	uli->can_deadlock = 1;
	return uli;
}

struct upsgi_lock_item *upsgi_rwlock_fast_init(char *id) {
	return upsgi_lock_fast_init(id);
}

void upsgi_lock_fast(struct upsgi_lock_item *uli) {
	sem_wait((sem_t *) uli->lock_ptr);
	uli->pid = upsgi.mypid;
}

void upsgi_unlock_fast(struct upsgi_lock_item *uli) {
	sem_post((sem_t *) uli->lock_ptr);
	uli->pid = 0;
}

pid_t upsgi_lock_fast_check(struct upsgi_lock_item *uli) {
	if (sem_trywait((sem_t *) uli->lock_ptr) == 0) {
		sem_post((sem_t *) uli->lock_ptr);
		return 0;
	}
	return uli->pid;
}

pid_t upsgi_rwlock_fast_check(struct upsgi_lock_item * uli) {
	return upsgi_lock_fast_check(uli);
}
void upsgi_rlock_fast(struct upsgi_lock_item *uli) {
	upsgi_lock_fast(uli);
}
void upsgi_wlock_fast(struct upsgi_lock_item *uli) {
	upsgi_lock_fast(uli);
}
void upsgi_rwunlock_fast(struct upsgi_lock_item *uli) {
	upsgi_unlock_fast(uli);
}


#elif defined(UPSGI_LOCK_USE_OSX_SPINLOCK)

#define UPSGI_LOCK_ENGINE_NAME "OSX spinlocks"
#define UPSGI_LOCK_SIZE		sizeof(OSSpinLock)
#define UPSGI_RWLOCK_SIZE	sizeof(OSSpinLock)


struct upsgi_lock_item *upsgi_lock_fast_init(char *id) {

	struct upsgi_lock_item *uli = upsgi_register_lock(id, 0);
	memset(uli->lock_ptr, 0, UPSGI_LOCK_SIZE);
	uli->can_deadlock = 1;
	return uli;
}

void upsgi_lock_fast(struct upsgi_lock_item *uli) {

	OSSpinLockLock((OSSpinLock *) uli->lock_ptr);
	uli->pid = upsgi.mypid;
}

void upsgi_unlock_fast(struct upsgi_lock_item *uli) {

	OSSpinLockUnlock((OSSpinLock *) uli->lock_ptr);
	uli->pid = 0;
}

pid_t upsgi_lock_fast_check(struct upsgi_lock_item *uli) {
	if (OSSpinLockTry((OSSpinLock *) uli->lock_ptr)) {
		OSSpinLockUnlock((OSSpinLock *) uli->lock_ptr);
		return 0;
	}
	return uli->pid;
}

struct upsgi_lock_item *upsgi_rwlock_fast_init(char *id) {
	struct upsgi_lock_item *uli = upsgi_register_lock(id, 1);
	memset(uli->lock_ptr, 0, UPSGI_LOCK_SIZE);
	uli->can_deadlock = 1;
	return uli;
}

void upsgi_rlock_fast(struct upsgi_lock_item *uli) {
	upsgi_lock_fast(uli);
}
void upsgi_wlock_fast(struct upsgi_lock_item *uli) {
	upsgi_lock_fast(uli);
}

pid_t upsgi_rwlock_fast_check(struct upsgi_lock_item *uli) {
	return upsgi_lock_fast_check(uli);
}

void upsgi_rwunlock_fast(struct upsgi_lock_item *uli) {
	upsgi_unlock_fast(uli);
}

#elif defined(UPSGI_LOCK_USE_WINDOWS_MUTEX)

#define UPSGI_LOCK_ENGINE_NAME "windows mutexes"
#define UPSGI_LOCK_SIZE         sizeof(HANDLE)
#define UPSGI_RWLOCK_SIZE       sizeof(HANDLE)


struct upsgi_lock_item *upsgi_lock_fast_init(char *id) {

        struct upsgi_lock_item *uli = upsgi_register_lock(id, 0);
	struct _SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(struct _SECURITY_ATTRIBUTES));
	sa.bInheritHandle = 1;
	uli->lock_ptr = CreateMutex(&sa, FALSE, NULL);
        return uli;
}

void upsgi_lock_fast(struct upsgi_lock_item *uli) {
	WaitForSingleObject(uli->lock_ptr, INFINITE);
        uli->pid = upsgi.mypid;
}

void upsgi_unlock_fast(struct upsgi_lock_item *uli) {
	ReleaseMutex(uli->lock_ptr);
        uli->pid = 0;
}

pid_t upsgi_lock_fast_check(struct upsgi_lock_item *uli) {
	if (WaitForSingleObject(uli->lock_ptr, 0) == WAIT_TIMEOUT) {
		return 0;
	}
        return uli->pid;
}

struct upsgi_lock_item *upsgi_rwlock_fast_init(char *id) {
	return upsgi_lock_fast_init(id);
}

void upsgi_rlock_fast(struct upsgi_lock_item *uli) {
	upsgi_lock_fast(uli);
}
void upsgi_wlock_fast(struct upsgi_lock_item *uli) {
	upsgi_lock_fast(uli);
}

pid_t upsgi_rwlock_fast_check(struct upsgi_lock_item *uli) {
	return upsgi_lock_fast_check(uli);
}

void upsgi_rwunlock_fast(struct upsgi_lock_item *uli) {
	upsgi_unlock_fast(uli);
}


#else

#define upsgi_lock_fast_init upsgi_lock_ipcsem_init
#define upsgi_lock_fast_check upsgi_lock_ipcsem_check
#define upsgi_lock_fast upsgi_lock_ipcsem
#define upsgi_unlock_fast upsgi_unlock_ipcsem

#define upsgi_rwlock_fast_init upsgi_rwlock_ipcsem_init
#define upsgi_rwlock_fast_check upsgi_rwlock_ipcsem_check

#define upsgi_rlock_fast upsgi_rlock_ipcsem
#define upsgi_wlock_fast upsgi_wlock_ipcsem
#define upsgi_rwunlock_fast upsgi_rwunlock_ipcsem

#define UPSGI_LOCK_SIZE sizeof(int)
#define UPSGI_RWLOCK_SIZE sizeof(int)

#define UPSGI_LOCK_ENGINE_NAME "ipcsem"

#endif

#ifdef __RUMP__
int semctl(int _0, int _1, int _2, ...) {
	return 0;
}
int semget(key_t _0, int _1, int _2) {
	return 0;
}
int semop(int _0, struct sembuf * _1, size_t _2) {
	return 0;
}
#undef UPSGI_LOCK_ENGINE_NAME
#define UPSGI_LOCK_ENGINE_NAME "fake"
#endif

struct upsgi_lock_item *upsgi_lock_ipcsem_init(char *id) {

	// used by ftok
	static int counter = 1;
	union semun {
		int val;
		struct semid_ds *buf;
		ushort *array;
	} semu;
	int semid;
	key_t myKey;

	struct upsgi_lock_item *uli = upsgi_register_lock(id, 0);

	if (upsgi.ftok) {
		myKey = ftok(upsgi.ftok, counter);
		if (myKey < 0) {
			upsgi_error("upsgi_lock_ipcsem_init()/ftok()");
			exit(1);
		}
		counter++;
		semid = semget(myKey, 1, IPC_CREAT | 0666);
	}
	else {
		semid = semget(IPC_PRIVATE, 1, IPC_CREAT | IPC_EXCL | 0666);
	}

	if (semid < 0) {
		upsgi_error("upsgi_lock_ipcsem_init()/semget()");
		exit(1);
	}
	// do this now, to allows triggering of atexit hook in case of problems
	memcpy(uli->lock_ptr, &semid, sizeof(int));

	semu.val = 1;
	if (semctl(semid, 0, SETVAL, semu)) {
		upsgi_error("upsgi_lock_ipcsem_init()/semctl()");
		exit(1);
	}

	return uli;
}

void upsgi_lock_ipcsem(struct upsgi_lock_item *uli) {

	int semid;
	struct sembuf sb;
	sb.sem_num = 0;
	sb.sem_op = -1;
	sb.sem_flg = SEM_UNDO;

	memcpy(&semid, uli->lock_ptr, sizeof(int));

retry:
	if (semop(semid, &sb, 1)) {
		if (errno == EINTR) goto retry; 
		upsgi_error("upsgi_lock_ipcsem()/semop()");
#ifdef EIDRM
		if (errno == EIDRM) {
			exit(UPSGI_BRUTAL_RELOAD_CODE);
		}
#endif
		exit(1);
	}
}

void upsgi_unlock_ipcsem(struct upsgi_lock_item *uli) {

	int semid;
	struct sembuf sb;
	sb.sem_num = 0;
	sb.sem_op = 1;
	sb.sem_flg = SEM_UNDO;

	memcpy(&semid, uli->lock_ptr, sizeof(int));

retry:
	if (semop(semid, &sb, 1)) {
		if (errno == EINTR) goto retry; 
		upsgi_error("upsgi_unlock_ipcsem()/semop()");
#ifdef EIDRM
		if (errno == EIDRM) {
			exit(UPSGI_BRUTAL_RELOAD_CODE);
		}
#endif
		exit(1);
	}

}

struct upsgi_lock_item *upsgi_rwlock_ipcsem_init(char *id) {
	return upsgi_lock_ipcsem_init(id);
}
void upsgi_rlock_ipcsem(struct upsgi_lock_item *uli) {
	upsgi_lock_ipcsem(uli);
}
void upsgi_wlock_ipcsem(struct upsgi_lock_item *uli) {
	upsgi_lock_ipcsem(uli);
}
void upsgi_rwunlock_ipcsem(struct upsgi_lock_item *uli) {
	upsgi_unlock_ipcsem(uli);
}

// ipc cannot deadlock
pid_t upsgi_lock_ipcsem_check(struct upsgi_lock_item *uli) {
	return 0;
}

void upsgi_ipcsem_clear(void) {

	if (upsgi.persistent_ipcsem) return;

	struct upsgi_lock_item *uli = upsgi.registered_locks;

	if (!upsgi.workers)
		goto clear;

	if (upsgi.mywid == 0)
		goto clear;

	if (upsgi.master_process && getpid() == upsgi.workers[0].pid)
		goto clear;

	if (!upsgi.master_process && upsgi.mywid == 1)
		goto clear;

	return;

clear:

#ifdef UPSGI_DEBUG
	upsgi_log("removing sysvipc semaphores...\n");
#endif
#ifdef GETPID
	while (uli) {
		int semid = 0;
		memcpy(&semid, uli->lock_ptr, sizeof(int));
		int ret = semctl(semid, 0, GETPID);
		if (ret > 0) {
			if (ret != (int) getpid() && !kill((pid_t) ret, 0)) {
				upsgi_log("found ipcsem mapped to alive pid %d. skipping ipcsem removal.\n", ret);
				return;
			}
		}
		uli = uli->next;
	}
	uli = upsgi.registered_locks;
#endif
	while (uli) {
		int semid = 0;
		memcpy(&semid, uli->lock_ptr, sizeof(int));
		if (semctl(semid, 0, IPC_RMID)) {
			upsgi_error("upsgi_ipcsem_clear()/semctl()");
		}
		uli = uli->next;
	}
}


pid_t upsgi_rwlock_ipcsem_check(struct upsgi_lock_item *uli) {
	return upsgi_lock_ipcsem_check(uli);
}

static int upsgi_lock_fd_get(struct upsgi_lock_item *uli) {
	int fd = -1;
	memcpy(&fd, uli->lock_ptr, sizeof(int));
	return fd;
}

struct upsgi_lock_item *upsgi_lock_fd_init(char *id) {
	struct upsgi_lock_item *uli = upsgi_register_lock(id, 0);
	int fd = upsgi_tmpfd();
	if (fd < 0) {
		upsgi_error("upsgi_lock_fd_init()/upsgi_tmpfd()");
		exit(1);
	}
	memcpy(uli->lock_ptr, &fd, sizeof(int));
	uli->lock_engine = UPSGI_LOCK_ITEM_ENGINE_FDLOCK;
	uli->can_deadlock = 0;
	return uli;
}

pid_t upsgi_lock_fd_check(struct upsgi_lock_item *uli) {
	int fd = upsgi_lock_fd_get(uli);
	struct flock fl;
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	if (fcntl(fd, F_SETLK, &fl) == 0) {
		fl.l_type = F_UNLCK;
		if (fcntl(fd, F_SETLK, &fl) < 0) {
			upsgi_error("upsgi_lock_fd_check()/fcntl(F_UNLCK)");
			exit(1);
		}
		return 0;
	}
	if (errno == EACCES || errno == EAGAIN) {
		return 1;
	}
	upsgi_error("upsgi_lock_fd_check()/fcntl(F_SETLK)");
	exit(1);
}

void upsgi_lock_fd(struct upsgi_lock_item *uli) {
	int fd = upsgi_lock_fd_get(uli);
	struct flock fl;
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
retry:
	if (fcntl(fd, F_SETLKW, &fl) < 0) {
		if (errno == EINTR) goto retry;
		upsgi_error("upsgi_lock_fd()/fcntl(F_SETLKW)");
		exit(1);
	}
	uli->pid = upsgi.mypid;
}

void upsgi_unlock_fd(struct upsgi_lock_item *uli) {
	int fd = upsgi_lock_fd_get(uli);
	struct flock fl;
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
retry:
	if (fcntl(fd, F_SETLKW, &fl) < 0) {
		if (errno == EINTR) goto retry;
		upsgi_error("upsgi_unlock_fd()/fcntl(F_SETLKW)");
		exit(1);
	}
	uli->pid = 0;
}

/*
	Unbit-specific workaround for robust-mutexes
*/
void upsgi_robust_mutexes_watchdog() {
	upsgi_log_initial("thunder lock watchdog diagnostics are enabled; no watchdog recovery thread is spawned\n");
}

static struct upsgi_lock_item *upsgi_thunder_lock_init(char *id) {
	if (upsgi.thunder_lock_backend_request) {
		if (!strcmp(upsgi.thunder_lock_backend_request, "auto")) {
			/* keep default selection logic below */
		}
		else if (!strcmp(upsgi.thunder_lock_backend_request, "fdlock")) {
			upsgi_set_thunder_lock_backend_state(UPSGI_THUNDER_LOCK_BACKEND_FDLOCK, "selected via --thunder-lock-backend=fdlock", 0);
			return upsgi_lock_fd_init(id);
		}
		else {
			upsgi_log("unsupported thunder-lock backend \"%s\" (supported: auto, fdlock)\n", upsgi.thunder_lock_backend_request);
			exit(1);
		}
	}

	if (upsgi.thunder_lock_backend == UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_PLAIN) {
		upsgi_set_thunder_lock_backend_state(UPSGI_THUNDER_LOCK_BACKEND_FDLOCK, "plain pthread mode has no owner-death recovery; using fd-lock compatibility backend", 0);
		return upsgi_lock_fd_init(id);
	}

	return upsgi_lock_init(id);
}

void upsgi_setup_locking() {

	int i;

	if (upsgi.locking_setup) return;

	upsgi_set_thunder_lock_backend_state(UPSGI_THUNDER_LOCK_BACKEND_NONE, NULL, 0);

	// use the fastest available locking
	if (upsgi.lock_engine) {
		if (!strcmp(upsgi.lock_engine, "ipcsem")) {
			upsgi_log_initial("lock engine: ipcsem\n");
			upsgi_set_thunder_lock_backend_state(UPSGI_THUNDER_LOCK_BACKEND_IPCSEM, "selected via generic lock engine override", 0);
			atexit(upsgi_ipcsem_clear);
			upsgi.lock_ops.lock_init = upsgi_lock_ipcsem_init;
			upsgi.lock_ops.lock_check = upsgi_lock_ipcsem_check;
			upsgi.lock_ops.lock = upsgi_lock_ipcsem;
			upsgi.lock_ops.unlock = upsgi_unlock_ipcsem;
			upsgi.lock_ops.rwlock_init = upsgi_rwlock_ipcsem_init;
			upsgi.lock_ops.rwlock_check = upsgi_rwlock_ipcsem_check;
			upsgi.lock_ops.rlock = upsgi_rlock_ipcsem;
			upsgi.lock_ops.wlock = upsgi_wlock_ipcsem;
			upsgi.lock_ops.rwunlock = upsgi_rwunlock_ipcsem;
			upsgi.lock_size = 8;
			upsgi.rwlock_size = 8;
			goto ready;
		}
		upsgi_log("unable to find lock engine \"%s\"\n", upsgi.lock_engine);
		exit(1);
	}

	upsgi_log_initial("lock engine: %s\n", UPSGI_LOCK_ENGINE_NAME);
#ifdef UPSGI_IPCSEM_ATEXIT
	atexit(upsgi_ipcsem_clear);
#endif
	upsgi.lock_ops.lock_init = upsgi_lock_fast_init;
	upsgi.lock_ops.lock_check = upsgi_lock_fast_check;
	upsgi.lock_ops.lock = upsgi_lock_fast;
	upsgi.lock_ops.unlock = upsgi_unlock_fast;
	upsgi.lock_ops.rwlock_init = upsgi_rwlock_fast_init;
	upsgi.lock_ops.rwlock_check = upsgi_rwlock_fast_check;
	upsgi.lock_ops.rlock = upsgi_rlock_fast;
	upsgi.lock_ops.wlock = upsgi_wlock_fast;
	upsgi.lock_ops.rwunlock = upsgi_rwunlock_fast;
	upsgi.lock_size = UPSGI_LOCK_SIZE;
	upsgi.rwlock_size = UPSGI_RWLOCK_SIZE;

#ifdef EOWNERDEAD
	if (upsgi_pthread_robust_mutexes_enabled) {
		upsgi_set_thunder_lock_backend_state(UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_ROBUST, "robust pthread mutex path active", 1);
	}
	else {
		upsgi_set_thunder_lock_backend_state(UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_PLAIN, "robust pthread mutexes unavailable before thunder-lock-specific fallback selection", 0);
	}
#else
	upsgi_set_thunder_lock_backend_state(UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_PLAIN, "robust pthread mutexes unavailable in this build before thunder-lock-specific fallback selection", 0);
#endif

ready:
	// application generic lock
	upsgi.user_lock = upsgi_malloc(sizeof(void *) * (upsgi.locks + 1));
	for (i = 0; i < upsgi.locks + 1; i++) {
		char *num = upsgi_num2str(i);
		upsgi.user_lock[i] = upsgi_lock_init(upsgi_concat2("user ", num));
		free(num);
	}

	// event queue lock (mitigate same event on multiple queues)
	if (upsgi.threads > 1) {
		pthread_mutex_init(&upsgi.thunder_mutex, NULL);
	}

	if (upsgi.master_process) {
		// signal table lock
		upsgi.signal_table_lock = upsgi_lock_init("signal");

		// fmon table lock
		upsgi.fmon_table_lock = upsgi_lock_init("filemon");

		// timer table lock
		upsgi.timer_table_lock = upsgi_lock_init("timer");

		// rb_timer table lock
		upsgi.rb_timer_table_lock = upsgi_lock_init("rbtimer");

		// cron table lock
		upsgi.cron_table_lock = upsgi_lock_init("cron");
	}

	if (upsgi.use_thunder_lock) {
		// process shared thunder lock
		upsgi.the_thunder_lock = upsgi_thunder_lock_init("thunder");
		if (upsgi.use_thunder_lock_watchdog) {
			upsgi_robust_mutexes_watchdog();
		}

	}

	upsgi.rpc_table_lock = upsgi_lock_init("rpc");

	upsgi.locking_setup = 1;
}


int upsgi_fcntl_lock(int fd) {
	struct flock fl;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;

	int ret = fcntl(fd, F_SETLKW, &fl);
	if (ret < 0)
		upsgi_error("fcntl()");

	return ret;
}

int upsgi_fcntl_is_locked(int fd) {

	struct flock fl;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;

	if (fcntl(fd, F_SETLK, &fl)) {
		return 1;
	}

	return 0;

}

void upsgi_deadlock_check(pid_t diedpid) {
	struct upsgi_lock_item *uli = upsgi.registered_locks;
	while (uli) {
		if (!uli->can_deadlock)
			goto nextlock;
		pid_t locked_pid = 0;
		if (uli->rw) {
			locked_pid = upsgi_rwlock_check(uli);
		}
		else {
			locked_pid = upsgi_lock_check(uli);
		}
		if (locked_pid == diedpid) {
			upsgi_log("[deadlock-detector] pid %d was holding lock %s (%p)\n", (int) diedpid, uli->id, uli->lock_ptr);
			if (uli->rw) {
				upsgi_rwunlock(uli);
			}
			else {
				upsgi_unlock(uli);
			}
		}
nextlock:
		uli = uli->next;
	}

}

int upsgi_user_lock(int lock_num) {
	if (lock_num < 0 || lock_num > upsgi.locks) {
		return -1;
	}
	upsgi_lock(upsgi.user_lock[lock_num]);
	return 0;
}

int upsgi_user_unlock(int lock_num) {
	if (lock_num < 0 || lock_num > upsgi.locks) {
		return -1;
	}
	upsgi_unlock(upsgi.user_lock[lock_num]);
	return 0;
}
