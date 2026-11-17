#!/usr/bin/env bash
# Builds Quickshell for Windows, together with the two native QML plugins the
# caelestia shell needs, and lays them out as a directory that runs without
# MSYS2 installed.
#
#   ci/windows/bundle.sh <work dir> <output dir>
#
# Run from an MSYS2 UCRT64 shell at the top of the repository. The work
# directory holds the build trees and can be kept between runs. The output
# directory is replaced.
#
# The result, with the program at the top so the directory can go on PATH:
#
#   quickshell.exe, Qt, the compiler runtime and every other DLL
#   qt.conf     tells Qt where the two directories below are
#   plugins/    Qt plugins
#   qml/        Qt's QML modules, Caelestia and M3Shapes
#   manifest.json
#
# Caelestia's plugin and M3Shapes are built here, not by whoever packages the
# shell, because a QML plugin has to be built against the Qt it is loaded
# into.
set -euo pipefail

if [ $# -ne 2 ]; then
	echo "usage: $0 <work dir> <output dir>" >&2
	exit 2
fi

src=$(pwd)
mkdir -p "$1" "$2"
work=$(cd "$1" && pwd)
out=$(cd "$2" && pwd)
here=$src/ci/windows

# shellcheck source=versions.env
. "$here/versions.env"

mingw=${MINGW_PREFIX:?run this from an MSYS2 MinGW shell}

# Fetches one revision of a repository into a directory, without history.
checkout() {
	local repo=$1 rev=$2 dir=$3
	if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" = "$rev" ]; then
		git -C "$dir" reset -q --hard
		git -C "$dir" clean -qfd
		return
	fi
	rm -rf "$dir"
	git init -q "$dir"
	# Without the user's configuration: a rewrite of GitHub URLs to SSH there
	# would make a public fetch need a key.
	GIT_CONFIG_GLOBAL=/dev/null git -C "$dir" -c core.autocrlf=false fetch -q --depth 1 "$repo" "$rev"
	git -C "$dir" -c core.autocrlf=false checkout -q FETCH_HEAD
}

echo "== Quickshell"
cmake -S "$src" -B "$work/quickshell" -G Ninja \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DDISTRIBUTOR="calebstewart/quickshell (Windows bundle)" \
	-DWAYLAND=OFF -DX11=OFF -DI3=OFF -DCRASH_HANDLER=OFF -DUSE_JEMALLOC=OFF \
	-DBLUETOOTH=OFF -DNETWORK=OFF \
	-DSERVICE_GREETD=OFF -DSERVICE_MPRIS=OFF -DSERVICE_NOTIFICATIONS=OFF \
	-DSERVICE_PAM=OFF -DSERVICE_PIPEWIRE=OFF -DSERVICE_POLKIT=OFF \
	-DSERVICE_STATUS_NOTIFIER=OFF -DSERVICE_UPOWER=OFF
cmake --build "$work/quickshell"

prefix=$work/prefix
rm -rf "$prefix"

echo "== Caelestia plugin ($CAELESTIA_REV)"
checkout "$CAELESTIA_REPO" "$CAELESTIA_REV" "$work/caelestia"
git -C "$work/caelestia" apply "$here/caelestia-plugin.patch"
cmake -S "$work/caelestia" -B "$work/caelestia-build" -G Ninja \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_INSTALL_PREFIX="$prefix" \
	-DENABLE_MODULES=plugin \
	-DVERSION="$CAELESTIA_VERSION" \
	-DGIT_REVISION="$CAELESTIA_REV" \
	-DINSTALL_QMLDIR=qml
cmake --build "$work/caelestia-build"
cmake --install "$work/caelestia-build"

echo "== M3Shapes ($M3SHAPES_REV)"
checkout "$M3SHAPES_REPO" "$M3SHAPES_REV" "$work/m3shapes"
cmake -S "$work/m3shapes" -B "$work/m3shapes-build" -G Ninja \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_INSTALL_PREFIX="$prefix" \
	-DINSTALL_QMLDIR=qml
cmake --build "$work/m3shapes-build"
cmake --install "$work/m3shapes-build"

echo "== Bundle"
rm -rf "${out:?}"/*
mkdir -p "$out/plugins" "$out/qml"

cp "$work/quickshell/quickshell.exe" "$out/"
cp -r "$mingw/share/qt6/plugins/." "$out/plugins/"
cp -r "$mingw/share/qt6/qml/." "$out/qml/"
cp -r "$prefix/qml/." "$out/qml/"

# Windows looks for a DLL beside the program and on PATH, not beside the
# plugin that asks for it, so the libraries the two plugins share go beside the program.
mv "$out/qml/Caelestia/lib/"*.dll "$out/"
rmdir "$out/qml/Caelestia/lib"
mv "$out/qml/M3Shapes/m3shapes.dll" "$out/"

cat >"$out/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = plugins
QmlImports = qml
EOF

# Copy in every DLL something in the bundle asks for and MSYS2 provides, then
# what those ask for, until nothing new turns up. A name MSYS2 does not have
# is part of Windows.
seen=$work/bundle-seen
: >"$seen"
while :; do
	added=0
	while IFS= read -r file; do
		grep -qxF "$file" "$seen" && continue
		echo "$file" >>"$seen"
		for dll in $(objdump -p "$file" 2>/dev/null | sed -n 's/^\s*DLL Name: //p'); do
			if [ ! -e "$out/$dll" ] && [ -e "$mingw/bin/$dll" ]; then
				cp "$mingw/bin/$dll" "$out/"
				added=1
			fi
		done
	done < <(find "$out" -type f \( -iname '*.dll' -o -iname '*.exe' \))
	[ "$added" = 1 ] || break
done

find "$out" -type f \( -iname '*.dll' -o -iname '*.exe' \) -exec strip --strip-unneeded {} +

# What went in, for whoever installs it and for the licences of the parts.
{
	printf '{\n'
	printf '  "quickshell": "%s",\n' "$(git -C "$src" rev-parse HEAD)"
	printf '  "caelestia": "%s",\n' "$CAELESTIA_REV"
	printf '  "caelestiaVersion": "%s",\n' "$CAELESTIA_VERSION"
	printf '  "m3shapes": "%s",\n' "$M3SHAPES_REV"
	printf '  "qt": "%s",\n' "$(qmake6 -query QT_VERSION)"
	printf '  "packages": [\n'
	pacman -Q | grep "^${MINGW_PACKAGE_PREFIX}-" | sed 's/.*/    "&"/' | sed '$!s/$/,/'
	printf '  ]\n'
	printf '}\n'
} >"$out/manifest.json"

echo "bundle in $out: $(du -sh "$out" | cut -f1), $(find "$out" -maxdepth 1 -name "*.dll" | wc -l) DLLs beside the program"
