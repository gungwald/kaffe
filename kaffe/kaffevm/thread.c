/*
 * thread.c
 * Thread support.
 *
 * Copyright (c) 1996, 1997
 *	Transvirtual Technologies, Inc.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution 
 * of this file. 
 */

#include "debug.h"

#include "config.h"
#include "config-std.h"
#include "config-mem.h"
#include "config-io.h"
#include "config-signal.h"
#include "jtypes.h"
#include "access.h"
#include "object.h"
#include "constants.h"
#include "errors.h"
#include "classMethod.h"
#include "baseClasses.h"
#include "lookup.h"
#include "thread.h"
#include "locks.h"
#include "exception.h"
#include "support.h"
#include "external.h"
#include "gc.h"
#include "jni.h"
#include "md.h"

Hjava_lang_Class* ThreadClass;
Hjava_lang_Class* ThreadGroupClass;
Hjava_lang_Thread* garbageman;
Hjava_lang_Thread* finalman;
Hjava_lang_ThreadGroup* standardGroup;

static void firstStartThread(void*);
static void createInitialThread(char*);
static Hjava_lang_Thread* createDaemon(void*, char*, int);
static iLock thread_start_lock;

/*
 * Initialise threads.
 */
void
initThreads(void)
{
	errorInfo info;

	/* Get a handle on the thread and thread group classes */
	ThreadClass = lookupClass(THREADCLASS, &info);
	assert(ThreadClass != 0);
	ThreadGroupClass = lookupClass(THREADGROUPCLASS, &info);
	assert(ThreadGroupClass != 0);

	/* Create base group */
	standardGroup = (Hjava_lang_ThreadGroup*)newObject(ThreadGroupClass);

	assert(standardGroup != 0);
	unhand(standardGroup)->parent = 0;
	unhand(standardGroup)->name = makeJavaString("main", 4);
	unhand(standardGroup)->maxPriority = java_lang_Thread_MAX_PRIORITY;
	unhand(standardGroup)->destroyed = 0;
	unhand(standardGroup)->daemon = 0;
	unhand(standardGroup)->nthreads = 0;
	unhand(standardGroup)->threads = (HArrayOfObject*)newArray(ThreadClass, 0);
	unhand(standardGroup)->ngroups = 0;
	unhand(standardGroup)->groups = (HArrayOfObject*)newArray(ThreadGroupClass, 0);


	/* Allocate a thread to be the main thread */
	createInitialThread("main");

	/* initialize start lock */
	initStaticLock(&thread_start_lock);

	/* Breaks encapsulation */
	if (DBGEXPR(NOGC, false, true))
	{
		extern void gcMan(void*);
		extern void finaliserMan(void*);

		/* Start the GC daemons we need */
		finalman = createDaemon(&finaliserMan, "finaliser", THREAD_MAXPRIO);
		garbageman = createDaemon(&gcMan, "gc", THREAD_MAXPRIO);
		gc_mode = GC_ENABLED;
	}
}

/*
 * Start a new thread running.
 */
void
startThread(Hjava_lang_Thread* tid)
{
	if (aliveThread(tid) == true) {
		throwException(IllegalThreadStateException);
	}
	/* Hold the start lock while the thread is created.
	 * This lock prevents the new thread from running until we're
	 * finished in create.
	 * See also firstStartThread.
	 */
	lockStaticMutex(&thread_start_lock);
	(*Kaffe_ThreadInterface.create)(tid, &firstStartThread);
	unlockStaticMutex(&thread_start_lock);
}

/*
 * Interrupt a thread
 */
void
interruptThread(Hjava_lang_Thread* tid)
{
	(*Kaffe_ThreadInterface.interrupt)(tid);
}

/*
 * Stop a thread from running and terminate it.
 */
void
stopThread(Hjava_lang_Thread* tid, Hjava_lang_Object* obj)
{
	if ((*Kaffe_ThreadInterface.currentJava)() == tid) {
		/*
		dprintf("STOPPING myself native %p\n", 
			(*Kaffe_ThreadInterface.currentNative)());
		dumpLocks();
		*/
		throwException((Hjava_lang_Throwable*)obj);
	}
	else {
		/*
		 * Note that we deviate from the spec here in that the target 
		 * thread won't throw the exception `obj', but it will 
		 * construct a new ThreadDeath exception when it dies.
		 */
		(*Kaffe_ThreadInterface.stop)(tid);
	}
}

/*
 * Create the initial thread with a given name.
 *  We should only ever call this once.
 */
static
void
createInitialThread(char* nm)
{
	Hjava_lang_Thread* tid;

	/* Allocate a thread to be the main thread */
	tid = (Hjava_lang_Thread*)newObject(ThreadClass);
	assert(tid != 0);

	unhand(tid)->name = (HArrayOfChar*)makeJavaCharArray(nm, strlen(nm));
	unhand(tid)->priority = java_lang_Thread_NORM_PRIORITY;
	unhand(tid)->threadQ = 0;
	unhand(tid)->daemon = 0;
	unhand(tid)->interrupting = 0;
	unhand(tid)->target = 0;
	unhand(tid)->group = standardGroup;

	(*Kaffe_ThreadInterface.createFirst)(tid);

	/* Attach thread to threadGroup */
	do_execute_java_method(unhand(tid)->group, "add", "(Ljava/lang/Thread;)V", 0, 0, tid);
}

