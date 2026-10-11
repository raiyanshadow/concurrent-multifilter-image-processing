#include "filters.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#define NUM_WORKERS 12
#define NUM_TASKS (NUM_WORKERS * 4)
// #include <sys/sysinfo.h>


// Citation: We used GPT to verify high-level ideas (i.e., using safe_close to ignore EBADF errors,
// since we may intentionally close the same fd multiple times to keep the pipeline setup code clean)
// and to help debug this file.


// Blocks of image
typedef struct {
    int task_id; // track which block we are on
    int start_row; // where to start the block
    int end_row; // where to end the block (exclusive)
} Task;

// Helper function for complete reads
ssize_t read_all(int fd, void *buf, size_t count) {
    size_t bytes_read = 0;
    while (bytes_read < count) {
        ssize_t ret = read(fd, (char*)buf + bytes_read, count - bytes_read);
        if (ret <= 0) return ret;
        bytes_read += ret;
    }
    return bytes_read;
}

// Helper function for complete writes
ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t bytes_written = 0;
    while (bytes_written < count) {
        ssize_t ret = write(fd, (const char*)buf + bytes_written, count - bytes_written);
        if (ret <= 0) return ret;
        bytes_written += ret;
    }
    return bytes_written;
}

// Close fd safely (ignore EBADF)
void safe_close(int fd, const char *msg) {
    if (close(fd) == -1 && errno != EBADF) {
        perror(msg);
        exit(1);
    }
}

