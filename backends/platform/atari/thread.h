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

#ifndef ATARI_THREAD_H
#define ATARI_THREAD_H

#include "common/mutex.h"

extern "C" long atari_200hz_init(void);					/* call in super */
extern "C" long atari_200hz_shutdown(void);				/* call in super */
extern "C" uint32 atari_200hz_counter;

extern "C" long atari_thread_init(void(*func)(void));	/* call in super */
extern "C" long atari_thread_shutdown(void);			/* call in super */
extern "C" void atari_thread_yield(void);

extern "C" void atari_mutex_lock(volatile uint32_t* m);
extern "C" void atari_mutex_unlock(volatile uint32_t* m);

class AtariMutex : public Common::MutexInternal {
private:
	volatile uint32_t _lock;
public:
	AtariMutex() { _lock = 0; }
	~AtariMutex() { }
	bool lock() override { atari_mutex_lock(&_lock); return true; }
	bool unlock() override { atari_mutex_unlock(&_lock); return true; }
};

#endif
