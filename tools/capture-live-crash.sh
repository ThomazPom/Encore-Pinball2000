#!/bin/sh
# Capture a live Encore guest after XINA has entered a fatal monitor.
# Attaching GDB pauses QEMU briefly; it does not write to guest memory.

set -eu
umask 077

usage() {
    cat <<'EOF'
Usage: tools/capture-live-crash.sh [OPTIONS]

Capture the RAM and host-side state of a running Encore QEMU process.

Options:
  --pid PID                 QEMU process to inspect (auto-detected by default)
  --output DIR              new destination directory
                            (default: /tmp/encore-live-crash-TIMESTAMP)
  --disassemble ADDR[:LEN]  disassemble guest bytes around ADDR; repeatable
                            LEN defaults to 256 bytes; hexadecimal is accepted
  -h, --help                show this help

Examples:
  tools/capture-live-crash.sh
  tools/capture-live-crash.sh --disassemble 0x227f3a:0x80 \
      --disassemble 0x24be80:0x100

The QEMU executable must retain enough debug symbols for GDB to resolve
current_machine and its RAM MemoryRegion. The tool never kills QEMU and never
writes guest memory. Normal ptrace permissions still apply.
EOF
}

pid=
output=
disassembly_specs=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --pid)
            [ "$#" -ge 2 ] || { echo "ERROR: --pid needs a value" >&2; exit 2; }
            pid=$2
            shift 2
            ;;
        --output)
            [ "$#" -ge 2 ] || { echo "ERROR: --output needs a value" >&2; exit 2; }
            output=$2
            shift 2
            ;;
        --disassemble)
            [ "$#" -ge 2 ] || { echo "ERROR: --disassemble needs a value" >&2; exit 2; }
            disassembly_specs="${disassembly_specs}${disassembly_specs:+
}$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

for command in gdb objdump sha256sum; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "ERROR: required command not found: $command" >&2
        exit 1
    }
done

if [ -z "$pid" ]; then
    matches=
    for proc_dir in /proc/[0-9]*; do
        [ -r "$proc_dir/cmdline" ] || continue
        argv=$(tr '\000' '\n' < "$proc_dir/cmdline")
        argv0=$(printf '%s\n' "$argv" | sed -n '1p')
        case "${argv0##*/}" in qemu-system-i386) ;; *) continue ;; esac
        printf '%s\n' "$argv" | grep -qx -- '-M' || continue
        printf '%s\n' "$argv" | grep -q '^pinball2000\(,\|$\)' || continue
        candidate=${proc_dir#/proc/}
        matches="${matches}${matches:+
}${candidate}"
    done
    count=$(printf '%s\n' "$matches" | awk 'NF { n++ } END { print n + 0 }')
    case "$count" in
        0)
            echo "ERROR: no running Encore QEMU process found" >&2
            exit 1
            ;;
        1) pid=$matches ;;
        *)
            echo "ERROR: several Encore QEMU processes found; select one with --pid:" >&2
            ps -o pid=,lstart=,args= -p "$(printf '%s' "$matches" | paste -sd, -)" >&2
            exit 1
            ;;
    esac
fi

case "$pid" in
    *[!0-9]*|'') echo "ERROR: invalid PID: $pid" >&2; exit 2 ;;
esac
[ -r "/proc/$pid/cmdline" ] || {
    echo "ERROR: process $pid is unavailable" >&2
    exit 1
}

cmdline_lines=$(tr '\000' '\n' < "/proc/$pid/cmdline")
argv0=$(printf '%s\n' "$cmdline_lines" | sed -n '1p')
case "${argv0##*/}" in
    qemu-system-i386) ;;
    *) echo "ERROR: PID $pid is not qemu-system-i386" >&2; exit 1 ;;
esac
printf '%s\n' "$cmdline_lines" | grep -q '^pinball2000\(,\|$\)' || {
    echo "ERROR: PID $pid is not an Encore pinball2000 machine" >&2
    exit 1
}
cmdline=$(printf '%s\n' "$cmdline_lines" | paste -sd' ' -)

if [ -z "$output" ]; then
    output="/tmp/encore-live-crash-$(date +%Y%m%d-%H%M%S)"
fi
case "$output" in
    *'
