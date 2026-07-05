# EDGAR fixture manifest

Captured by tools/edgar_capture.sh. One row per recorded file.
Identity used: `Matthew C. Tedder matthewct@gmail.com`

| URL | file | captured (UTC) |
|---|---|---|
| https://www.sec.gov/files/company_tickers.json | examples/fixtures/edgar/company_tickers.json | 2026-07-03 00:14:28Z |
| https://data.sec.gov/submissions/CIK0000320193.json | examples/fixtures/edgar/submissions_CIK0000320193.json | 2026-07-03 00:14:31Z |
| https://data.sec.gov/api/xbrl/companyfacts/CIK0000320193.json | examples/fixtures/edgar/companyfacts_CIK0000320193.json | 2026-07-03 00:14:37Z |
| https://data.sec.gov/submissions/CIK0000019617.json | examples/fixtures/edgar/submissions_CIK0000019617.json | 2026-07-03 00:14:40Z |
| https://data.sec.gov/api/xbrl/companyfacts/CIK0000019617.json | examples/fixtures/edgar/companyfacts_CIK0000019617.json | 2026-07-03 00:14:42Z |
| https://data.sec.gov/submissions/CIK0001334036.json | examples/fixtures/edgar/submissions_CIK0001334036.json | 2026-07-03 00:14:43Z |
| https://data.sec.gov/api/xbrl/companyfacts/CIK0001334036.json | examples/fixtures/edgar/companyfacts_CIK0001334036.json | 2026-07-03 00:14:45Z |
| https://www.sec.gov/Archives/edgar/data/320193/000114036126025622/form4.xml | examples/fixtures/edgar/form4_sample.xml | 2026-07-03 (WP-XML-2; captured via authorized identity) |
| https://www.sec.gov/Archives/edgar/data/1596355/000159635526000003/inftable.xml | examples/fixtures/edgar/f13_infotable_sample.xml | 2026-07-03 (WP-XML-5; 13F-HR information table, CIK 1596355, accession 0001596355-26-000003, 17 holdings; captured via authorized identity) |
| https://www.sec.gov/Archives/edgar/data/2070900/000182912626007147/quantumsphereacq_10ka.htm | examples/fixtures/edgar/tenk_10ka_sample.htm | 2026-07-03 (WP-XML-7; real Form 10-K/A inline-XBRL HTML, CIK 2070900 QuantumSphere Acquisition Corp, accession 0001829126-26-007147, ~52 KB; captured via authorized identity) |
| https://www.sec.gov/Archives/edgar/data/1596355/000159635526000002/inftable.xml | examples/fixtures/edgar/f13_infotable_2026q1_sample.xml | 2026-07-03 (WP-OWN-3; 13F-HR info table, CIK 1596355, accession 0001596355-26-000002, period 2026-03-31 filed 2026-04-08, 19 holdings — the PRIOR quarter to f13_infotable_sample.xml (period 2026-06-30); captured via authorized identity) |
| https://www.sec.gov/Archives/edgar/data/2114878/000121390026024764/primary_doc.xml | examples/fixtures/edgar/sc13g_trinity_novus_sample.xml | 2026-07-04 (WP-OWN-4; structured-era SCHEDULE 13G primary_doc.xml, subject TRINITY BIOTECH PLC issuerCik 0000888721, reporting person Novus Diagnostics Ltd. CIK 2114878, accession 0001213900-26-024764, filed 2026-03-06, classPercent 5.99, unamended — the PASSIVE holder; ns http://www.sec.gov/edgar/schedule13g; captured via authorized identity) |
| https://www.sec.gov/Archives/edgar/data/1224962/000119312526204043/primary_doc.xml | examples/fixtures/edgar/sc13d_trinity_perceptive_sample.xml | 2026-07-04 (WP-OWN-4; structured-era SCHEDULE 13D/A primary_doc.xml, same subject TRINITY BIOTECH PLC issuerCIK 0000888721, reporting person Perceptive Advisors LLC CIK 1224962, accession 0001193125-26-204043, filed 2026-05-04, amendment 8, percentOfClass 9.9 — the ACTIVIST on the same issuer; ns http://www.sec.gov/edgar/schedule13D — note capital-D ns + issuerCIK/dateOfEvent/reportingPersons schema differences from 13g; captured via authorized identity) |
| https://www.sec.gov/Archives/edgar/data/1334036/000133403626000006/crox-20251231.htm | examples/fixtures/edgar/tenk_crox_2025_sample.htm | 2026-07-04 (WP-MDA-1; real Form 10-K inline-XBRL HTML, Crocs Inc. CIK 1334036, accession 0001334036-26-000006, FY2025 period 2025-12-31 filed 2026-02-12, ~2.2 MB; has real ITEM 1A/7/7A/8 section headers; the LATER of two consecutive years for MD&A extraction + YoY diff; captured via authorized identity) |
| https://www.sec.gov/Archives/edgar/data/1334036/000133403625000009/crox-20241231.htm | examples/fixtures/edgar/tenk_crox_2024_sample.htm | 2026-07-04 (WP-MDA-1; real Form 10-K inline-XBRL HTML, Crocs Inc. CIK 1334036, accession 0001334036-25-000009, FY2024 period 2024-12-31 filed 2025-02-13, ~2.3 MB; the PRIOR consecutive year to tenk_crox_2025_sample.htm; captured via authorized identity) |
