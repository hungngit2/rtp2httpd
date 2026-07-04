# <img src="./icon.svg" width="24" height="24"> rtp2httpd - IPTV Streaming Gateway

[>> Official Documentation <<](https://rtp2httpd.com)

[>> 中文文档 <<](https://rtp2httpd.com)

rtp2httpd is a multimedia streaming gateway server. This project is a complete rewrite of [oskar456/rtp2httpd](https://github.com/oskar456/rtp2httpd), adding many new features on top of the original project, designed specifically for IPTV environments in mainland China (though it works anywhere multicast IPTV is deployed).

rtp2httpd converts multicast RTP/UDP streams and RTSP streams into unicast HTTP streams, and implements carrier-grade FCC ([Fast Channel Change](https://blog.csdn.net/yangzex/article/details/131328837)) for fast channel switching. It's a drop-in replacement for `udpxy` and `msd_lite`, giving IPTV users a viewing experience close to a native set-top box.

Armbian and Debian-based systems are supported through the installer script in [scripts/install-armbian.sh](scripts/install-armbian.sh):

```bash
curl -fsSL https://raw.githubusercontent.com/stackia/rtp2httpd/main/scripts/install-armbian.sh | sudo sh
```

The installer downloads the latest release binary, installs it to `/usr/local/bin/rtp2httpd`, creates an example config at `/etc/rtp2httpd.conf`, and sets up a systemd service.

## ✨ Key Features

### 📡 Multi-Protocol Support

- **RTP->HTTP**: Converts multicast RTP/UDP streams into standard HTTP streams
- **RTSP->HTTP**: Converts RTSP into HTTP video streams, with full support for the RTSP/RTP protocol stack and UDP NAT traversal (STUN)
  - Enables catchup/timeshift playback for IPTV RTSP timeshift sources
- **HTTP->HTTP**: A full reverse proxy implementation that can proxy IPTV internal-network HLS sources to a LAN or the public internet for easy viewing
- **udpxy compatibility**: Fully compatible with the udpxy URL format
- **M3U playlist integration**: Supports M3U/M3U8 format, automatically detects and converts channel URLs, and serves a standardized playlist
  - Supports external M3U URLs
  - Intelligently detects RTP/RTSP URLs and converts them into HTTP proxy format
  - Automatically handles catchup-source timeshift/playback URLs
  - Access the converted playlist via `http://<server:port>/playlist.m3u`
- **Packet-loss and jitter resistance**: Supports out-of-order recovery and FEC (Forward Error Correction) to ensure playback quality
  - Automatically corrects out-of-order RTP packets, eliminating glitches caused by network jitter
  - Supports Reed-Solomon FEC redundancy recovery, tolerating light packet loss (requires FEC support from the multicast upstream)
- **Channel snapshots**: Quickly fetch a channel's snapshot image via an HTTP request, reducing decoding pressure on the playback client

### ⚡ FCC Fast Channel Change

- **Carrier FCC protocol support**: Paired with a carrier's FCC server, achieves millisecond-level channel switching, on par with a native IPTV set-top box
- **Fast decoding**: FCC ensures an IDR frame is delivered immediately on channel change, ready for the player to decode right away

### 📊 Real-Time Status Monitoring

- **Web status page**: View real-time server status in your browser at `http://<server:port>/status`
- **Client connection stats**: Shows each connection's IP, status, bandwidth usage, and total data transferred
- **System log viewer**: View server logs in real time, with adjustable log verbosity
- **Remote management**: Force-disconnect clients from the web interface

### 🎬 Built-in Player

- **Use directly in the browser**: A modern, built-in web-based player UI that opens directly in the browser, with a responsive desktop/mobile layout
- **Fast startup**: Paired with FCC for fast stream startup and fast channel switching
- **Timeshift and catchup support**: Supports EPG (electronic program guide), timeshift, and catchup playback (requires an RTSP catchup source)
- **Zero overhead**: A pure web frontend implementation with virtually no extra resource cost on rtp2httpd (no decoding/transcoding overhead)

### 🚀 High-Performance Optimizations

- **Non-blocking I/O model**: Uses epoll event-driven I/O to efficiently handle large numbers of concurrent connections
- **Multi-core optimization**: Supports multiple worker processes to fully utilize multi-core CPUs and maximize throughput
- **Buffer pool optimization**: Pre-allocated buffer pools avoid frequent memory allocation; buffers are dynamically shared across clients based on load, preventing slow clients from starving out others
- **Zero-copy**: Supports the Linux kernel's MSG_ZEROCOPY feature, avoiding data copies between user space and kernel space
- **Lightweight**: Written in pure C with zero dependencies, small and simple, suitable for a wide range of embedded devices (routers, ONTs, NAS, etc.)
  - The binary is only 368KB (x86_64), and includes all web player frontend assets
- See the **[benchmark report](https://rtp2httpd.com/reference/benchmark)** (performance comparison against msd_lite, udpxy, and tvgate)

## 📹 Demos

### Fast Channel Switching + Timeshift/Catchup

https://github.com/user-attachments/assets/ca1a332f-d6e7-4a1e-be88-92bef67758b3

> [!TIP]
> Fast channel switching requires a player optimized for IPTV, such as [mytv-android](https://github.com/mytv-android/mytv-android) / [TiviMate](https://tivimate.com) / [Cloud Stream](https://apps.apple.com/us/app/cloud-stream-iptv-player/id1138002135) (the player used in the video is mytv-android).
> Common generic players such as PotPlayer / IINA don't specifically optimize startup speed, so the FCC effect is less noticeable.

### Built-in Player

https://github.com/user-attachments/assets/b32f134d-87ac-46d0-90fe-50ffa410069a

> [!TIP]
> Requires an M3U playlist to be configured first; open it in your browser at `http://<server:port>/player`.
> Due to browser decoding limitations, some channels may not be supported (shown as no audio or a black screen).

### Real-Time Status Monitoring

<img width="3046" height="1508" alt="web-dashboard" src="https://github.com/user-attachments/assets/8758c0ab-b144-41ed-8d90-9c41b375e22b" />

### 25 Concurrent 1080p Multicast Streams

https://github.com/user-attachments/assets/9d531ab6-6c35-4c50-802a-71f88b6b22c5

> [!NOTE]
> Each stream is 8 Mbps. Total CPU usage is only 25% of a single core (i3-N305), consuming 4MB of memory.

## 📖 Documentation

- **[Quick Start](https://rtp2httpd.com/en/guide/quick-start)**: OpenWrt quick configuration guide
- **[Installation](https://rtp2httpd.com/en/guide/installation)**: Installation guide for various platforms

If this is your first time setting up an IPTV multicast forwarding service and you're not familiar with the related networking concepts (DHCP authentication, routing, multicast, firewalls), we recommend starting with the [setup tutorial](https://rtp2httpd.com/en/reference/related-resources#iptv-setup-tutorials) first.

## 📄 License

This project is released under the GNU General Public License v2.0. This means:

- ✅ You can deploy it in a commercial environment (e.g. internal enterprise use)
- ✅ You can offer a paid IPTV transcoding service built on it
- ✅ You can use this software as part of a paid IPTV consulting service
- ✅ You can sell hardware devices that include this software
- ⚠️ If you modify the code, you must publish the modified source code
- ⚠️ If you distribute binaries, you must also provide the source code
- ⚠️ You may not make it closed-source and sell it

## 🙏 Acknowledgments

- The developers of the original project, [oskar456/rtp2httpd](https://github.com/oskar456/rtp2httpd)
- Everyone in the industry willing to publicly document FCC protocol details
- All testers and users who provided feedback
