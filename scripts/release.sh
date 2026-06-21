#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
yapf_dir="$(cd -- "$script_dir/.." && pwd)"
config_file="${YAPF_CONFIG:-/etc/yapf.conf}"

source_dir=""
overlay_dir=""
output_dir=""
app_id=""
entry="public/index.php"
machine_code=""
env_file=""
expires_at="null"
limit_type="none"
limit_value="0"
strip_output=0
pack_args=()
config_loaded=0

usage() {
  cat <<'USAGE'
Usage: release.sh --source DIR --output DIR --app-id ID --machine-code CODE [options]

Build a runnable YAPF app folder.

Required:
  --source DIR          PHP project source directory.
  --output DIR          Output runtime app directory.
  --app-id ID           App id stored in app/license/state files.
  --machine-code CODE   Customer machine id from yapf-client --raw.

Options:
  --overlay DIR         Optional overlay directory copied over source before packing.
  --entry FILE          Main PHP file inside source. Default: public/index.php.
  --config FILE         Config file with release defaults. Default: /etc/yapf.conf.
  --env-file FILE       Copy this file as .env. If omitted, a minimal .env is generated.
  --expires-at VALUE    null, YYYY-MM-DD, or epoch timestamp. Default: null.
  --limit-type TYPE     none, days, or runs. Default: none.
  --limit-value N       Limit value. Default: 0.
  --strip               Strip start, client, and yapf_loader.so in the output directory.
  --exclude PATH        Forwarded to yapf-pack. Can be repeated.
  --allow-ext LIST      Forwarded to yapf-pack.
  --allow-file PATH     Forwarded to yapf-pack. Can be repeated.
  --help                Show this help.

Config defaults:
  YAPF_RELEASE_SOURCE_DIR
  YAPF_RELEASE_OVERLAY_DIR
  YAPF_RELEASE_OUTPUT_DIR
  YAPF_RELEASE_APP_ID
  YAPF_RELEASE_ENTRY
  YAPF_RELEASE_MACHINE_CODE
  YAPF_RELEASE_ENV_FILE
  YAPF_RELEASE_EXPIRES_AT
  YAPF_RELEASE_LIMIT_TYPE
  YAPF_RELEASE_LIMIT_VALUE
  YAPF_RELEASE_STRIP
  YAPF_RELEASE_EXCLUDE=(tests docs)
  YAPF_RELEASE_ALLOW_EXT=php,json,lock
  YAPF_RELEASE_ALLOW_FILE=(composer.lock artisan)
USAGE
}

load_config() {
  if [[ "$config_loaded" -eq 1 ]]; then
    return
  fi
  if [[ -f "$config_file" ]]; then
    source "$config_file"
  fi
  config_loaded=1
}

pre_scan_config() {
  while (($# > 0)); do
    case "$1" in
      --config)
        require_value "$1" "${2:-}"
        config_file="$2"
        shift 2
        ;;
      --help|-h)
        usage
        exit 0
        ;;
      *)
        shift
        ;;
    esac
  done
}

append_pack_arg_values() {
  local option="$1"
  shift
  local value
  for value in "$@"; do
    [[ -n "$value" ]] || continue
    pack_args+=("$option" "$value")
  done
}

