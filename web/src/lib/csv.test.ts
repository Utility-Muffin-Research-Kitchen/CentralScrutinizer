import { describe, expect, it } from "vitest";

import { parseCsv, serializeCsv } from "./csv";

describe("csv", () => {
  it("round-trips awkward cells", () => {
    const rows = [
      ["source", "title"],
      ["primary", 'A "quoted", comma\nand a break'],
      ["primary", ""],
      ["primary", "Pokémon ポケモン"],
    ];

    expect(parseCsv(serializeCsv(rows))).toEqual(rows);
  });

  it("keeps a cell a spreadsheet would run as a formula as text, and round-trips it", () => {
    for (const value of ["=SUM(A1)", "+1", "-Zero-", "@here", "'=already", "'plain", "it's"]) {
      const csv = serializeCsv([["a", value]]);

      expect(parseCsv(csv)).toEqual([["a", value]]);
    }
    expect(serializeCsv([["=SUM(A1)"]])).toContain("'=SUM(A1)");
    expect(serializeCsv([["'plain"]])).not.toContain("''plain");
  });

  it("reads what Excel and other tools write", () => {
    expect(parseCsv("﻿a,b\r\n1,2\r\n")).toEqual([["a", "b"], ["1", "2"]]);
    expect(parseCsv("a,b\n1,2")).toEqual([["a", "b"], ["1", "2"]]);
    expect(parseCsv("a,b\r1,2\r")).toEqual([["a", "b"], ["1", "2"]]);
    expect(parseCsv('a\n""\n')).toEqual([["a"]]);
    expect(parseCsv("a,b\n\n1,\n")).toEqual([["a", "b"], ["1", ""]]);
  });
});
