# Wiring Chart Preferences & Rules

> Auto-generated preference file for the FPV Rover project.
> Governs how `wiring_chart.xlsx` and the Python generator `make_wiring_xlsx.py` should behave.

---

## 1. File Formats

| Priority | Format | Use Case |
|----------|--------|----------|
| ✅ **Primary** | `wiring_chart.xlsx` | Mixed text/calc — sortable wire table + live totals |
| ✅ **Secondary** | `wiring_chart.md` | Plain-text reference (human-readable, git-friendly) |
| ✅ **Reference**  | `wiring_chart_v2.md` | Alternate markdown layout (also embedded tables) |

---

## 2. Spreadsheet Layout Rules

### 2.1 Title rows must NEVER be part of the data table
- **Title row** (row 1) and **subtitle** (row 2) are placed **above** the Excel Table.
- The Excel Table (`WireTable`) starts at the header row (row 4) and must **not** include title/subtitle in its `ref` range.
- *Reason:* If the title is inside the table range, sort operations can push the title into data rows.

### 2.2 Table header row is always row 4
| Row | Content | Notes |
|-----|---------|-------|
| 1 | Title (merged across all columns) | Bold, large font |
| 2 | Subtitle (merged) | Smaller, italic |
| 3 | Blank separator | Keeps title visually distinct |
| 4 | Column headers | Part of the Excel Table (headerRowCount=1) |
| 5+ | Data rows | All sortable/filterable via the Table |

### 2.3 Freeze panes
- **All_Wires tab:** frozen at `A5` — title, subtitle, blank, and headers stay visible.
- **Summary tab:** frozen at `A5` — title, subtitle, blank, and headers stay visible.

---

## 3. Column Standards

### 3.1 All_Wires table columns (9 columns)

| # | Column Name | Description | Sortable? |
|---|-------------|-------------|-----------|
| 1 | ID | Unique wire ID (P1, M1, S1, E1, etc.) | ✅ |
| 2 | Section | Source section group | ✅ |
| 3 | From Device | Sending/part-of device | ✅ |
| 4 | From Pin | Pin on source device | ✅ |
| 5 | To Device | Receiving device | ✅ |
| 6 | To Pin | Pin on destination | ✅ |
| 7 | Signal / Notes | Human-readable purpose | ✅ |
| 8 | Phase | `existing` / `Phase 1` / `Phase 2` / `—` | ✅ |
| 9 | Level-shifted? | `Yes` / `—` | ✅ |

### 3.2 Summary table columns (2 columns)

| # | Column Name | Description | Formula? |
|---|-------------|-------------|----------|
| 1 | Section | Section name | — |
| 2 | Wires | Count of wires in that section | `=SUM(B5:B<n>)` in total row |

---

## 4. Visual Styling Rules

| Element | Rule |
|---------|------|
| Header fill color | Blue-grey `#1F4E78` (Table headers), Purple `#7030A0` (Summary headers) |
| Total row fill color | Orange `#E26B0A` |
| Header font | Bold, white (`#FFFFFF`) |
| Border | Thin grey borders on all cells (`#BFBFBF`) |
| Table style | `TableStyleMedium2` with row stripes |
| Gridlines | Hidden (clean appearance) |

---

## 5. Section / Wire ID Conventions

| Section | ID Prefix | Wire Count |
|---------|-----------|------------|
| Power_Distribution | P1–P9 | 9 |
| Motor_Drive | M1–M6 | 6 |
| Serial_UART | S1–S4 | 4 |
| HC_SR04_Trigger | T1 | 1 |
| HC_SR04_Echo | E1–E5 | 5 |
| Other_Sensors | O1–O2 | 2 |
| I2C_Bus | I2C-1–I2C-4 | 4 |
| **TOTAL** | | **31** |

---

## 6. Phase Definitions

| Phase | Meaning |
|-------|---------|
| `existing` | Wire is part of the current MVP build |
| `Phase 1` | Planned for first expansion |
| `Phase 2` | Planned for second expansion |
| `—` | Not applicable / N/A |

---

## 7. Regenerating the Xlsx

```bash
python make_wiring_xlsx.py
# → outputs: wiring_chart.xlsx
```

- The script (`make_wiring_xlsx.py`) is the **single source of truth** for the spreadsheet.
- All wire data is hardcoded in the `ALL_WIRES` list — edit there for any additions/changes.
- Always re-run the script after editing the source list.
- **Rule:** After any schema change, re-verify that the title row is still outside the table `ref`.

---

## 8. Markdown Table Rules (for .md files)

- Use pipe `|` and dash `-` syntax (GFM / CommonMark compatible).
- Header separator row: `|---|---|` (no colons = default left alignment).
- For alignment: `:---` = left, `:---:` = center, `---:` = right.
- Always include a blank line before and after tables.
- Escape pipes `|` inside cell content as `\|`.
- Column count must be consistent across all rows (including the separator).

---

## 9. Code Formatting & Documentation Standards

All code written for this project must follow these rules:

- **Industry-standard formatting:** Python code must be formatted with `black` (PEP 8 compliant, 88-char line length). JavaScript/TypeScript with `prettier` or `eslint --fix`. C/C++ with `clang-format`. INI/YAML/JSON with standard 2-space or tab conventions.
- **Documented throughout:** Every module, function, class, and non-trivial block must have a docstring or inline comment explaining its purpose. Module-level docstrings describe the file's overall purpose.
- **No magic numbers:** Constants must be named and defined at module level (e.g., `HEADER_ROW = 4`, not bare `4`).
- **Type hints:** Python functions should include type hints for parameters and return values.
- **Descriptive names:** Variables and functions use descriptive, self-documenting names — no cryptic abbreviations.
