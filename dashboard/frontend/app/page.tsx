'use client';

import React, { useEffect, useState, useRef, useCallback } from 'react';
import {
  LineChart,
  Line,
  AreaChart,
  Area,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from 'recharts';

interface LiveSample {
  ts: number;
  hr: number | null;
  spo2: number | null;
  rr: number | null;
  fingerDetected: boolean;
  confidence: number;
}

interface AnalyticsData {
  latestEpoch: {
    hrMean: number | null;
    hrMin: number | null;
    hrMax: number | null;
    spo2Mean: number | null;
    spo2Min: number | null;
    spo2Max: number | null;
    hrvRmssd: number | null;
    hrvSdnn: number | null;
    rrMean: number | null;
    quality: number | null;
    lastUpdate: string;
  } | null;
  ahi: { value: number | null; severity: string | null; eventsCount: number; sleepHours: number | null };
  odi: { value: number | null; severity: string | null; eventsCount: number; sleepHours: number | null };
  warnings: { metric: string; level: string; message: string; value: number }[];
  trend: { time: string; hr: number | null; spo2: number | null; rr: number | null; hrv_rmssd: number | null; hrv_sdnn: number | null }[];
  sleepHistory: { time: string; isSleeping: number }[];
}

function SeverityBadge({ severity }: { severity: string | null }) {
  if (!severity) return null;
  const styles: Record<string, string> = {
    Normal: 'bg-green-50 text-green-700 border-green-200',
    Ringan: 'bg-orange-50 text-orange-700 border-orange-200',
    Sedang: 'bg-orange-50 text-orange-700 border-orange-200',
    Berat: 'bg-red-50 text-red-700 border-red-200',
  };
  return (
    <span className={`inline-flex items-center px-2 py-0.5 text-xs font-medium rounded-full border ${styles[severity] || 'bg-neutral-50 text-neutral-600 border-neutral-200'}`}>
      {severity}
    </span>
  );
}

function StatusDot({ active, color }: { active: boolean; color?: string }) {
  const bg = active ? (color || 'bg-green-500') : 'bg-neutral-300';
  return <span className={`inline-block w-2 h-2 rounded-full ${bg} ${active ? 'animate-pulse' : ''}`} />;
}

const WS_URL = process.env.NEXT_PUBLIC_WS_URL || 'ws://localhost:4000';
const LIVE_BUFFER_SIZE = 20;

export default function Dashboard() {
  const [liveBuffer, setLiveBuffer] = useState<LiveSample[]>([]);
  const [wsConnected, setWsConnected] = useState(false);
  const [analytics, setAnalytics] = useState<AnalyticsData | null>(null);
  const wsRef = useRef<WebSocket | null>(null);

  // WebSocket connection for live data
  useEffect(() => {
    function connect() {
      const ws = new WebSocket(WS_URL);
      wsRef.current = ws;

      ws.onopen = () => setWsConnected(true);
      ws.onclose = () => {
        setWsConnected(false);
        setTimeout(connect, 3000);
      };
      ws.onerror = () => ws.close();
      ws.onmessage = (event) => {
        try {
          const sample: LiveSample = JSON.parse(event.data);
          setLiveBuffer((prev) => [...prev.slice(-(LIVE_BUFFER_SIZE - 1)), sample]);
        } catch { /* ignore parse errors */ }
      };
    }
    connect();
    return () => { wsRef.current?.close(); };
  }, []);

  // Analytics polling
  const fetchAnalytics = useCallback(async () => {
    try {
      const res = await fetch('/api/analytics');
      if (res.ok) {
        setAnalytics(await res.json());
      }
    } catch { /* silent */ }
  }, []);

  useEffect(() => {
    fetchAnalytics();
    const interval = setInterval(fetchAnalytics, 10000);
    return () => clearInterval(interval);
  }, [fetchAnalytics]);

  const latestLive = liveBuffer[liveBuffer.length - 1] || null;
  const sparkData = liveBuffer.map((s, i) => ({
    idx: i,
    hr: s.hr,
    spo2: s.spo2,
    rr: s.rr,
  }));

  const hasCritical = analytics?.warnings?.some((w) => w.level === 'critical');
  const hasWarning = (analytics?.warnings?.length || 0) > 0;

  return (
    <div className="min-h-screen p-6 md:p-10">
      <div className="max-w-6xl mx-auto">
        {/* Header */}
        <header className="mb-8 flex flex-col md:flex-row md:items-end md:justify-between gap-4">
          <div>
            <h1 className="text-2xl md:text-3xl font-bold text-neutral-900 typewriter">
              Pemantauan Pasien Real-time
            </h1>
            <p className="text-sm text-neutral-500 mt-2 font-mono">
              Edge AI &middot; Dual-Stream Sleep Apnea Monitoring
            </p>
          </div>
          <div className="flex items-center gap-3 text-xs text-neutral-500 font-mono border border-dashed border-neutral-200 rounded-md px-3 py-2">
            <div className="flex items-center gap-1.5">
              <StatusDot active={wsConnected} />
              <span>{wsConnected ? 'WebSocket Live' : 'Disconnected'}</span>
            </div>
            {latestLive && (
              <span className="text-neutral-300">|</span>
            )}
            {latestLive && (
              <div className="flex items-center gap-1.5">
                <StatusDot active={latestLive.fingerDetected} color={latestLive.fingerDetected ? 'bg-green-500' : 'bg-orange-400'} />
                <span>{latestLive.fingerDetected ? 'Finger OK' : 'No finger'}</span>
              </div>
            )}
          </div>
        </header>

        {/* Warning Banner */}
        {hasWarning && analytics && (
          <div className={`mb-6 border border-dashed rounded-lg p-4 ${hasCritical ? 'border-red-300 bg-red-50/50 pulse-border' : 'border-orange-300 bg-orange-50/50'}`}>
            <div className="flex items-center gap-2 mb-2">
              <span className={`text-xs font-mono uppercase tracking-wider ${hasCritical ? 'text-red-600' : 'text-orange-600'}`}>
                {hasCritical ? 'CRITICAL' : 'WARNING'}
              </span>
            </div>
            <div className="space-y-1">
              {analytics.warnings.map((w, i) => (
                <p key={i} className={`text-sm font-mono ${w.level === 'critical' ? 'text-red-700' : 'text-orange-700'}`}>
                  {w.message}
                </p>
              ))}
            </div>
          </div>
        )}

        {/* ===== LIVE PANEL ===== */}
        <section className="mb-8">
          <div className="flex items-center gap-2 mb-4">
            <span className="text-xs uppercase tracking-wider text-neutral-400 font-medium">Live Monitor</span>
            <span className="text-xs font-mono text-neutral-300">(last {LIVE_BUFFER_SIZE}s)</span>
          </div>

          {/* Live Vitals Cards */}
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-4">
            <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
              <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">Heart Rate</p>
              <p className="font-mono text-3xl font-bold text-neutral-900">
                {latestLive?.hr ?? '--'}
                <span className="text-sm text-neutral-400 ml-1">BPM</span>
              </p>
            </div>
            <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
              <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">SpO2</p>
              <p className="font-mono text-3xl font-bold text-neutral-900">
                {latestLive?.spo2 ?? '--'}
                <span className="text-sm text-neutral-400 ml-1">%</span>
              </p>
            </div>
            <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
              <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">Respiratory Rate</p>
              <p className="font-mono text-3xl font-bold text-neutral-900">
                {latestLive?.rr ?? '--'}
                <span className="text-sm text-neutral-400 ml-1">RPM</span>
              </p>
            </div>
          </div>

          {/* Live Sparklines */}
          {sparkData.length > 1 && (
            <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
              <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-4">Rolling Sparkline</p>
              <div className="h-40 w-full">
                <ResponsiveContainer width="100%" height="100%">
                  <LineChart data={sparkData} margin={{ top: 5, right: 10, left: 0, bottom: 5 }}>
                    <YAxis yAxisId="hr" stroke="#a3a3a3" fontSize={10} tickLine={false} axisLine={false} domain={['dataMin - 5', 'dataMax + 5']} width={30} />
                    <YAxis yAxisId="spo2" orientation="right" stroke="#a3a3a3" fontSize={10} tickLine={false} axisLine={false} domain={[88, 100]} width={30} />
                    <Tooltip
                      contentStyle={{ borderRadius: '6px', border: '1px dashed #e5e5e5', boxShadow: 'none', fontFamily: 'var(--font-geist-mono)', fontSize: '11px' }}
                    />
                    <Line yAxisId="hr" type="monotone" dataKey="hr" name="HR" stroke="#0a0a0a" strokeWidth={2} dot={false} isAnimationActive={false} />
                    <Line yAxisId="spo2" type="monotone" dataKey="spo2" name="SpO2" stroke="#737373" strokeWidth={2} strokeDasharray="4 2" dot={false} isAnimationActive={false} />
                  </LineChart>
                </ResponsiveContainer>
              </div>
            </div>
          )}

          {!wsConnected && liveBuffer.length === 0 && (
            <div className="border border-dashed border-neutral-300 rounded-lg p-8 text-center">
              <div className="w-5 h-5 border-2 border-neutral-300 border-t-neutral-900 rounded-full animate-spin mx-auto mb-3" />
              <p className="text-sm text-neutral-500 font-mono">Menghubungkan WebSocket...</p>
            </div>
          )}
        </section>

        {/* ===== ANALYTICS PANEL ===== */}
        <section>
          <div className="flex items-center gap-2 mb-4">
            <span className="text-xs uppercase tracking-wider text-neutral-400 font-medium">Analytics</span>
            <span className="text-xs font-mono text-neutral-300">(60s epochs, polls every 10s)</span>
          </div>

          {analytics ? (
            <>
              {/* AHI / ODI / Latest Epoch */}
              <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4 mb-4">
                {/* AHI */}
                <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
                  <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">AHI (Apnea-Hypopnea Index)</p>
                  {analytics.ahi.value !== null ? (
                    <>
                      <p className="font-mono text-2xl font-bold text-neutral-900">
                        {analytics.ahi.value}<span className="text-sm text-neutral-400 ml-1">/jam</span>
                      </p>
                      <div className="mt-2"><SeverityBadge severity={analytics.ahi.severity} /></div>
                      <p className="text-xs text-neutral-400 font-mono mt-2">
                        {analytics.ahi.eventsCount} events &middot; {analytics.ahi.sleepHours} jam tidur
                      </p>
                    </>
                  ) : (
                    <p className="font-mono text-sm text-neutral-400 mt-1">Menunggu data tidur...</p>
                  )}
                </div>

                {/* ODI */}
                <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
                  <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">ODI (Oxygen Desaturation Index)</p>
                  {analytics.odi.value !== null ? (
                    <>
                      <p className="font-mono text-2xl font-bold text-neutral-900">
                        {analytics.odi.value}<span className="text-sm text-neutral-400 ml-1">/jam</span>
                      </p>
                      <div className="mt-2"><SeverityBadge severity={analytics.odi.severity} /></div>
                      <p className="text-xs text-neutral-400 font-mono mt-2">
                        {analytics.odi.eventsCount} desaturasi &middot; {analytics.odi.sleepHours} jam tidur
                      </p>
                    </>
                  ) : (
                    <p className="font-mono text-sm text-neutral-400 mt-1">Menunggu data tidur...</p>
                  )}
                </div>

                {/* Latest Epoch Summary */}
                <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
                  <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">Epoch Terakhir</p>
                  {analytics.latestEpoch ? (
                    <div className="grid grid-cols-2 gap-2">
                      <div>
                        <p className="text-xs text-neutral-400">HR (avg)</p>
                        <p className="font-mono text-lg font-bold text-neutral-900">{analytics.latestEpoch.hrMean ?? '--'}</p>
                      </div>
                      <div>
                        <p className="text-xs text-neutral-400">SpO2 (avg)</p>
                        <p className="font-mono text-lg font-bold text-neutral-900">{analytics.latestEpoch.spo2Mean ?? '--'}</p>
                      </div>
                      <div>
                        <p className="text-xs text-neutral-400">HRV RMSSD</p>
                        <p className="font-mono text-lg font-bold text-neutral-900">{analytics.latestEpoch.hrvRmssd ?? '--'}</p>
                      </div>
                      <div>
                        <p className="text-xs text-neutral-400">Quality</p>
                        <p className="font-mono text-lg font-bold text-neutral-900">{analytics.latestEpoch.quality ?? '--'}%</p>
                      </div>
                    </div>
                  ) : (
                    <p className="font-mono text-sm text-neutral-400 mt-1">Belum ada epoch...</p>
                  )}
                </div>
              </div>

              {/* Trend Chart */}
              {analytics.trend.length > 1 && (
                <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white mb-4">
                  <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-6">Trend HR &amp; SpO2 (per epoch)</p>
                  <div className="h-64 w-full">
                    <ResponsiveContainer width="100%" height="100%">
                      <LineChart data={analytics.trend} margin={{ top: 5, right: 10, left: 0, bottom: 5 }}>
                        <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#e5e5e5" />
                        <XAxis dataKey="time" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} fontFamily="var(--font-geist-mono)" />
                        <YAxis yAxisId="left" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} domain={['dataMin - 5', 'dataMax + 5']} />
                        <YAxis yAxisId="right" orientation="right" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} domain={[88, 100]} />
                        <Tooltip contentStyle={{ borderRadius: '6px', border: '1px dashed #e5e5e5', boxShadow: 'none', fontFamily: 'var(--font-geist-mono)', fontSize: '11px' }} />
                        <Line yAxisId="left" type="monotone" dataKey="hr" name="HR (BPM)" stroke="#0a0a0a" strokeWidth={2} dot={false} isAnimationActive={false} />
                        <Line yAxisId="right" type="monotone" dataKey="spo2" name="SpO2 (%)" stroke="#737373" strokeWidth={2} strokeDasharray="4 2" dot={false} isAnimationActive={false} />
                      </LineChart>
                    </ResponsiveContainer>
                  </div>
                </div>
              )}

              {/* Sleep History */}
              {analytics.sleepHistory.length > 0 && (
                <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
                  <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-6">Riwayat Status Tidur</p>
                  <div className="h-44 w-full">
                    <ResponsiveContainer width="100%" height="100%">
                      <AreaChart data={analytics.sleepHistory} margin={{ top: 5, right: 10, left: 0, bottom: 5 }}>
                        <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#e5e5e5" />
                        <XAxis dataKey="time" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} fontFamily="var(--font-geist-mono)" />
                        <YAxis stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} domain={[0, 1]} ticks={[0, 1]} tickFormatter={(v) => (v === 1 ? 'Tidur' : 'Bangun')} width={50} fontFamily="var(--font-geist-mono)" />
                        <Tooltip contentStyle={{ borderRadius: '6px', border: '1px dashed #e5e5e5', boxShadow: 'none', fontFamily: 'var(--font-geist-mono)', fontSize: '11px' }} formatter={(v) => [Number(v) === 1 ? 'Tidur' : 'Bangun', 'Status']} />
                        <Area type="stepAfter" dataKey="isSleeping" name="Status Tidur" stroke="#0a0a0a" strokeWidth={1.5} fill="#fafafa" isAnimationActive={false} />
                      </AreaChart>
                    </ResponsiveContainer>
                  </div>
                </div>
              )}
            </>
          ) : (
            <div className="border border-dashed border-neutral-300 rounded-lg p-8 text-center">
              <div className="w-5 h-5 border-2 border-neutral-300 border-t-neutral-900 rounded-full animate-spin mx-auto mb-3" />
              <p className="text-sm text-neutral-500 font-mono">Memuat data analytics...</p>
            </div>
          )}
        </section>

        {/* Footer */}
        <footer className="mt-8 text-center">
          <p className="text-xs text-neutral-400 font-mono">
            Dual-Stream Architecture &middot; Live 1s + Epoch 60s &middot; MAX30102 + ESP32-S3
          </p>
        </footer>
      </div>
    </div>
  );
}
