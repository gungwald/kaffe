/*
 * exception.c
 * Handle exceptions for the interpreter or translator.
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
#include "config-signal.h"
#include "config-mem.h"
#include "config-setjmp.h"
#include "jtypes.h"
#include "access.h"
#include "object.h"
#include "constants.h"
#include "classMethod.h"
#include "code.h"
#include "exception.h"
#include "baseClasses.h"
#include "lookup.h"
#include "thread.h"
#include "errors.h"
#include "itypes.h"
#include "external.h"
#include "soft.h"
#include "md.h"
#include "locks.h"
#include "stackTrace.h"

#if defined(INTERPRETER)
#define	DEFINEFRAME()		/* Does nothing */
#define	FIRSTFRAME(f, e)	/* Does nothing */
#define	DISPATCHFRAME(e)	dispatchException((Hjava_lang_Throwable*)(e), 0)
#define	EXCEPTIONPROTO		int sig
#define	EXCEPTIONFRAME(f, c)	/* Does nothing */
#define	EXCEPTIONFRAMEPTR	0
#elif defined(TRANSLATOR)
#define	DEFINEFRAME()		exceptionFrame frame
#define	DISPATCHFRAME(e)	dispatchException((Hjava_lang_Throwable*)(e), &frame)
#define	EXCEPTIONFRAMEPTR	&frame
#endif
#define	GETNEXTFRAME(F)		((*Kaffe_ThreadInterface.nextFrame)(F))

static void nullException(EXCEPTIONPROTO);
static void floatingException(EXCEPTIONPROTO);
static void dispatchException(Hjava_lang_Throwable*, struct _exceptionFrame*) __NORETURN__;

Hjava_lang_Object* buildStackTrace(struct _exceptionFrame*);

extern Hjava_lang_Object* exceptionObject;
extern uintp Kaffe_JNI_estart;
extern uintp Kaffe_JNI_eend;
extern void Kaffe_JNIExceptionHandler(void);

extern void Tspinoffall(void);

/*
 * Throw an internal exception.
 */
void
throwException(Hjava_lang_Throwable* eobj)
{
	if (eobj != 0) {
		unhand(eobj)->backtrace = buildStackTrace(0);
	}
	throwExternalException(eobj);
}

/*
 * Create and throw an exception resulting from an error during VM processing.
 */
void 
throwError(errorInfo* einfo)
{
	Hjava_lang_Throwable *err;

	/* special case this, message is actually a throwable here */
	if (!strcmp(einfo->classname, 
		    "java.lang.ExceptionInInitializerError")) 
	{
		err = (Hjava_lang_Throwable*)execute_java_constructor(
			    einfo->classname, 
			    0, "(Ljava/lang/Throwable;)V", einfo->mess);
	} else 
	if (!strcmp(einfo->classname, "java.lang.RethrowException")) {
		err = (Hjava_lang_Throwable*)einfo->mess;
	} else {
		err = (Hjava_lang_Throwable*)execute_java_constructor(
			    einfo->classname, 
			    0, "(Ljava/lang/String;)V",
			    makeJavaString(einfo->mess, strlen(einfo->mess)));
	}
	throwException(err);
}

/*
 * Throw an exception.
 */
void
throwExternalException(Hjava_lang_Throwable* eobj)
{
	DEFINEFRAME();
	if (eobj == 0) {
		fprintf(stderr, "Exception thrown on null object ... aborting\n");
		ABORT();
		EXIT(1);
	}
	FIRSTFRAME(frame, eobj);
	DISPATCHFRAME(eobj);
}

void
throwOutOfMemory(void)
{
	Hjava_lang_Throwable* err;

	err = OutOfMemoryError;
	if (err != NULL) {
		throwException(err);
	}
	fprintf(stderr, "(Insufficient memory)\n");
	EXIT(-1);
}