void process_filters(Filter *filters, int count, pixel **pixels, BMPHeader *header, char *out_filename) {
    if (filters == NULL) {
        fprintf(stderr, "filters is NULL\n");
        exit(1);
    }
    if (pixels == NULL) {
        fprintf(stderr, "pixels is NULL\n");
        exit(1);
    }
    if (header == NULL) {
        fprintf(stderr, "header is NULL\n");
        exit(1);
    }
    if (out_filename == NULL) {
        fprintf(stderr, "out_filename is NULL\n");
        exit(1);
    }

    int height = header->height;
    int width = header->width;

    if (height < NUM_TASKS) {
        fprintf(stderr,
            "Error: image height (%d) is smaller than NUM_TASKS (%d).\n"
            "Current implementation requires height >= NUM_TASKS.\n",
            height, NUM_TASKS);
        exit(1);
    }

    if (height <= 0 || width <= 0) {
        fprintf(stderr, "invalid image dimensions: width=%d height=%d\n", width, height);
        exit(1);
    }

    // Partition the image into tasks
    Task *tasks = malloc(NUM_TASKS * sizeof(Task));
    if (!tasks) {
        perror("malloc");
        exit(1);
    }
    
    int rows_per_task = height / NUM_TASKS;
    int remainder = height % NUM_TASKS;

    // Populate tasks array
    int row = 0;
    for (int i = 0; i < NUM_TASKS; i++) {
        int extra = 0;
        if (i < remainder) extra = 1; // first 'remainder' tasks get +1 row
        tasks[i].task_id = i;
        tasks[i].start_row = row;
        tasks[i].end_row = row + rows_per_task + extra;
        row += rows_per_task + extra;
    }

    // File descriptors
    int fd_p2c[NUM_WORKERS][2]; // parent -> child pipes
    int fd_c2p[NUM_WORKERS][2]; // child -> parent pipes
    int ids[NUM_WORKERS];

    // Create pipes
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (pipe(fd_p2c[i]) == -1) {
            perror("pipe parent to child");
            exit(1);
        }
        if (pipe(fd_c2p[i]) == -1) {
            perror("pipe child to parent");
            exit(1);
        }
    }

    // Fork workers
    for (int i = 0; i < NUM_WORKERS; i++) {
        int ret = fork();
        ids[i] = ret;
        
        if (ret == 0) { // child process

            // Keep only needed fds (p2c read, c2p write)
            safe_close(fd_p2c[i][1], "close child p2c write");
            safe_close(fd_c2p[i][0], "close child c2p read");

            // Close all unrelated pipes
            for (int j = 0; j < NUM_WORKERS; j++) {
                if (j != i) {
                    safe_close(fd_p2c[j][0], "close unused p2c read end");
                    safe_close(fd_p2c[j][1], "close unused p2c write end");
                    safe_close(fd_c2p[j][0], "close unused c2p read end");
                    safe_close(fd_c2p[j][1], "close unused c2p write end");
                }
            }

            Task task;
            ssize_t read_ret;

            int max_rows_per_child = rows_per_task + remainder; // max block size
            pixel *block_buffer = malloc(max_rows_per_child * width * sizeof(pixel));
            if (!block_buffer) {
                perror("malloc");
                exit(1);
            }
            // Use halo rows so sequential neighborhood filters see correct context
            int halo = count;
            int ext_rows = max_rows_per_child + 2 * halo;
        
            pixel *curr_block = malloc(ext_rows * width * sizeof(pixel));
            if (!curr_block)
            {
              perror("malloc");
              free(block_buffer);
              exit(1);
            }
            pixel *next_block = malloc(ext_rows * width * sizeof(pixel));
            if (!next_block) {
                perror("malloc");
                free(curr_block);
                free(block_buffer);
                exit(1);
            }

            // Process tasks from parent
            while ((read_ret = read_all(fd_p2c[i][0], &task, sizeof(Task))) == (ssize_t)sizeof(Task)) {
                printf("Child %d task %d: start=%d, end=%d\n", i, task.task_id, task.start_row, task.end_row);

                int block_rows = task.end_row - task.start_row;
                if (block_rows > max_rows_per_child) {
                    fprintf(stderr, "FATAL: block_rows=%d > max=%d\n",
                            block_rows, max_rows_per_child);
                    exit(1);
                }

                ext_rows = block_rows + 2 * halo;
                int base = task.start_row - halo;

                // Load task rows plus halo rows with edge clamping
                for (int r = 0; r < ext_rows; r++) {
                    int global_row = base + r;

                    if (global_row < 0) {
                        global_row = 0;
                    } else if (global_row >= height) {
                        global_row = height - 1;
                    }

                    memcpy(curr_block + r * width, pixels[global_row], width * sizeof(pixel));
                }

                // Apply filters sequentially over the whole extended block
                for (int f = 0; f < count; f++) {
                    for (int r = 0; r < ext_rows; r++) {
                        int up = (r == 0) ? 0 : r - 1;
                        int mid = r;
                        int down = (r == ext_rows - 1) ? ext_rows - 1 : r + 1;

                        pixel *in_rows[3] = {
                            curr_block + up * width,
                            curr_block + mid * width,
                            curr_block + down * width
                        };

                        pixel *out_rows[3] = {
                            next_block + up * width,
                            next_block + mid * width,
                            next_block + down * width
                        };

                        Window in_win = {width, {in_rows[0], in_rows[1], in_rows[2]}};
                        Window out_win = {width, {out_rows[0], out_rows[1], out_rows[2]}};

                        int global_row = task.start_row - halo + r;
                        if (global_row < 0) {
                            global_row = 0;
                        } else if (global_row >= height) {
                            global_row = height - 1;
                        }

                        filters[f].apply(&in_win, &out_win, filters[f].strength, height, global_row);
                    }

                    pixel *tmp_block = curr_block;
                    curr_block = next_block;
                    next_block = tmp_block;
                }

                // Copy only the actual task rows back to the block buffer
                for (int r = 0; r < block_rows; r++) {
                    memcpy(block_buffer + r * width,
                           curr_block + (r + halo) * width,
                           width * sizeof(pixel));
                }

                // Send task metadata back
                if (write_all(fd_c2p[i][1], &task, sizeof(Task)) <= 0) {
                    perror("write c2p");
                    free(curr_block);
                    free(next_block);
                    free(block_buffer);
                    exit(1);
                }

                // Send processed pixels
                if (write_all(fd_c2p[i][1], block_buffer, block_rows * width * sizeof(pixel)) <= 0) {
                    perror("write c2p buffer");
                    free(curr_block);
                    free(next_block);
                    free(block_buffer);
                    exit(1);
                }
            }
            
            free(curr_block);
            free(next_block);
            free(block_buffer);

            if (read_ret == -1) {
                perror("read p2c");
                exit(1);
            }
            if (read_ret != 0) {
                fprintf(stderr, "partial read on p2c\n");
                exit(1);
            }

            // Close own fds
            safe_close(fd_p2c[i][0], "close child p2c read");
            safe_close(fd_c2p[i][1], "close child c2p write");

            exit(0);
        }

        else if (ret > 0) { // parent process
            // Close unused ends
            safe_close(fd_p2c[i][0], "close parent p2c read");
            safe_close(fd_c2p[i][1], "close parent c2p write");
        }
        else {
            perror("fork");
            exit(1);
        }
    }

    // Output image buffer
    pixel *processed_pixels = malloc(height * width * sizeof(pixel));
    if (!processed_pixels) {
      perror("malloc");
      exit(1);
    }

    int next_task = 0;
    int tasks_completed = 0;
    int worker_open[NUM_WORKERS];
    int worker_busy[NUM_WORKERS];

    // Track worker state
    for (int i = 0; i < NUM_WORKERS; i++) {
        worker_open[i] = 1;
        worker_busy[i] = 0;
    }

    // Send initial tasks (one per worker)
    for (int i = 0; i < NUM_WORKERS && next_task < NUM_TASKS; i++) {
        if (write_all(fd_p2c[i][1], &tasks[next_task], sizeof(Task)) <= 0) {
            perror("write initial task p2c");
            exit(1);
        }
        worker_busy[i] = 1;
        next_task++;
    }

    // Close unused workers
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (!worker_busy[i] && worker_open[i]) {
            safe_close(fd_p2c[i][1], "close idle p2c write");
            worker_open[i] = 0;
        }
    }

    // Main scheduling loop
    while (tasks_completed < NUM_TASKS) {
        fd_set read_set;
        FD_ZERO(&read_set);

        int maxfd = -1;

        // Add active workers to select set
        for (int i = 0; i < NUM_WORKERS; i++) {
            if (worker_busy[i]) {
                FD_SET(fd_c2p[i][0], &read_set);
                if (fd_c2p[i][0] > maxfd) {
                    maxfd = fd_c2p[i][0];
                }
            }
        }

        if (maxfd == -1) {
            fprintf(stderr, "No active workers but tasks remain\n");
            exit(1);
        }

        // Wait for any worker to finish
        int ready = select(maxfd + 1, &read_set, NULL, NULL, NULL);
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            exit(1);
        }

        // Handle finished workers
        for (int i = 0; i < NUM_WORKERS; i++) {
            if (worker_busy[i] && FD_ISSET(fd_c2p[i][0], &read_set)) {

                Task finished_task;

                ssize_t r = read_all(fd_c2p[i][0], &finished_task, sizeof(Task));
                if (r == -1) {
                    perror("read finished task");
                    exit(1);
                }
                if (r == 0) {
                    fprintf(stderr, "unexpected EOF while reading finished task\n");
                    exit(1);
                }
                if (r != (ssize_t)sizeof(Task)) {
                    fprintf(stderr, "partial read of finished task\n");
                    exit(1);
                }

                printf("Parent got task %d from worker %d\n", finished_task.task_id, i);

                int rows = finished_task.end_row - finished_task.start_row;

                // Read processed pixels
                pixel *block_buffer = malloc(rows * width * sizeof(pixel));
                if (!block_buffer) {
                    perror("malloc");
                    exit(1);
                }

                ssize_t buffer_ret = read_all(fd_c2p[i][0], block_buffer,
                                              rows * width * sizeof(pixel));
                if (buffer_ret == -1) {
                    perror("read c2p buffer");
                    exit(1);
                }
                if (buffer_ret != (ssize_t)(rows * width * sizeof(pixel))) {
                    fprintf(stderr, "partial read of c2p buffer\n");
                    exit(1);
                }

                // Copy result into final image
                printf("Copying output for task %d\n", finished_task.task_id);
                memcpy(processed_pixels + finished_task.start_row * width,
                       block_buffer,
                       rows * width * sizeof(pixel));

                free(block_buffer);
                tasks_completed++;

                // Assign next task if available
                if (next_task < NUM_TASKS) {
                    if (write_all(fd_p2c[i][1], &tasks[next_task], sizeof(Task)) <= 0) {
                        perror("write next task p2c");
                        exit(1);
                    }
                    worker_busy[i] = 1;
                    next_task++;
                } else {
                    // No more tasks → close worker pipe
                    worker_busy[i] = 0;
                    if (worker_open[i]) {
                        safe_close(fd_p2c[i][1], "close finished p2c write");
                        worker_open[i] = 0;
                    }
                }
            }
        }
    }

    // Close all read ends
    for (int i = 0; i < NUM_WORKERS; i++) {
        safe_close(fd_c2p[i][0], "close c2p");
    }

    // Wait for all children
    printf("Waiting for all children to finish...\n");
    for (int t = 0; t < NUM_WORKERS; t++) {
        if (waitpid(ids[t], NULL, 0) == -1) {
            perror("waitpid");
            exit(1);
        }
        printf("Worker %d child finished\n", t);
    }

    // Write final image
    printf("Writing to output image %s\n", out_filename);
    write_bitmap(out_filename, header, processed_pixels);
    printf("Processing complete!\n");

    // Free memory
    free(processed_pixels);
    free(tasks);
}
