/* ScummVM - Graphic Adventure Engine
*
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

 #include "symbols.h"

/* -------------------------------------------------------------------------
 * Minimal two-thread preemptive context switcher
 *
 * Thread0 is main thread inheriting context from application
 * Thread1 is task thread started via atari_thread_init().
 * 
 *  - Usermode threads only, does not preempt or yield
 *    threads which are in supervisor mode.
 *
 *  - Preempts by tail-calling yield from the 200hz timer
 *
 *  - Manual yield is provided by atari_thread_yield()
 *    Uses Trap #4 internally
 *
 *  - my_trap4 is guaranteed to always receive a stacked format0 frame.
 *    interrupts are disabled inside. the use of some global temp
 *    variable inside is safe.
 *
 *  - threads spin-yield when waiting on mutex
 *    not super efficient and if needed we could/should add
 *    sleep mechanism with wake-on-mutex-unlock.
 * -----------------------------------------------------------------------*/

	.extern SYM(Getcookie)

	.global SYM(atari_200hz_init)
	.global SYM(atari_200hz_shutdown)
	.global	SYM(atari_200hz_counter)
	.global SYM(atari_thread_init)
	.global SYM(atari_thread_shutdown)
	.global SYM(atari_thread_yield)
	.global SYM(atari_mutex_lock)
	.global SYM(atari_mutex_unlock)

	.text
	.even

/* -------------------------------------------------------------------------
 *
 * TimerC
 *
 * -----------------------------------------------------------------------*/

SYM(atari_200hz_init):
	move.w	sr,-(sp)
	or.w	#0x700,sr
	move.l	0x114,old_200hz			/* save old timerc vector */
	move.l	#my_200hz,0x114.w		/* set new timerc vector */
	move.w	(sp)+,sr
	moveq.l	#0,d0
	rts

SYM(atari_200hz_shutdown):
	move.w	sr,-(sp)
	or.w	#0x700,sr
	move.l	old_200hz(pc),0x114.w	/* restore old timerc vector */
	move.w	(sp)+,sr
	moveq.l	#0,d0
	rts

	.align 16
SYM(atari_200hz_counter):
	dc.l	0
	dc.l	0x58425241	/* XBRA */
	dc.l	0x5343554d	/* SCUM */
old_200hz:
	dc.l	0
my_200hz:
	addq.l	#1,SYM(atari_200hz_counter)
	move.w	#0,-(sp)				/* push a fake format0 frame for tail-calling */
	pea.l	my_trap4_from_isr(pc)	/* into thread scheduler after TOS timerC handler */
	move.w	#0x2700,-(sp)			/* with interrupts disabled */
	jmp		([old_200hz,pc])		/* now jump to the TOS timerC handler */


/* -------------------------------------------------------------------------
 *
 * Context switcher
 *
 * -----------------------------------------------------------------------*/

	.align 16
_tcb0_btm:		ds.l 127
_tcb0_top:		ds.l 1
_tcb1_btm:		ds.l 127
_tcb1_top:		ds.l 1

	.align 16
_thread_func:	dc.l 0
_sav_sp:		dc.l 0
_cur_tcb:		dc.b 0
_padding:		dc.b 0
_fpu_info:		dc.w 0xffff/* 0xffff=no fpu, 0x0000=6888X, 0x0002=68060 */

	.align 16
	dc.l	0
	dc.l	0x58425241	/* XBRA */
	dc.l	0x5343554d	/* SCUM */
old_trap4:
	dc.l	0
my_trap4:
	move.w	#0x2700,sr
my_trap4_from_isr:
	btst.b	#5,(sp)					/* don't preempt if thread is in supervisor */
	bne.b	1f
	tst.l	_thread_func(pc)		/* threads enabled? */
	bne.b	.select
1:	rte

.select:
	move.l	sp,_sav_sp				/* save ssp */
	eor.b	#1,_cur_tcb				/* select next thread */
	beq.b	2f
	lea.l	_tcb0_top(pc),sp
	move.l	_tcb1_top(pc),(sp)
	bra.b	.save
2:	lea.l	_tcb1_top(pc),sp
	move.l	_tcb0_top(pc),(sp)

.save:
	movem.l	d0-d7/a0-a6,-(sp)		/* cpu */
	move.l	%usp,a0					/* usp */
	lea.l	60(sp),a5
	move.l	a0,-(sp)
	move.w	_fpu_info(pc),d0		/* fpu */
	bmi.b	3f
	fsave	-(sp)
	move.b	(sp,d0.w),d0
	beq.b	3f
	fmovem.x %fp0-%fp7,-(sp)
	fmovem.l %fpcr/%fpsr/%fpiar,-(sp)
