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
	or.w	#0x0700,sr			/* disable interrupts */
	btst.b	#5,(sp)				/* called from super? */
	bne.b	.ignore
	cmp.l	#0,_thread_func		/* threads enabled? */
	bne.b	.select
.ignore:
	rte

.select:
	move.l	sp,_sav_sp			/* save ssp */
	tst.b	_cur_tcb
	bne.b	.select0

.select1:
	st		_cur_tcb
	move.l	#_tcb0_top,_tcb_cur_top
	move.l	#_tcb1_top,_tcb_new_top
	bra.b	.save

.select0:
	sf		_cur_tcb
	move.l	#_tcb1_top,_tcb_cur_top
	move.l	#_tcb0_top,_tcb_new_top

.save:
	move.l	_tcb_cur_top,sp
	movem.l	d0-d7/a0-a6,-(sp)	/* save cpu */
	move.l	%usp,a0				/* save usp */
	move.l	a0,-(sp)
	/* todo: save fpu */
	move.l	_sav_sp,a0			/* save frame */
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	_tcb_cur_top,a1		/* save frame ptr */
	move.l	sp,(a1)

.restore:
	move.l	_tcb_new_top,a1
	move.l	(a1),sp
	move.l	(sp)+,-(a0)			/* restore frame */
	move.l	(sp)+,-(a0)
	move.l	(sp)+,a0			/* restore usp */
	move.l	a0,%usp
	/* todo: restore fpu */
	movem.l	(sp)+,d0-d7/a0-a6	/* restore cpu */
	move.l	_sav_sp,sp			/* restore ssp */
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
	move.l	0x90.w,old_trap4		/* assign yield function in unused trap4 */
	move.l	#my_trap4,0x90.w
	move.l	6(sp),_thread_func		/* assign thread1 function */
	move.l	#0,_cur_tcb				/* thread0 is current */
	move.l	#_tcb1_top,a0
	movem.l	d0-d7/a0-a6,-(a0)		/* cpu */
	move.l	#_stack1_top,-(a0)		/* usp */
	move.w	#0,-(a0)				/* frame: fmt/vec */
	move.l	#thread_tramp,-(a0)		/* frame: pc */
	move.w	(sp),d0					/* frame: sr */
	and.w	#0x700,d0
	move.w	d0,-(a0)
	move.l	(a0)+,d0				/* swap frame around */
	move.l	(a0),d1
	move.l	d0,(a0)
	move.l	d1,-(a0)
	move.l	a0,_tcb1_top			/* frame ptr */
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
	tas		(a0)
	beq.b	.locked_new
	cmp.b	1(a0),d0			/* did we hold the lock? */
	beq.b	.locked_inc
	trap	#4					/* no, yield and try again */
	bra.b	atari_mutex_lock
.locked_inc:
	add.w	#1,2(a0)			/* increment count */
	rts
.locked_new:
	move.b	d0,1(a0)			/* set id */
	move.w	#1,2(a0)			/* set count */
	rts

SYM(atari_mutex_unlock):
	move.l	4(sp),a0
	sub.w	#1,2(a0)			/* decrement count */
	bne.b	1f
	move.b	#0,(a0)				/* clear tas */
1:	rts




/*--------------------------------------------------------------------*/
	.bss
/*--------------------------------------------------------------------*/

	.align 16
SYM(atari_200hz_counter):	ds.l 1
_thread_func:				ds.l 1
_sav_sp:					ds.l 1
_cur_tcb:					ds.l 1
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
