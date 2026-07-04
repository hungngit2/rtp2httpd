import { StrictMode, useCallback, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import { SettingTabs } from "../components/setting/setting-tabs";
import { useLocale } from "../hooks/use-locale";
import { useSettingApi } from "../hooks/use-setting-api";
import { useSettingTranslation } from "../hooks/use-setting-translation";
import { useTheme } from "../hooks/use-theme";
import type { SettingField } from "../lib/setting-fields";

function valuesToStrings(raw: Record<string, unknown>): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [key, value] of Object.entries(raw)) {
    if (Array.isArray(value)) {
      out[key] = value.join("\n");
    } else if (typeof value === "boolean") {
      out[key] = value ? "true" : "false";
    } else {
      out[key] = String(value ?? "");
    }
  }
  return out;
}

function SettingPage() {
  const { locale } = useLocale("setting-locale");
  const t = useSettingTranslation(locale);
  useTheme("setting-theme");
  const { getConfig, saveConfig } = useSettingApi();

  const [activeTab, setActiveTab] = useState<SettingField["tab"]>("basic");
  const [values, setValues] = useState<Record<string, string>>({});
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState<{ kind: "success" | "error"; text: string } | null>(null);

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const raw = await getConfig();
      setValues(valuesToStrings(raw));
    } catch (err) {
      setMessage({ kind: "error", text: err instanceof Error ? err.message : String(err) });
    } finally {
      setLoading(false);
    }
  }, [getConfig]);

  useEffect(() => {
    load();
  }, [load]);

  const handleFieldChange = useCallback((key: string, value: string) => {
    setValues((prev) => ({ ...prev, [key]: value }));
  }, []);

  const handleSave = useCallback(async () => {
    setSaving(true);
    setMessage(null);
    try {
      await saveConfig(values);
      setMessage({ kind: "success", text: t("saveSuccess") });
      await load();
    } catch (err) {
      setMessage({ kind: "error", text: err instanceof Error ? err.message : t("saveError") });
    } finally {
      setSaving(false);
    }
  }, [saveConfig, values, t, load]);

  if (loading) {
    return <div className="p-6 text-sm text-muted-foreground">…</div>;
  }

  return (
    <div className="mx-auto flex max-w-3xl flex-col gap-6 p-6">
      <h1 className="text-xl font-semibold">{t("title")}</h1>

      <SettingTabs
        activeTab={activeTab}
        onTabChange={setActiveTab}
        values={values}
        onFieldChange={handleFieldChange}
        translate={t}
        disabled={saving}
      />

      <div className="flex items-center gap-3">
        <button
          type="button"
          onClick={handleSave}
          disabled={saving}
          className="rounded bg-primary px-4 py-2 text-sm text-primary-foreground disabled:opacity-50"
        >
          {saving ? t("saving") : t("save")}
        </button>
        {/* status-page-path/player-page-path come straight from get-config; see task-6-brief.md
         * Step 5 note: if the operator customizes these paths, a stale current-URL rewrite would
         * be wrong, so we link to the configured paths directly instead. */}
        <a href={values["status-page-path"] || "/status"} className="text-sm underline">
          {t("openStatusPage")}
        </a>
        <a href={values["player-page-path"] || "/player"} className="text-sm underline">
          {t("openPlayerPage")}
        </a>
        {message && (
          <span className={message.kind === "success" ? "text-sm text-green-600" : "text-sm text-red-600"}>
            {message.text}
          </span>
        )}
      </div>
    </div>
  );
}

createRoot(document.getElementById("root") as HTMLElement).render(
  <StrictMode>
    <SettingPage />
  </StrictMode>,
);
