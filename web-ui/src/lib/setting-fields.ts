export type SettingFieldType = "text" | "number" | "select" | "checkbox" | "textarea";

export interface SettingFieldOption {
  value: string;
  label: string;
}

export interface SettingField {
  key: string;
  tab: "basic" | "network" | "player" | "advanced";
  type: SettingFieldType;
  labelKey: string;
  helpKey?: string;
  placeholder?: string;
  options?: SettingFieldOption[];
  min?: number;
  max?: number;
  dependsOn?: { key: string; equals: string };
}

export const SETTING_FIELDS: SettingField[] = [
  { key: "listen", tab: "basic", type: "textarea", labelKey: "listenAddresses", helpKey: "listenAddressesHelp" },
  {
    key: "verbosity",
    tab: "basic",
    type: "select",
    labelKey: "loggingLevel",
    options: [
      { value: "0", label: "Fatal" },
      { value: "1", label: "Error" },
      { value: "2", label: "Warn" },
      { value: "3", label: "Info" },
      { value: "4", label: "Debug" },
    ],
  },
  { key: "upstream-interface", tab: "network", type: "text", labelKey: "upstreamInterface", placeholder: "iptv" },
  {
    key: "upstream-interface-multicast",
    tab: "network",
    type: "text",
    labelKey: "upstreamInterfaceMulticast",
    placeholder: "iptv",
  },
  {
    key: "upstream-interface-fcc",
    tab: "network",
    type: "text",
    labelKey: "upstreamInterfaceFcc",
    placeholder: "iptv",
  },
  {
    key: "upstream-interface-rtsp",
    tab: "network",
    type: "text",
    labelKey: "upstreamInterfaceRtsp",
    placeholder: "iptv",
  },
  {
    key: "upstream-interface-http",
    tab: "network",
    type: "text",
    labelKey: "upstreamInterfaceHttp",
    placeholder: "iptv",
  },
  { key: "maxclients", tab: "network", type: "number", labelKey: "maxClients", min: 1, max: 5000, placeholder: "5" },
  { key: "workers", tab: "network", type: "number", labelKey: "workers", min: 1, max: 64, placeholder: "1" },
  {
    key: "buffer-pool-max-size",
    tab: "network",
    type: "number",
    labelKey: "bufferPoolMaxSize",
    min: 1024,
    max: 1048576,
    placeholder: "16384",
  },
  {
    key: "udp-rcvbuf-size",
    tab: "network",
    type: "number",
    labelKey: "udpRcvbufSize",
    min: 65536,
    max: 16777216,
    placeholder: "524288",
  },
  {
    key: "mcast-rejoin-interval",
    tab: "network",
    type: "number",
    labelKey: "mcastRejoinInterval",
    min: 0,
    max: 86400,
    placeholder: "0",
  },
  {
    key: "fcc-listen-port-range",
    tab: "network",
    type: "text",
    labelKey: "fccListenPortRange",
    placeholder: "40000-40100",
  },
  { key: "zerocopy-on-send", tab: "network", type: "checkbox", labelKey: "zerocopyOnSend" },
  { key: "rtsp-stun-server", tab: "network", type: "text", labelKey: "rtspStunServer", placeholder: "stun.miwifi.com" },
  {
    key: "external-m3u",
    tab: "player",
    type: "text",
    labelKey: "externalM3u",
    placeholder: "https://example.com/playlist.m3u",
  },
  {
    key: "external-m3u-update-interval",
    tab: "player",
    type: "number",
    labelKey: "externalM3uUpdateInterval",
    min: 0,
    placeholder: "7200",
  },
  { key: "player-page-path", tab: "player", type: "text", labelKey: "playerPagePath", placeholder: "/player" },
  { key: "status-page-path", tab: "advanced", type: "text", labelKey: "statusPagePath", placeholder: "/status" },
  {
    key: "setting-page-path",
    tab: "advanced",
    type: "text",
    labelKey: "settingPagePath",
    placeholder: "/setting",
  },
  {
    key: "app-path-prefix",
    tab: "advanced",
    type: "text",
    labelKey: "appPathPrefix",
    placeholder: "/app/rtp2httpd",
  },
  { key: "use-relative-path-in-m3u", tab: "advanced", type: "checkbox", labelKey: "useRelativePathInM3u" },
  { key: "hostname", tab: "advanced", type: "text", labelKey: "hostname" },
  { key: "r2h-token", tab: "advanced", type: "text", labelKey: "r2hToken" },
  { key: "cors-allow-origin", tab: "advanced", type: "text", labelKey: "corsAllowOrigin", placeholder: "*" },
  { key: "xff", tab: "advanced", type: "checkbox", labelKey: "xff" },
  {
    key: "access-log",
    tab: "advanced",
    type: "text",
    labelKey: "accessLog",
    placeholder: "/tmp/rtp2httpd-access.log",
  },
  { key: "log-format", tab: "advanced", type: "text", labelKey: "logFormat" },
  { key: "http-proxy-user-agent", tab: "advanced", type: "text", labelKey: "httpProxyUserAgent" },
  { key: "rtsp-user-agent", tab: "advanced", type: "text", labelKey: "rtspUserAgent" },
  { key: "video-snapshot", tab: "advanced", type: "checkbox", labelKey: "videoSnapshot" },
  {
    key: "ffmpeg-path",
    tab: "advanced",
    type: "text",
    labelKey: "ffmpegPath",
    placeholder: "ffmpeg",
    dependsOn: { key: "video-snapshot", equals: "true" },
  },
  {
    key: "ffmpeg-args",
    tab: "advanced",
    type: "text",
    labelKey: "ffmpegArgs",
    placeholder: "-hwaccel none",
    dependsOn: { key: "video-snapshot", equals: "true" },
  },
];
