#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#define MEM_BLOCK_SIZE 4096

// Function Prototypes
void init_tracing();
void start_tracing();
void stop_tracing();
void perform_open();
void perform_close();
void perform_read();
void perform_write();
void perform_mount();
void perform_umount();
void perform_ls();
int open_file(const char *path, int flags, mode_t mode);
void* allocate_aligned_buffer(size_t size);
void cleanup(FILE *fd1, FILE *fd2);

// Global Variables
FILE *trace_file = NULL;
FILE *tracing_on_file = NULL;

const char *file_path = "/mnt/exofs0/my_file";
const char *dir_path = "/mnt/exofs0";
const char *mount_source = "/dev/osd0";
const char *mount_target = "/mnt/exofs0";
const char *filesystem_type = "exofs";
unsigned long mount_flags = 0;
const char *mount_data = "pid=0x10000";
int use_cache = 0;

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <read/write/open/close/mount/umount/ls> <cache_disabled/cache_enabled>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[2], "cache_disabled") == 0) {
        use_cache = 0;
    } else if (strcmp(argv[2], "cache_enabled") == 0) {
        use_cache = 1;
    } else {
        fprintf(stderr, "Invalid cache option. Use cache_disabled or cache_enabled.\n");
        return EXIT_FAILURE;
    }

    init_tracing();
    stop_tracing();

    if (strcmp(argv[1], "open") == 0) {
        perform_open();
    } else if (strcmp(argv[1], "close") == 0) {
        perform_close();
    } else if (strcmp(argv[1], "read") == 0) {
        perform_read();
    } else if (strcmp(argv[1], "write") == 0) {
        perform_write();
    } else if (strcmp(argv[1], "mount") == 0) {
        perform_mount();
    } else if (strcmp(argv[1], "umount") == 0) {
        perform_umount();
    } else if (strcmp(argv[1], "ls") == 0) {
        perform_ls();
    } else {
        fprintf(stderr, "Invalid operation. Use read, write, open, close, mount, umount, or ls.\n");
        return EXIT_FAILURE;
    }

    cleanup(trace_file, tracing_on_file);
    return EXIT_SUCCESS;
}

void init_tracing() {
    FILE *pid_file = fopen("/sys/kernel/debug/tracing/set_ftrace_pid", "w");
    if (!pid_file) {
        perror("Failed to open set_ftrace_pid");
        exit(EXIT_FAILURE);
    }

    fprintf(pid_file, "%d\n", getpid());
    fclose(pid_file);

    trace_file = fopen("/sys/kernel/debug/tracing/trace", "w");
    if (!trace_file) {
        perror("Failed to open trace file");
        exit(EXIT_FAILURE);
    }

    tracing_on_file = fopen("/sys/kernel/debug/tracing/tracing_on", "w");
    if (!tracing_on_file) {
        perror("Failed to open tracing on file");
        fclose(trace_file);
        exit(EXIT_FAILURE);
    }
}

void start_tracing() {
    fprintf(trace_file, "0");  // Clear the buffer
    fprintf(tracing_on_file, "1");  // Start tracing
}

void stop_tracing() {
    fprintf(tracing_on_file, "0");  // Stop tracing
}

int open_file(const char *path, int flags, mode_t mode) {
    int fd = open(path, flags, mode);
    if (fd == -1) {
        perror("Failed to open file");
        stop_tracing();
    }
    return fd;
}

void* allocate_aligned_buffer(size_t size) {
    void *buffer = NULL;
    if (posix_memalign(&buffer, MEM_BLOCK_SIZE, size) != 0) {
        perror("Failed to allocate aligned memory");
        stop_tracing();
        exit(EXIT_FAILURE);
    }
    return buffer;
}

void perform_open() {
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (!use_cache) flags |= O_DIRECT;

    start_tracing();
    int fd = open_file(file_path, flags, 0644);
    if (fd != -1) {
        close(fd);
    }
    stop_tracing();
}

void perform_close() {
    int flags = O_WRONLY;
    if (!use_cache) flags |= O_DIRECT;

    int fd = open_file(file_path, flags, 0);
    if (fd == -1) return;

    start_tracing();
    if (close(fd) == -1) {
        perror("Failed to close file");
    }
    stop_tracing();
}

void perform_read() {
    int flags = O_RDONLY;
    if (!use_cache) flags |= O_DIRECT;

    int fd = open_file(file_path, flags, 0);
    if (fd == -1) return;

    void *buffer = allocate_aligned_buffer(MEM_BLOCK_SIZE);

    start_tracing();
    if (read(fd, buffer, MEM_BLOCK_SIZE) == -1) {
        perror("Failed to read file");
    }
    stop_tracing();

    close(fd);
    free(buffer);
}

void perform_write() {
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (!use_cache) flags |= O_DIRECT;

    int fd = open_file(file_path, flags, 0644);
    if (fd == -1) return;

    void *buffer = allocate_aligned_buffer(MEM_BLOCK_SIZE);

    start_tracing();
    if (write(fd, buffer, MEM_BLOCK_SIZE) == -1) {
        perror("Failed to write to file");
    }
    stop_tracing();

    close(fd);
    free(buffer);
}

void perform_mount() {
    start_tracing();
    if (mount(mount_source, mount_target, filesystem_type, mount_flags, mount_data) != 0) {
        perror("Error mounting");
    } else {
        printf("Mounted %s on %s with filesystem type %s and data %s\n", mount_source, mount_target, filesystem_type, mount_data);
    }
    stop_tracing();
}

void perform_umount() {
    start_tracing();
    if (umount(mount_target) != 0) {
        perror("Error unmounting");
    } else {
        printf("Unmounted %s\n", mount_target);
    }
    stop_tracing();
}

void perform_ls() {
    DIR *dir;
    struct dirent *entry;
    
    start_tracing();
    
    dir = opendir(dir_path);
    if (dir == NULL) {
        perror("Failed to open directory");
        stop_tracing();
        return;
    }
    
    printf("Directory listing for %s:\n", dir_path);
    while ((entry = readdir(dir)) != NULL) {
        printf("  %s (type: %d)\n", entry->d_name, entry->d_type);
    }
    
    if (closedir(dir) == -1) {
        perror("Failed to close directory");
    }
    
    stop_tracing();
}

void cleanup(FILE *fd1, FILE *fd2) {
    if (fd1) {
        fclose(fd1);
    }
    if (fd2) {
        fclose(fd2);
    }
}
