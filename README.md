# Concurrent Multi-Filter Image Processing

A concurrent image-processing application written in C that applies customizable sequences of image filters to 24-bit and 32-bit BMP images. The application uses a **process-based worker pool**, POSIX pipes, and dynamic task scheduling to process image regions concurrently while preserving correctness across block boundaries.

## Overview

The application divides an image into row-based tasks and distributes them across multiple worker processes. Each worker applies the requested filters to its assigned region and sends the processed pixels back to the parent process, which reconstructs the final image.

Rather than waiting for workers in a fixed order, the parent uses `select()` to monitor worker pipes and dispatch new tasks as workers finish. This allows faster workers to receive additional work without waiting for slower workers.

The project explores operating-system concepts including process creation, inter-process communication (IPC), concurrent execution, file-descriptor management, and robust pipe I/O.

## Features

- **10 image filters:** Grayscale, vignette, emboss, sharpen, glitch effect, Gaussian blur, pixelate, Laplacian edge detection, invert, and black and white.
- **Customizable filter pipelines:** Apply multiple filters sequentially in a user-specified order, with configurable strengths for supported filters.
- **Multiprocess parallelism:** Distribute image-processing tasks across 24 worker processes.
- **Dynamic task scheduling:** Use `select()` to monitor worker pipes and assign new tasks as workers become available.
- **POSIX pipe communication:** Exchange task metadata and processed pixel buffers between the parent and worker processes.
- **Boundary-correct filtering:** Use halo rows to preserve neighboring-pixel context when applying sequential filters to partitioned images.
- **Robust I/O handling:** Implement `read_all()` and `write_all()` to handle partial reads and writes, alongside process cleanup and error checking.
- **BMP support:** Process 24-bit and 32-bit bitmap images while preserving the alpha channel for 32-bit images.

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
./main dog.bmp 3 grayscale emboss glitch_effect50
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
