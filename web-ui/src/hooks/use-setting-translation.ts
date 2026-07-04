import { useCallback } from "react";
import type { TranslationKey } from "../i18n/setting";
import { translate } from "../i18n/setting";
import type { Locale } from "../lib/locale";

export function useSettingTranslation(locale: Locale) {
  return useCallback((key: TranslationKey) => translate(locale, key), [locale]);
}
