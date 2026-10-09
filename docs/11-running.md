---
title: Running it for real
---

# 11. Running it for real

## Why it "crashed" after a while

For the first few days I started the radar from an SSH session. Every so
often the screen dropped back to the text console, and my SSH window said
"broken pipe". The radar hadn't crashed: when the SSH connection dropped,
Linux sent the programs started from it a **hangup signal** (SIGHUP),
which ends a program by default. The radar's signal handler politely gave
the screen back to the console on the way out.

The fix is not to run it from a login session at all, but as a
**systemd service**.

## The service

```
cd lvgl-radar/radar
./install-service.sh
```

That writes `/etc/systemd/system/radar.service` for your user and folder,
enables it to start at boot, and starts it now. The interesting lines:

```ini
[Unit]
Wants=network-online.target
After=network-online.target
Conflicts=getty@tty1.service        # no login prompt on the radar's screen

[Service]
User=gordo                          # not root
WorkingDirectory=/home/gordo/lvgl-radar/radar
ExecStart=/home/gordo/lvgl-radar/radar/build/radar radar.conf
Restart=always                      # if it ever stops, start it again
SupplementaryGroups=video input tty gpio
AmbientCapabilities=CAP_SYS_TTY_CONFIG
```

The groups give access to the framebuffer, the touch screen, the console
and the GPIO pins. The capability allows exactly one privileged thing:
switching the console to graphics mode (chapter 1). So the radar runs as
an ordinary user, not as root.

Day to day:

```
sudo systemctl restart radar       # after rebuilding or editing radar.conf
sudo systemctl stop radar          # e.g. to run the touch demo
journalctl -u radar -f             # its messages, live
./install-service.sh remove        # take it out again
```

## Wi-Fi that stays awake

The Pi's Wi-Fi saves power by dozing between packets, which can make SSH
sluggish and drop connections. With NetworkManager (the Pi OS default),
turn it off for your network:

```
nmcli connection show                                   # your network's name
sudo nmcli connection modify "NAME" 802-11-wireless.powersave 2
```

(`2` means "disable".) It takes effect the next time the connection comes
up.

## Demo mode: your sky, anywhere

To show the radar away from home, where your receiver can't be reached,
record a while of your own sky and replay it:

```
cd lvgl-radar/radar
./demo/demo.py record --minutes 90       # at home, at a busy time of day
./demo/demo-mode on
```

`demo.py record` polls everything the radar would: every source in
`radar.conf`, the receiver's stats, the weather, METARs for your airports,
and adsbdb details for every aircraft it sees. It saves them in
`recording/`, compressed.

`demo-mode on` installs a small replay server as a second service and
points the radar at it, using a `demo.conf` made from your `radar.conf`.
From then on the Pi boots straight into the replay, with **no network
needed**. The replay plays in real time and loops, with a few seconds of
empty sky between loops so tracks clear instead of jumping. The forecast
days are moved to start today, and the demo keeps its own range plot
so your real one isn't touched.

```
./demo/demo-mode off                     # back to live data
```

> **The clock needs the network.** The Pi has no battery-backed clock. With
> no network at all it starts from roughly when it was last shut down.
> A phone hotspot the Pi has joined before fixes that, and also brings
> adsb.fi's live data.

## Showing the screen to a room

A 5-inch screen is hard to see from the back row. Two ways to put it on a
projector or a video call:

**An HDMI splitter** sends the Pi's picture to both the round screen and
a projector. It only works if the projector accepts a 1080 × 1080 picture,
so test it before the day.

**fbmirror**, in the repo's `tools/` folder, shows the framebuffer live in
a web browser:

```
sudo apt install python3-pil
./tools/fbmirror.py
```

Then open `http://radar.local:8081/` on a laptop on the same network
(the Pi's hostname, plus `.local`). It reads `/dev/fb0` four times a
second, converts the RGB565 pixels with Pillow, and sends each changed
frame as a JPEG in a *Motion JPEG* stream, which browsers show as live
video with no plugins. Share that browser tab in Jitsi or Zoom and remote
viewers see the screen too.

It works for anything on the framebuffer: the touch demo, the radar, even
the text console.
