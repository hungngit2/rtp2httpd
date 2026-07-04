import { SETTING_FIELDS, type SettingField } from "../../lib/setting-fields";
import { SettingFieldInput } from "./setting-field-input";

const TABS: Array<{ id: SettingField["tab"]; labelKey: string }> = [
  { id: "basic", labelKey: "tabBasic" },
  { id: "network", labelKey: "tabNetwork" },
  { id: "player", labelKey: "tabPlayer" },
  { id: "advanced", labelKey: "tabAdvanced" },
];

interface SettingTabsProps {
  activeTab: SettingField["tab"];
  onTabChange: (tab: SettingField["tab"]) => void;
  values: Record<string, string>;
  onFieldChange: (key: string, value: string) => void;
  translate: (key: string) => string;
  disabled?: boolean;
}

export function SettingTabs({ activeTab, onTabChange, values, onFieldChange, translate, disabled }: SettingTabsProps) {
  const fieldsForTab = SETTING_FIELDS.filter((f) => f.tab === activeTab);

  return (
    <div className="flex flex-col gap-4">
      <div className="flex gap-2 border-b">
        {TABS.map((tab) => (
          <button
            key={tab.id}
            type="button"
            onClick={() => onTabChange(tab.id)}
            className={`px-3 py-2 text-sm ${activeTab === tab.id ? "border-b-2 border-primary font-medium" : "text-muted-foreground"}`}
          >
            {translate(tab.labelKey)}
          </button>
        ))}
      </div>
      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2">
        {fieldsForTab.map((field) => {
          const isVisible = !field.dependsOn || (values[field.dependsOn.key] ?? "") === field.dependsOn.equals;
          if (!isVisible) return null;
          return (
            <SettingFieldInput
              key={field.key}
              field={field}
              label={translate(field.labelKey)}
              value={values[field.key] ?? ""}
              onChange={onFieldChange}
              disabled={disabled}
            />
          );
        })}
      </div>
    </div>
  );
}
