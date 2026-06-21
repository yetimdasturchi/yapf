#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_yapf_dir="$(cd -- "$script_dir/.." && pwd)"
config_file="${YAPF_CONFIG:-/etc/yapf.conf}"
source_dir="${YAPF_SOURCE_DIR:-}"
target="all"
make_args=()

usage() {
  cat <<'USAGE'
Usage: yapf-build [target] [options] [-- MAKE_ARGS...]

Build YAPF native tools and loader from source.

Targets:
  all       Build native tools and loader. Default.
  native    Build yapf-pack, yapf-seal, yapf-client, and start.
  loader    Build yapf_loader.so.
  clean     Remove build artifacts.

Options:
  --source DIR       YAPF source directory. Default: current checkout if available.
  --config FILE      Config file. Default: /etc/yapf.conf.
  --help             Show this help.

Environment:
  MACHINE_SALT       Required for native/loader builds.
  CRYPTO_SECRET      Required for native/loader builds.
  PHP_CONFIG         Optional php-config binary.
  PHPIZE             Optional phpize binary.
  PHP_BIN            Optional PHP runtime binary used by start.
USAGE
}

while (($# > 0)); do
  case "$1" in
    all|native|loader|clean)
      target="$1"
      shift
      ;;
    --source)
      [[ -n "${2:-}" ]] || { echo "--source requires a value" >&2; exit 2; }
      source_dir="$2"
      shift 2
      ;;
    --config)
      [[ -n "${2:-}" ]] || { echo "--config requires a value" >&2; exit 2; }
      config_file="$2"
      shift 2
      ;;
    --)
      shift
      make_args+=("$@")
      break
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      make_args+=("$1")
      shift
      ;;
  esac
done

if [[ -f "$config_file" ]]; then
  source "$config_file"
fi

if [[ -z "$source_dir" ]]; then
  if [[ -f "$PWD/Makefile" && -d "$PWD/src" && -d "$PWD/include" ]]; then
    source_dir="$PWD"
  elif [[ -f "$repo_yapf_dir/Makefile" ]]; then
    source_dir="$repo_yapf_dir"
  fi
fi

if [[ -z "$source_dir" || ! -f "$source_dir/Makefile" ]]; then
  echo "YAPF source directory not found. Pass --source DIR." >&2
  exit 1
fi

if [[ "$target" != "clean" ]]; then
  machine_salt="${MACHINE_SALT:-${YAPF_MACHINE_SALT:-}}"
  crypto_secret="${CRYPTO_SECRET:-${YAPF_CRYPTO_SECRET:-}}"
  [[ -n "$machine_salt" ]] || { echo "MACHINE_SALT is required" >&2; exit 1; }
  [[ -n "$crypto_secret" ]] || { echo "CRYPTO_SECRET is required" >&2; exit 1; }
  make_args+=("MACHINE_SALT=$machine_salt" "CRYPTO_SECRET=$crypto_secret")
fi

case "$target" in
  all)
    make -C "$source_dir" native loader "${make_args[@]}"
    ;;
  *)
    make -C "$source_dir" "$target" "${make_args[@]}"
    ;;
esac
