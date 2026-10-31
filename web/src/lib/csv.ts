/* Spreadsheets run a cell that starts with one of these as a formula, and titles and
   descriptions include scraped text. A leading apostrophe keeps it as text, and parsing
   takes that apostrophe off again so a file round-trips unchanged. */
const FORMULA_PREFIX = /^'*[=+\-@\t\r]/;

function protectCell(value: string): string {
  return FORMULA_PREFIX.test(value) ? `'${value}` : value;
}

function unprotectCell(value: string): string {
  return value.startsWith("'") && FORMULA_PREFIX.test(value) ? value.slice(1) : value;
}

function quoteCell(value: string): string {
  const cell = protectCell(value);

  return /[",\r\n]/.test(cell) ? `"${cell.replace(/"/g, '""')}"` : cell;
}

/* With a byte order mark and CRLF, the form Excel opens as UTF-8. */
export function serializeCsv(rows: string[][]): string {
  return `﻿${rows.map((row) => row.map(quoteCell).join(",")).join("\r\n")}\r\n`;
}

export function parseCsv(text: string): string[][] {
  const source = text.startsWith("﻿") ? text.slice(1) : text;
  const rows: string[][] = [];
  let row: string[] = [];
  let cell = "";
  let quoted = false;
  let wasQuoted = false;

  const endCell = () => {
    row.push(unprotectCell(cell));
    cell = "";
    wasQuoted = false;
  };
  const endRow = () => {
    endCell();
    if (row.length > 1 || row[0] !== "") {
      rows.push(row);
    }
    row = [];
  };

  for (let i = 0; i < source.length; i++) {
    const c = source[i];

    if (quoted) {
      if (c === '"' && source[i + 1] === '"') {
        cell += '"';
        i++;
      } else if (c === '"') {
        quoted = false;
      } else {
        cell += c;
      }
    } else if (c === '"' && cell === "") {
      quoted = true;
      wasQuoted = true;
    } else if (c === ",") {
      endCell();
    } else if (c === "\n") {
      endRow();
    } else if (c === "\r") {
      if (source[i + 1] === "\n") {
        i++;
      }
      endRow();
    } else {
      cell += c;
    }
  }
  if (cell !== "" || row.length > 0 || wasQuoted) {
    endRow();
  }
  return rows;
}
