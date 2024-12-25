namespace __tsan {
  void Acquire(ThreadState *thr, uptr pc, uptr addr);
  void Release(ThreadState *thr, uptr pc, uptr addr);
  void FdClose(ThreadState *thr, uptr pc, int fd, bool write);
  void FdAcquire(ThreadState *thr, uptr pc, int fd);
  void FdRelease(ThreadState *thr, uptr pc, int fd);
  void FdOnFork(ThreadState *thr, uptr pc);
  void ForkBefore(ThreadState *thr, uptr pc);
  void ForkChildAfter(ThreadState *thr, uptr pc, bool start_thread);
  void ForkParentAfter(ThreadState *thr, uptr pc);
} // namespace __tsan

static USED void syscall_acquire(uptr pc, uptr addr) {
  XSAN_SYSCALL();
  __tsan::Acquire(__tsan::cur_thread(), pc, addr);
  DPrintf("syscall_acquire(0x%zx))\n", addr);
}

static USED void syscall_release(uptr pc, uptr addr) {
  XSAN_SYSCALL();
  DPrintf("syscall_release(0x%zx)\n", addr);
  __tsan::Release(__tsan::cur_thread(), pc, addr);
}

static void syscall_fd_close(uptr pc, int fd) {
  __tsan::FdClose(__tsan::cur_thread(), pc, fd, true);
}

static USED void syscall_fd_acquire(uptr pc, int fd) {
  XSAN_SYSCALL();
  __tsan::FdAcquire(__tsan::cur_thread(), pc, fd);
  DPrintf("syscall_fd_acquire(%d)\n", fd);
}

static USED void syscall_fd_release(uptr pc, int fd) {
  XSAN_SYSCALL();
  DPrintf("syscall_fd_release(%d)\n", fd);
  __tsan::FdRelease(__tsan::cur_thread(), pc, fd);
}

static void syscall_pre_fork(uptr pc) {
  __tsan::ForkBefore(__tsan::cur_thread(), pc);
}

static void syscall_post_fork(uptr pc, int pid) {
  __tsan::ThreadState *thr = __tsan::cur_thread();
  if (pid == 0) {
    // child
    __tsan::ForkChildAfter(thr, pc, true);
    __tsan::FdOnFork(thr, pc);
  } else if (pid > 0) {
    // parent
    __tsan::ForkParentAfter(thr, pc);
  } else {
    // error
    __tsan::ForkParentAfter(thr, pc);
  }
}

#    define COMMON_SYSCALL_ACQUIRE(addr) \
      syscall_acquire(GET_CALLER_PC(), (uptr)(addr))

#    define COMMON_SYSCALL_RELEASE(addr) \
      syscall_release(GET_CALLER_PC(), (uptr)(addr))

#    define COMMON_SYSCALL_FD_CLOSE(fd) syscall_fd_close(GET_CALLER_PC(), fd)

#    define COMMON_SYSCALL_FD_ACQUIRE(fd) \
      syscall_fd_acquire(GET_CALLER_PC(), fd)

#    define COMMON_SYSCALL_FD_RELEASE(fd) \
      syscall_fd_release(GET_CALLER_PC(), fd)

#    define COMMON_SYSCALL_PRE_FORK() syscall_pre_fork(GET_CALLER_PC())

#    define COMMON_SYSCALL_POST_FORK(res) \
      syscall_post_fork(GET_CALLER_PC(), res)