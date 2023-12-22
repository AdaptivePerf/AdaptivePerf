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
