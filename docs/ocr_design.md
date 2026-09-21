# OCR — reading text out of an image

Status: **SHIPPED** as `stdlib/ocr.bas` (2026-09-21), over the tesseract CLI
per §4. `read`, `grid`, orientation, script and skew all exist; §3's `mode:` for
the image-organising job does not, and §8's remaining questions are still open. The measurements in §1 were
taken on 2026-09-20 with Tesseract 5.5.0 and are reproducible with the probe in
§9.

---

## 1. The measurement that decides the design

A 760×300 image of an ordinary branch activity report — three detail rows, a
header, a total — through `tesseract report.png -`:

```
BRANCH ACTIVITY REPORT

ACCT NAME
100294781 SMITH, J
100294782 OKONKWO, A
100294783 FERNANDEZ, L

BRANCH TOTAL

16-JUL-2026

POSTED

16-JUL-26
16-JUL-26
16-JUL-26

AMOUNT

1,250.00
...
```

**Nothing is missing and the document is destroyed.** Every word was recognised,
at 90–96% confidence. But `100294781`, `SMITH, J`, `16-JUL-26` and `1,250.00`
are one *row* of the report, and in this output they are twelve lines apart in
three separate blocks. The association between an account and its amount is
gone, and what remains still reads like a plausible document.

This is the failure mode this project keeps finding, in a new place: **not an
error, an ordinary-looking result that is wrong.** A program that ingests
invoices this way gets amounts that belong to nobody, and nothing raises.

> My first reading of this output was that the right-hand side had been
> *dropped*. It had not — it was reordered. Recorded because the wrong diagnosis
> was the more alarming one and the true one is worse: dropped data is missing,
> reordered data is misattributed.

### 1.1 And the fix is already in the box

Tesseract's TSV output gives every word a bounding box and a confidence. Placing
each word at `round(left / median_char_width)` on a line keyed by its `top`
reconstructs the page:

```
BRANCH ACTIVITY  REPORT        16-JUL-2026
ACCT        NAME              POSTED       AMOUNT
100294781   SMITH, J          16-JUL-26    1,250.00
100294782   OKONKWO, A        16-JUL-26     -487.31
100294783   FERNANDEZ, L      16-JUL-26    9,004.55
BRANCH TOTAL                               9,767.24
```

That is a **print-image report**, which is exactly what `ari` parses and what
`ari_discover` infers a specification from. So OCR is not a new pipeline. It is
the missing front door to one that already exists, has a limitations register,
and has been measured against a 24-source corpus.

### 1.2 Grouping by the engine's own lines, and where it stops working

The reconstruction above bins words by raw `top`, which is my arithmetic rather
than the engine's. Tesseract's TSV already carries `block_num`, `par_num` and
`line_num` — it has done baseline fitting — and grouping by those, then merging
lines whose vertical spans overlap, both fixes §1's split blocks and tolerates
skew. Measured against rotation of the same page:

| skew | rows | verdict |
|---|---|---|
| 0° | 6 | intact |
| 0.5° | 6 | intact |
| 1° | 6 | intact |
| 2° | 6 | intact |
| 3° | 6 | intact |
| **5°** | 10 | **misattributed** |
| 8° | 10 | collapsed |

> A first attempt estimated the skew angle from word centroids and corrected for
> it. It did not work — at 2° it reported a slope of −0.003 where the true value
> is 0.035 — and the failure is what found the right answer: the engine has
> already done this, better, and the TSV hands it over.

### 1.3 The result that sets the hardest rule

At 5° the grid does not break. It produces this:

```
100294781  SMITH,  J          16-JUL-26     -487.31
```

Account `100294781` is SMITH — correct. `-487.31` is **account 100294782's
amount**. The row is well-formed, the account is real, the name matches the
account, the date is right, and the money belongs to somebody else.

Mean word confidence at 5° is **90.9** — the same as clean. Confidence cannot
see this, because every individual word *was* read correctly. What went wrong is
the association between them, and no per-word measure has an opinion about that.

**So skew must be measured and refused, not tolerated.** A library that silently
returns the row above has produced the worst output in this document: not an
error, not a gap, but one customer's money under another customer's account, in
a form that will pass every downstream validation a reader is likely to write.

