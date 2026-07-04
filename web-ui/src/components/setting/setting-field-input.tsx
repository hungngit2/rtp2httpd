import type { SettingField } from "../../lib/setting-fields";

interface SettingFieldInputProps {
  field: SettingField;
  label: string;
  value: string;
  onChange: (key: string, value: string) => void;
  disabled?: boolean;
}

export function SettingFieldInput({ field, label, value, onChange, disabled }: SettingFieldInputProps) {
  const inputId = `setting-field-${field.key}`;

  if (field.type === "checkbox") {
    return (
      <label htmlFor={inputId} className="flex items-center gap-2 text-sm">
        <input
          id={inputId}
          type="checkbox"
          checked={value === "true"}
          disabled={disabled}
          onChange={(e) => onChange(field.key, e.target.checked ? "true" : "false")}
          className="h-4 w-4"
        />
        {label}
      </label>
    );
  }

  if (field.type === "select") {
    return (
      <label htmlFor={inputId} className="flex flex-col gap-1 text-sm">
        <span>{label}</span>
        <select
          id={inputId}
          value={value}
          disabled={disabled}
          onChange={(e) => onChange(field.key, e.target.value)}
          className="rounded border bg-background px-2 py-1"
        >
          {field.options?.map((opt) => (
            <option key={opt.value} value={opt.value}>
              {opt.label}
            </option>
          ))}
        </select>
      </label>
    );
  }

  if (field.type === "textarea") {
    return (
      <label htmlFor={inputId} className="flex flex-col gap-1 text-sm">
        <span>{label}</span>
        <textarea
          id={inputId}
          value={value}
          disabled={disabled}
          onChange={(e) => onChange(field.key, e.target.value)}
          rows={4}
          className="rounded border bg-background px-2 py-1 font-mono text-xs"
        />
      </label>
    );
  }

  return (
    <label htmlFor={inputId} className="flex flex-col gap-1 text-sm">
      <span>{label}</span>
      <input
        id={inputId}
        type={field.type === "number" ? "number" : "text"}
        value={value}
        disabled={disabled}
        placeholder={field.placeholder}
        min={field.min}
        max={field.max}
        onChange={(e) => onChange(field.key, e.target.value)}
        className="rounded border bg-background px-2 py-1"
      />
    </label>
  );
}
