#!/usr/bin/env python3
import subprocess, sys, os

W = 80
H = 24
ROW = W + 1
FSIZE = ROW * H
VIDEO = "/tmp/bad-apple-ascii-main/bad_apple.mp4"
OUT = "examples/bad_apple.cl"

if not os.path.exists(VIDEO):
    sys.exit("Video not found. Run: unzip -o /tmp/badapple_repo.zip " +
             "\"bad-apple-ascii-main/bad_apple.mp4\" -d /tmp/")

print("Extracting frames...", file=sys.stderr)
proc = subprocess.Popen(
    ["ffmpeg", "-i", VIDEO, "-f", "rawvideo", "-pix_fmt", "rgb24",
     "-s", f"{W}x{H}", "-vsync", "0", "-"],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
psize = W * H * 3
frames = []
while True:
    raw = proc.stdout.read(psize)
    if len(raw) < psize:
        break
    out = bytearray(FSIZE)
    for y in range(H):
        for x in range(W):
            i = (y * W + x) * 3
            g = (raw[i] + raw[i+1] + raw[i+2]) // 3
            out[y * ROW + x] = ord('#') if g < 128 else ord(' ')
        out[y * ROW + W] = 10
    frames.append(bytes(out))
    if len(frames) % 500 == 0:
        print(f"  {len(frames)} frames", file=sys.stderr)
proc.wait()
total = len(frames)
print(f"Total: {total} frames, {FSIZE} bytes/frame", file=sys.stderr)

data = b''.join(frames)

print("Writing Clean source...", file=sys.stderr)
with open(OUT, "w") as f:
    f.write(f"""# Bad Apple!! animation in Clean
# {total} frames, {W}x{H}, {FSIZE} bytes/frame

extern fn get_frame_ptr(data, frame, fsize)

fn main() effect
  hide_cursor()
  let data = """)
    f.write('"')
    # Write frame data as literal newline-separated rows inside the string
    # Each row = 80 chars + newline (no leading newline before first frame row)
    for i in range(0, len(data), ROW):
        row = data[i:i+ROW]
        f.write(row.decode('ascii', errors='replace'))
    f.write('"')

    f.write(f"""
  let fsize = {FSIZE}
  let total = {total}
  var frame = 0
  let start = time_ms()
  while frame < total
    clear_screen()
    set_fg(frame * 7 % 255 + 1)
    var ptr = get_frame_ptr(data, frame, fsize)
    print_str(ptr, fsize)
    frame += 1
    var target = start + frame * 33
    while time_ms() < target
      target = target
  show_cursor()
  reset_attr()
  set_fg(15)
  print_str("  Bad Apple!! in Clean  ", 24)
""")

print(f"Wrote {OUT}", file=sys.stderr)
sz = os.path.getsize(OUT)
print(f"Source size: {sz / 1024 / 1024:.1f} MB", file=sys.stderr)
