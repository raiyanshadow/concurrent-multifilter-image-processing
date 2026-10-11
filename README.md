# Concurrent Multi-Filter Image Processing

A concurrent image-processing application written in C that applies customizable sequences of image filters to 24-bit and 32-bit BMP images. The application uses a **process-based worker pool**, POSIX pipes, and dynamic task scheduling to process image regions concurrently while preserving correctness across block boundaries.

## Overview

The application divides an image into row-based tasks and distributes them across worker processes. Each worker applies the requested filters to its assigned region and sends the processed pixels back to the parent process, which reconstructs the final image.

Rather than waiting for workers in a fixed order, the parent uses `select()` to monitor worker pipes and dispatch new tasks as workers finish. This allows workers to receive more work as soon as they become available, while results can be collected in a different order from the order in which tasks were dispatched.

The project explores operating-system concepts including process creation, inter-process communication (IPC), concurrent execution, file-descriptor management, and robust pipe I/O.

## Features

- **10 image filters:** Grayscale, vignette, emboss, sharpen, glitch effect, Gaussian blur, pixelate, Laplacian edge detection, invert, and black and white.
- **Customizable filter pipelines:** Apply filters sequentially in a user-specified order, with configurable strengths for supported filters.
- **Multiprocess parallelism:** Distribute image-processing tasks across a pool of worker processes.
- **Dynamic task scheduling:** Use `select()` to monitor worker pipes and assign new tasks as workers become available.
- **POSIX pipe communication:** Exchange task metadata and processed pixel buffers between the parent and worker processes.
- **Boundary-correct filtering:** Use halo rows to preserve neighboring-pixel context when applying sequential filters to partitioned images.
- **Robust I/O handling:** Use `read_all()` and `write_all()` to handle partial reads and writes, alongside process cleanup and error checking.
- **BMP support:** Process 24-bit and 32-bit bitmap images while preserving the alpha channel for 32-bit images.

## Example Files and Results

The [`images/`](images/) directory contains the original sample images and PNG previews of several filter pipelines. These previews let GitHub display the results directly in this README.

### Test image: multiple-filter pipeline

<table>
  <tr>
    <th>Original</th>
    <th>Pixelate + Gaussian blur + vignette + glitch + pixelate</th>
  </tr>
  <tr>
    <td><img src="images/test.png" alt="Original test image" width="420"></td>
    <td><img src="images/test_pixelate30_gaussian_blur50_vignette80_glitch_effect10_pixelate10.png" alt="Test image processed with pixelation, Gaussian blur, vignette, glitch effect, and a second pixelation pass" width="420"></td>
  </tr>
</table>

Reproduce the filtered image with:

```bash
./main test.bmp 5 pixelate30 gaussian_blur50 vignette80 glitch_effect10 pixelate10
```

### City image: edge detection and glitch effect

<table>
  <tr>
    <th>Original city image</th>
    <th>Edge detection + glitch effect</th>
  </tr>
  <tr>
    <td><img src="images/city.png" alt="Original city image" width="420"></td>
    <td><img src="images/city_edge_detection_glitch_effect20.png" alt="City image processed with edge detection and a glitch effect at strength 20" width="420"></td>
  </tr>
</table>

Reproduce this result with:

```bash
./main city.bmp 2 edge_detection glitch_effect20
```

### City image: grayscale, Gaussian blur, and sharpen

<table>
  <tr>
    <th>Concurrent output</th>
    <th>Sequential output</th>
  </tr>
  <tr>
    <td><img src="images/city_grayscale_gaussian_blur40_sharpen.png" alt="City image processed with grayscale, Gaussian blur strength 40, and sharpening using the concurrent implementation" width="420"></td>
    <td><img src="images/city_grayscale_gaussian_blur40_sharpen.bmp.sequential.png" alt="City image processed with grayscale, Gaussian blur strength 40, and sharpening using the sequential implementation" width="420"></td>
  </tr>
</table>

The two outputs above use the same filter pipeline and provide a visual comparison of the sequential and process-based implementations. The corresponding command is:

```bash
./main city.bmp 3 grayscale gaussian_blur40 sharpen
```

