#!/usr/bin/env python3
# Opens a camera and pushes JPEG frames over UDP to whichever client last said
# hello, while periodically announcing itself so CalibGUI's discovery list
# can find it.

import argparse
import socket
import struct
import time
import random
import cv2

DISCOVERY_MULTICAST_GROUP = "224.0.0.123"
DISCOVERY_APPLICATION_ID = 7681412
DISCOVERY_MAGIC = b"SO7V"
DISCOVERY_PROTOCOL_VERSION = 1
DISCOVERY_PACKET_IAMHERE = 0
DISCOVERY_SEND_INTERVAL_S = 5.0

SUBSCRIBER_TIMEOUT_S = 3.0


def build_iamhere_packet(peer_id, snapshot_index, user_data: bytes) -> bytes:
    # Wire format: magic(4) + version(1) + reserved(3) + packet_type(1) +
    # application_id(4) + peer_id(4) + snapshot_index(8) + user_data_len(2),
    # all big-endian, followed by the user_data bytes themselves. Matches
    # udp-discovery's protocol version 1 (udp_discovery_protocol.cpp).
    header = struct.pack(
        ">4sB3xBIIQH",
        DISCOVERY_MAGIC,
        DISCOVERY_PROTOCOL_VERSION,
        DISCOVERY_PACKET_IAMHERE,
        DISCOVERY_APPLICATION_ID,
        peer_id,
        snapshot_index,
        len(user_data),
    )
    return header + user_data


def main():
    parser = argparse.ArgumentParser(description="Stream a camera to CalibGUI over UDP.")
    parser.add_argument("--camera", default="0", help="cv2.VideoCapture source (device index or path)")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=int, default=30)
    # Kept comfortably under the ~65507-byte single-datagram UDP limit at 1280x720
    # (quality 80 regularly exceeds it); oversized frames are dropped, not fatal,
    # but a sane default avoids dropping most/all frames in typical use.
    parser.add_argument("--jpeg-quality", type=int, default=60)
    parser.add_argument("--port", type=int, default=6600, help="UDP port to listen on and advertise")
    parser.add_argument("--name", default=socket.gethostname(), help="Name advertised to CalibGUI")
    parser.add_argument("--discovery-port", type=int, default=12021)
    args = parser.parse_args()

    camera_source = int(args.camera) if args.camera.isdigit() else args.camera
    cap = cv2.VideoCapture(camera_source)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
    cap.set(cv2.CAP_PROP_FPS, args.fps)
    if not cap.isOpened():
        raise SystemExit(f"Could not open camera '{args.camera}'")

    video_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    video_sock.bind(("0.0.0.0", args.port))
    video_sock.setblocking(False)

    beacon_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    beacon_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    user_data = f"{args.name}:{args.port}".encode()
    peer_id = random.getrandbits(32)
    snapshot_index = 0
    last_beacon_time = 0.0
    subscriber_addr = None
    last_hello_time = 0.0
    jpeg_params = [cv2.IMWRITE_JPEG_QUALITY, args.jpeg_quality]

    print(f"Streaming camera '{args.camera}' as '{args.name}' on UDP port {args.port}")

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                continue

            while True:
                try:
                    _, addr = video_sock.recvfrom(64)
                except BlockingIOError:
                    break
                subscriber_addr = addr
                last_hello_time = time.monotonic()

            now = time.monotonic()

            if subscriber_addr and now - last_hello_time <= SUBSCRIBER_TIMEOUT_S:
                ok, encoded = cv2.imencode(".jpg", frame, jpeg_params)
                if ok:
                    try:
                        video_sock.sendto(encoded.tobytes(), subscriber_addr)
                    except OSError as e:
                        # A JPEG frame occasionally exceeds the 65507-byte UDP
                        # datagram limit (e.g. a highly detailed scene at high
                        # quality); drop that one frame rather than crash.
                        print(f"Dropping oversized frame ({len(encoded)} bytes): {e}")

            if now - last_beacon_time >= DISCOVERY_SEND_INTERVAL_S:
                packet = build_iamhere_packet(peer_id, snapshot_index, user_data)
                snapshot_index += 1
                beacon_sock.sendto(packet, (DISCOVERY_MULTICAST_GROUP, args.discovery_port))
                beacon_sock.sendto(packet, ("255.255.255.255", args.discovery_port))
                last_beacon_time = now
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        video_sock.close()
        beacon_sock.close()


if __name__ == "__main__":
    main()
