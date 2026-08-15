#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
die() { echo "lab.sh: $*" >&2; exit 1; }
LAB_DIR=${ENCORE_QEMU_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/encore-qemu}
HOST_LOCALE=${ENCORE_QEMU_LOCALE:-${LANG:-en_US.UTF-8}}
case "$HOST_LOCALE" in C|C.*|POSIX) HOST_LOCALE=en_US.UTF-8 ;; esac
HOST_LAYOUT=${ENCORE_QEMU_KEYBOARD:-$(sed -n 's/^XKBLAYOUT="\{0,1\}\([^" ]*\)"\{0,1\}$/\1/p' /etc/default/keyboard 2>/dev/null | head -1)}
HOST_LAYOUT=${HOST_LAYOUT:-us}
HOST_VARIANT=${ENCORE_QEMU_KEYBOARD_VARIANT:-$(sed -n 's/^XKBVARIANT="\{0,1\}\([^" ]*\)"\{0,1\}$/\1/p' /etc/default/keyboard 2>/dev/null | head -1)}
[[ "$HOST_LOCALE" =~ ^[A-Za-z0-9_.@-]+$ ]] || die "unsafe host locale '$HOST_LOCALE'"
[[ "$HOST_LAYOUT" =~ ^[A-Za-z0-9_-]+$ ]] || die "unsafe host keyboard layout '$HOST_LAYOUT'"
[[ "$HOST_VARIANT" =~ ^[A-Za-z0-9_-]*$ ]] || die "unsafe host keyboard variant '$HOST_VARIANT'"
if [ -n "$HOST_VARIANT" ]; then HOST_KEYMAP="$HOST_LAYOUT($HOST_VARIANT)"; else HOST_KEYMAP=$HOST_LAYOUT; fi
BASE_TAG=$(printf '%s-%s-%s' "$HOST_LOCALE" "$HOST_LAYOUT" "${HOST_VARIANT:-default}" | tr -cs 'A-Za-z0-9._-' _)
BASE=$LAB_DIR/debian13-minimal-$BASE_TAG.qcow2
OVERLAY=$LAB_DIR/current.qcow2
PIDFILE=$LAB_DIR/qemu.pid
LOG=$LAB_DIR/serial.log
SSH_PORT=${ENCORE_QEMU_SSH_PORT:-22222}
RAM=${ENCORE_QEMU_RAM:-2048}
CPUS=${ENCORE_QEMU_CPUS:-2}
KERNEL_URL=${ENCORE_QEMU_KERNEL_URL:-https://deb.debian.org/debian/dists/trixie/main/installer-amd64/current/images/netboot/debian-installer/amd64/linux}
INITRD_URL=${ENCORE_QEMU_INITRD_URL:-https://deb.debian.org/debian/dists/trixie/main/installer-amd64/current/images/netboot/debian-installer/amd64/initrd.gz}

need() { command -v "$1" >/dev/null 2>&1 || die "missing '$1' (on Debian: apt install qemu-system-x86 qemu-utils curl cpio gzip openssh-client)"; }

prereqs() {
    for command in qemu-system-x86_64 qemu-img curl cpio gzip ssh tar; do need "$command"; done
}

running() {
    local pid
    { [ -s "$PIDFILE" ] && read -r pid < "$PIDFILE"; } 2>/dev/null || return 1
    [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null
}

stop_vm() {
    local pid=""
    { [ -s "$PIDFILE" ] && read -r pid < "$PIDFILE"; } 2>/dev/null || true
    if running; then
        kill "$pid" 2>/dev/null || true
        for _ in {1..30}; do running || break; sleep 0.2; done
        running && kill -KILL "$pid" 2>/dev/null || true
    fi
    rm -f "$PIDFILE"
}

wait_ssh() {
    for _ in {1..180}; do
        ssh_guest true >/dev/null 2>&1 && return 0
        running || { tail -80 "$LOG" >&2; die "VM stopped before SSH became ready"; }
        sleep 2
    done
    tail -80 "$LOG" >&2
    die "timed out waiting for guest SSH"
}

wait_ssh_down() {
    for _ in {1..60}; do
        ssh_guest true >/dev/null 2>&1 || return 0
        sleep 0.25
    done
    die "guest did not leave the previous boot"
}

ssh_guest() {
    sshpass -p cabinet ssh -p "$SSH_PORT" \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR -o ConnectTimeout=2 root@127.0.0.1 "$@"
}

accel_args() {
    if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
        printf '%s\n' -enable-kvm -cpu host
    else
        printf '%s\n' -accel tcg -cpu max
    fi
}

start_overlay() {
    [ -f "$OVERLAY" ] || die "no overlay; run '$0 reset' first"
    running && die "VM is already running"
    : > "$LOG"
    mapfile -t accel < <(accel_args)
    if [ "${ENCORE_QEMU_HEADLESS:-0}" = 1 ]; then
        display=(-display none)
    else
        qemu-system-x86_64 -display help 2>&1 | grep -qx gtk ||
            die "QEMU GTK display unavailable (install qemu-system-gui, or set ENCORE_QEMU_HEADLESS=1)"
        display=(-display gtk,zoom-to-fit=on,show-tabs=off)
    fi
    qemu-system-x86_64 "${accel[@]}" -machine q35 \
        -m "$RAM" -smp "$CPUS" -drive "file=$OVERLAY,if=virtio,format=qcow2" \
        -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22" \
        -device virtio-net-pci,netdev=net0,addr=2 \
        -vga none -device VGA,xres=800,yres=600,addr=1 \
        "${display[@]}" -serial "file:$LOG" -daemonize -pidfile "$PIDFILE"
    wait_ssh
}

make_initrd() {
    local work=$1
    curl -fL --retry 3 -o "$work/linux" "$KERNEL_URL"
    curl -fL --retry 3 -o "$work/initrd.gz" "$INITRD_URL"
    sed -e "s|@HOST_LOCALE@|$HOST_LOCALE|g" \
        -e "s|@HOST_KEYMAP@|$HOST_KEYMAP|g" \
        -e "s|@HOST_LAYOUT@|$HOST_LAYOUT|g" \
        -e "s|@HOST_VARIANT@|$HOST_VARIANT|g" \
        "$SCRIPT_DIR/preseed.cfg" > "$work/preseed.cfg"
    cp "$work/initrd.gz" "$work/preseed-initrd.gz"
    (
        cd "$work"
        printf '%s\n' preseed.cfg | cpio -o -H newc 2>/dev/null | gzip -c
    ) >> "$work/preseed-initrd.gz"
}

prepare() {
    prereqs
    mkdir -p "$LAB_DIR"
    [ ! -e "$BASE" ] || { echo "Base already exists: $BASE"; return; }
    local work
    work=$(mktemp -d "$LAB_DIR/prepare.XXXXXX")
    make_initrd "$work"
    qemu-img create -f qcow2 "$LAB_DIR/installing.qcow2" 16G
    : > "$LOG"
    echo "Installing stripped Debian 13 base ($HOST_LOCALE, $HOST_KEYMAP)..."
    mapfile -t accel < <(accel_args)
    qemu-system-x86_64 "${accel[@]}" -machine q35 \
        -m "$RAM" -smp "$CPUS" -drive "file=$LAB_DIR/installing.qcow2,if=virtio,format=qcow2" \
        -netdev user,id=net0 -device virtio-net-pci,netdev=net0,addr=2 \
        -vga none -device VGA,xres=800,yres=600,addr=1 \
        -display none -serial "file:$LOG" \
        -kernel "$work/linux" -initrd "$work/preseed-initrd.gz" \
        -append "auto=true priority=critical preseed/file=/preseed.cfg language=en country=US locale=en_US.UTF-8 keymap=$HOST_LAYOUT interface=auto netcfg/disable_autoconfig=false console=ttyS0,115200n8 --- quiet" \
        -no-reboot
    [ "$(du -m "$LAB_DIR/installing.qcow2" | awk '{print $1}')" -ge 500 ] ||
        die "installer stopped before producing a complete base image"
    qemu-img check "$LAB_DIR/installing.qcow2"
    mv "$LAB_DIR/installing.qcow2" "$BASE"
    chmod a-w "$BASE"
    rm -rf -- "$work"
    echo "Sealed base image: $BASE"
}

reset_overlay() {
    prereqs
    stop_vm
    [ -f "$BASE" ] || die "base missing; run '$0 prepare'"
    rm -f "$OVERLAY"
    qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$OVERLAY"
    echo "Fresh overlay: $OVERLAY"
}

copy_checkout() {
    tar -C "$REPO_ROOT" --exclude=.git --exclude=build --exclude=savedata -cf - . |
        ssh_guest "rm -rf /opt/Encore-PB2K && mkdir -p /opt/Encore-PB2K && tar -C /opt/Encore-PB2K -xf - && chown -R root:root /opt/Encore-PB2K"
    ssh_guest 'ln -sfn /opt/Encore-PB2K /home/cabinet/Encore-PB2K; cat > /opt/Encore-PB2K/qemu-system-i386 <<'"'"'EOF'"'"'
#!/bin/sh
case " $* " in
  *" -M help "*) echo "pinball2000 Williams Pinball 2000"; exit 0 ;;
  *" -audio help "*) echo "Available audio drivers: sdl"; exit 0 ;;
  *" --version "*) echo "QEMU emulator version 10.0.8"; exit 0 ;;
esac
exec sleep 15
EOF
chmod 0755 /opt/Encore-PB2K/qemu-system-i386'
    ssh_guest 'cat > /usr/local/bin/cage <<'"'"'EOF'"'"'
#!/bin/sh
while [ "$#" -gt 0 ] && [ "$1" != -- ]; do shift; done
[ "${1:-}" = -- ] && shift
export WAYLAND_DISPLAY=wayland-encore-lab
exec "$@"
EOF
chmod 0755 /usr/local/bin/cage'
}