## Concurrency Demonstration

The application uses a worker pool and dynamic task assignment rather than assigning one fixed region to each worker for the entire run. The debug output makes this behavior observable:

- `Child ... task ...` reports the row range assigned to a worker.
- `Parent got task ... from worker ...` shows the parent receiving a completed task.
- `Copying output for task ...` marks when the returned pixels are copied into the output image.
- `Worker ... child finished` confirms that a worker process has been reaped.

Because workers finish at different times, task-completion messages need not appear in task-ID order. When a worker completes a task, the parent can dispatch another task to it, keeping available workers occupied without blocking on one specific worker's result.

For example, run a sample pipeline and inspect the worker/task messages:

```bash
BENCH_MODE=concurrent ./main test.bmp 5 pixelate30 gaussian_blur50 vignette80 glitch_effect10 pixelate10
```

A shortened excerpt from a run illustrates workers being assigned new tasks while the parent collects completed results:

```text
Child 2 task 12: start=108, end=117
Parent got task 5 from worker 5
Copying output for task 5
Child 5 task 13: start=117, end=126
Parent got task 4 from worker 4
Copying output for task 4
Child 4 task 14: start=126, end=135
```

Here, each task processes a range of image rows. The parent does not need to collect results in task-ID order: it monitors worker pipes, copies each returned result into the correct output region, and dispatches more work as workers become available. The exact message order varies between runs because worker processes execute concurrently and are scheduled by the operating system.

## Performance Benchmarking

The program includes benchmark modes for comparing the sequential implementation with the process-based concurrent implementation. Use the **same input image and filter sequence** for each run so that the timings are comparable.

### Concurrent run

```bash
BENCH_MODE=concurrent ./main city.bmp 3 grayscale gaussian_blur40 sharpen
```

### Sequential run

```bash
BENCH_MODE=sequential ./main city.bmp 3 grayscale gaussian_blur40 sharpen
```

### Run both and compare

```bash
BENCH_MODE=both ./main city.bmp 3 grayscale gaussian_blur40 sharpen
```

In `both` mode, the sequential and concurrent implementations process the same input with the same filter pipeline. The sequential result is written to a filename ending in `.sequential.bmp`, while the concurrent result is written to the normal output filename. The program reports the measured processing times and speedup.

### Recorded benchmark: five-filter test pipeline

The following result was measured using `test.bmp` (640 × 426 pixels), 12 worker processes, and this pipeline: `pixelate30`, `gaussian_blur50`, `vignette80`, `glitch_effect10`, and `pixelate10`.

```bash
BENCH_MODE=both ./main test.bmp 5 pixelate30 gaussian_blur50 vignette80 glitch_effect10 pixelate10
```

| Metric | Measured result |
|---|---:|
| Sequential processing time | 2.817512 s |
| Concurrent processing time | 0.245564 s |
| Speedup | 11.474× |
| Parallel efficiency (12 workers) | 95.61% |
| Processing-time reduction | 91.3% |

The concurrent run took about 8.7% of the sequential runtime for this particular workload. The corresponding filtered preview is [`images/test_pixelate30_gaussian_blur50_vignette80_glitch_effect10_pixelate10.png`](images/test_pixelate30_gaussian_blur50_vignette80_glitch_effect10_pixelate10.png).

This is one measured run, not a universal performance guarantee. Run benchmarks more than once and compare results rather than relying on a single timing. Process creation, pipe setup, IPC, scheduling, image size, hardware, and system load all affect runtime. Small images may not benefit from parallel execution because process and communication overhead can outweigh the work saved; larger images are generally a more informative test of the parallel implementation.

## Tech Stack

`C` · `POSIX Processes` · `fork()` · `POSIX Pipes` · `select()` · `waitpid()` · `Make`

## Architecture

The application follows a parent–worker architecture.

