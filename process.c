
#include "filters.h"

#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define NUM_WORKERS 12
#define NUM_TASKS (NUM_WORKERS * 4)

typedef struct {
    int task_id;
    int start_row;
    int end_row;
} Task;

/* ---------- Pipe helpers ---------- */

static ssize_t read_all(int fd, void *buf, size_t count) {
    size_t bytes_read = 0;

    while (bytes_read < count) {
        ssize_t ret = read(fd, (char *)buf + bytes_read,
                           count - bytes_read);
        if (ret <= 0) {
            return ret;
        }
        bytes_read += (size_t)ret;
    }

    return (ssize_t)bytes_read;
}

static ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t bytes_written = 0;

    while (bytes_written < count) {
        ssize_t ret = write(fd, (const char *)buf + bytes_written,
                            count - bytes_written);
        if (ret <= 0) {
            return ret;
        }
        bytes_written += (size_t)ret;
    }

    return (ssize_t)bytes_written;
}

static void safe_close(int fd, const char *msg) {
    if (close(fd) == -1 && errno != EBADF) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

/* ---------- Timing ---------- */

static double elapsed_seconds(struct timespec start,
                              struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec)
         + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

/* ---------- Sequential implementation ---------- */

static double run_sequential(
    Filter *filters,
    int count,
    pixel **pixels,
    BMPHeader *header,
    const char *out_filename,
    Task *tasks
) {
    int width = header->width;
    int height = header->height;
    int halo = count;

    int rows_per_task = height / NUM_TASKS;
    int remainder = height % NUM_TASKS;
    int max_rows = rows_per_task + (remainder > 0 ? 1 : 0);
    int max_ext_rows = max_rows + 2 * halo;

    pixel *processed_pixels =
        malloc((size_t)height * width * sizeof(pixel));
    pixel *block_buffer =
        malloc((size_t)max_rows * width * sizeof(pixel));
    pixel *curr_block =
        malloc((size_t)max_ext_rows * width * sizeof(pixel));
    pixel *next_block =
        malloc((size_t)max_ext_rows * width * sizeof(pixel));

    if (!processed_pixels || !block_buffer ||
        !curr_block || !next_block) {
        perror("malloc sequential buffers");
        free(processed_pixels);
        free(block_buffer);
        free(curr_block);
        free(next_block);
        exit(EXIT_FAILURE);
    }

    struct timespec start, end;

    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < NUM_TASKS; i++) {
        int block_rows = tasks[i].end_row - tasks[i].start_row;
        int ext_rows = block_rows + 2 * halo;

        /* Load task rows plus halo rows, clamping at image boundaries. */
        for (int r = 0; r < ext_rows; r++) {
            int global_row = tasks[i].start_row - halo + r;

            if (global_row < 0) {
                global_row = 0;
            } else if (global_row >= height) {
                global_row = height - 1;
            }

            memcpy(curr_block + (size_t)r * width,
                   pixels[global_row],
                   (size_t)width * sizeof(pixel));
        }

        pixel *current = curr_block;
        pixel *next = next_block;

        /* Apply all filters in order. */
        for (int f = 0; f < count; f++) {
            for (int r = 0; r < ext_rows; r++) {
                int up = (r == 0) ? 0 : r - 1;
                int mid = r;
                int down = (r == ext_rows - 1) ? ext_rows - 1 : r + 1;

                pixel *in_rows[3] = {
                    current + (size_t)up * width,
                    current + (size_t)mid * width,
                    current + (size_t)down * width
                };

                pixel *out_rows[3] = {
                    next + (size_t)up * width,
                    next + (size_t)mid * width,
                    next + (size_t)down * width
                };

                Window in_win = {
                    width, {in_rows[0], in_rows[1], in_rows[2]}
                };
                Window out_win = {
                    width, {out_rows[0], out_rows[1], out_rows[2]}
                };

                int global_row = tasks[i].start_row - halo + r;

                if (global_row < 0) {
                    global_row = 0;
                } else if (global_row >= height) {
                    global_row = height - 1;
                }

                filters[f].apply(&in_win, &out_win,
                                 filters[f].strength, height, global_row);
            }

            pixel *tmp = current;
            current = next;
            next = tmp;
        }

        /* Copy the task's actual rows into its result buffer. */
        for (int r = 0; r < block_rows; r++) {
            memcpy(block_buffer + (size_t)r * width,
                   current + (size_t)(r + halo) * width,
                   (size_t)width * sizeof(pixel));
        }

        memcpy(processed_pixels + (size_t)tasks[i].start_row * width,
               block_buffer,
               (size_t)block_rows * width * sizeof(pixel));
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    double processing_time = elapsed_seconds(start, end);

    /* Keep image writing outside the processing measurement. */
    printf("Writing sequential output to %s\n", out_filename);
    fflush(stdout);

    if (write_bitmap(out_filename, header, processed_pixels) != 0) {
        fprintf(stderr, "Failed to write sequential output: %s\n",
                out_filename);
        exit(EXIT_FAILURE);
    }

    free(processed_pixels);
    free(block_buffer);
    free(curr_block);
    free(next_block);

    return processing_time;
}

/* ---------- Concurrent implementation ---------- */

static double run_concurrent(
    Filter *filters,
    int count,
    pixel **pixels,
    BMPHeader *header,
    const char *out_filename,
    Task *tasks
) {
    int width = header->width;
    int height = header->height;
    int rows_per_task = height / NUM_TASKS;
    int remainder = height % NUM_TASKS;

    int fd_p2c[NUM_WORKERS][2];
    int fd_c2p[NUM_WORKERS][2];
    pid_t ids[NUM_WORKERS];

    struct timespec start, end;

    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    /* Create pipes for each worker. */
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (pipe(fd_p2c[i]) == -1) {
            perror("pipe parent to child");
            exit(EXIT_FAILURE);
        }

        if (pipe(fd_c2p[i]) == -1) {
            perror("pipe child to parent");
            exit(EXIT_FAILURE);
        }
    }

    /* Start workers. */
    for (int i = 0; i < NUM_WORKERS; i++) {
        pid_t ret = fork();
        ids[i] = ret;

        if (ret == 0) {
            /* Child: retain only this worker's pipe ends. */
            safe_close(fd_p2c[i][1], "close child p2c write");
            safe_close(fd_c2p[i][0], "close child c2p read");

            for (int j = 0; j < NUM_WORKERS; j++) {
                if (j != i) {
                    safe_close(fd_p2c[j][0], "close unused p2c read");
                    safe_close(fd_p2c[j][1], "close unused p2c write");
                    safe_close(fd_c2p[j][0], "close unused c2p read");
                    safe_close(fd_c2p[j][1], "close unused c2p write");
                }
            }

            int max_rows = rows_per_task + remainder;
            int halo = count;
            int max_ext_rows = max_rows + 2 * halo;

            pixel *block_buffer =
                malloc((size_t)max_rows * width * sizeof(pixel));
            pixel *curr_block =
                malloc((size_t)max_ext_rows * width * sizeof(pixel));
            pixel *next_block =
                malloc((size_t)max_ext_rows * width * sizeof(pixel));

            if (!block_buffer || !curr_block || !next_block) {
                perror("malloc child buffers");
                free(block_buffer);
                free(curr_block);
                free(next_block);
                _exit(EXIT_FAILURE);
            }

            Task task;
            ssize_t read_ret;

            while ((read_ret = read_all(fd_p2c[i][0],
                                        &task, sizeof(task)))
                   == (ssize_t)sizeof(task)) {

                printf("Child %d task %d: start=%d, end=%d\n",
                       i, task.task_id, task.start_row, task.end_row);
                fflush(stdout);

                int block_rows = task.end_row - task.start_row;
                int ext_rows = block_rows + 2 * halo;

                /* Load task rows and halo rows. */
                for (int r = 0; r < ext_rows; r++) {
                    int global_row = task.start_row - halo + r;

                    if (global_row < 0) {
                        global_row = 0;
                    } else if (global_row >= height) {
                        global_row = height - 1;
                    }

                    memcpy(curr_block + (size_t)r * width,
                           pixels[global_row],
                           (size_t)width * sizeof(pixel));
                }

                pixel *current = curr_block;
                pixel *next = next_block;

                /* Apply filters in order. */
                for (int f = 0; f < count; f++) {
                    for (int r = 0; r < ext_rows; r++) {
                        int up = (r == 0) ? 0 : r - 1;
                        int mid = r;
                        int down = (r == ext_rows - 1)
                                 ? ext_rows - 1 : r + 1;

                        pixel *in_rows[3] = {
                            current + (size_t)up * width,
                            current + (size_t)mid * width,
                            current + (size_t)down * width
                        };

                        pixel *out_rows[3] = {
                            next + (size_t)up * width,
                            next + (size_t)mid * width,
                            next + (size_t)down * width
                        };

                        Window in_win = {
                            width, {in_rows[0], in_rows[1], in_rows[2]}
                        };
                        Window out_win = {
                            width, {out_rows[0], out_rows[1], out_rows[2]}
                        };

                        int global_row = task.start_row - halo + r;

                        if (global_row < 0) {
                            global_row = 0;
                        } else if (global_row >= height) {
                            global_row = height - 1;
                        }

                        filters[f].apply(&in_win, &out_win,
                                         filters[f].strength,
                                         height, global_row);
                    }

                    pixel *tmp = current;
                    current = next;
                    next = tmp;
                }

                /* Copy the actual task rows into the outgoing buffer. */
                for (int r = 0; r < block_rows; r++) {
                    memcpy(block_buffer + (size_t)r * width,
                           current + (size_t)(r + halo) * width,
                           (size_t)width * sizeof(pixel));
                }

                if (write_all(fd_c2p[i][1], &task, sizeof(task))
                    != (ssize_t)sizeof(task)) {
                    perror("write task to parent");
                    _exit(EXIT_FAILURE);
                }

                size_t bytes = (size_t)block_rows * width * sizeof(pixel);

                if (write_all(fd_c2p[i][1], block_buffer, bytes)
                    != (ssize_t)bytes) {
                    perror("write pixels to parent");
                    _exit(EXIT_FAILURE);
                }
            }

            free(block_buffer);
            free(curr_block);
            free(next_block);

            if (read_ret == -1) {
                perror("read task from parent");
                _exit(EXIT_FAILURE);
            }

            if (read_ret != 0) {
                fprintf(stderr, "partial task read in worker\n");
                _exit(EXIT_FAILURE);
            }

            safe_close(fd_p2c[i][0], "close child p2c read");
            safe_close(fd_c2p[i][1], "close child c2p write");

            _exit(EXIT_SUCCESS);
        } else if (ret > 0) {
            /* Parent: close unused ends. */
            safe_close(fd_p2c[i][0], "close parent p2c read");
            safe_close(fd_c2p[i][1], "close parent c2p write");
        } else {
            perror("fork");
            exit(EXIT_FAILURE);
        }
    }

    pixel *processed_pixels =
        malloc((size_t)height * width * sizeof(pixel));

    if (!processed_pixels) {
        perror("malloc processed pixels");
        exit(EXIT_FAILURE);
    }

    int next_task = 0;
    int tasks_completed = 0;
    int worker_open[NUM_WORKERS];
    int worker_busy[NUM_WORKERS];

    for (int i = 0; i < NUM_WORKERS; i++) {
        worker_open[i] = 1;
        worker_busy[i] = 0;
    }

    /* Give each worker its first task. */
    for (int i = 0; i < NUM_WORKERS && next_task < NUM_TASKS; i++) {
        if (write_all(fd_p2c[i][1], &tasks[next_task],
                      sizeof(Task)) != (ssize_t)sizeof(Task)) {
            perror("write initial task");
            exit(EXIT_FAILURE);
        }

        worker_busy[i] = 1;
        next_task++;
    }

    /* Close pipes for workers that did not receive a task. */
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (!worker_busy[i] && worker_open[i]) {
            safe_close(fd_p2c[i][1], "close idle p2c");
            worker_open[i] = 0;
        }
    }

    /* Dynamically schedule tasks as workers finish. */
    while (tasks_completed < NUM_TASKS) {
        fd_set read_set;
        FD_ZERO(&read_set);

        int maxfd = -1;

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
            exit(EXIT_FAILURE);
        }

        int ready = select(maxfd + 1, &read_set, NULL, NULL, NULL);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("select");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < NUM_WORKERS; i++) {
            if (!worker_busy[i] ||
                !FD_ISSET(fd_c2p[i][0], &read_set)) {
                continue;
            }

            Task finished_task;

            ssize_t r = read_all(fd_c2p[i][0],
                                 &finished_task, sizeof(finished_task));

            if (r != (ssize_t)sizeof(finished_task)) {
                fprintf(stderr,
                        "Failed to read completed task metadata\n");
                exit(EXIT_FAILURE);
            }

            printf("Parent got task %d from worker %d\n",
                   finished_task.task_id, i);
            fflush(stdout);

            int rows = finished_task.end_row - finished_task.start_row;
            size_t bytes = (size_t)rows * width * sizeof(pixel);

            pixel *block_buffer = malloc(bytes);

            if (!block_buffer) {
                perror("malloc returned block");
                exit(EXIT_FAILURE);
            }

            ssize_t buffer_ret = read_all(fd_c2p[i][0],
                                          block_buffer, bytes);

            if (buffer_ret != (ssize_t)bytes) {
                fprintf(stderr,
                        "Failed to read completed task pixels\n");
                free(block_buffer);
                exit(EXIT_FAILURE);
            }

            printf("Copying output for task %d\n",
                   finished_task.task_id);
            fflush(stdout);

            memcpy(processed_pixels
                       + (size_t)finished_task.start_row * width,
                   block_buffer, bytes);

            free(block_buffer);
            tasks_completed++;

            /* Give the worker another task, if any remain. */
            if (next_task < NUM_TASKS) {
                if (write_all(fd_p2c[i][1], &tasks[next_task],
                              sizeof(Task)) != (ssize_t)sizeof(Task)) {
                    perror("write next task");
                    exit(EXIT_FAILURE);
                }

                worker_busy[i] = 1;
                next_task++;
            } else {
                worker_busy[i] = 0;

                if (worker_open[i]) {
                    safe_close(fd_p2c[i][1], "close finished p2c");
                    worker_open[i] = 0;
                }
            }
        }
    }

    /* Close all parent read ends. */
    for (int i = 0; i < NUM_WORKERS; i++) {
        safe_close(fd_c2p[i][0], "close parent c2p");
    }

    printf("Waiting for all children to finish...\n");
    fflush(stdout);

    for (int i = 0; i < NUM_WORKERS; i++) {
        int status;

        if (waitpid(ids[i], &status, 0) == -1) {
            perror("waitpid");
            exit(EXIT_FAILURE);
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Worker %d exited unsuccessfully\n", i);
            exit(EXIT_FAILURE);
        }

        printf("Worker %d child finished\n", i);
        fflush(stdout);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    double processing_time = elapsed_seconds(start, end);

    /* Output writing is excluded from the processing time. */
    printf("Writing to output image %s\n", out_filename);
    fflush(stdout);

    if (write_bitmap(out_filename, header, processed_pixels) != 0) {
        fprintf(stderr, "Failed to write concurrent output: %s\n",
                out_filename);
        exit(EXIT_FAILURE);
    }

    printf("Processing complete!\n");
    fflush(stdout);

    free(processed_pixels);
    return processing_time;
}

