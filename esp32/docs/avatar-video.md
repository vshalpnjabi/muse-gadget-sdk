# Muse's app animations as the avatar (not built yet)

A plan for showing the Muse app's own avatar videos on a board's screen in
place of a drawn avatar. Nothing here is implemented; it records what was
found on 2026-11-07 on a Waveshare ESP32-S3-Touch-AMOLED-1.75C so the work
can start from it.

## What Muse has

Asked through `tools/muse/chat.py`, a Muse listed its avatar files on its VM,
in `~/workspace/avatars/`:

| File | What |
|---|---|
| `avatar-<id>-9.webp` | the still, 1600 x 1600 |
| `avatar-<id>-9.mp4` | idle / connecting loop |
| `avatar-<id>-9-working.mp4` | working / waiting loop |
| `avatar-<id>-9-making_something.mp4`, `-milestone_level_up.mp4` | one-off events |
| six 9:16 dance videos | not suited to a round screen |

There are no listening, thinking or talking videos, so SPEAKING would reuse
idle (or idle plus the board's own mouth or glow). The files are on the VM
only. A gadget can read them with `POST /fs/read` (see "Requests to Muse's
VM" in `AGENTS.md`), 1 KB per request.

## How it could work

1. **Get the videos.** Fetch each MP4 through the board, or have Muse
   transcode it on its VM and send it. Re-encode on the host to what the
   board decodes: H.264 baseline or VP8, square, at the panel's size or half
   of it, a few fps to a dozen, as a raw elementary stream.
2. **Store them in flash.** The 1.75C has 32 MB; `partitions_muse.csv`
   ends at 0x822000 and is shared with 16 MB boards. Add a board-specific
   table that keeps every existing offset and appends a data partition
   (about 23 MB) for the streams. Never move `prod_data`/`prod_bak` or the
   table offset.
3. **Play them as the face.** The phone-video path already decodes H.264 and
   VP8 full screen on this board (`components/muse/muse_video.c`,
   `muse_vdec.c`, the panel's `draw_frame`). A flash "transport" filling a
   `muse_video_transport_t` from the partition and looping would reuse it,
   picking the stream by mode (idle loop; working loop in THINKING and
   WORKING). The status line, captions and settings would have to draw over
   or around the video, which the video view doesn't do today.

## The blocker: internal RAM

`AGENTS.md` (Video from a phone) puts the decode and show tasks at about
20 KB of internal RAM once a stream starts. With live calls on and one LVGL
draw thread, this board had about 22-28 KB of internal RAM free and an 8 KB
largest block. Video would need something given up first: live calls, the
second display buffer, or a review of what sits in internal RAM. Check
`link.heartbeat`'s `int=` before and after.

## Cheaper first step (built instead)

The board shows the app still, scaled to the screen, and animates it itself
(breathing, a pulse while Muse speaks, a coloured glow per mode). See the
Appearance setting.
