# ps4-stream

Ultra-low-latency **720p60 / 1080p60 hardware H.264 live streamer** for jailbroken PlayStation 4 consoles, wrapped in standard **188-byte MPEG-TS (`video/mp2t`)** over `TCP_NODELAY`.

Watch your PS4 screen live on **Linux, macOS, Windows, Android, and iOS** using standard players like **mpv**, **ffplay**, or **VLC**—with **~20–40 ms latency** (near-TV speed) and zero browser overhead.

---

## Features
- **Hardware H.264 Capture:** Uses the PS4's built-in `libSceAvcap` hardware encoder (`720p60` on Base/Slim PS4; `1080p60` on PS4 Pro).
- **Standard MPEG-TS over HTTP & Raw TCP (`Port 5900`):** Auto-detects HTTP (`http://<PS4_IP>:5900/stream.ts`) and raw TCP (`tcp://<PS4_IP>:5900`) on the same port.
- **Near-Zero Latency:** Built with `TCP_NODELAY`, real wall-clock 90 kHz PCR/PTS timestamps, and zero transcoding on the PS4.
- **Fresh-Boot Safe:** Automatically waits for `SceRemotePlay` to settle after boot and retries until `Avcap` is ready.
- **Graceful Remote Shutdown (`Port 5901`):** Connect to TCP port `5901` at any time to cleanly release `Avcap` and exit without rebooting.
- **On-Screen PS4 Notifications:** Shows status, viewer connect/disconnect alerts, and shutdown messages directly on the PS4 UI.

---

## Supported Consoles & Firmware
- **PS4 Base / Slim:** `720p @ 60 fps` (`ps4-stream-720p.elf`)
- **PS4 Pro:** `1080p @ 60 fps` (`ps4-stream-1080p.elf`) or `720p @ 60 fps`
- **Firmware:** Supports firmwares covered by the underlying PBS PS4 runtime layout discovery backend (tested on **FW 11.00**; video only—PS4 audio is not implemented yet).

---

## Quick Start

### 1. Send the Payload to Your PS4
Enable **GoldHEN** (with BinLoader enabled on port `9021`), then send the binary matching your console:

```bash
# PS4 Base / Slim (720p60)
socat -t 99999999 - TCP:<PS4_IP>:9021 < ps4-stream-720p.elf

# PS4 Pro (1080p60)
socat -t 99999999 - TCP:<PS4_IP>:9021 < ps4-stream-1080p.elf
```
Wait for the PS4 notification: `PS4 720p60 TS Stream ready (5900) | Kill (5901)`.

### 2. Watch on Desktop (Linux / macOS / Windows)
- **mpv (Recommended — ~20–40 ms latency):**
  ```bash
  mpv --profile=low-latency --untimed --no-cache http://<PS4_IP>:5900/stream.ts
  ```
- **ffplay (FFmpeg):**
  ```bash
  ffplay -probesize 32 -analyzeduration 0 -fflags nobuffer+flush_packets -flags low_delay -framedrop -sync ext http://<PS4_IP>:5900/stream.ts
  ```

### 3. Watch on Android (Near-TV Speed)
1. Install **mpv-android** (Google Play or F-Droid).
2. Open **Settings -> Advanced -> Edit mpv.conf** and add:
   ```ini
   profile=low-latency
   untimed=yes
   cache=no
   demuxer-readahead-secs=0
   ```
3. Enable **Hardware decoding** in **Settings -> Video**.
4. Tap **Open URL** and enter:
   ```text
   http://<PS4_IP>:5900/stream.ts
   ```

*(Note: VLC for Android/iOS also opens `http://<PS4_IP>:5900/stream.ts`, though VLC enforces a 2–3 second live stream buffer.)*

### 4. Stop the Server Cleanly
```bash
nc -zv <PS4_IP> 5901
```

---

## Building from Source
Requires `ps4-payload-sdk` (`orbis-clang`) in `./ps4-payload-sdk`:

```bash
./build.sh
```
This builds both `ps4-stream-720p.elf` and `ps4-stream-1080p.elf`.

---

## Credits & License
- **PS4 Capture Backend:** Built on top of the PS4 `libSceAvcap` capture and kernel layout discovery backend from [seregonwar/PBS-Payload](https://github.com/seregonwar/PBS-Payload).
- **SDK:** [ps4-payload-dev / ps4-payload-sdk](https://github.com/ps4-payload-dev/sdk).
- **License:** **GPL-3.0-or-later** (inherited from `PBS-Payload`).

---

## Support / Donations

If **ps4-stream** helped you stream your PS4 with ultra-low latency and you'd like to support further PS4 homebrew development (such as PS4 audio streaming support and testing on more firmwares):

- **Bitcoin (BTC):** `bc1qt9wxjtq6th3nxmkwg5pa8jhskrn29z6zd7nvum`

Starring the repository (⭐) or sharing test results on other PS4 firmwares also helps a lot!