Compare speckle, which also breaks the grid but drops mean confidence to **33.4**
— that failure announces itself. Skew does not, which is why it gets a rule and
speckle does not need one.

### 1.4 Measured against real photographs, and it moved the design

Seven hand-held phone photographs of real documents (Motorola, 4080x3060), in
`examples/scans/` and never committed. They answered the question §8.4 asked and
the answer was not the one the synthetic work pointed at.

**Real skew is not the problem.** Measured from the engine's own line boxes,
every page sits between **-1.08 and +1.85 degrees** — comfortably inside where
reconstruction works, and nowhere near the 3-5 degrees where §1.3's
misattribution begins. A person holding a phone over a page is straighter than
the synthetic experiment assumed.

**Orientation is the problem.** Five of seven were rotated by a quarter turn or
more, and a 90-degree error is not read as a rotation. It is read as a page with
**87 degrees of skew and 30% confidence**:

| | before correction | after |
|---|---|---|
| words | 365 | 366 |
| mean confidence | **29.9** | **78.6** |
| measured "skew" | **86.67°** | **0.02°** |

**EXIF is necessary and not sufficient.** Two photographs carried an orientation
tag and needed it applied. Two more declared `orientation: 1` — upright, nothing
to do — and were still a quarter turn out, because *the page was sideways inside
a correctly-oriented photograph*. No metadata can know that.

**Tesseract's OSD is the remedy, and it is weak.** It placed four of seven,
returned **no answer at all** for one (which a brute-force search fixed:
36.2% at 0°, **80.7% at 270°**), and reports orientation confidence between
**0.60 and 26.4** — a range wide enough that the number has to be carried to the
caller rather than acted on silently.

### 1.5 The failure that says nothing: the wrong language

Three of the seven are **Korean** documents. Only `eng.traineddata` is
installed.

Tesseract did not report a missing language. It did not return an empty page. It
returned **39 to 82 words at 30-45% confidence** — Hangul read as English words,
a plausible-looking result of the right general shape.

That is §5's "language data is a new shape for this tree" stated as a
measurement rather than a concern, and it is worse than expected: the cost of a
missing language is not a refusal, it is *quiet nonsense*. A pipeline that
checked only "did OCR return text" passes.

**The signal exists and the library must carry it.** OSD reports the SCRIPT it
saw, and on exactly those three it answered non-Latin (`Japanese`, the usual CJK
confusion) at confidence **0.60-0.95**, against `Latin` at **9.7-10.4** for the
pages that read correctly. So a page can say *"I look like a script you do not
have data for"*, and the refusal in §6 is writable.

### 1.5b What the missing language cost, measured

`tesseract-ocr-kor` installed, the same three pages re-read. The two genuinely
Korean ones:

| page | lang | words | mean conf | **conf ≥ 80** |
|---|---|---|---|---|
| A | `eng` | 39 | 41.6 | **6** |
| A | `kor+eng` | 62 | 81.3 | **47** |
| B | `eng` | 75 | 36.1 | **10** |
| B | `kor+eng` | 143 | 84.4 | **114** |

**Eleven times the usable words** on one page, nearly eight on the other. This
is the cost of §1.5's silent failure, and it is not a degradation — it is the
difference between a page that was read and one that was not.

**The control is what makes that trustworthy.** A Latin page (the same set's
English-language document, correctly oriented) must not move when Korean data is
added, and does not:

```
eng       34 words   80.7   26 conf>=80
kor+eng   34 words   80.7   26 conf>=80     identical; 24 of 24 words shared, none lost
```

**`kor` ALONE IS WORSE THAN NOTHING on the wrong page** — on that same Latin
document it produced *more* words (49 against 34) at *half* the confidence (34.7
against 80.7), inventing structure in text it has no business reading. So the
rule is not "detect the script and switch to it": it is that languages
**combine**, `kor+eng` beating either alone on a mixed page and costing a pure
one nothing. These documents mix Hangul with Latin names, dates and record
numbers on the same line, which is ordinary rather than exotic.

