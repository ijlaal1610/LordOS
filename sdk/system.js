// system.js — información del sistema y del servidor, como hooks de React.
import { useEffect, useState } from 'react';
import { formatNumber } from './i18n.js';

export const systemInfo = () => lord.system.info();

function usePoll(read, intervalMs) {
  const [value, setValue] = useState(read);
  useEffect(() => {
    const id = setInterval(() => setValue(read()), intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);
  return value;
}

export const useSystemInfo = (intervalMs = 5000) => usePoll(systemInfo, intervalMs);
export const useServerStats = (intervalMs = 2000) => usePoll(() => lord.stats(), intervalMs);

// Uso de CPU (0..1) calculado entre lecturas de /proc/stat.
export function useCpuUsage(intervalMs = 1500) {
  const [usage, setUsage] = useState(0);
  useEffect(() => {
    let [t0, i0] = lord.system.cpuTimes();
    const id = setInterval(() => {
      const [t1, i1] = lord.system.cpuTimes();
      const dt = t1 - t0;
      setUsage(dt > 0 ? Math.max(0, Math.min(1, 1 - (i1 - i0) / dt)) : 0);
      t0 = t1; i0 = i1;
    }, intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);
  return usage;
}

export function formatBytes(n) {
  const u = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
  return `${n < 10 && i ? formatNumber(n, 1) : Math.round(n)} ${u[i]}`;
}

export function formatDuration(s) {
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d} d ${h} h` : h ? `${h} h ${m} min` : `${m} min`;
}