/*
 * Start a daemon thread.
 */
static
Hjava_lang_Thread*
createDaemon(void* func, char* nm, int prio)
{
	Hjava_lang_Thread* tid;

DBG(VMTHREAD,	dprintf("createDaemon %s\n", nm);	)

	/* Keep daemon threads as root objects */
	tid = (Hjava_lang_Thread*)newObject(ThreadClass);
	assert(tid != 0);

	unhand(tid)->name = (HArrayOfChar*)makeJavaCharArray(nm, strlen(nm));
	unhand(tid)->priority = prio;
	unhand(tid)->threadQ = 0;
	unhand(tid)->daemon = 1;
	unhand(tid)->interrupting = 0;
	unhand(tid)->target = 0;
	unhand(tid)->group = 0;

	(*Kaffe_ThreadInterface.create)(tid, func);

	return (tid);
}

/*
 * All threads start here.
 */
static
void
firstStartThread(void* arg)
{
	extern JNIEnv Kaffe_JNIEnv;
	JNIEnv *env = &Kaffe_JNIEnv;
	Hjava_lang_Thread* tid;
	jmethodID runmethod;
	jthrowable eobj;

	/* 
	 * Make sure the thread who created us returned from
	 * Kaffe_ThreadInterface.create.  This ensures that privateInfo
	 * is set when we run.
	 */
	lockStaticMutex(&thread_start_lock);
	unlockStaticMutex(&thread_start_lock);

	tid  = (*Kaffe_ThreadInterface.currentJava)();

DBG(VMTHREAD,	dprintf("firstStartThread %x\n", tid);		)

	/*
	 * We use JNI here to make sure the stack is unrolled when we get
	 * into the uncaughtException handler.  Otherwise, we wouldn't be
	 * able to handle StackOverflowError.
	 */

	/* Find the run()V method and call it */
	runmethod = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, tid),  
					"run", "()V");
	if (runmethod != 0) {
		(*env)->CallVoidMethod(env, tid, runmethod);

		/* note that since exception.c does not allow JNI to catch
		 * ThreadDeath (for now!), we won't see ThreadDeath here.
		 * That is, we must invoke the uncaughtException paper
		 * if we see an exception here.
		 */
		eobj = (*env)->ExceptionOccurred(env);
		(*env)->ExceptionClear(env);
	} else {
		/* eobj will usually be NoSuchMethodError */
		eobj = (*env)->ExceptionOccurred(env);
	}

	/* If all else fails we call the the uncaught exception method
	 * on this thread's group.  Note we must set a flag so we 
	 * don't do this again while in the handler.
	 */
	if (eobj != 0 && unhand(tid)->dying == false) {
		jobject group = unhand(tid)->group;
		jclass groupclass = (*env)->GetObjectClass(env, group);
		jmethodID uncaughtmeth = 
			(*env)->GetMethodID(env, groupclass,
				"uncaughtException",
				"(Ljava/lang/Thread;Ljava/lang/Throwable;)V");

		unhand(tid)->dying = true;
		(*env)->CallVoidMethod(env, group, uncaughtmeth, tid, eobj);
		/* exceptions thrown in `uncaughtException' are 
		 * silently ignored says the JLS.
		 */
		(*env)->ExceptionClear(env);
	}
	exitThread();
}

/*
 * Yield process to another thread of equal priority.
 */
void
yieldThread(void)
{
	(*Kaffe_ThreadInterface.yield)();
}

/*
 * Change thread priority.
 */
void
setPriorityThread(Hjava_lang_Thread* tid, int prio)
{
	unhand(tid)->priority = prio;

	(*Kaffe_ThreadInterface.setPriority)(tid, prio);
}

/*
 * Terminate a thread.
 */
void
exitThread(void)
{
        do_execute_java_method(getCurrentThread(), "finish", "()V", 0, 0);
	(*Kaffe_ThreadInterface.exit)();
}

/*
 * Put a thread to sleep.
 */
void
sleepThread(jlong time)
{
	if (time > 0) {
		(*Kaffe_ThreadInterface.sleep)(time);
	}
}

/*
 * Is this thread alive?
 */
bool
aliveThread(Hjava_lang_Thread* tid)
{
	bool status;

DBG(VMTHREAD,	dprintf("aliveThread: tid 0x%x\n", tid);		)

	status = (*Kaffe_ThreadInterface.alive)(tid);

	return (status);
}

/*
 * How many stack frames have I invoked?
 */
jint
framesThread(Hjava_lang_Thread* tid)
{
	jint count;

	count = (*Kaffe_ThreadInterface.frames)(tid);

	return (count);
}

/*
 * Get the current Java thread.
 */
Hjava_lang_Thread*
getCurrentThread(void)
{
	return ((*Kaffe_ThreadInterface.currentJava)());
}

/*
 * Finalize a thread.
 *  This is to free the native thread context.
 */
void
finalizeThread(Hjava_lang_Thread* tid)
{
	(*Kaffe_ThreadInterface.finalize)(tid);
}