is_enabled() {
  case "${1:-0}" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

apply_config_defaults() {
  load_config

  source_dir="${YAPF_RELEASE_SOURCE_DIR:-$source_dir}"
  overlay_dir="${YAPF_RELEASE_OVERLAY_DIR:-$overlay_dir}"
  output_dir="${YAPF_RELEASE_OUTPUT_DIR:-$output_dir}"
  app_id="${YAPF_RELEASE_APP_ID:-$app_id}"
  entry="${YAPF_RELEASE_ENTRY:-$entry}"
  machine_code="${YAPF_RELEASE_MACHINE_CODE:-$machine_code}"
  env_file="${YAPF_RELEASE_ENV_FILE:-$env_file}"
  expires_at="${YAPF_RELEASE_EXPIRES_AT:-$expires_at}"
  limit_type="${YAPF_RELEASE_LIMIT_TYPE:-$limit_type}"
  limit_value="${YAPF_RELEASE_LIMIT_VALUE:-$limit_value}"
  strip_output="${YAPF_RELEASE_STRIP:-$strip_output}"

  if declare -p YAPF_RELEASE_EXCLUDE >/dev/null 2>&1; then
    if [[ "$(declare -p YAPF_RELEASE_EXCLUDE)" == declare\ -a* ]]; then
      append_pack_arg_values --exclude "${YAPF_RELEASE_EXCLUDE[@]}"
    else
      append_pack_arg_values --exclude "$YAPF_RELEASE_EXCLUDE"
    fi
  fi

  if [[ -n "${YAPF_RELEASE_ALLOW_EXT:-}" ]]; then
    pack_args+=(--allow-ext "$YAPF_RELEASE_ALLOW_EXT")
  fi

  if declare -p YAPF_RELEASE_ALLOW_FILE >/dev/null 2>&1; then
    if [[ "$(declare -p YAPF_RELEASE_ALLOW_FILE)" == declare\ -a* ]]; then
      append_pack_arg_values --allow-file "${YAPF_RELEASE_ALLOW_FILE[@]}"
    else
      append_pack_arg_values --allow-file "$YAPF_RELEASE_ALLOW_FILE"
    fi
  fi
}

repo_artifact_exists() {
  [[ -f "$yapf_dir/build/bin/yapf-pack" && \
     -f "$yapf_dir/build/bin/yapf-seal" && \
     -f "$yapf_dir/build/bin/yapf-client" && \
     -f "$yapf_dir/build/bin/start" && \
     -f "$yapf_dir/build/loader/yapf_loader.so" ]]
}

resolve_artifacts() {
  load_config

  if repo_artifact_exists; then
    pack_bin="$yapf_dir/build/bin/yapf-pack"
    seal_bin="$yapf_dir/build/bin/yapf-seal"
    client_bin="$yapf_dir/build/bin/yapf-client"
    start_bin="$yapf_dir/build/bin/start"
    loader_so="$yapf_dir/build/loader/yapf_loader.so"
    return
  fi

  local lib_dir="${YAPF_LIB_DIR:-/usr/lib/yapf}"
  local bin_dir="${YAPF_BIN_DIR:-/usr/bin}"
  pack_bin="${YAPF_PACK_BIN:-$bin_dir/yapf-pack}"
  seal_bin="${YAPF_SEAL_BIN:-$bin_dir/yapf-seal}"
  client_bin="${YAPF_CLIENT_BIN:-$lib_dir/client}"
  start_bin="${YAPF_START_BIN:-$lib_dir/start}"
  loader_so="${YAPF_LOADER_SO:-$lib_dir/yapf_loader.so}"
}

require_value() {
  local name="$1"
  local value="${2:-}"
  if [[ -z "$value" ]]; then
    echo "$name requires a value" >&2
    exit 2
  fi
}

pre_scan_config "$@"
apply_config_defaults

while (($# > 0)); do
  case "$1" in
    --source)
      require_value "$1" "${2:-}"
      source_dir="$2"
      shift 2
      ;;
    --overlay)
      require_value "$1" "${2:-}"
      overlay_dir="$2"
      shift 2
      ;;
    --output)
      require_value "$1" "${2:-}"
      output_dir="$2"
      shift 2
      ;;
    --app-id)
      require_value "$1" "${2:-}"
      app_id="$2"
      shift 2
      ;;
    --entry)
      require_value "$1" "${2:-}"
      entry="$2"
      shift 2
      ;;
    --config)
      require_value "$1" "${2:-}"
      config_file="$2"
      shift 2
      ;;
    --machine-code)
      require_value "$1" "${2:-}"
      machine_code="$2"
      shift 2
      ;;
    --env-file)
      require_value "$1" "${2:-}"
      env_file="$2"
      shift 2
      ;;
    --expires-at)
      require_value "$1" "${2:-}"
      expires_at="$2"
      shift 2
      ;;
    --limit-type)
      require_value "$1" "${2:-}"
      limit_type="$2"
      shift 2
      ;;
    --limit-value)
      require_value "$1" "${2:-}"
      limit_value="$2"
      shift 2
      ;;
    --strip)
      strip_output=1
      shift
      ;;
    --exclude|--allow-ext|--allow-file)
      require_value "$1" "${2:-}"
      pack_args+=("$1" "$2")
      shift 2
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

