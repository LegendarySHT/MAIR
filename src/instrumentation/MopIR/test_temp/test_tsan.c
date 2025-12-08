#include <pthread.h>
#include <stdio.h>

int Global;

void *Thread1(void *x) {
  int thread_arg = *(int *)x; // 将 void* 转换为 int*
  for (int i = 0; i < thread_arg; i++) {
    Global++;
  }
  return NULL;
}

void *Thread2(void *x) {
  int thread_arg = *(int *)x; // 将 void* 转换为 int*
  for (int i = 0; i < thread_arg; i++) {
    Global--;
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  pthread_t t[2];
  pthread_create(&t[0], NULL, Thread1, &argc);
  pthread_create(&t[1], NULL, Thread2, &argc);
  pthread_join(t[0], NULL);
  pthread_join(t[1], NULL);
}