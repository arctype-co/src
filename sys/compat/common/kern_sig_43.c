/*	$NetBSD: kern_sig_43.c,v 1.37 2021/09/07 11:43:02 riastradh Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: kern_sig_43.c,v 1.37 2021/09/07 11:43:02 riastradh Exp $");

#if defined(_KERNEL_OPT)
#include "opt_compat_netbsd.h"
#endif

#include <sys/param.h>
#include <sys/signalvar.h>
#include <sys/resourcevar.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/timeb.h>
#include <sys/times.h>
#include <sys/buf.h>
#include <sys/acct.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/wait.h>
#include <sys/ktrace.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/core.h>
#include <sys/kauth.h>

#include <sys/syscall.h>
#include <sys/syscallvar.h>
#include <sys/syscallargs.h>
#include <sys/compat_stub.h>

#include <sys/cpu.h>

#include <compat/sys/signal.h>
#include <compat/common/compat_mod.h>

void compat_43_sigmask_to_sigset(const int *, sigset_t *);
void compat_43_sigset_to_sigmask(const sigset_t *, int *);
void compat_43_sigvec_to_sigaction(const struct sigvec *, struct sigaction *);
void compat_43_sigaction_to_sigvec(const struct sigaction *, struct sigvec *);
void compat_43_sigstack_to_sigaltstack(const struct sigstack *, struct sigaltstack *);
void compat_43_sigaltstack_to_sigstack(const struct sigaltstack *, struct sigstack *);

static struct syscall_package kern_sig_43_syscalls[] = {
	{ SYS_compat_43_osigblock, 0, (sy_call_t *)compat_43_sys_sigblock },
	{ SYS_compat_43_osigsetmask, 0, (sy_call_t *)compat_43_sys_sigsetmask },
	{ SYS_compat_43_osigstack, 0, (sy_call_t *)compat_43_sys_sigstack },
	{ SYS_compat_43_osigvec, 0, (sy_call_t *)compat_43_sys_sigvec },
	{ SYS_compat_43_okillpg, 0, (sy_call_t *)compat_43_sys_killpg },
	{ 0, 0, NULL }
};

void
compat_43_sigmask_to_sigset(const int *sm, sigset_t *ss)
{

	memset(ss, 0, sizeof(*ss));
	ss->__bits[0] = *sm;
	ss->__bits[1] = 0;
	ss->__bits[2] = 0;
	ss->__bits[3] = 0;
}

void
compat_43_sigset_to_sigmask(const sigset_t *ss, int *sm)
{

	*sm = ss->__bits[0];
}

void
compat_43_sigvec_to_sigaction(const struct sigvec *sv, struct sigaction *sa)
{

	memset(sa, 0, sizeof(*sa));
	sa->sa_handler = sv->sv_handler;
	compat_43_sigmask_to_sigset(&sv->sv_mask, &sa->sa_mask);
	sa->sa_flags = sv->sv_flags ^ SA_RESTART;
}

void
compat_43_sigaction_to_sigvec(const struct sigaction *sa, struct sigvec *sv)
{

	memset(sv, 0, sizeof(*sv));
	sv->sv_handler = sa->sa_handler;
	compat_43_sigset_to_sigmask(&sa->sa_mask, &sv->sv_mask);
	sv->sv_flags = sa->sa_flags ^ SA_RESTART;
}

void
compat_43_sigstack_to_sigaltstack(const struct sigstack *ss, struct sigaltstack *sa)
{
	memset(sa, 0, sizeof(*sa));
	sa->ss_sp = ss->ss_sp;
	sa->ss_size = SIGSTKSZ;	/* Use the recommended size */
	sa->ss_flags = 0;
	if (ss->ss_onstack)
		sa->ss_flags |= SS_ONSTACK;
}

void
compat_43_sigaltstack_to_sigstack(const struct sigaltstack *sa, struct sigstack *ss)
{
	memset(ss, 0, sizeof(*ss));
	ss->ss_sp = sa->ss_sp;
	if (sa->ss_flags & SS_ONSTACK)
		ss->ss_onstack = 1;
	else
		ss->ss_onstack = 0;
}

