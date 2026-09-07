import ctypes
import socket
import time
import psutil

# =====================================================
# ESP32
# =====================================================
ESP32_IP = "YOUR_ESP32_IP"
ESP32_PORT = 4210

UPDATE_INTERVAL = 0.5

if ESP32_IP == "YOUR_ESP32_IP":
    raise SystemExit(
        "Set ESP32_IP at the top of this file to the address shown on the ESP32 display."
    )

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setblocking(False)

# =====================================================
# WINDOWS MEDIA / CONTROL KEYS
# =====================================================
VK_VOLUME_MUTE = 0xAD
VK_VOLUME_DOWN = 0xAE
VK_VOLUME_UP = 0xAF
VK_MEDIA_PLAY_PAUSE = 0xB3
VK_SNAPSHOT = 0x2C

KEYEVENTF_KEYUP = 0x0002

user32 = ctypes.windll.user32


def tap_virtual_key(vk_code):
    user32.keybd_event(vk_code, 0, 0, 0)
    time.sleep(0.03)
    user32.keybd_event(vk_code, 0, KEYEVENTF_KEYUP, 0)


def execute_pc_command(command):
    """
    Returns True when the command was executed successfully.
    """
    try:
        if command == "VOL_UP":
            tap_virtual_key(VK_VOLUME_UP)

        elif command == "VOL_DOWN":
            tap_virtual_key(VK_VOLUME_DOWN)

        elif command == "MUTE":
            tap_virtual_key(VK_VOLUME_MUTE)

        elif command == "PLAY_PAUSE":
            tap_virtual_key(VK_MEDIA_PLAY_PAUSE)

        elif command == "SCREENSHOT":
            # Windows Print Screen: copies the full screen to clipboard.
            tap_virtual_key(VK_SNAPSHOT)

        elif command == "LOCK":
            # Acknowledge is sent by the caller before this is executed.
            user32.LockWorkStation()

        else:
            return False

        return True

    except Exception:
        return False


# =====================================================
# HELPERS
# =====================================================
def sanitize_name(name):
    if not name:
        return "N/A"

    name = str(name)
    name = name.replace(",", " ")
    name = name.replace("\n", " ")
    name = name.replace("\r", " ")

    return name[:38]


def get_tcp_latency_ms(
    host="1.1.1.1",
    port=443,
    timeout=0.8
):
    start = time.perf_counter()

    try:
        with socket.create_connection(
            (host, port),
            timeout=timeout
        ):
            return (
                time.perf_counter() - start
            ) * 1000.0

    except OSError:
        return -1.0


# Prime and retain process CPU state.
process_cpu_state = {}


def get_top_processes():
    top_cpu_name = "N/A"
    top_cpu_pct = 0.0

    top_ram_name = "N/A"
    top_ram_mb = 0.0

    cpu_count = psutil.cpu_count(logical=True) or 1

    live_pids = set()

    for proc in psutil.process_iter(
        ["pid", "name", "memory_info"]
    ):
        try:
            pid = proc.info["pid"]
            live_pids.add(pid)

            # psutil's Process.cpu_percent can exceed 100% on multicore.
            # Divide by logical CPU count for a Task-Manager-like scale.
            raw_cpu = proc.cpu_percent(interval=None)
            cpu = raw_cpu / cpu_count

            mem_info = proc.info.get("memory_info")

            ram_mb = (
                mem_info.rss / 1024 / 1024
                if mem_info
                else 0.0
            )

            name = sanitize_name(
                proc.info.get("name")
                or f"PID {pid}"
            )

            if cpu > top_cpu_pct:
                top_cpu_pct = cpu
                top_cpu_name = name

            if ram_mb > top_ram_mb:
                top_ram_mb = ram_mb
                top_ram_name = name

        except (
            psutil.NoSuchProcess,
            psutil.AccessDenied,
            psutil.ZombieProcess
        ):
            continue

    return (
        top_cpu_name,
        top_cpu_pct,
        top_ram_name,
        top_ram_mb
    )


# =====================================================
# COMMAND RECEIVE / ACK
# =====================================================
def send_ack(command, ok):
    result = "OK" if ok else "ERR"

    ack = f"ACK,{command},{result}"

    sock.sendto(
        ack.encode("utf-8"),
        (ESP32_IP, ESP32_PORT)
    )


def poll_control_commands():
    """
    The ESP32 sends commands back to the same UDP source port
    that Python uses for telemetry. Non-blocking receive keeps
    rotary-control response fast.
    """
    while True:
        try:
            data, address = sock.recvfrom(256)

        except BlockingIOError:
            break

        except OSError:
            break

        text = data.decode(
            "utf-8",
            errors="ignore"
        ).strip()

        if not text.startswith("CMD,"):
            continue

        command = text[4:].strip().upper()

        print(f"\nPC CONTROL -> {command}")

        if command == "LOCK":
            # Send ACK first because LockWorkStation may immediately
            # hide the desktop.
            send_ack(command, True)
            execute_pc_command(command)
            continue

        ok = execute_pc_command(command)
        send_ack(command, ok)

        if command == "SCREENSHOT" and ok:
            print(
                "Screenshot copied to Windows clipboard."
            )


# =====================================================
# PRIME COUNTERS
# =====================================================
psutil.cpu_percent(interval=None)

