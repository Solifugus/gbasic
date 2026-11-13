# Reading a PDF — `stdlib/pdf.bas`

Status: **Shipped** as `stdlib/pdf.bas` (2026-11-09), book gap G54, gated by
`tests/run_pdf.sh`. Matthew's rulings, 2026-11-09:
OCR is an **option**, never automatic, and **every page says how it was read**.

---

## 1. What is wanted, and from where

Chapter 18 of the gBASIC books (volume 3) reads supplier invoices that arrive as
PDFs. It drives poppler with `process.run`: `pdfinfo` for the page count and to
find a damaged file, `pdftotext -layout` per page, and `pdfimages` for a
scanned page's picture. It reads that picture with `ocr`. The gap ledger (G54)
asks for:

- a page's text laid out as printed;
- its words with positions, in the shape `ocr.read_page` gives them, so one
  `ocr.grid` serves both;
- the page count;
- the pictures on a page.

Or, as `ocr` is over Tesseract, "a library over poppler that says so."

## 2. The choice: a library over poppler's programs

`pdf` is a library over `pdftotext`, `pdfinfo`, `pdftohtml` and `pdfimages`,
the poppler-utils programs, run through `process.run`. `ocr` is built the same
way, over the `tesseract` program.

Why not inside the interpreter:

- **Licence.** poppler is GPL, and gBASIC is Apache-2.0, so linking it is out.
  Running its programs as separate processes does not link it. MuPDF is AGPL,
  the same answer. qpdf is Apache-2.0 but extracts no text.
- **Size.** Our own reader would need to handle content streams, every font
  encoding, ToUnicode maps, CID fonts and damaged cross-reference tables.
  Measured on the way to Chapter 18: poppler rebuilds a PDF that has lost only
  its trailer and reads all of it. That is years of work we would be
  repeating.
- **What it costs.** A dependency the program must find. `pdf.available()`
  answers whether it is there, as `ocr.available()` does, and every verb
  refuses by name without it.

An in-interpreter reader stays possible later. This library's values would not
change, only what produces them.

## 3. A page is read one of four ways, and says which

| `how` | the page holds | its words come from |
|---|---|---|
| `text` | text, and no picture the size of the page | the PDF's own text (`pdftotext`) |
| `scan` | one picture the size of the page | nothing unless `ocr: true` is given |
| `ocr` | a scan, read because `ocr: true` was given | `ocr.read_page` over the picture as stored |
| `blank` | neither | none |

- **A picture the size of the page** covers at least nine tenths of the page
  each way, as placed. That is Chapter 18's measure.
- **A searchable scan** is a page-sized picture with text over it, often
  invisible: someone's OCR, or a stamp. Its text's origin is unknown and it
  carries no confidence.
  - Without `ocr`, the page is `scan`. Its `words` are that text, and
    `text_layer: true` says where they came from.
  - With `ocr: true`, the picture is read, and `text_layer_set_aside: true`
    says the layer was not used. That is Chapter 18's rule.
- **Small pictures on a text page** (a logo, a signature, a stamp) are listed
  in `images` with their positions, and never read. A program that wants a
  stamp read takes that one picture out (`pdf.image`) and gives it to `ocr`.
- **Anything else is refused by name**, never guessed. That means several
  page-sized pictures, or small pictures with no text.

**Why OCR is never automatic.** A PDF's own text is what its producer wrote.
OCR is an engine's guess with a confidence. A program that cannot tell which
it got will take a 70%-confident total as fact. OCR is also seconds a page
where text is milliseconds, and a loop over a thousand files should not find
that out by accident.

## 4. The values

```basic
info = pdf.info(path)
' { ok, why, pages, title, producer, creator, encrypted,
'   sizes: [ { width, height, rotation } ] }     ' points, one per page

p = pdf.read_page(path, n, options)               ' n from 1
' { ok, why, n, how, width, height, rotation, units,
'   scan_rotation, scan_rotation_confidence,
'   words: [ { text, left, top, width, height, block, paragraph, line,
'              confidence } ],
'   images: [ { left, top, width, height, pixels_wide, pixels_high } ],
'   text_layer, text_layer_set_aside, skew }

r = pdf.read(path, options)                       ' { ok, why, pages: [ page ] }
t = pdf.text(path, n)                             ' laid out as printed
f = pdf.image(path, n, index, dir)                ' that picture, as stored, from 0
```