assert_stripped_guest() {
    ssh_guest 'command -v run0 >/dev/null && ! command -v pkttyagent >/dev/null && ! command -v pkexec >/dev/null && ! command -v sudo >/dev/null'
}

enable_nonroot_escalation() {
    ssh_guest 'DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends polkitd >/dev/null'
}

install_as_cabinet() {
    local run_as_root=${1:-0}
    export LAB_SSH_PORT=$SSH_PORT
    export LAB_RUN_AS_ROOT=$run_as_root
    expect <<'EXPECT_EOF'
set timeout 1200
spawn sshpass -p cabinet ssh -tt -p $env(LAB_SSH_PORT) -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR cabinet@127.0.0.1 "cd /opt/Encore-PB2K && ./install.sh --cage"
expect {
    -re {Password:|Mot de passe[^:]*:} { send -- "cabinet\r" }
    timeout { exit 124 }
    eof { exit 125 }
}
expect {
    -re {Cabinet session user[^:]*:} {
        set root_answer [expr {$env(LAB_RUN_AS_ROOT) ? "y" : "n"}]
        foreach answer [list cabinet swe1 y $root_answer n n y] {
            send -- "$answer\r"
            after 150
        }
    }
    timeout { exit 124 }
    eof { exit 125 }
}
expect {
    -re {Install[^?]*\?} { send -- "y\r"; exp_continue }
    -re {enable /dev/parport0 at boot[^?]*\?} { send -- "n\r"; exp_continue }
    eof { set result [wait]; exit [lindex $result 3] }
    timeout { exit 124 }
}
EXPECT_EOF
}

