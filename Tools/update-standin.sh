#!/bin/sh
# Regenerates the snapshot in Yoi/YoiExtension/WebUI/yoi-standin.js (the browser stand-in's
# parameters, defaults and factory drawings) from the registered Audio Unit and
# YoiFactoryShapes.hpp. Build and run the Yoi app first so macOS has the current plug-in.
set -e
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

clang++ -std=c++17 -I Yoi/YoiExtension/DSP Tools/standin/shapes.cpp -o "$work/shapes"
"$work/shapes" > "$work/shapes.json"
swiftc -O Tools/standin/parameters.swift -o "$work/parameters" 2>/dev/null
"$work/parameters" > "$work/parameters.json"

python3 - "$work/parameters.json" "$work/shapes.json" Yoi/YoiExtension/WebUI/yoi-standin.js <<'PY'
import json, re, sys
snapshot = json.load(open(sys.argv[1]))
snapshot['shapes'] = json.load(open(sys.argv[2]))
path = sys.argv[3]
source = open(path).read()
line = 'const SNAPSHOT = ' + json.dumps(snapshot, separators=(',', ':')) + ';'
updated, count = re.subn(r'const SNAPSHOT = .*;', lambda _: line, source, count=1)
if count != 1:
    sys.exit('No SNAPSHOT line found in ' + path)
open(path, 'w').write(updated)
print('Updated', path, '-', len(snapshot['parameters']), 'parameters,', len(snapshot['shapes']), 'drawings')
PY
