"use client";

import { createContext, useContext, useEffect, useState, type ReactNode } from "react";
import zhCN from "../i18n/zh_CN.json";

export type Language = "en" | "zh_CN";
type Dictionary = Record<string, string>;

const dictionaries: Record<Language, Dictionary> = { en: {}, zh_CN: zhCN };
const I18nContext = createContext<{ language: Language; t: (key: string) => string }>({
  language: "en",
  t: (key) => key,
});

export function normalizeLanguage(language: string | undefined): Language {
  const normalized = language?.trim().toLowerCase();
  return normalized === "zh" || normalized === "zh-cn" || normalized === "zh_cn" ? "zh_CN" : "en";
}

export function I18nProvider({ children }: { children: ReactNode }) {
  const [language, setLanguage] = useState<Language>("en");
  useEffect(() => {
    let cancelled = false;
    const loadLanguage = () => fetch("/api/session")
      .then((response) => response.ok ? response.json() : null)
      .then((session: { language?: string } | null) => {
        if (!cancelled) setLanguage(normalizeLanguage(session?.language));
      })
      .catch(() => { if (!cancelled) setLanguage("en"); });
    loadLanguage();
    return () => { cancelled = true; };
  }, []);
  const dictionary = dictionaries[language];
  useEffect(() => {
    document.documentElement.lang = language === "zh_CN" ? "zh-CN" : "en";
  }, [language]);
  return <I18nContext.Provider value={{ language, t: (key) => dictionary[key] ?? key }}>{children}</I18nContext.Provider>;
}

export function useT() {
  return useContext(I18nContext).t;
}

export function tFormat(
  t: (key: string) => string,
  key: string,
  params: Record<string, string | number>,
): string {
  let result = t(key);
  for (const [name, value] of Object.entries(params)) {
    result = result.split(`{${name}}`).join(String(value));
  }
  return result;
}
