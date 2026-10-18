# GPU in LordOS: composition, dmabuf and games

lord-server has two paths for building the screen:

- **CPU** (the usual one): the UI and the app windows are copied into the canvas and a dumb
  buffer is sent with a page flip. It needs nothing besides the kernel.
- **GPU** (new, `server/src/gpu/gpu.c`): Mesa's GBM + EGL + GLES2. The user approved it on
  2026-10-16 for games and GPU apps on AMD and Nvidia. See "Dependency policy" in
  `ARCHITECTURE.md`.

Modesetting is still our own (`kms.c`, ioctls without libdrm). GBM is created on the same card fd,
and its buffers and the apps' dmabufs become framebuffers with `DRM_IOCTL_MODE_ADDFB2`.

## Screenshots (VM 116, Mesa llvmpipe forced with `LORD_GPU=1`)

| | |
|---|---|
| ![ui](gpu-ui.png) | ![gtk](gpu-gtk.png) |
| The LordOS UI composited by EGL/GLES2 | GTK Demo (shm) under the Dock: the stacking order between app windows and LordOS windows is respected |
| ![menu](gpu-menu.png) | ![dmabuf](gpu-dmabuf.png) |
| GTK menu (xdg_popup) on the top layer | `dmabuf-client`: an app's dmabuf buffers (udmabuf, linear) at 72 FPS, 400/400 presented |
| ![fs](gpu-fullscreen.png) | |
| Fullscreen: the compositor presents it on top of everything | |

## How the path is chosen

1. `LORD_GPU=0`: always CPU.
2. The kernel is asked which driver the card has (`DRM_IOCTL_VERSION`). If it's one without 3D
   (simpledrm, bochs, cirrus, qxl, ast, mgag200, vkms, udl, virtio-gpu without virgl…), the CPU
   is used **without loading Mesa**: lord-server stays at ~32 MB.
3. Otherwise, GBM + EGL are set up. If `GL_RENDERER` is a software one (llvmpipe, softpipe), it
   falls back to the CPU, because compositing with llvmpipe costs more than our own path (see
   measurements). `LORD_GPU=1` forces the GPU anyway, for testing.
4. If a frame fails (eglSwapBuffers, ADDFB2, page flip), the GPU is turned off and the CPU path
   takes over.

Diagnostics: `LORD_GPU_DEBUG=1` shows the dmabuf formats, failing imports and the frames sent by
direct scanout. The log says `gpu: GPU composition (renderer…)` or why not.

## How it composites

- **The UI** (React → CPU canvas) is a fullscreen texture. Per frame only what changed (the
  damage) is uploaded, with `GL_EXT_unpack_subimage`.
- **App windows** don't go through the canvas. Wherever a window goes, the UI leaves a
  **transparent hole** (alpha 0) and the surface is recorded. When presenting:
  1. the window textures (layer 0);
  2. on top, the UI with its premultiplied alpha;
  3. on top of everything, the apps' menus, fullscreen and the cursor (layer 1).

  If a LordOS window (or the Dock) covers an app's window, it really covers it. If one app covers
  another, too: the hole was opened later.
- **The canvas keeps alpha.** Our own blends (`canvas.c`, `term_paint.c`) now compute the output
  alpha. Nothing changes on the CPU path (KMS is XRGB).
- **Background blur** (Dock, Control Center) also carries alpha. Over an app window it tints but
  doesn't blur, because the window isn't in the canvas. Blurring app content is pending; it needs
  a GPU blur pass.
- **Content:**
  - shm: the usual copy, uploaded to the surface's texture when it changes;
  - dmabuf: `EGL_EXT_image_dma_buf_import(_modifiers)` and `GL_OES_EGL_image_external`, with no
    copies. One EGLImage per buffer, reused.
- **Repainting:** when an app only changes its content, the UI isn't repainted (no CPU per app
  frame); it's just presented again. If the size or geometry (the shape of the hole) changes, it
  is.
- **Direct scanout:** if the fullscreen window has a dmabuf the size of the output
  (XRGB/ARGB8888, one plane), there are no menus on top and the cursor goes through the cursor
  plane, its buffer goes straight to the primary plane (`PRIME_FD_TO_HANDLE` + `ADDFB2` + page
  flip): zero copies. The buffer is returned to the app when it's no longer on screen.
- **Cursor:** on the GPU path it goes through the KMS cursor plane (`DRM_IOCTL_MODE_CURSOR2`), so
  moving the mouse composites nothing. If the driver has no cursor plane, it's drawn as a quad
  (`LORD_SW_CURSOR=1` forces it).

## New protocols

