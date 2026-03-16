# POSIX Minimal Profile (Level 1) Plan

This document defines a minimal POSIX-compatible syscall surface for Rodnix to run simple user programs with files, processes, and basic I/O. It is a practical "Level 1" profile, not full POSIX.

## Goals
1. Run simple user programs (ELF), shell, and basic utilities.
2. Provide a stable UNIX-like ABI surface for libc.
3. Keep implementation scope small and incremental, with clear dependencies.

## Non-Goals (For This Profile)
1. Job control, ptys, sockets, threads, permissions model, extended ACLs.
2. Full POSIX conformance suite.
3. Advanced filesystem features (xattrs, quotas).

## Syscall Surface (Minimal Semantics)

### Process And Program Execution
1. `fork()`
   - Duplicate current process address space and file descriptor table.
   - Child gets return value 0; parent gets child PID.
   - Inherit signal dispositions and masks.
2. `execve(path, argv, envp)`
   - Replace address space with ELF image.
   - Respect `FD_CLOEXEC` on file descriptors.
3. `_exit(status)`
   - Terminate process immediately without flushing user-space buffers.
4. `waitpid(pid, status, options)`
   - Wait for child exit (at least `WNOHANG` support is helpful).
   - Generate `SIGCHLD` for parent on child exit.

### File Descriptors And I/O
1. `openat(dirfd, path, flags, mode)`
   - `open()` can be a libc wrapper calling `openat(AT_FDCWD, ...)`.
   - Support `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND`.
2. `close(fd)`
3. `read(fd, buf, count)`
4. `write(fd, buf, count)`
5. `lseek(fd, offset, whence)`
6. `dup2(oldfd, newfd)`
7. `pipe(pipefd[2])`
8. `fcntl(fd, cmd, ...)`
   - Minimum: `F_GETFD`, `F_SETFD` with `FD_CLOEXEC`.
   - Optional: `O_NONBLOCK` via `F_GETFL`/`F_SETFL`.

### Filesystem Metadata And Paths
1. `stat(path, statbuf)`
2. `fstat(fd, statbuf)`
3. `access(path, mode)`
4. `chdir(path)`
5. `getcwd(buf, size)`
6. `mkdir(path, mode)`
7. `unlink(path)`
8. `rename(oldpath, newpath)`

### Memory Management
1. `mmap(addr, length, prot, flags, fd, offset)`
2. `munmap(addr, length)`
3. `mprotect(addr, length, prot)`
4. `brk(addr)`

### Time
1. `clock_gettime(clock_id, timespec)`
   - At least `CLOCK_REALTIME` and `CLOCK_MONOTONIC`.

### Signals
1. `sigaction(signum, act, oldact)`
2. `sigprocmask(how, set, oldset)`
3. `kill(pid, sig)`

### Terminal (TTY)
1. `ioctl(fd, request, ...)`
   - Minimal termios subset for canonical mode and echo.
2. `isatty(fd)`
   - Usually libc calls `ioctl(TCGETS)` to detect.

## Implementation Plan (Incremental)

### Phase 0: Groundwork
1. Define syscall ABI table and numbering strategy.
2. Add trace logging for new syscalls (debug builds).
3. Add basic errno mapping and test harness in userland.

### Phase 1: Core I/O And Process
1. Implement `openat`, `close`, `read`, `write`, `lseek`.
2. Implement `fork`, `_exit`, `waitpid`, `execve`.
3. Implement `dup2`, `pipe`, `fcntl(F_GETFD/F_SETFD)`.
4. Verify ELF loader honors `FD_CLOEXEC`.

### Phase 2: Filesystem UX
1. Implement `stat`, `fstat`, `access`.
2. Implement `chdir`, `getcwd`.
3. Implement `mkdir`, `unlink`, `rename`.

### Phase 3: Memory
1. Implement `mmap`, `munmap`.
2. Implement `mprotect` for ELF segment protections.
3. Implement `brk` for libc fallback path.

### Phase 4: Signals And Time
1. Implement `sigaction`, `sigprocmask`, `kill`.
2. Implement basic signal delivery on `waitpid` and `SIGCHLD`.
3. Implement `clock_gettime`.

### Phase 5: TTY
1. Implement `ioctl` termios minimum.
2. Wire `isatty` via termios `ioctl`.

## Dependencies And Notes
1. `fork` requires VM with COW or full address space duplication.
2. `execve` requires ELF loader, argv/envp placement, and stack setup.
3. `mmap/mprotect` require page tables with per-page permissions.
4. `pipe` requires kernel buffer, blocking semantics, and wakeups.
5. `SIGCHLD` requires exit status tracking and parent notification.

## Suggested Test Checklist
1. Run a hello-world user program via `execve`.
2. Run a simple shell and verify pipelines: `echo test | cat`.
3. Verify redirection: `echo hi > file` and `cat < file`.
4. Verify `stat` and `ls` on directories.
5. Verify `sleep` or time-based utility using `clock_gettime`.

## Future Extensions (Not In Level 1)
1. Job control and ptys.
2. Sockets and networking.
3. Threads and synchronization.
4. Permissions and user/group model beyond minimal.
