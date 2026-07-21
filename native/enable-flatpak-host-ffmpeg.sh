#!/usr/bin/env bash
set -euo pipefail

app_id="org.prismlauncher.PrismLauncher"

# Prism is a KDE Flatpak and does not ship FFmpeg development/runtime libs.
# host-os is read-only; it exposes the host libraries under /run/host.
flatpak override --user "$app_id" \
  --nofilesystem=/usr/lib \
  --filesystem=host-os:ro \
  --env=LD_LIBRARY_PATH=/run/host/usr/lib:/run/host/usr/lib64

echo "Enabled read-only host FFmpeg lookup for $app_id"
echo "Restart Prism Launcher before testing MediaPlayer."