'*) echo "ERROR: output path must not contain a newline" >&2; exit 2 ;;
esac
[ ! -e "$output" ] || {
    echo "ERROR: output already exists: $output" >&2
    exit 1
}
parent=${output%/*}
[ "$parent" != "$output" ] || parent=.
[ -d "$parent" ] || {
    echo "ERROR: output parent does not exist: $parent" >&2
    exit 1
}
mkdir -m 700 -- "$output"
output=$(cd "$output" && pwd -P)

printf '%s\n' "$cmdline" > "$output/command-line.txt"
ps -o pid=,ppid=,user=,lstart=,etime=,stat=,%cpu=,%mem=,args= \
    -p "$pid" > "$output/process.txt"
cp "/proc/$pid/maps" "$output/host-maps.txt"
qemu_executable=$(readlink -f "/proc/$pid/exe")
script_path=$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)/$(basename "$0")
{
    printf 'captured_at=%s\n' "$(date --iso-8601=seconds)"
    printf 'host=%s\n' "$(hostname)"
    printf 'pid=%s\n' "$pid"
    printf 'qemu_executable=%s\n' "$qemu_executable"
    printf 'qemu_sha256=%s\n' "$(sha256sum "$qemu_executable" | awk '{print $1}')"
    printf 'capture_tool_sha256=%s\n' "$(sha256sum "$script_path" | awk '{print $1}')"
    printf 'qemu_stdout=%s\n' "$(readlink "/proc/$pid/fd/1" 2>/dev/null || true)"
    printf 'qemu_stderr=%s\n' "$(readlink "/proc/$pid/fd/2" 2>/dev/null || true)"
    printf 'kernel=%s\n' "$(uname -srmo)"
    if command -v git >/dev/null 2>&1; then
        script_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
        commit=$(git -C "$script_root" rev-parse HEAD 2>/dev/null || true)
        [ -z "$commit" ] || printf 'encore_commit=%s\n' "$commit"
        if [ -n "$(git -C "$script_root" status --porcelain 2>/dev/null || true)" ]; then
            printf 'encore_dirty=yes\n'
        else
            printf 'encore_dirty=no\n'
        fi
    fi
} > "$output/metadata.txt"

gdb_commands=$output/gdb.commands
cat > "$gdb_commands" <<EOF
set pagination off
set confirm off
printf "RAM_MEMORY_REGION=%p\\n", current_machine->ram
printf "RAM_BLOCK=%p\\n", current_machine->ram->ram_block
printf "GUEST_RAM_HOST=%p\\n", current_machine->ram->ram_block->host
printf "GUEST_RAM_SIZE=0x%lx\\n", current_machine->ram->ram_block->max_length
info threads
thread apply all bt 8
dump binary memory guest-ram.bin current_machine->ram->ram_block->host current_machine->ram->ram_block->host + current_machine->ram->ram_block->max_length
detach
quit
EOF

echo "Capturing QEMU $pid (it will pause briefly while GDB is attached)..."
if ! (cd "$output" && gdb -q -batch -p "$pid" -x "$gdb_commands" \
        > gdb.txt 2>&1); then
    echo "ERROR: GDB capture failed; QEMU was not terminated." >&2
    echo "Details: $output/gdb.txt" >&2
    exit 1
fi
rm -f -- "$gdb_commands"

[ -s "$output/guest-ram.bin" ] || {
    echo "ERROR: GDB did not produce a guest RAM image" >&2
    exit 1
}

stat -c 'guest_ram_size=%s' "$output/guest-ram.bin" >> "$output/metadata.txt"

if [ -n "$disassembly_specs" ]; then
    : > "$output/guest-disassembly.txt"
    printf '%s\n' "$disassembly_specs" | while IFS= read -r spec; do
        address=${spec%%:*}
        if [ "$address" = "$spec" ]; then
            length=0x100
        else
            length=${spec#*:}
        fi
        case "$address:$length" in
            *[!0-9a-fA-FxX:]*)
                echo "ERROR: invalid --disassemble range: $spec" >&2
                exit 2
                ;;
        esac
        start=$((address))
        size=$((length))
        [ "$size" -gt 0 ] || {
            echo "ERROR: disassembly length must be positive: $spec" >&2
            exit 2
        }
        end=$((start + size))
        {
            printf '\n===== guest 0x%x..0x%x =====\n' "$start" "$end"
            objdump -D -b binary -m i386 -M intel --adjust-vma=0 \
                --start-address="$start" --stop-address="$end" \
                "$output/guest-ram.bin"
        } >> "$output/guest-disassembly.txt"
    done
fi

cat > "$output/README.txt" <<EOF
Encore live-crash capture

QEMU PID: $pid
Guest RAM: guest-ram.bin
Integrity: SHA256SUMS
Host state: metadata.txt, process.txt, command-line.txt, host-maps.txt
GDB attachment/backtraces: gdb.txt
Guest disassembly: guest-disassembly.txt (when requested)

The capture briefly paused and detached from QEMU. It did not terminate QEMU
or deliberately write to guest memory.
EOF

(
    cd "$output"
    for artifact in README.txt command-line.txt gdb.txt guest-ram.bin \
            host-maps.txt metadata.txt process.txt; do
        sha256sum "$artifact"
    done
    if [ -f guest-disassembly.txt ]; then
        sha256sum guest-disassembly.txt
    fi
) > "$output/SHA256SUMS"

echo "Capture complete: $output"
cat "$output/SHA256SUMS"
