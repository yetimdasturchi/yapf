#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
yapf_dir="$(cd -- "$script_dir/.." && pwd)"

prefix="/usr"
sysconfdir="/etc"
bindir=""
libdir=""
config_path=""
strip_output=0

usage() {
  cat <<'USAGE'
Usage: install.sh [options]

Install YAPF tools into a system layout.

Options:
  --prefix DIR       Install prefix. Default: /usr.
  --sysconfdir DIR   Config directory. Default: /etc.
  --bindir DIR       Binary directory. Default: PREFIX/bin.
  --libdir DIR       Runtime library directory. Default: PREFIX/lib/yapf.
  --config FILE      Config file path. Default: SYSCONFDIR/yapf.conf.
  --strip            Strip installed runtime binaries.
  --help             Show this help.

Installed layout:
  /etc/yapf.conf
  /usr/bin/yapf-build
  /usr/bin/yapf-release
  /usr/bin/yapf-pack
  /usr/bin/yapf-seal
  /usr/bin/yapf-client
  /usr/lib/yapf/start
  /usr/lib/yapf/yapf_loader.so
  /usr/lib/yapf/client
USAGE
}

while (($# > 0)); do
  case "$1" in
    --prefix)
      [[ -n "${2:-}" ]] || { echo "--prefix requires a value" >&2; exit 2; }
      prefix="$2"
      shift 2
      ;;
    --sysconfdir)
      [[ -n "${2:-}" ]] || { echo "--sysconfdir requires a value" >&2; exit 2; }
      sysconfdir="$2"
      shift 2
      ;;
    --bindir)
      [[ -n "${2:-}" ]] || { echo "--bindir requires a value" >&2; exit 2; }
      bindir="$2"
      shift 2
      ;;
    --libdir)
      [[ -n "${2:-}" ]] || { echo "--libdir requires a value" >&2; exit 2; }
      libdir="$2"
      shift 2
      ;;
    --config)
      [[ -n "${2:-}" ]] || { echo "--config requires a value" >&2; exit 2; }
      config_path="$2"
      shift 2
      ;;
    --strip)
      strip_output=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

bindir="${bindir:-$prefix/bin}"
libdir="${libdir:-$prefix/lib/yapf}"
config_path="${config_path:-$sysconfdir/yapf.conf}"

pack_bin="$yapf_dir/build/bin/yapf-pack"
seal_bin="$yapf_dir/build/bin/yapf-seal"
client_bin="$yapf_dir/build/bin/yapf-client"
start_bin="$yapf_dir/build/bin/start"
loader_so="$yapf_dir/build/loader/yapf_loader.so"

for file in "$pack_bin" "$seal_bin" "$client_bin" "$start_bin" "$loader_so" "$script_dir/build.sh" "$script_dir/release.sh"; do
  if [[ ! -f "$file" ]]; then
    echo "Missing install source: $file" >&2
    echo "Run: make -C tools/yapf native loader" >&2
    exit 1
  fi
done

install -d "$bindir" "$libdir" "$(dirname -- "$config_path")"
install -m 0755 "$pack_bin" "$bindir/yapf-pack"
install -m 0755 "$seal_bin" "$bindir/yapf-seal"
install -m 0755 "$script_dir/build.sh" "$bindir/yapf-build"
install -m 0755 "$script_dir/release.sh" "$bindir/yapf-release"
install -m 0755 "$start_bin" "$libdir/start"
install -m 0755 "$loader_so" "$libdir/yapf_loader.so"
install -m 0755 "$client_bin" "$libdir/client"
ln -sfn "$libdir/client" "$bindir/yapf-client"

if [[ ! -f "$config_path" ]]; then
  tmp_conf="$(mktemp)"
  sed \
    -e "s|^YAPF_LIB_DIR=.*|YAPF_LIB_DIR=$libdir|" \
    -e "s|^YAPF_BIN_DIR=.*|YAPF_BIN_DIR=$bindir|" \
    -e "s|^YAPF_PACK_BIN=.*|YAPF_PACK_BIN=$bindir/yapf-pack|" \
    -e "s|^YAPF_SEAL_BIN=.*|YAPF_SEAL_BIN=$bindir/yapf-seal|" \
    -e "s|^YAPF_RELEASE_BIN=.*|YAPF_RELEASE_BIN=$bindir/yapf-release|" \
    -e "s|^YAPF_START_BIN=.*|YAPF_START_BIN=$libdir/start|" \
    -e "s|^YAPF_LOADER_SO=.*|YAPF_LOADER_SO=$libdir/yapf_loader.so|" \
    -e "s|^YAPF_CLIENT_BIN=.*|YAPF_CLIENT_BIN=$libdir/client|" \
    "$yapf_dir/config/yapf.conf.example" > "$tmp_conf"
  install -m 0644 "$tmp_conf" "$config_path"
  rm -f "$tmp_conf"
fi

if [[ "$strip_output" -eq 1 ]]; then
  if ! command -v strip >/dev/null 2>&1; then
    echo "strip command not found" >&2
    exit 1
  fi
  strip "$libdir/start"
  strip "$libdir/yapf_loader.so"
  strip "$libdir/client"
fi

cat <<EOF
YAPF installed:
  config: $config_path
  bin:    $bindir
  lib:    $libdir
EOF
