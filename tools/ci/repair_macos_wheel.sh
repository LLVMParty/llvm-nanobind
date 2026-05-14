#!/usr/bin/env bash
#
# repair_macos_wheel.sh — delocate plus auditwheel-style hash mangling for macOS wheels.
#
# Usage as CIBW_REPAIR_WHEEL_COMMAND_MACOS:
#   bash tools/ci/repair_macos_wheel.sh {wheel} {dest_dir} {delocate_archs}
#
# The script first runs delocate-wheel, then renames vendored dylibs from e.g.
# libLLVM.dylib to libLLVM-<sha256-prefix>.dylib and patches Mach-O load
# commands. This prevents DYLD_LIBRARY_PATH from shadowing a bundled dylib with
# a same-named system/Homebrew dylib.

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: $0 WHEEL DEST_DIR [DELOCATE_ARCHS]" >&2
  exit 2
fi

WHEEL="$1"
DEST_DIR="$2"
DELOCATE_ARCHS="${3:-}"
HASH_LEN="${HASH_LEN:-8}"

if ! [[ "$HASH_LEN" =~ ^[0-9]+$ ]] || [[ "$HASH_LEN" -lt 1 ]]; then
  echo "HASH_LEN must be a positive integer, got: $HASH_LEN" >&2
  exit 2
fi

for cmd in delocate-wheel unzip find file otool install_name_tool shasum codesign python; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "required command not found: $cmd" >&2
    exit 1
  fi
done

mkdir -p "$DEST_DIR"

# Step 1: run delocate, preserving cibuildwheel's architecture validation when
# {delocate_archs} is provided.
delocate_args=(-w "$DEST_DIR" -v)
if [[ -n "$DELOCATE_ARCHS" ]]; then
  delocate_args+=(--require-archs "$DELOCATE_ARCHS")
fi
delocate_args+=("$WHEEL")

echo "+ delocate-wheel ${delocate_args[*]}"
delocate-wheel "${delocate_args[@]}"

REPAIRED_WHEEL="$DEST_DIR/$(basename "$WHEEL")"
if [[ ! -f "$REPAIRED_WHEEL" ]]; then
  echo "delocate did not create expected wheel: $REPAIRED_WHEEL" >&2
  exit 1
fi

# Step 2: unpack the repaired wheel.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
unzip -q "$REPAIRED_WHEEL" -d "$WORK"

# Step 3: locate delocate vendor directories.
DYLIB_DIRS=()
while IFS= read -r -d '' dir; do
  DYLIB_DIRS+=("$dir")
done < <(find "$WORK" -type d -name '*.dylibs' -print0)

if [[ ${#DYLIB_DIRS[@]} -eq 0 ]]; then
  echo "No .dylibs directories found; delocated wheel left unchanged: $REPAIRED_WHEEL"
  exit 0
fi

OLD_BASENAMES=()
NEW_BASENAMES=()

add_rename_mapping() {
  local old_name="$1"
  local new_name="$2"
  local i

  for ((i = 0; i < ${#OLD_BASENAMES[@]}; i++)); do
    if [[ "${OLD_BASENAMES[$i]}" == "$old_name" ]]; then
      if [[ "${NEW_BASENAMES[$i]}" != "$new_name" ]]; then
        echo "conflicting dylib basename mapping for $old_name: ${NEW_BASENAMES[$i]} vs $new_name" >&2
        exit 1
      fi
      return 0
    fi
  done

  OLD_BASENAMES+=("$old_name")
  NEW_BASENAMES+=("$new_name")
}

collect_macho_files() {
  MACHO_FILES=()
  local file_path
  while IFS= read -r -d '' file_path; do
    if file "$file_path" | grep -q 'Mach-O'; then
      MACHO_FILES+=("$file_path")
    fi
  done < <(find "$WORK" -type f \( -name '*.so' -o -name '*.dylib' \) -print0)
}

# Step 4: hash-rename every vendored dylib and patch its LC_ID_DYLIB.
for dylib_dir in "${DYLIB_DIRS[@]}"; do
  while IFS= read -r -d '' dylib; do
    old_basename="$(basename "$dylib")"
    stem="${old_basename%.dylib}"
    hash="$(shasum -a 256 "$dylib" | awk -v n="$HASH_LEN" '{ print substr($1, 1, n) }')"
    new_basename="${stem}-${hash}.dylib"
    new_path="$dylib_dir/$new_basename"

    if [[ "$old_basename" == "$new_basename" ]]; then
      continue
    fi

    echo "Renaming vendored dylib: $old_basename -> $new_basename"
    mv "$dylib" "$new_path"

    old_id="$(otool -D "$new_path" | awk 'NR == 2 { print $1 }')"
    if [[ -n "$old_id" && "$old_id" == *"$old_basename"* ]]; then
      new_id="${old_id/$old_basename/$new_basename}"
      echo "  LC_ID_DYLIB: $old_id -> $new_id"
      install_name_tool -id "$new_id" "$new_path"
    else
      echo "  warning: LC_ID_DYLIB for $new_basename did not contain $old_basename: ${old_id:-<none>}" >&2
    fi

    add_rename_mapping "$old_basename" "$new_basename"
  done < <(find "$dylib_dir" -maxdepth 1 -type f -name '*.dylib' -print0)
done

# Re-collect after renaming so vendored dylibs are patched as consumers too.
collect_macho_files

# Step 5: patch LC_LOAD_DYLIB in every Mach-O consumer in the wheel.
for macho in "${MACHO_FILES[@]}"; do
  deps="$(otool -L "$macho" | awk 'NR > 1 { print $1 }')"

  for ((i = 0; i < ${#OLD_BASENAMES[@]}; i++)); do
    old_basename="${OLD_BASENAMES[$i]}"
    new_basename="${NEW_BASENAMES[$i]}"

    while IFS= read -r dep; do
      [[ -z "$dep" ]] && continue
      if [[ "$dep" == *"$old_basename"* ]]; then
        new_dep="${dep/$old_basename/$new_basename}"
        if [[ "$dep" != "$new_dep" ]]; then
          echo "  LC_LOAD_DYLIB in ${macho#$WORK/}: $dep -> $new_dep"
          install_name_tool -change "$dep" "$new_dep" "$macho"
        fi
      fi
    done <<< "$deps"
  done
done

# Step 6: ad-hoc sign modified Mach-O files. install_name_tool invalidates
# existing signatures, and Apple Silicon requires loadable Mach-O files to have
# a valid signature.
echo "Re-signing Mach-O files..."
collect_macho_files
for macho in "${MACHO_FILES[@]}"; do
  codesign --force --sign - --timestamp=none "$macho" >/dev/null
  echo "  signed ${macho#$WORK/}"
done

# Step 7: remove the old RECORD and repack. wheel pack regenerates RECORD.
DIST_INFO="$(find "$WORK" -maxdepth 2 -type d -name '*.dist-info' | head -n 1)"
if [[ -z "$DIST_INFO" || ! -d "$DIST_INFO" ]]; then
  echo "could not find .dist-info directory in wheel" >&2
  exit 1
fi

if ! python -m wheel version >/dev/null 2>&1; then
  echo "python package 'wheel' is required for repacking; installing it..."
  python -m pip install --disable-pip-version-check wheel
fi

rm -f "$DIST_INFO/RECORD"
rm -f "$REPAIRED_WHEEL"
python -m wheel pack "$WORK" -d "$DEST_DIR"

echo "Done. Repaired wheel(s):"
ls -1 "$DEST_DIR"/*.whl
