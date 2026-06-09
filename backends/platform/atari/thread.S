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
 * Simple two-thread preemptive context switcher
 *
 * Thread0 is main thread inheriting context from application
 * Thread1 is task thread started via atari_thread_init().
 * 
 *  - Usermode threads only, it does not preempt or yield
 *    threads that are in supervisor mode.
 *
 *  - Preempts by tail-calling yield from the 200hz timer
 *
 *  - Manual yield is provided by atari_thread_yield()
 *    Uses Trap #4 internally
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


/* -------------------------------------------------------------------------
 *
 * TimerC
 *
 * -----------------------------------------------------------------------*/

	.text
	.even
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
	dc.l	0
	dc.l	0
	dc.l	0x58425241	/* XBRA */
	dc.l	0x5343554d	/* SCUM */
old_200hz:
	dc.l	0
my_200hz:
	addq.l	#1,SYM(atari_200hz_counter)
	move.w	#0,-(sp)				/* push a fake frame for tail-calling */
	move.l	#my_trap4,-(sp)			/* my_trap4 after TOS timerC handler */
	move.w	#0x2700,-(sp)
	move.l	old_200hz(pc),-(sp)		/* TOS timerC handler */
	rts


/* -------------------------------------------------------------------------
 *
 * Context switcher
 *
 * -----------------------------------------------------------------------*/

	.align 16
	dc.l	0
	dc.l	0
	dc.l	0x58425241	/* XBRA */
	dc.l	0x5343554d	/* SCUM */
old_trap4:
	dc.l	0
my_trap4:
	or.w	#0x0700,sr				/* disable interrupts */
	btst.b	#5,(sp)					/* don't preempt if thread is in supervisor */
	bne.b	1f
	cmp.l	#0,_thread_func			/* threads enabled? */
	bne.b	.select
1:	rte

.select:
	move.l	sp,_sav_sp				/* save ssp */
	eor.b	#1,_cur_tcb				/* select next thread */
	beq.b	2f
	move.l	#_tcb0_top,_tcb_cur_top
	move.l	#_tcb1_top,_tcb_new_top
	bra.b	.save
2:	move.l	#_tcb1_top,_tcb_cur_top
	move.l	#_tcb0_top,_tcb_new_top

.save:
	move.l	_tcb_cur_top,sp
	movem.l	d0-d7/a0-a6,-(sp)		/* save cpu */
	move.l	%usp,a0					/* save usp */
	move.l	a0,-(sp)
	move.w	_fpu_info,d0			/* save fpu */
	bmi.b	3f
	fsave	-(sp)
	move.b	(sp,d0.w),d0
	beq.b	3f
	fmovem.x %fp0-%fp7,-(sp)
	fmovem.l %fpcr/%fpsr/%fpiar,-(sp)
3:	move.w	d0,-(sp)
	move.l	_sav_sp,a0				/* save frame */
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	_tcb_cur_top,a1			/* save tcb ptr */
	move.l	sp,(a1)

.restore:
	move.l	_tcb_new_top,a1
	move.l	(a1),sp
	move.l	(sp)+,-(a0)				/* restore frame */
	move.l	(sp)+,-(a0)
	move.w	(sp)+,d0				/* restore fpu */
	bmi.b	5f
	tst.b	d0
	beq.b	4f
	fmovem.l (sp)+,%fpcr/%fpsr/%fpiar
	fmovem.x (sp)+,%fp0-%fp7
4:	frestore (sp)+
5:	move.l	(sp)+,a0				/* restore usp */
	move.l	a0,%usp
	movem.l	(sp)+,d0-d7/a0-a6		/* restore cpu */
	move.l	_sav_sp,sp				/* restore ssp */
	rte


/*--------------------------------------------------------------------*/
SYM(atari_thread_yield):
	trap	#4
	rts


/*--------------------------------------------------------------------*/
	.even
SYM(atari_thread_init):
	move.w	sr,-(sp)
	or.w	#0x0700,sr

	move.w	#0xffff,_fpu_info		/* determine fpu type */
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

	move.l	#_tcb1_top,a0			/* init thread1 context */
	movem.l	d0-d7/a0-a6,-(a0)		/* cpu */
	move.l	#_stack1_top,-(a0)		/* usp */
	moveq	#0,d1					/* fpu */
	move.w	_fpu_info,d0
	bmi.b	4f
	beq.b	3f
	move.l	d1,-(a0)				/* 68060 null frame */
	move.l	d1,-(a0)
3:	move.l	d1,-(a0)				/* 68XXX null frame */
	move.b	d1,d0
4:	move.w	d0,-(a0)				/* fpu context info */
	move.w	d1,-(a0)				/* frame: fmt/vec */
	move.l	#thread_tramp,-(a0)		/* frame: pc */
	move.w	(sp),d0					/* frame: sr */
	and.w	#0x700,d0
	move.w	d0,-(a0)
	move.l	(a0)+,d0				/* swap frame around */
	move.l	(a0),d1
	move.l	d0,(a0)
	move.l	d1,-(a0)
	move.l	a0,_tcb1_top			/* tcb ptr */
	move.w	(sp)+,sr
	moveq.l	#0,d0
	rts
thread_tramp:
	move.l	_thread_func,a0
	jsr		(a0)
1:	bra.b	1b


/*--------------------------------------------------------------------*/
SYM(atari_thread_shutdown):
	move.w	sr,-(sp)
	or.w	#0x700,sr
	move.l	old_trap4,0x90.w		/* restore old trap4 */
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
	move.b	_cur_tcb,d0
	move.l	4(sp),a0
	tas		(a0)					/* try lock */
	beq.b	2f
	cmp.b	1(a0),d0				/* already ours? */
	beq.b	1f
	trap	#4						/* no, yield and try again */
	bra.b	atari_mutex_lock
1:	add.w	#1,2(a0)				/* yes, increment counter */
	rts
2:	move.b	d0,1(a0)				/* init owner id */
	move.w	#1,2(a0)				/* init counter */
	rts

SYM(atari_mutex_unlock):
	move.l	4(sp),a0
	sub.w	#1,2(a0)				/* decrement counter */
	bne.b	1f
	move.b	#0,(a0)					/* unlock */
1:	rts


/*--------------------------------------------------------------------*/
	.bss
/*--------------------------------------------------------------------*/

	.align 16
SYM(atari_200hz_counter):	ds.l 1
_thread_func:				ds.l 1
_sav_sp:					ds.l 1
_cur_tcb:					ds.b 1
_padding:					ds.b 1
_fpu_info:					ds.w 1	/* 0xffff=no fpu, 0x0000=6888X, 0x0002=68060 */
_tcb_cur_top:				ds.l 1
_tcb_new_top:				ds.l 1

	.align 16
_tcb0_btm:					ds.l 255
_tcb0_top:					ds.l 1
_tcb1_btm:					ds.l 255
_tcb1_top:					ds.l 1

	.align 16
_stack1_btm:				ds.l 8191
_stack1_top:				ds.l 1