# Prime per-process CPU counters.
for proc in psutil.process_iter():
    try:
        proc.cpu_percent(interval=None)
    except (
        psutil.NoSuchProcess,
        psutil.AccessDenied,
        psutil.ZombieProcess
    ):
        pass

net = psutil.net_io_counters()

last_net_recv = net.bytes_recv
last_net_sent = net.bytes_sent

disk = psutil.disk_io_counters()

last_disk_read = (
    disk.read_bytes
    if disk
    else 0
)

last_disk_write = (
    disk.write_bytes
    if disk
    else 0
)

# =====================================================
# CACHED / SLOW VALUES
# =====================================================
latency_ms = -1.0

top_cpu_name = "N/A"
top_cpu_pct = 0.0

top_ram_name = "N/A"
top_ram_mb = 0.0

last_slow_update = 0.0

# =====================================================
# SEND TIMING
# =====================================================
last_send_time = 0.0
last_counter_time = time.time()

print("========================================")
print(" InnoCircuitsLab PC Dashboard + Control")
print("========================================")
print()
print(f"ESP32: {ESP32_IP}:{ESP32_PORT}")
print("Control listener: ACTIVE")
print("Press Ctrl+C to stop.")
print()

try:
    while True:
        now = time.time()

        # ---------------------------------------------
        # Always poll controls quickly.
        # ---------------------------------------------
        poll_control_commands()

        # ---------------------------------------------
        # Telemetry every 0.5 s
        # ---------------------------------------------
        if now - last_send_time >= UPDATE_INTERVAL:
            dt = now - last_counter_time

            if dt <= 0:
                dt = UPDATE_INTERVAL

            # CPU / RAM
            cpu = psutil.cpu_percent(interval=None)
            ram = psutil.virtual_memory().percent

            # Network
            net = psutil.net_io_counters()

            download_bytes = (
                net.bytes_recv -
                last_net_recv
            )

            upload_bytes = (
                net.bytes_sent -
                last_net_sent
            )

            download_mb = (
                download_bytes /
                dt /
                1024 /
                1024
            )

            upload_mb = (
                upload_bytes /
                dt /
                1024 /
                1024
            )

            total_rx_gb = (
                net.bytes_recv /
                1024 /
                1024 /
                1024
            )

            total_tx_gb = (
                net.bytes_sent /
                1024 /
                1024 /
                1024
            )

            last_net_recv = net.bytes_recv
            last_net_sent = net.bytes_sent

            # Uptime
            uptime_seconds = int(
                time.time() -
                psutil.boot_time()
            )

            # CPU frequency
            freq = psutil.cpu_freq()

            cpu_freq = (
                freq.current
                if freq
                else 0.0
            )

            # Disk read/write
            disk = psutil.disk_io_counters()

            if disk:
                read_delta = (
                    disk.read_bytes -
                    last_disk_read
                )

                write_delta = (
                    disk.write_bytes -
                    last_disk_write
                )

                disk_read_mb = (
                    read_delta /
                    dt /
                    1024 /
                    1024
                )

                disk_write_mb = (
                    write_delta /
                    dt /
                    1024 /
                    1024
                )

                last_disk_read = disk.read_bytes
                last_disk_write = disk.write_bytes

            else:
                disk_read_mb = 0.0
                disk_write_mb = 0.0

            process_count = len(
                psutil.pids()
            )

            # Disk free
            try:
                disk_usage = psutil.disk_usage(
                    "C:\\"
                )
            except Exception:
                disk_usage = psutil.disk_usage(
                    "/"
                )

            disk_free_pct = (
                100.0 -
                disk_usage.percent
            )

            # Slower metrics every ~2 seconds.
            if now - last_slow_update >= 2.0:
                latency_ms = get_tcp_latency_ms()

                (
                    top_cpu_name,
                    top_cpu_pct,
                    top_ram_name,
                    top_ram_mb
                ) = get_top_processes()

                last_slow_update = now

            packet = (
                f"{cpu:.1f},"
                f"{ram:.1f},"
                f"{download_mb:.3f},"
                f"{upload_mb:.3f},"
                f"{uptime_seconds},"
                f"{cpu_freq:.1f},"
                f"{disk_read_mb:.3f},"
                f"{disk_write_mb:.3f},"
                f"{process_count},"
                f"{total_rx_gb:.3f},"
                f"{total_tx_gb:.3f},"
                f"{latency_ms:.1f},"
                f"{disk_free_pct:.1f},"
                f"{top_cpu_pct:.1f},"
                f"{sanitize_name(top_cpu_name)},"
                f"{top_ram_mb:.1f},"
                f"{sanitize_name(top_ram_name)}"
            )

            sock.sendto(
                packet.encode(
                    "utf-8",
                    errors="replace"
                ),
                (
                    ESP32_IP,
                    ESP32_PORT
                )
            )

            latency_text = (
                "OFF"
                if latency_ms < 0
                else f"{latency_ms:.0f}ms"
            )

            print(
                f"\rCPU {cpu:5.1f}% | "
                f"RAM {ram:5.1f}% | "
                f"NET {latency_text:>5} | "
                f"TOP CPU {top_cpu_name[:14]:14} "
                f"{top_cpu_pct:5.1f}%",
                end="",
                flush=True
            )

            last_send_time = now
            last_counter_time = now

        # Fast control polling without burning CPU.
        time.sleep(0.02)

except KeyboardInterrupt:
    print("\nStopped.")

finally:
    sock.close()