- **Units.** Words and pictures are in points, from the top-left corner.
  `ocr.grid` measures a line by its own characters, so points and pixels lay
  out alike.
- **`confidence`** is `unknown` for a PDF's own text. It is not 100: nothing
  measured it. On an `ocr` page it is the engine's.
- **`skew`** is `unknown` for a text page and the engine's for an `ocr` page,
  so `ocr.grid`'s refusal of a crooked page applies unchanged. That refusal
  names the page's `units` (`"points"` here, `"pixels"` from `ocr`).
- **A turned scan** (book gap G66, 2026-11-10). `ocr` reports the turn it
  found and answers its words in the page as read. `scan_rotation` and
  `scan_rotation_confidence` carry that onto the pdf page; `rotation` stays
  the PDF's own `/Rotate`, a different thing. The picture's place is turned
  the same way inside the page, the words are scaled from the turned
  picture's pixels to its turned place, the pictures are turned with it, and
  `width` and `height` become the page as read. A turn tesseract could not
  tell (a page with few characters: the two-line fixture, at 100 and at
  300 dpi) is `unknown` and nothing is turned.
- **Failure is a value, not a raise.** `ok: false` comes with poppler's own
  words, such as *"Couldn't find trailer dictionary"*. A batch over a
  supplier's mailbox meets damaged files as an ordinary case. A missing
  poppler, a bad option and a page number out of range raise, as wrong
  programs.
- **Options are a closed set:** `ocr` (default false) and `languages` (passed
  to `ocr`). Others are refused by name.

## 5. Tests (`tests/run_pdf.sh`)

Every fixture is made per run by `gpdf`, gBASIC's writer, with known text at
known places, and a scan of it rendered by `pdftoppm`:

- a text page;
- a scan;
- a searchable scan;
- the fuller page at 300 dpi turned 0, 90, 180 and 270 degrees (by PIL), each
  placed with a different margin on every side, where each word and picture
  should land worked out by hand from those margins (skips without PIL);
- a text page with a logo;
- two page-sized pictures;
- a blank page;
- a file cut short.

The oracle is what `gpdf` was told to write. It is never poppler's own answer.
That is the rule `ari` learned: measure with the tool, and decide from outside
it.

- **The OCR tier** skips without `tesseract`, saying so.
- **The whole suite** skips without poppler-utils, saying nothing was read.

## 6. Not decided here

- Encrypted PDFs. `pdfinfo` reports them; a password option is left until
  someone has one.
- Forms and annotations.
- A page rotated in its own dictionary (`/Rotate`). `sizes` reports the
  rotation, and whether words come back in the rotated frame is to be measured
  before it is promised.

## 7. How each part is got

- **The page count, sizes and rotations** come from `pdfinfo`, once for the
  document and once with `-f 1 -l N` for each page's own size.
- **Words** come from `pdftotext -bbox-layout`, which writes XHTML: page, flow,
  block, line, word. Each word has its box in points. A flow and a block
  together become `ocr`'s `block`, and the line within it `ocr`'s `line`, so
  `ocr.grid` lays a text page out with no change.
- **Pictures** come from `pdftohtml -xml -zoom 1`, which places each one in
  points. `pdfimages -list` gives pixel sizes but no position. The file
  `pdftohtml` writes is read for its PNG header's pixel size, then removed.
- **A scan's picture** for OCR is taken out by `pdfimages -png`. The words OCR
  finds are in its pixels, turned upright, and are scaled to the page's points
  by the picture's placed size, turned the same way, over its turned pixel
  size. Not measured: a picture the PDF itself draws turned (a rotating
  matrix), whose stored pixels are on their side of how it shows.
- **`pdf.image`** uses `pdfimages -all`, which keeps a JPEG a JPEG. Its order is
  the PDF's object order, which may differ from `images`', and the reference
  says so.

**The paths not measured** are named by the suite rather than claimed:
- an encrypted file;
- a page with `/Rotate`;
- a CID font with no ToUnicode map, where poppler's own text is wrong and this
  library passes it on.
