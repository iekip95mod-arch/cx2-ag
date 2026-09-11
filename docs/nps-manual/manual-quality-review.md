# Second manual quality review

Reviewed 9 September 2026. This edition supersedes the first reviewed handbook. Earlier editions are retained.

- [Word handbook](/private/tmp/nps-manual-work/quality-pass/nps-beginners-handbook.docx)
- [PDF handbook](/private/tmp/nps-manual-work/quality-pass/nps-beginners-handbook.pdf)
- [Searchable illustrated HTML handbook](/private/tmp/nps-manual-work/quality-pass/nps-beginners-handbook.html)

These finished editions remain in the temporary build folder. Copying them into the manual folder was blocked by the file-protection hook, and the safe-copy tool refused the cross-project source paths. Earlier editions in the manual folder remain unchanged.

The handbook has 81 pages, 78 sections, 177 menu entries and 25 numbered figure placements plus the cover. It uses 22 unique embedded images. The three-way feature table and both comparison sections remain on pages 5 to 8.

## Changes that improve the instructions

| Topic | Improvement |
| --- | --- |
| First lesson | Put the four submissions on separate lines and explain how to enter the exclamation mark. |
| Lesson setup | Reset mode, hint progression and the prefix variable before relevant lessons and practice exercises. |
| Fractions and derivatives | Explain the difference between a fraction template and typed division. State that the current derivative template inserts diff(,x), which remains a function call. |
| History | Give Ctrl+Menu explicitly, explain the two history actions and retain the focus-dependent deletion warning. |
| Restart CAS | Explain that the menu inserts a command. Enter executes it. Selecting the menu item alone does not reset Giac. |
| Actions reference | Replace generic descriptions with the actual action and distinguish immediate actions from inserted commands. Replace an unusable lesson reference with existing section titles. |
| Templates reference | Explain the inserted structure, where to fill it and examples for each mathematical template. Explain all seven unit insertions. |
| Physics reference | Give quantities and expected values for the eight prepared motion problems. Explain signed displacement and velocity despite shorter menu labels. |
| Rounding | State that Round changes the numerical value. Explain why the falling-object example displays 25 m/s after rounding 24.5 m/s to two significant figures. |
| Integer examples | Explain modular exponentiation through a remainder calculation and describe collection positions using ordinary counting. |
| Pictures | Replace a clipped editor test grid with a clearer retained stacked-fraction test. Identify it explicitly as a test document. |
| Standalone reading | Remove a reference to the earlier conversation and explain the menu-path and multiline-example conventions. |

## Evidence checked in this pass

Current Ki V4 menu definitions, template insertion, history handling and action handlers were checked in nps/lua/nps_v4.lua. Relevant Giac command descriptions were checked against the local reference. TI keyboard and stock-model descriptions were checked against the official [TI-Nspire CX II handheld guide](https://education.ti.com/-/media/76D782E2918B468EA5A827E9CEDF097E). The earlier review records the primary sources supporting the three-way comparison.

Eight native host motion requests were exercised against the prepared menu values. Their displayed values were 17 m/s, 5 m/s, 3 m/s^2, 4 s, 44 m, 44 m, 11 m/s and 25 m/s. All reported an unavailable dependency for backend verification. These checks establish the native values and precision behavior observed on the host, not Giac agreement or a handheld result.

The previous pass's 3742 UI smoke checks and 43 native example requests remain prior-pass evidence. They were not rerun in this pass. Their restrictions, including unavailable matrix traces and Giac-only execution, remain documented in [the first accuracy review](manual-review.md).

## Document and navigation checks

- All 78 section headings occur on their expected PDF pages. Every contents page reference matches. No near-empty overflow pages or text outside the checked page bounds were detected.
- Fifteen changed pages were rendered and visually inspected: 4, 9, 11, 15, 16, 24, 25, 26, 34, 45, 53, 55, 56, 57 and 59.
- The Word package contains 26 picture placements with alternative descriptions, 22 embedded image files and 78 section bookmarks. It was exported through LibreOffice for the PDF layout check. Microsoft Word itself was not used for a separate rendering check.
- All 157 internal HTML links resolve to existing targets. All 26 embedded HTML images decode successfully.
- In a real browser, searching for fraction returned two sections. Clearing the search restored 78. Contents links opened the comparison and first lesson. Enlarging the lesson picture opened its image, and Escape closed it.

The review supports the revised instructions and document quality within those checks. It does not establish fresh physical-device behavior. Historical captures and annotated illustrations retain their evidence labels. No calculator code, installation or device state was changed for this review.

The [full revised content](/private/tmp/nps-manual-work/quality-pass/manual-source.json), [replacement records](/private/tmp/nps-manual-work/quality-changes.json) and [artifact checks](/private/tmp/nps-manual-work/quality-pass/verification.json) remain with the finished build.
