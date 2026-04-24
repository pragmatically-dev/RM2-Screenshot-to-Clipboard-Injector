# ReMarkable Clipboard Injector

PLEASE READ [REPORT.md](REPORT.md) FIRST

A native C++/Qt extension for the ReMarkable 2 tablet that allows you to instantly capture any region of the screen (using an interactive drag-to-select overlay) and paste it back into your notebooks as editable vector strokes.

## Features
- **Snipping Tool Overlay**: Darkens the screen and lets you draw a selection rectangle over the content you want to copy.
- **NEON-Accelerated Processing**: Captures the raw framebuffer and performs high-speed Gaussian blurring and Laplacian edge detection entirely on-device using ARM NEON intrinsics.
- **Native Vector Paste**: Converts the selected area into editable vector lines and injects them directly into the `xochitl` scene graph via the tablet's clipboard. No PDF disruption.

## Repository Structure
- `clipboard-injector/`: The main C++/Qt source code and `.qmd` UI injection scripts.
- `qmldiff_repo/`: Rust tool used to hash the QMD files before deployment.
- `deploy.py`: Python script to push the compiled `.so` extension to the tablet and restart `xochitl`.
- `hashtab.txt`: The QML hash mapping file required by `qmldiff` to patch the tablet's UI safely.

## Build Instructions
The project uses a cross-compilation Docker container. To build the extension:

1. Update your QMD files (if modifying the UI) and hash them:
   ```bash
   docker run --rm -v "%CD%:/work" -w /work rust:latest ./qmldiff_repo/target/release/qmldiff hash-diffs hashtab.txt clipboard-injector/plain.qmd
   ```
2. Build the Docker container (which cross-compiles the C++ code with NEON support):
   ```bash
   cd clipboard-injector
   docker build --no-cache -t clipboard-injector .
   ```
3. Extract the compiled `.so`:
   ```bash
   docker create --name ci-build clipboard-injector
   docker cp ci-build:/src/clipboard-injector.so ./clipboard-injector.so
   docker rm ci-build
   ```

## Deployment
Run the deployment script to send the extension to the ReMarkable tablet over USB/Wi-Fi:
```bash
python deploy.py
```
*(Make sure to update the IP and SSH password inside `deploy.py` if necessary).*

## Acknowledgments
This project would not have been possible without the incredible foundational work, tools, and research provided by the ReMarkable homebrew community. Special thanks to the following individuals for their related contributions:
- **[asivery](https://github.com/asivery)** - For `xovi`, `qmldiff`, and `framebuffer-spy`.
- **[HookedBehemoth](https://github.com/HookedBehemoth)** - For reverse-engineering efforts and QML research.
- **[ingatellent](https://github.com/ingatellent)** - For the `stickers` extension which inspired the vtable injection approach.
- **[rmitchellscott](https://github.com/rmitchellscott)** - For `rm-shot` and framebuffer extraction logic.
- **[ricklupton](https://github.com/ricklupton)** - For `rmscene` and deep documentation of the native `.rm` stroke data formats.