**AND MEAN CONFIDENCE IS THE WRONG MEASURE.** On a third page, adding `kor`
*lowered* mean confidence by 2.5 while *raising* high-confidence words by 11 —
the two numbers point in opposite directions on the same run. `conf >= 80` is
the discriminating one, and the reason is the whole subject of this document:
the failure being looked for is **plausible** output, which sits in the middle
of the confidence range exactly where an average conceals it.

### 1.6 What did survive

On the four pages that read, the columnar structure is real and recoverable:
**31 to 48 x-positions shared by three or more words**, which is the signature of
a column rather than prose — prose starts each word wherever the last one ended.
The reconstruction approach is sound on paper; it is everything *before* it that
the synthetic experiment had not modelled.

---

## 2. What follows from that

**R1. `ocr.read` does not return a string.** It returns a page of words, each
with its text, its box and its confidence. A `text` accessor exists and its
documentation says, with §1's example, that it is lossy for anything columnar.
Returning a string as the primary answer would make the defect above the
default behaviour and hide the evidence needed to detect it.

**R2. Confidence travels and is never silently discarded.** A word is reported
with the number the engine gave it. There is no default threshold that drops
low-confidence words, because a dropped word is indistinguishable from a word
that was not there — the distinction `finio`'s Axiom 7 already draws between
*unknown* and *invalid*. A caller may filter; the library may not.

**R3. A page can become a grid, and the grid is `ari`'s.** `ocr.grid(page)`
produces the text layout above. The seam is deliberate: everything after it is
existing, tested machinery, and OCR's job ends at producing a faithful grid.

**R0. Orientation is settled before anything else, and how is reported.** §1.4
measured this as the dominant real-world failure, ahead of every effect the
synthetic work found: EXIF applied where present, OSD consulted always, and the
resulting rotation plus its confidence carried on the page. A page whose
orientation was *guessed* at confidence 0.6 is not the same evidence as one that
was certain, and only the caller can decide what to do about it.

**R0b. The script is reported against the languages available.** §1.5: reading
Korean with English data yields words, not an error. A page that looks like a
script we have no data for must say so — the one signal that distinguishes
quiet nonsense from a genuine read.

**R4. Skew is measured, and past a threshold `grid` refuses.** §1.3 is the
reason: beyond about 3° the reconstruction misattributes rather than failing, and
confidence stays at 90. A caller may ask for the words anyway — `read` is not
affected, the words are individually fine — but the *grid* is the thing that
claims a row belongs together, and it may not make that claim when the evidence
for it has gone. A hand-held photograph is routinely past 3°.

**R5. Nothing is corrected.** No spell-check, no "0 looks like O in a numeric
column", no re-reading a field because it failed a checksum. Every one of those
turns a visible misread into an invisible one, and this library's whole value is
that a misread is *detectable*. Correction belongs above, where a caller knows
what the field means.

---

## 3. The two jobs are not the same job

**Document ingestion** wants positions, confidence and accuracy, on a few
hundred pages. Cost per page is irrelevant; a wrong amount is not.

**Organising a pile of images** wants just enough text to say *this is an
invoice, this is a receipt, this is a photo of a whiteboard*, over thousands of
files. Accuracy tolerance is high, cost per image matters, and positions mostly
do not.

These pull in opposite directions and the design serves the first. The second is
the first with `ocr.text` and a cheaper page-segmentation mode, and is worth an
explicit `mode:` rather than a separate library — but **the default is the
careful one**, because a caller who wanted speed will say so and a caller who
did not should not silently get a worse read.

---

## 4. The open decision: CLI or native module

This is the one I would not settle alone, because it reaches packaging.

| | `process.run` over the CLI | native `#if HAVE_TESSERACT` |
|---|---|---|
| build dependency | **none** | libtesseract + leptonica |
| in the lean tarball | **yes** | no |
| per call | fork + exec + temp file | in-process |
| failure surface | argv, exit codes, TSV parsing | linker, ABI |
| available today | yes, 5.5.0 is installed | needs `libtesseract-dev` |

The lean release tarball carries no optional modules, so a native OCR module is
**absent from the download a reader gets**. The CLI path works there on any
machine with `tesseract` installed, which is an apt-get away and needs no
rebuild.

