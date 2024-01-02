// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) 2023 CERN.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; only version 2 of the License.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>

#define NUM_THREADS 16
#define STACK_DEPTH 10

int go(int x) {
  int a = 0;
  for (int i = 0; i < x; i++) {
    if (i % 2 == 0) {
      a += i;
    } else {
      a -= i;
    }
  }
  return a;
}

void recurse(int x) {
  if (x <= 0) {
    int z = 0;
    for (int i = 0; i < 100000; i++) {
      int a = go(i - 1);

      if (a > 0) {
	z = 1;
      }
    }
    
    if (z) {
      printf("z is true\n");
    }

    return;
  }

  recurse(x - 1);
}

void *test(void *vargp) {
  recurse(STACK_DEPTH - 2);
  return NULL;
}

void create(pthread_t *thread_ids, int i) {
  pthread_create(&thread_ids[i], NULL, test, NULL);
}

int main() {
  setbuf(stdout, NULL);

  pthread_t thread_ids[NUM_THREADS];

  printf("Starting with %i threads and stack depth of %i...\n",
	 NUM_THREADS, STACK_DEPTH);

  for (int i = 0; i < NUM_THREADS; i++) {
    create(thread_ids, i);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(thread_ids[i], NULL);
  }

  printf("Done!\n");

  return 0;
}