test_install() {
    local backend=${1:-cage} execution=${2:-user} run_as_root=0
    [[ "$backend" == cage ]] || die "automated lab validates cage"
    case "$execution" in user) ;; root) run_as_root=1 ;; *) die "execution must be user or root" ;; esac
    reset_overlay; start_overlay; assert_stripped_guest; copy_checkout; enable_nonroot_escalation
    install_as_cabinet "$run_as_root"
    ssh_guest "test -s /etc/encore-pinball2000/session.conf; grep -qx BACKEND=cage /etc/encore-pinball2000/session.conf; grep -qx RUN_AS_ROOT=$run_as_root /etc/encore-pinball2000/session.conf; grep -q 'Restart=always' /etc/systemd/system/getty@tty1.service.d/49-encore.conf"
    ssh_guest 'systemctl reboot' >/dev/null 2>&1 || true
    wait_ssh_down
    wait_ssh
    if [[ "$execution" == root ]]; then
        ssh_guest 'for i in $(seq 1 100); do systemd-inhibit --list --no-pager --no-legend | grep -q "Encore cabinet root diagnostic" && exit 0; sleep .1; done; exit 1'
    else
        ssh_guest 'for i in $(seq 1 100); do systemd-inhibit --list --no-pager --no-legend | grep -q "Encore cabinet session" && exit 0; sleep .1; done; exit 1'
    fi
    ssh_guest 'for i in $(seq 1 250); do test -s /run/systemd/system/getty@tty1.service.d/50-encore-maintenance.conf && systemctl is-active --quiet getty@tty1.service && exit 0; sleep .1; done; journalctl -b -u getty@tty1.service --no-pager; exit 1'
    ssh_guest 'journalctl -b --no-pager | grep -q "session opened for user cabinet"; journalctl -b --no-pager | grep -q "session closed for user cabinet"'
    ssh_guest 'cd /opt/Encore-PB2K && ./uninstall.sh'
    ssh_guest 'test ! -e /etc/encore-pinball2000/session.conf; test ! -e /etc/systemd/system/getty@tty1.service.d/49-encore.conf; test ! -e /run/systemd/system/getty@tty1.service.d/50-encore-maintenance.conf; test ! -e /var/lib/encore-pinball2000/install-mode; test "$(getent passwd cabinet | cut -d: -f7)" = /bin/bash'
    stop_vm
    echo "PASS: stripped Debian install/reboot/session/maintenance/uninstall ($backend, $execution)"
}

manual_vm() {
    reset_overlay
    start_overlay
    assert_stripped_guest
    copy_checkout
    enable_nonroot_escalation
    cat <<EOF

Manual stripped-Debian VM is ready in the 800x600 QEMU window.
Login: cabinet / cabinet

  cd ~/Encore-PB2K
  ./install.sh

The first password prompt is run0/pkttyagent authentication. The base image is
untouched; discard this experiment with: $0 reset
EOF
}

case "${1:-}" in
    prepare) prepare ;;
    reset) reset_overlay ;;
    boot) start_overlay ;;
    stop) stop_vm ;;
    shell) need sshpass; shift; ssh_guest "$@" ;;
    test) prereqs; need sshpass; need expect; test_install "${2:-cage}" "${3:-user}" ;;
    manual) prereqs; need sshpass; manual_vm ;;
    all) prepare; need expect; test_install cage user; test_install cage root ;;
    *) echo "Usage: $0 {all|prepare|reset|boot|manual|shell|stop|test cage [user|root]}" >&2; exit 2 ;;
esac