/* ---------- Public entry point ---------- */

void process_filters(Filter *filters, int count, pixel **pixels,
                     BMPHeader *header, char *out_filename) {
    if (!filters || !pixels || !header || !out_filename) {
        fprintf(stderr, "process_filters received a NULL argument\n");
        exit(EXIT_FAILURE);
    }

    int height = header->height;
    int width = header->width;

    if (height <= 0 || width <= 0) {
        fprintf(stderr, "Invalid image dimensions: %d x %d\n",
                width, height);
        exit(EXIT_FAILURE);
    }

    if (height < NUM_TASKS) {
        fprintf(stderr,
                "Error: image height (%d) is smaller than NUM_TASKS (%d).\n"
                "Current implementation requires height >= NUM_TASKS.\n",
                height, NUM_TASKS);
        exit(EXIT_FAILURE);
    }

    if (count <= 0) {
        fprintf(stderr, "Filter count must be positive\n");
        exit(EXIT_FAILURE);
    }

    Task *tasks = malloc(NUM_TASKS * sizeof(*tasks));

    if (!tasks) {
        perror("malloc tasks");
        exit(EXIT_FAILURE);
    }

    int rows_per_task = height / NUM_TASKS;
    int remainder = height % NUM_TASKS;
    int row = 0;

    for (int i = 0; i < NUM_TASKS; i++) {
        int rows = rows_per_task + (i < remainder ? 1 : 0);

        tasks[i].task_id = i;
        tasks[i].start_row = row;
        tasks[i].end_row = row + rows;

        row += rows;
    }

    const char *mode = getenv("BENCH_MODE");

    if (!mode || mode[0] == '\0') {
        mode = "concurrent";
    }

    if (strcmp(mode, "sequential") == 0) {
        double seconds = run_sequential(
            filters, count, pixels, header, out_filename, tasks);

        printf("\n=== Performance ===\n");
        printf("Mode: Sequential\n");
        printf("Tasks: %d\n", NUM_TASKS);
        printf("Elapsed processing time: %.6f seconds\n", seconds);
    } else if (strcmp(mode, "concurrent") == 0) {
        double seconds = run_concurrent(
            filters, count, pixels, header, out_filename, tasks);

        printf("\n=== Performance ===\n");
        printf("Mode: Concurrent (%d worker processes)\n", NUM_WORKERS);
        printf("Tasks: %d\n", NUM_TASKS);
        printf("Elapsed processing time: %.6f seconds\n", seconds);
    } else if (strcmp(mode, "both") == 0) {
        size_t filename_len = strlen(out_filename);
        const char suffix[] = ".sequential.bmp";

        char *seq_filename = malloc(filename_len + sizeof(suffix));

        if (!seq_filename) {
            perror("malloc sequential filename");
            free(tasks);
            exit(EXIT_FAILURE);
        }

        memcpy(seq_filename, out_filename, filename_len);
        memcpy(seq_filename + filename_len, suffix, sizeof(suffix));

        double seq_seconds = run_sequential(
            filters, count, pixels, header, seq_filename, tasks);

        double concurrent_seconds = run_concurrent(
            filters, count, pixels, header, out_filename, tasks);

        printf("\n=== Performance Comparison ===\n");
        printf("Sequential processing time: %.6f seconds\n", seq_seconds);
        printf("Concurrent processing time: %.6f seconds\n",
               concurrent_seconds);

        if (concurrent_seconds > 0.0) {
            double speedup = seq_seconds / concurrent_seconds;

            printf("Speedup (sequential / concurrent): %.3fx\n", speedup);
            printf("Parallel efficiency (speedup / %d): %.2f%%\n",
                   NUM_WORKERS, speedup / NUM_WORKERS * 100.0);
        }

        printf("Sequential output: %s\n", seq_filename);
        printf("Concurrent output: %s\n", out_filename);

        free(seq_filename);
    } else {
        fprintf(stderr,
                "Unknown BENCH_MODE '%s'. "
                "Use sequential, concurrent, or both.\n",
                mode);

        free(tasks);
        exit(EXIT_FAILURE);
    }

    free(tasks);
}
