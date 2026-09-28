#!/usr/bin/env bash
# Shared, side-effect-free IPv4 validation for the launcher and installer.

valid_ipv4() {
  local value="$1" octet
  local -a octets

  [[ "$value" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]] || return 1
  IFS=. read -r -a octets <<<"$value"
  ((${#octets[@]} == 4)) || return 1
  for octet in "${octets[@]}"; do
    ((10#$octet <= 255)) || return 1
  done
}

ipv4_to_u32() {
  local value="$1"
  local -a octets

  valid_ipv4 "$value" || return 1
  IFS=. read -r -a octets <<<"$value"
  printf '%u\n' "$((
    (10#${octets[0]} << 24) |
    (10#${octets[1]} << 16) |
    (10#${octets[2]} << 8) |
    10#${octets[3]}
  ))"
}

valid_ipv4_netmask() {
  local mask inverse

  mask="$(ipv4_to_u32 "$1")" || return 1
  inverse=$((0xffffffff ^ mask))
  (( (inverse & (inverse + 1)) == 0 ))
}

ipv4_same_subnet() {
  local address gateway mask

  address="$(ipv4_to_u32 "$1")" || return 1
  gateway="$(ipv4_to_u32 "$2")" || return 1
  mask="$(ipv4_to_u32 "$3")" || return 1
  (( (address & mask) == (gateway & mask) ))
}
