#!/usr/bin/env bash
# make-books.sh - turn your own GOG Ultima III PDFs into 3DS-sized book pages.
#
# Usage: tools/make-books.sh <dir-with-GOG-pdfs> <sd-staging-dir>
#   <dir-with-GOG-pdfs> holds Ultima_3_manual.pdf, Ultima_3_map.pdf,
#       "Ultima 3 - Wizard Spellbook.pdf", "Ultima 3 - Cleric Spellbook.pdf",
#       Ultima_3_cluebook.pdf (the GOG "Ultima 1+2+3" extras)
#   <sd-staging-dir> is the folder that mirrors the SD card root; pages land in
#       <sd-staging-dir>/3ds/u3-3ds/books/<book>/p-NN.jpg
#
# Needs: pdftoppm (poppler-utils). For the large-text Book of Play also:
#   tesseract, imagemagick (convert, identify).
#
# The original manuals are copyrighted: these outputs stay on your own SD card
# and are never committed to the repo.
set -euo pipefail
SRC=${1:?pdf folder}; OUT=${2:?sd staging folder}/3ds/u3-3ds/books
mkdir -p "$OUT"/{manual,cleric,wizard,map,cluebook}
J="-jpeg -jpegopt quality=82,optimize=y"

pdftoppm -r 108 $J "$SRC/Ultima_3_manual.pdf"              "$OUT/manual/p"
pdftoppm -r 108 $J "$SRC/Ultima 3 - Cleric Spellbook.pdf"  "$OUT/cleric/p"
pdftoppm -r 108 $J "$SRC/Ultima 3 - Wizard Spellbook.pdf"  "$OUT/wizard/p"
pdftoppm -r 108 -jpeg -jpegopt quality=85,optimize=y "$SRC/Ultima_3_map.pdf" "$OUT/map/p"
pdftoppm -r 90  $J "$SRC/Ultima_3_cluebook.pdf"            "$OUT/cluebook/p"

# Large-text Book of Play (manual.txt): OCR each half of the two-page spreads.
if command -v tesseract >/dev/null && command -v convert >/dev/null; then
	WORK=$(mktemp -d)
	pdftoppm -r 300 -gray -png "$SRC/Ultima_3_manual.pdf" "$WORK/man"
	for f in "$WORK"/man-*.png; do
		b=${f%.png}
		read -r w h < <(identify -format "%w %h" "$f")
		if [ "$w" -gt "$h" ]; then convert "$f" -crop 50%x100% +repage "${b}_%d.png"; else cp "$f" "${b}_0.png"; fi
		rm "$f"
	done
	for f in "$WORK"/man-*_*.png; do tesseract "$f" "${f%.png}" >/dev/null 2>&1; done
	python3 "$(dirname "$0")/ocr-to-book.py" "$WORK" man "$OUT/manual.txt"
	rm -rf "$WORK"
else
	echo "tesseract/imagemagick not found: skipping the large-text Book of Play" >&2
fi
du -sh "$OUT"
