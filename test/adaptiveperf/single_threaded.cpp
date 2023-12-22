// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) 2023 CERN.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <iostream>
#include <unistd.h>

using namespace std;

void test1(int a);
void test2(int b);
void test3(int c);

void test1(int a) {
  cout << "Hello " << a << "! (not a factorial)" << endl;
  sleep(1);
  test2(a + 1);
}

void test2(int b) {
  cout << "Hello " << b << "." << endl;
  sleep(2);
  test3(2 * b);
}

void test3(int c) {
  cout << "Say goodbye to: " << c << endl;
  for (int i = 0; i < 1000000000; i++) {
    c *= 2;
  }

  cout << "New value: " << c << endl;
}

int main() {
  test1(5);
  test1(2);
  return 0;
}
