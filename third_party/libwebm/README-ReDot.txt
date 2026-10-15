libwebm (WebM/Matroska muxer + parser), BSD-3-Clause (see LICENSE.TXT, PATENTS.TXT).
Upstream: https://chromium.googlesource.com/webm/libwebm
Pinned tag: libwebm-1.0.0.31, commit 6745fd29e0245fc584b0bb9f65018ea2366fe7fb
Vendored unmodified: mkvmuxer/, mkvparser/, common/ only (no samples/tests/webm_parser).
ReDot uses it only as a Matroska (not WebM) container for H.264 + AAC; libwebm picks
DocType "matroska" automatically for non-WebM codec ids.