if [[ -z "$source_dir" || -z "$output_dir" || -z "$app_id" || -z "$machine_code" ]]; then
  usage >&2
  exit 2
fi

resolve_artifacts

for file in "$pack_bin" "$seal_bin" "$client_bin" "$start_bin" "$loader_so"; do
  if [[ ! -f "$file" ]]; then
    echo "Missing build artifact: $file" >&2
    echo "Run make -C tools/yapf native loader or install YAPF system tools." >&2
    exit 1
  fi
done

if [[ ! -d "$source_dir" ]]; then
  echo "Source directory not found: $source_dir" >&2
  exit 1
fi

if [[ -n "$overlay_dir" && ! -d "$overlay_dir" ]]; then
  echo "Overlay directory not found: $overlay_dir" >&2
  exit 1
fi

if [[ -n "$env_file" && ! -f "$env_file" ]]; then
  echo "Env file not found: $env_file" >&2
  exit 1
fi

pack_source="$source_dir"
tmp_source=""
if [[ -n "$overlay_dir" ]]; then
  tmp_parent="$(mktemp -d "${TMPDIR:-/tmp}/yapf-release.XXXXXX")"
  tmp_source="$tmp_parent/source"
  mkdir -p "$tmp_source"
  cp -a "$source_dir"/. "$tmp_source"/
  cp -a "$overlay_dir"/. "$tmp_source"/
  pack_source="$tmp_source"
  trap '[[ -n "${tmp_parent:-}" ]] && rm -rf "$tmp_parent"' EXIT
fi

mkdir -p "$output_dir/storage"
cp "$start_bin" "$output_dir/start"
cp "$client_bin" "$output_dir/client"
cp "$loader_so" "$output_dir/yapf_loader.so"
chmod 0755 "$output_dir/start" "$output_dir/client"

if is_enabled "$strip_output"; then
  if ! command -v strip >/dev/null 2>&1; then
    echo "strip command not found" >&2
    exit 1
  fi
  strip "$output_dir/start"
  strip "$output_dir/client"
  strip "$output_dir/yapf_loader.so"
fi

state_id="$(openssl rand -hex 16)"
state_seed="$(openssl rand -hex 32)"

"$pack_bin" \
  --source "$pack_source" \
  --output "$output_dir/app.yapfc" \
  --app-id "$app_id" \
  --entry "$entry" \
  "${pack_args[@]}"

"$seal_bin" \
  --kind license \
  --output "$output_dir/license.yapfl" \
  --app-id "$app_id" \
  --machine-code "$machine_code" \
  --state-id "$state_id" \
  --state-seed "$state_seed" \
  --expires-at "$expires_at" \
  --limit-type "$limit_type" \
  --limit-value "$limit_value"

"$seal_bin" \
  --kind state \
  --output "$output_dir/license.yapfs" \
  --app-id "$app_id" \
  --machine-code "$machine_code" \
  --state-id "$state_id" \
  --state-seed "$state_seed"

if [[ -n "$env_file" ]]; then
  cp "$env_file" "$output_dir/.env"
else
  app_storage="$(cd -- "$output_dir" && pwd)/storage"
  printf 'APP_ENV=production\nAPP_STORAGE=%s\n' "$app_storage" > "$output_dir/.env"
fi

cat <<EOF
YAPF release created:
  app:     $app_id
  source:  $source_dir
  overlay: ${overlay_dir:-none}
  output:  $output_dir
  entry:   $entry
  strip:   $strip_output
EOF