1. **Partitioning:** The parent divides the image into row-based tasks.
2. **Worker creation:** The parent creates worker processes using `fork()` and establishes dedicated pipes for communication with each worker.
3. **Task dispatch:** The parent sends task metadata to available workers through parent-to-worker pipes.
4. **Concurrent processing:** Each worker loads the required image rows, including halo rows, and applies the requested filters to its assigned region.
5. **Completion detection:** The parent monitors worker-to-parent pipes with `select()` to identify completed tasks without blocking on a particular worker.
6. **Result collection:** Workers return task metadata and processed pixel buffers. The parent places each result into the correct image region.
7. **Finalization:** Once all tasks are complete, the parent writes the reconstructed image to a new BMP file and reaps the worker processes using `waitpid()`.

### Handling Filter Boundaries

Some filters, including Gaussian blur, sharpen, emboss, and Laplacian edge detection, depend on neighboring pixels. Processing isolated blocks without additional context can produce incorrect results near block boundaries, especially when multiple filters are applied sequentially.

To address this, workers process additional **halo rows** around their assigned regions. The halo size is based on the number of filters in the pipeline, allowing intermediate filtering stages to access neighboring rows while ensuring that only the task's assigned rows are returned to the parent.

### Inter-Process Communication

Each worker has dedicated parent-to-worker and worker-to-parent pipes.

- **Parent → Worker:** A `Task` structure containing a task ID and the start and end rows.
- **Worker → Parent:** The completed task metadata followed by the processed pixel buffer.

The `read_all()` and `write_all()` helpers repeatedly perform system calls until the requested data has been transferred or an error occurs. This handles partial pipe reads and writes rather than assuming a single system call transfers the entire message.

## Available Filters

| Filter | Description |
|---|---|
| `grayscale` | Convert pixels to grayscale |
| `vignette` | Darken the image toward its edges |
| `emboss` | Create an embossed, relief-like effect |
| `sharpen` | Enhance edges and local contrast |
| `glitch_effect` | Apply a glitch-style visual distortion |
| `gaussian_blur` | Smooth the image using a Gaussian kernel |
| `pixelate` | Create a pixelated appearance |
| `edge_detection` | Detect edges using a Laplacian operator |
| `invert` | Invert pixel colors |
| `black_and_white` | Convert the image to black and white |

Filters that support a strength parameter accept a value from 1 to 100 appended to the filter name, such as `vignette50` or `gaussian_blur25`.

## Build and Run

### Prerequisites

- A C compiler
- `make`
- A POSIX-compatible operating system supporting `fork()`, pipes, `select()`, and `waitpid()`
- A 24-bit or 32-bit BMP input image

### 1. Clone the repository

```bash
git clone https://github.com/raiyanshadow/concurrent-multifilter-image-processing.git
cd concurrent-multifilter-image-processing
```

### 2. Compile

```bash
make
```

### 3. Run

```bash
./main <input_image.bmp> <number_of_filters> <filter1> ... <filterN>
```

Provide the input image, the number of filters, and the filters in the order they should be applied.

**Example:**

```bash
./main test.bmp 3 vignette50 sharpen glitch_effect25
```

This applies a vignette with strength 50, sharpens the result, and then applies a glitch effect with strength 25. The output filename reflects the input image and selected filter sequence:

```text
test_vignette50_sharpen_glitch_effect25.bmp
```

Another example:

```bash
./main city.bmp 3 grayscale emboss glitch_effect50
```

This converts the image to grayscale, applies embossing, and finishes with a glitch effect.

**Input constraint:** The image height must be sufficient for the configured task partitioning. See the source configuration for the current worker and task counts.

## Error Handling and Resource Management

The application includes checks for invalid command-line arguments, unsupported filters, invalid filter strengths, unreadable input files, and unsupported BMP formats.

For process and pipe operations, the implementation checks system-call failures and handles incomplete data transfers. File descriptors are closed appropriately, worker processes are reaped after completion, and failures that prevent the image from being reconstructed cause the program to terminate with an error.

## Key Concepts Demonstrated

- Process-based parallelism and worker-pool design
- Inter-process communication using POSIX pipes
- I/O multiplexing with `select()`
- Dynamic task scheduling and load balancing
- Sliding-window image processing and convolution kernels
- Boundary handling for sequential filters
- Robust system-call handling and process lifecycle management
- Binary image formats and pixel-buffer manipulation