Against that: every other module in this tree is native, `process.run` per page
is slow for the image-organising job, and shelling out is the thing
`docs/mail_design.md` explicitly moved *away* from when `sendmail` became
`smtp`.

**My recommendation: start with the CLI, behind an interface that does not
promise which it is.** The evidence is that the CLI gives us everything the
design needs — §1's grid was reconstructed from `tesseract`'s own TSV — and it
costs nothing to build. If per-page cost becomes the complaint, the native
module replaces the implementation without moving the API, because the API is
words-with-boxes either way.

---

## 5. Language data is a new shape for this tree

Every optional module so far needs a **shared library** and nothing else.
Tesseract needs `*.traineddata` at run time — `eng.traineddata` is ~15 MB and a
machine may have one language or thirty.

So this is the `GBASIC_DEFAULT_STDLIB` problem again: a path resolved at run
time, which must fail by **naming what is missing**, not by returning an empty
page. An absent language is a refusal that says which language and where it
looked. A page that OCRs to nothing because the data file was not found is the
worst outcome available, and it is the default one if nobody designs against it.

---

## 6. What must be refused

- **A page with no words is not an error**, it is a page with no words — a blank
  scan is ordinary. `ok` is about whether the *engine ran*.
- **An unknown language is refused by name**, listing what is installed.
- **An unreadable or non-image file is refused**, not read as an empty page.
- **`grid` on a page whose words carry no boxes is refused**, because the grid's
  entire content is positional and one built without boxes would be a plausible
  document in source order — §1's defect, produced by our own library.

---

## 7. What this is not

Not layout analysis, not table extraction, not a PDF reader (`gpdf` exists and a
born-digital PDF should never go near OCR — it has real text already), not
handwriting, not document classification. Each is a reasonable thing to want and
each is a different design.

---

## 8. Open questions

1. CLI or native (§4) — the packaging decision.
2. Does `ocr.grid` belong here or in `ari`? It is a *layout* operation and `ari`
   already owns a grid type.
3. Multi-page input: TIFF and PDF are page *sequences*. Does `read` take a page
   number, or return pages?
4. ~~**What is the real threshold?**~~ **ANSWERED 2026-09-20 by §1.4, and the
   question was wrong.** Real hand-held skew is under 2 degrees; the threshold
   between 3 and 5 is real and almost never reached. What is reached, on five
   of seven photographs, is a quarter turn. The open question that replaces it:
   **how should a page whose orientation was guessed at confidence 0.6 be
   reported?** Brute-forcing all four rotations and keeping the best-scoring one
   is measurable and cheap (four OCR passes) but it optimises for *confident
   nonsense* exactly when the language data is wrong, which §1.5 shows is a real
   state and not a hypothetical.
5. **Which languages ship, and who decides?** §1.5b settles the mechanism and
   leaves the packaging: languages **combine** (`kor+eng`), a wrong language
   alone is worse than none, and the gain where it is needed is 8-11x in usable
   words. What remains is distribution — `kor` is one apt package and there are
   over a hundred; a lean release carrying none, a default carrying one, and a
   document in a script nobody installed are three situations a caller must be
   able to tell apart, and only the third is a refusal.
6. ~~**How is skew measured?**~~ §1.2 puts the
   break between 3° and 5° on one synthetic page in one font at one size. That
   number is not trustworthy enough to hard-code: it will move with font size,
   line spacing and column gap, and it was measured on rendered text rather than
   on paper. What is needed is a corpus of **real photographs and scans** —
   where the skew is genuine, the lighting is uneven and the paper is not flat —
   to find where the misattribution in §1.3 actually begins, and whether a usable
   skew estimate can be had from the engine's own line boxes rather than from a
   preprocessing step that would need an image library.
5. Is a deskew *preprocessing* step needed after all? It would need leptonica or
   equivalent, which changes the §4 answer.

---

## 9. The probe

`/tmp` scratch, reproducible: render §1's report with PIL, run
`tesseract report.png out tsv`, and place each word at
`round(left / median_char_width)` on a line keyed by `top`. The plain-text
output and the reconstructed grid are both in §1.
