# ReMarkable Clipboard Injector: Technical Report

## Overview
The **ReMarkable Clipboard Injector** is a native C++/Qt6 extension for the ReMarkable 2 tablet. It introduces an interactive "Crop-to-Paste" feature, allowing users to select a region of their screen (using a visual overlay similar to the Windows Snipping Tool) and instantly convert the pixels in that region into editable, high-resolution vector strokes injected directly into the tablet's clipboard.

This project was built entirely through pair-programming between a human developer and an AI (Google DeepMind's Antigravity).

## Architecture & How It Works

The program operates across three primary layers: the QML UI injection, the C++ Backend (NEON Accelerated), and the Scene Graph Vtable Bridge.

### 1. UI Injection (QML / QMD)
The ReMarkable interface (`xochitl`) is built with Qt/QML. Instead of modifying the binary, we use `qmldiff` (via the `xovi` extension manager) to dynamically patch the QML tree at runtime.
- **Toolbar Buttons**: We inject two buttons into the `Toolbar.qml` component (Crop and Paste).
- **Selection Overlay**: When the Crop button is pressed, an invisible `Item` injected into `DocumentView.qml` (re-parented to the global `sceneView`) becomes visible. It dims the screen (`opacity: 0.3`) and uses a `MouseArea` to track the stylus or touch input, drawing a white bounding box.
- **Trigger**: Upon release, the coordinates are passed to the C++ backend via `ClipboardInjector.captureArea(x, y, w, h)`.

### 2. The C++ Backend (Image Processing Pipeline)
When `captureArea` is invoked, the C++ backend executes a high-performance image processing pipeline:
1. **Framebuffer Access**: It uses symbols from the `framebuffer-spy` extension to read the raw `/dev/fb0` memory.
2. **Grayscale Conversion**: The raw BGRA or RGB565 memory is converted to an 8-bit grayscale format.
3. **Gaussian Blur**: A 3x3 convolution is applied to smooth out anti-aliasing artifacts from the e-ink display.
4. **Laplacian Edge Detection**: A strict edge-detection kernel isolates the structural outlines of the text or drawings.
5. **Vectorization**: The algorithm scans the resulting edge map horizontally. For every contiguous dark pixel, it generates a microscopic horizontal line segment (length of 1 pixel). 

**ARM NEON Optimization**: Because the ReMarkable 2 uses a Cortex-A7 (ARMv7) processor with limited clock speed, steps 2, 3, and 4 are heavily vectorized using ARM NEON intrinsics. This allows the tablet to process full-screen captures (1404x1872) in a fraction of a second without blocking the UI thread.

### 3. Scene Graph Vtable Bridge & Clipboard
The ReMarkable clipboard requires lines to be instantiated as native `SceneLineItem` C++ objects. 
- Because we compile our extension outside of the official `xochitl` source tree, we lack the internal Virtual Method Tables (vtables) required to instantiate these objects directly.
- **The Hack**: We instruct the QML layer to draw a microscopic "dummy" line using native tools (`sceneController.addDrawingLine`). We then clone it, steal its vtable pointer, and apply it to our custom structs. 
- Once the vtable is secured, the backend writes a JSON file (`/tmp/clipboard_inject.json`) representing the lines (using tool ID 15 "Fineliner" and thickness `0.25` for maximum sharpness). The QML layer reads this JSON and populates the native `Clipboard.items`.

## Dependencies
This project relies heavily on the open-source ReMarkable modding ecosystem:
- **[xovi](https://github.com/asivery/xovi)**: The extension loader and manager.
- **[qmldiff](https://github.com/asivery/qmldiff)**: Used to hash and patch the `xochitl` QML files without altering the base filesystem.
- **[framebuffer-spy](https://github.com/asivery/rm-xovi-extensions/tree/master/framebuffer-spy)**: A `xovi` extension used to safely access the `xochitl` framebuffer dimensions and memory address.
- **eeems/remarkable-toolchain**: Dockerized cross-compilation environment for Qt6 and ARMv7.

## Current Limitations & Compatibility
- **Architecture**: This project is strictly compiled for **ARMv7 with NEON support (`armv7-a -mfpu=neon -mfloat-abi=hard`)**. It is designed specifically for the ReMarkable 2 (and potentially ReMarkable 1, depending on framebuffer formats). It will not work on x86, ARM64 (e.g., ReMarkable Paper Pro), or standard Linux distributions.
- **Firmware**: It targets Qt6-based `xochitl` firmware (version 3.0+). 

## AI Transparency Statement
This project, including the image processing algorithms, ARM NEON intrinsics, QML injection vectors, and reverse-engineering of the ReMarkable's clipboard data structures, was collaboratively authored by a human developer and an AI assistant (Google DeepMind). The AI was responsible for writing the C++ optimizations, debugging memory segmentation faults in the QML parser, and structuring the Dockerized deployment pipeline, while the human developer provided the architectural vision, device testing, and overall project direction.