static
void
dispatchException(Hjava_lang_Throwable* eobj, struct _exceptionFrame* baseframe)
{
	char* cname;
	Hjava_lang_Class* class;
	Hjava_lang_Object* obj;
	iLock* lk;
	Hjava_lang_Thread* ct;

	/* Release the interrupts (in case they were held when this
	 * happened - and hope this doesn't break anything).
	 * XXX - breaks the thread abstraction model !!!
	 */
	Tspinoffall();

	ct = getCurrentThread();

	class = OBJECT_CLASS(&eobj->base);
	cname = CLASS_CNAME(class);

	/* Save exception object */
	unhand(ct)->exceptObj = eobj;

	/* Search down exception stack for a match */
#if defined(INTERPRETER)
	{
		exceptionInfo einfo;
		vmException* frame;
		bool res;

		for (frame = (vmException*)unhand(ct)->exceptPtr; frame != 0; frame = frame->prev) {

			if (frame->meth == (Method*)1) {
				unhand(ct)->exceptPtr = (struct Hkaffe_util_Ptr*)frame;
				Kaffe_JNIExceptionHandler();
			}

			/* Look for handler */
			res = findExceptionBlockInMethod(frame->pc, eobj->base.dtable->class, frame->meth, &einfo);

			/* Find the sync. object */
			if (einfo.method == 0 || (einfo.method->accflags & ACC_SYNCHRONISED) == 0) {
				obj = 0;
			}
			else if (einfo.method->accflags & ACC_STATIC) {
				obj = &einfo.class->head;
			}
			else {
				obj = frame->mobj;
			}

			/* If handler found, call it */
			if (res == true) {
				unhand(ct)->needOnStack = STACK_HIGH;
				frame->pc = einfo.handler;
				longjmp(frame->jbuf, 1);
			}


			/* If not here, exit monitor if synchronised. */
			lk = getLock(obj);
			if (lk != 0 && lk->holder == (*Kaffe_ThreadInterface.currentNative)()) {
				unlockMutex(obj);
			}
		}
	}
#elif defined(TRANSLATOR)
	{
		exceptionFrame* frame;
		exceptionInfo einfo;

		for (frame = baseframe; frame != 0; frame = GETNEXTFRAME(frame)) {
			findExceptionInMethod(PCFRAME(frame), class, &einfo);

                        if (einfo.method == 0 && PCFRAME(frame) >= Kaffe_JNI_estart && PCFRAME(frame) < Kaffe_JNI_eend) {
				Kaffe_JNIExceptionHandler();
                        }

			/* Find the sync. object */
			if (einfo.method == 0 || (einfo.method->accflags & ACC_SYNCHRONISED) == 0) {
				obj = 0;
			}
			else if (einfo.method->accflags & ACC_STATIC) {
				obj = &einfo.class->head;
			}
			else {
				obj = FRAMEOBJECT(frame);
			}

			/* Handler found - dispatch exception */
			if (einfo.handler != 0) {
				unhand(ct)->exceptObj = 0;
				unhand(ct)->needOnStack = STACK_HIGH;
				CALL_KAFFE_EXCEPTION(frame, einfo, eobj);
			}

			/* If method found and synchronised, unlock the lock */
			lk = getLock(obj);
			if (lk != 0 && lk->holder == (*Kaffe_ThreadInterface.currentNative)()) {
				unlockMutex(obj);
			}
		}
	}
#endif

	/* Clear held exception object */
	unhand(ct)->exceptObj = 0;

	/* We must catch 'java.lang.ThreadDeath' exceptions now and
	 * kill the thread rather than the machine.
	 */
	if (strcmp(cname, THREADDEATHCLASS) == 0) {
		exitThread();
	} 

	/* I don't know what to do here. */
	fprintf(stderr, "Internal error.\n"
		"Please check your CLASSPATH and your installation.\n");
	ABORT();
}

/*
 * Setup the internal exceptions.
 */
void
initExceptions(void)
{
DBG(INIT,	printf("initExceptions()\n");			)
	if (DBGEXPR(EXCEPTION, false, true)) {
	/* Catch signals we need to convert to exceptions */
#if defined(SIGSEGV)
		catchSignal(SIGSEGV, nullException);
#endif
#if defined(SIGBUS)
		catchSignal(SIGBUS, nullException);
#endif
#if defined(SIGFPE)
		catchSignal(SIGFPE, floatingException);
#endif
#if defined(SIGPIPE)
		catchSignal(SIGPIPE, SIG_IGN);
#endif
	}
}

/*
 * Null exception - catches bad memory accesses.
 */
static void
nullException(EXCEPTIONPROTO)
{
	Hjava_lang_Throwable* npe;

	DEFINEFRAME();

	/* don't catch the signal if debugging exceptions */
	if (DBGEXPR(EXCEPTION, false, true))
		catchSignal(sig, nullException);

	EXCEPTIONFRAME(frame, ctx);
	npe = (Hjava_lang_Throwable*)NullPointerException;
	unhand(npe)->backtrace = buildStackTrace(EXCEPTIONFRAMEPTR);
	DISPATCHFRAME(npe);
}

/*
 * Division by zero.
 */
static void
floatingException(EXCEPTIONPROTO)
{
	Hjava_lang_Throwable* ae;
	DEFINEFRAME();

	/* don't catch the signal if debugging exceptions */
	if (DBGEXPR(EXCEPTION, false, true))
		catchSignal(sig, floatingException);
	EXCEPTIONFRAME(frame, ctx);
	ae = (Hjava_lang_Throwable*)ArithmeticException;
	unhand(ae)->backtrace = buildStackTrace(EXCEPTIONFRAMEPTR);
	DISPATCHFRAME(ae);
}

/*
 * Setup a signal handler.
 */
void
catchSignal(int sig, void* handler)
{
	sigset_t nsig;

#if defined(HAVE_SIGACTION)

	struct sigaction newact;

	newact.sa_handler = (SIG_T)handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags = 0;
#if defined(SA_SIGINFO)
	newact.sa_flags |= SA_SIGINFO;
#endif
	sigaction(sig, &newact, NULL);

#elif defined(HAVE_SIGNAL)

	signal(sig, (SIG_T)handler);

#else
	ABORT();
#endif

	/* Unblock this signal */
	sigemptyset(&nsig);
	sigaddset(&nsig, sig);
	sigprocmask(SIG_UNBLOCK, &nsig, 0);
}