3:	move.w	d0,-(sp)
	move.l	_sav_sp(pc),a0			/* exception frame */
	move.l	(a5),a4
	move.l	(a0)+,-(sp)
	move.l	(a0),-(sp)
	move.l	sp,(a5)					/* context ptr */

.restore:
	move.l	(a4)+,(a0)				/* exception frame */
	move.l	(a4)+,-(a0)
	move.w	(a4)+,d0				/* fpu */
	bmi.b	5f
	beq.b	4f
	fmovem.l (a4)+,%fpcr/%fpsr/%fpiar
	fmovem.x (a4)+,%fp0-%fp7
4:	frestore (a4)+
5:	move.l	(a4)+,a1				/* usp */
	move.l	a0,sp					/* restore ssp */
	move.l	a1,%usp
	movem.l	(a4),d0-d7/a0-a6		/* cpu */
	rte


/*--------------------------------------------------------------------*/
SYM(atari_thread_yield):
	trap	#4
	rts


/*--------------------------------------------------------------------*/
SYM(atari_thread_init):
	move.w	sr,-(sp)
	or.w	#0x0700,sr

	move.w	#0xffff,_fpu_info		/* determine fpu info */
	move.l	#0,-(sp)
	move.l	sp,-(sp)
	move.l	#0x5F465055,-(sp)		/* _FPU */
	jsr		SYM(Getcookie)
	addq.l	#8,sp
	move.l	(sp)+,d1
	tst.l	d0						/* C_FOUND */
	bne.b	2f
	swap	d1
	btst.b	#4,d1					/* 68060 */
	beq.b	1f
	move.w	#2,_fpu_info
	bra.b	2f
1:	and.b	#0x0e,d1				/* 68040, 68882, 68881 */
	beq.b	2f
	move.w	#0,_fpu_info

2:	move.l	0x90.w,old_trap4		/* general setup */
	move.l	#my_trap4,0x90.w
	move.l	6(sp),_thread_func
	move.b	#0,_cur_tcb

	lea.l	_tcb1_top(pc),a0		/* init thread1 context */
	movem.l	d0-d7/a0-a6,-(a0)		/* cpu */
	move.l	#_stack1_top,-(a0)		/* usp */
	moveq	#0,d1					/* fpu */
	move.w	_fpu_info(pc),d0
	bmi.b	4f
	beq.b	3f
	move.l	d1,-(a0)				/* 68060 null frame */
	move.l	d1,-(a0)
3:	move.l	d1,-(a0)				/* 68XXX null frame */
	move.b	d1,d0
4:	move.w	d0,-(a0)				/* fpu context info */
	move.w	d1,-(a0)				/* exception frame: fmt/vec */
	move.l	#thread_tramp,-(a0)		/* exception frame: pc */
	move.w	(sp),d0					/* exception frame: sr */
	and.w	#0x700,d0
	move.w	d0,-(a0)
	move.l	(a0)+,d0				/* swap frame around */
	move.l	(a0),d1
	move.l	d0,(a0)
	move.l	d1,-(a0)
	move.l	a0,_tcb1_top			/* context ptr */
	move.w	(sp)+,sr
	moveq.l	#0,d0
	rts
thread_tramp:
	jsr		([_thread_func,pc])
1:	bra.b	1b


/*--------------------------------------------------------------------*/
SYM(atari_thread_shutdown):
	move.w	sr,-(sp)
	or.w	#0x700,sr
	move.l	old_trap4(pc),0x90.w	/* restore old trap4 */
	move.l	#0,_thread_func			/* clear thread1 func */
	move.w	(sp)+,sr
	moveq.l	#0,d0
	rts


/* -------------------------------------------------------------------------
 *
 * Recursive mutex
 *
 * -----------------------------------------------------------------------*/

SYM(atari_mutex_lock):
	move.b	_cur_tcb(pc),d0
	move.l	4(sp),a0
	tas		(a0)					/* try lock */
	beq.b	2f
	cmp.b	1(a0),d0				/* was ours? */
	beq.b	1f
	trap	#4						/* no, yield and try again */
	bra.b	atari_mutex_lock
1:	addq.w	#1,2(a0)				/* yes, increment counter */
	rts
2:	move.b	d0,1(a0)				/* init owner id */
	move.w	#1,2(a0)				/* init counter */
	rts

SYM(atari_mutex_unlock):
	move.l	4(sp),a0
	subq.w	#1,2(a0)				/* decrement counter */
	bne.b	1f
	move.b	#0,(a0)					/* unlock */
1:	rts


/*--------------------------------------------------------------------*/
	.bss
/*--------------------------------------------------------------------*/

	.align 16
_stack1_btm:				ds.l 8191
_stack1_top:				ds.l 1