int
compat_43_sys_sigblock(struct lwp *l, const struct compat_43_sys_sigblock_args *uap, register_t *retval)
{
	/* {
		syscallarg(int) mask;
	} */
	struct proc *p = l->l_proc;
	int nsm, osm;
	sigset_t nss, oss;
	int error;

	nsm = SCARG(uap, mask);
	compat_43_sigmask_to_sigset(&nsm, &nss);
	mutex_enter(p->p_lock);
	error = sigprocmask1(l, SIG_BLOCK, &nss, &oss);
	mutex_exit(p->p_lock);
	if (error)
		return (error);
	compat_43_sigset_to_sigmask(&oss, &osm);
	*retval = osm;
	return (0);
}

int
compat_43_sys_sigsetmask(struct lwp *l, const struct compat_43_sys_sigsetmask_args *uap, register_t *retval)
{
	/* {
		syscallarg(int) mask;
	} */
	struct proc *p = l->l_proc;
	int nsm, osm;
	sigset_t nss, oss;
	int error;

	nsm = SCARG(uap, mask);
	compat_43_sigmask_to_sigset(&nsm, &nss);
	mutex_enter(p->p_lock);
	error = sigprocmask1(l, SIG_SETMASK, &nss, &oss);
	mutex_exit(p->p_lock);
	if (error)
		return (error);
	compat_43_sigset_to_sigmask(&oss, &osm);
	*retval = osm;
	return (0);
}

/* ARGSUSED */
int
compat_43_sys_sigstack(struct lwp *l, const struct compat_43_sys_sigstack_args *uap, register_t *retval)
{
	/* {
		syscallarg(struct sigstack *) nss;
		syscallarg(struct sigstack *) oss;
	} */
	struct sigstack nss, oss;
	struct sigaltstack nsa, osa;
	int error;

	if (SCARG(uap, nss)) {
		error = copyin(SCARG(uap, nss), &nss, sizeof(nss));
		if (error)
			return (error);
		compat_43_sigstack_to_sigaltstack(&nss, &nsa);
	}
	error = sigaltstack1(l,
	    SCARG(uap, nss) ? &nsa : 0, SCARG(uap, oss) ? &osa : 0);
	if (error)
		return (error);
	if (SCARG(uap, oss)) {
		compat_43_sigaltstack_to_sigstack(&osa, &oss);
		error = copyout(&oss, SCARG(uap, oss), sizeof(oss));
		if (error)
			return (error);
	}
	return (0);
}

/*
 * Generalized interface signal handler, 4.3-compatible.
 */
/* ARGSUSED */
int
compat_43_sys_sigvec(struct lwp *l, const struct compat_43_sys_sigvec_args *uap, register_t *retval)
{
	/* {
		syscallarg(int) signum;
		syscallarg(const struct sigvec *) nsv;
		syscallarg(struct sigvec *) osv;
	} */
	struct sigvec nsv, osv;
	struct sigaction nsa, osa;
	int error;

	if (SCARG(uap, nsv)) {
		error = copyin(SCARG(uap, nsv), &nsv, sizeof(nsv));
		if (error)
			return (error);
		compat_43_sigvec_to_sigaction(&nsv, &nsa);
	}
	error = sigaction1(l, SCARG(uap, signum),
	    SCARG(uap, nsv) ? &nsa : 0, SCARG(uap, osv) ? &osa : 0,
	    NULL, 0);
	if (error)
		return (error);
	if (SCARG(uap, osv)) {
		compat_43_sigaction_to_sigvec(&osa, &osv);
		error = copyout(&osv, SCARG(uap, osv), sizeof(osv));
		if (error)
			return (error);
	}
	return (0);
}


/* ARGSUSED */
int
compat_43_sys_killpg(struct lwp *l, const struct compat_43_sys_killpg_args *uap, register_t *retval)
{
	/* {
		syscallarg(int) pgid;
		syscallarg(int) signum;
	} */
	ksiginfo_t ksi;
	int pgid = SCARG(uap, pgid);

	pgid &= kern_sig_43_pgid_mask;

	if ((u_int)SCARG(uap, signum) >= NSIG)
		return (EINVAL);
	memset(&ksi, 0, sizeof(ksi));
	ksi.ksi_signo = SCARG(uap, signum);
	ksi.ksi_code = SI_USER;
	ksi.ksi_pid = l->l_proc->p_pid;
	ksi.ksi_uid = kauth_cred_geteuid(l->l_cred);
	return killpg1(l, &ksi, pgid, 0);
}

int
kern_sig_43_init(void)
{

	return syscall_establish(NULL, kern_sig_43_syscalls);
}

int
kern_sig_43_fini(void)
{

	return syscall_disestablish(NULL, kern_sig_43_syscalls);
}
