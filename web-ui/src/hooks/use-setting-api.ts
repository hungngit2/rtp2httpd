import { useCallback } from "react";
import { buildStatusPath } from "../lib/url";

/**
 * buildStatusPath is page-relative (derived from window.location.pathname),
 * not status-specific despite its name — it works unchanged from /setting.
 */
export function useSettingApi() {
  const getConfig = useCallback(async (): Promise<Record<string, unknown>> => {
    const response = await fetch(buildStatusPath("/api/get-config"));
    if (!response.ok) {
      throw new Error(`Request failed with status ${response.status}`);
    }
    return response.json();
  }, []);

  const saveConfig = useCallback(async (values: Record<string, string>): Promise<void> => {
    const response = await fetch(buildStatusPath("/api/save-config"), {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams(values).toString(),
    });
    const data = await response.json().catch(() => undefined);
    if (!response.ok || data?.success === false) {
      throw new Error(data?.error ?? `Request failed with status ${response.status}`);
    }
  }, []);

  return { getConfig, saveConfig };
}