| Protocol | Version | What it does |
|---|---|---|
| `zwp_linux_dmabuf_v1` | 4 | The apps' GPU buffers, with feedback: format and modifier table, main render node. With GPU, whatever EGL imports; on the CPU path, only linear ARGB/XRGB8888, which is mapped (`DMA_BUF_IOCTL_SYNC`) and copied. If EGL can't import a linear dmabuf, it's mapped anyway |
| `wp_presentation` | 1 | Time and vblank number of the flip that showed each frame (KMS's `flip_ns`/`flip_seq`) |
| `wp_viewporter` | 1 | Cropping and scaling of a surface (GPU path; the CPU path scales with a bilinear blit since `DISPLAYS.md`) |
| `zwp_relative_pointer_manager_v1` | 1 | Raw mouse motion, without edges (first-person games) |
| `zwp_pointer_constraints_v1` | 1 | Lock or confine the pointer to the window; locked: no `motion`, fixed cursor |
| `xdg_toplevel.set_fullscreen` | — | Now real fullscreen: it takes the output, above everything, with the keyboard; **❖Esc** exits |
| `wl_pointer.set_cursor` | — | The app's cursor (image or **hidden**, as games do) while the pointer is over it |

Several displays and HiDPI (`DISPLAYS.md`): one GBM/EGL surface and UI texture per display on a
single context; app textures are shared and drawn at each display's scale; `wp_fractional_scale_v1`
and `zxdg_output_manager_v1` are there. Direct scanout only with one display.

Not there: `xdg_activation` and explicit sync (`linux-drm-syncobj`). dma-resv
implicit sync is used.

## Tested (VM 116 `lordos-gpu`)

There's no real GPU in the VM: the Proxmox host only has the ASPEED card, with no render node, so
virgl doesn't start. virtio-gpu 2D was used with software Mesa inside (llvmpipe on kms_swrast).
That really exercises the GBM/EGL/GLES/KMS code, but **not** the AMD or Nvidia drivers.

| Test | Result |
|---|---|
| UI via EGL/GLES2 (llvmpipe, `LORD_GPU=1`) | looks the same as via CPU (screenshot) |
| GTK Demo (shm), menus, Dock on top | correct layer order; clicks and menus work |
| `glmark2-es2-wayland` 640×400 | 75 FPS on GPU (synced to the 75 Hz vblank); uses shm, because llvmpipe in the app doesn't use dmabuf |
| `vkcube` (lavapipe) | works (300 frames), via shm |
| `dmabuf-client` (linear udmabuf) in a window | GPU: 72 FPS, 400/400 presented (wp_presentation); CPU: 131 FPS |
| `dmabuf-client --fullscreen` 1280×800 | GPU: 42.9 FPS; CPU: 42.5 FPS; on top of everything and with keyboard |
| `dmabuf-client --lock` (lock + relative + hidden cursor) | locked; relative events arrive; 0 absolute while locked; hidden cursor; on GPU and on CPU |
| neverball (SDL2 + OpenGL over Wayland) | forced GPU: ~20 frames/s; CPU: ~24 frames/s (llvmpipe: the game and the compositor compete for the same CPU) |
| Automatic selection in the VM | "virtio_gpu without 3D" → CPU without loading Mesa: 32 MB of RSS |
| `LORD_GPU=0` | everything as before |

lord-server CPU with `glmark2-es2-wayland` in a window for 10 s:

| Path | CPU | RSS |
|---|---|---|
| GPU with llvmpipe (`LORD_GPU=1`) | ~300 % (llvmpipe composites 1280×800 per frame with several threads) | 158 MB |
| CPU | ~100 % | 36 MB |

With llvmpipe the GPU isn't worth it; that's why CPU is the default. With a real card,
compositing is the GPU's work, and the CPU only uploads what changed in the UI.

**Untested:**
- **Real AMD and Nvidia:** pending the live ISO on the user's PC (AMD).
- **Importing a GPU dmabuf via EGL:** virtio-gpu doesn't accept dmabufs from another source, so in
  the VM that import fails (`EGL_BAD_ALLOC`) and udmabufs are mapped.
- **Direct scanout:** in the VM it fails for the same reason. The code is there and falls back to
  compositing if it fails.

To test it on the AMD PC (live ISO):
1. Boot.
2. In the LordOS Terminal, `grep gpu /tmp/lord.log` (or whatever log the ISO uses): it must say
   `GPU composition (AMD Radeon …)` and `cursor by hardware (cursor plane)`.
3. `vkcube`, `glmark2-es2-wayland`, a game (SuperTuxKart, 0 A.D.).
4. With `LORD_GPU_DEBUG=1`, in fullscreen `direct scanout: N frames` must appear.

## Test client

`server/tests/dmabuf_client.c` (`make -C server build/dmabuf-client`, needs
`libwayland-client` and `/dev/udmabuf`, which in Debian belongs to the `kvm` group):

```sh
build/dmabuf-client [--fullscreen] [--lock] [--frames N] [--size 800x600]
```

It prints frames, FPS, presented/discarded and, with `--lock`, the pointer events.

## Packages

To build with GPU (if they're missing, `make` builds without GPU, `GPU=0`):

```sh
sudo apt-get install -y --no-install-recommends libgbm-dev libegl-dev libgles-dev
```

At runtime: `libgbm1`, `libegl1`, `libgles2`, `libegl-mesa0` and the driver
(`libgl1-mesa-dri`: radeonsi/AMD, iris/Intel, nouveau; Nvidia: its driver with GBM, or NVK).

Only for testing in VM 116: `mesa-utils`, `mesa-utils-bin`, `mesa-vulkan-drivers`,
`vulkan-tools`, `glmark2-es2-wayland` and `neverball`. The user `lord` was added to the `kvm`
group for `/dev/udmabuf`.

On the Proxmox host `libgl1` and `libegl1` were installed (`vga: virtio-gl` requires them). Still,
virgl doesn't work without a render node on the host, and VM 116 was left with `vga: virtio`.

## VM 116 (`lordos-gpu`)

- Full clone of 115 (`qm clone 115 116 --name lordos-gpu --full`), with `vga: virtio`
  (2D).
- IP 172.17.0.52 via cloud-init (`ipconfig0`), snippet `local:snippets/lordos-gpu.yaml` (same as
  115's with another hostname), new MAC.
- It's for the GPU path. 115 is still the development VM and wasn't touched.

## Pending

- Test on real AMD (live ISO) and on Nvidia.
- UI blur that includes app content (GPU pass).
- Direct scanout of non-fullscreen windows (overlay planes).
- `linux-drm-syncobj` (explicit sync, the one Nvidia prefers), `xdg_activation`, tearing control (`wp_tearing_control`) and VRR.
- Letting JS know a window is fullscreen (today it receives the `fullscreen` event, but the themes
  ignore it).
