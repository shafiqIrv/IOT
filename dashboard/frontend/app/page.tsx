'use client';

import React, { useEffect, useState } from 'react';
import {
  LineChart,
  Line,
  AreaChart,
  Area,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from 'recharts';

function SeverityBadge({ severity }: { severity: string | null }) {
  if (!severity) return null;
  const styles: Record<string, string> = {
    Normal: 'bg-green-50 text-green-700 border-green-200',
    Ringan: 'bg-orange-50 text-orange-700 border-orange-200',
    Sedang: 'bg-orange-50 text-orange-700 border-orange-200',
    Berat: 'bg-red-50 text-red-700 border-red-200',
  };
  return (
    <span
      className={`inline-flex items-center px-2 py-0.5 text-xs font-medium rounded-full border ${styles[severity] || 'bg-neutral-50 text-neutral-600 border-neutral-200'}`}
    >
      {severity}
    </span>
  );
}

function StatusDot({ active }: { active: boolean }) {
  return (
    <span
      className={`inline-block w-2 h-2 rounded-full ${active ? 'bg-green-500' : 'bg-neutral-300'}`}
    />
  );
}

export default function Dashboard() {
  const [data, setData] = useState<any>(null);
  const [loading, setLoading] = useState(true);
  const [showHrv, setShowHrv] = useState(false);

  const fetchData = async () => {
    try {
      const response = await fetch('/api/data');
      if (response.ok) {
        const result = await response.json();
        setData(result);
      }
    } catch (error) {
      console.error('Gagal mengambil data:', error);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchData();
    const interval = setInterval(fetchData, 3000);
    return () => clearInterval(interval);
  }, []);

  if (loading && !data) {
    return (
      <div className="min-h-screen flex flex-col items-center justify-center">
        <div className="w-5 h-5 border-2 border-neutral-300 border-t-neutral-900 rounded-full animate-spin mb-4" />
        <p className="text-sm text-neutral-500 font-mono">Menghubungkan ke database...</p>
      </div>
    );
  }

  if (!data || data.error) {
    return (
      <div className="min-h-screen flex items-center justify-center">
        <div className="border border-dashed border-neutral-300 rounded-lg p-10 text-center max-w-md">
          <div className="w-10 h-10 mx-auto mb-4 border border-dashed border-orange-300 rounded-full flex items-center justify-center">
            <span className="text-orange-500 text-lg">!</span>
          </div>
          <h2 className="text-lg font-semibold text-neutral-900 mb-2">Data Belum Tersedia</h2>
          <p className="text-sm text-neutral-500">
            Pastikan ESP32 sudah mengirimkan data melalui MQTT.
          </p>
        </div>
      </div>
    );
  }

  const { latest, history, sleepHistory, ahi, odi, warnings } = data;
  const hrv = latest.hrv || {};
  const hasCritical = warnings?.some((w: any) => w.level === 'critical');
  const hasWarning = warnings?.length > 0;

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
              Edge AI &middot; Sleep Apnea Detection System
            </p>
          </div>
          <div className="flex items-center gap-2 text-xs text-neutral-500 font-mono border border-dashed border-neutral-200 rounded-md px-3 py-2">
            <StatusDot active={true} />
            <span>Live &middot; {latest.lastUpdate}</span>
          </div>
        </header>

        {/* Warning Banner */}
        {hasWarning && (
          <div
            className={`mb-6 border border-dashed rounded-lg p-4 ${hasCritical ? 'border-red-300 bg-red-50/50 pulse-border' : 'border-orange-300 bg-orange-50/50'}`}
          >
            <div className="flex items-center gap-2 mb-2">
              <span className={`text-xs font-mono uppercase tracking-wider ${hasCritical ? 'text-red-600' : 'text-orange-600'}`}>
                {hasCritical ? 'CRITICAL' : 'WARNING'}
              </span>
            </div>
            <div className="space-y-1">
              {warnings.map((w: any, i: number) => (
                <p key={i} className={`text-sm font-mono ${w.level === 'critical' ? 'text-red-700' : 'text-orange-700'}`}>
                  {w.message}
                </p>
              ))}
            </div>
          </div>
        )}

        {/* Hero Vitals */}
        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">
              Heart Rate
            </p>
            <p className="font-mono text-3xl font-bold text-neutral-900">
              {latest.heartRate ?? '--'}
              <span className="text-sm text-neutral-400 ml-1">BPM</span>
            </p>
            <div className="mt-3">
              <SeverityBadge severity={latest.heartRate ? (latest.heartRate >= 50 && latest.heartRate <= 100 ? 'Normal' : 'Ringan') : null} />
            </div>
          </div>

          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">
              SpO2
            </p>
            <p className="font-mono text-3xl font-bold text-neutral-900">
              {latest.spo2 ?? '--'}
              <span className="text-sm text-neutral-400 ml-1">%</span>
            </p>
            <div className="mt-3">
              <SeverityBadge severity={latest.spo2 ? (latest.spo2 >= 92 ? 'Normal' : latest.spo2 >= 88 ? 'Ringan' : 'Berat') : null} />
            </div>
          </div>

          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">
              Respiratory Rate
            </p>
            <p className="font-mono text-3xl font-bold text-neutral-900">
              {latest.respiratoryRate ?? '--'}
              <span className="text-sm text-neutral-400 ml-1">RPM</span>
            </p>
            <div className="mt-3">
              <SeverityBadge severity={latest.respiratoryRate ? 'Normal' : null} />
            </div>
          </div>
        </div>

        {/* Bento Grid */}
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4 mb-6">
          {/* AHI */}
          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">
              AHI (Apnea-Hypopnea Index)
            </p>
            {ahi.value !== null ? (
              <>
                <p className="font-mono text-2xl font-bold text-neutral-900">
                  {ahi.value}
                  <span className="text-sm text-neutral-400 ml-1">/jam</span>
                </p>
                <div className="mt-2 flex items-center gap-2">
                  <SeverityBadge severity={ahi.severity} />
                </div>
                <p className="text-xs text-neutral-400 font-mono mt-2">
                  {ahi.eventsCount} events &middot; {ahi.sleepHours} jam tidur
                </p>
              </>
            ) : (
              <p className="font-mono text-sm text-neutral-400 mt-1">Menunggu data tidur...</p>
            )}
          </div>

          {/* ODI */}
          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">
              ODI (Oxygen Desaturation Index)
            </p>
            {odi.value !== null ? (
              <>
                <p className="font-mono text-2xl font-bold text-neutral-900">
                  {odi.value}
                  <span className="text-sm text-neutral-400 ml-1">/jam</span>
                </p>
                <div className="mt-2 flex items-center gap-2">
                  <SeverityBadge severity={odi.severity} />
                </div>
                <p className="text-xs text-neutral-400 font-mono mt-2">
                  {odi.eventsCount} desaturasi &middot; {odi.sleepHours} jam tidur
                </p>
              </>
            ) : (
              <p className="font-mono text-sm text-neutral-400 mt-1">Menunggu data tidur...</p>
            )}
          </div>

          {/* Status Vital */}
          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">
              Status Vital
            </p>
            <p className={`font-mono text-2xl font-bold ${latest.isCritical ? 'text-red-600' : 'text-green-700'}`}>
              {latest.isCritical ? 'Kritis' : 'Normal'}
            </p>
            <div className="mt-2">
              <SeverityBadge severity={latest.isCritical ? 'Berat' : 'Normal'} />
            </div>
          </div>

          {/* Sleep Status */}
          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">
              Status Tidur
            </p>
            <p className="font-mono text-2xl font-bold text-neutral-900">
              {latest.sleepStatus}
            </p>
            <p className="text-xs text-neutral-400 font-mono mt-2">
              Probabilitas: {latest.sleepProb}%
            </p>
          </div>

          {/* Apnea Detection */}
          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-2">
              Deteksi Apnea
            </p>
            <p className={`font-mono text-2xl font-bold ${latest.apneaWarning ? 'text-red-600' : 'text-green-700'}`}>
              {latest.apneaWarning ? 'Terdeteksi' : 'Aman'}
            </p>
            {latest.apneaWarning && (
              <p className="text-xs text-red-500 font-mono mt-2">
                Probabilitas: {latest.apneaProb}%
              </p>
            )}
            <div className="mt-2">
              <SeverityBadge severity={latest.apneaWarning ? 'Berat' : 'Normal'} />
            </div>
          </div>

          {/* HRV Summary */}
          <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
            <div className="flex items-center justify-between mb-2">
              <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium">
                HRV
              </p>
              <button
                type="button"
                onClick={() => setShowHrv((v) => !v)}
                className="text-xs font-mono text-neutral-500 hover:text-neutral-900 transition-colors"
              >
                {showHrv ? 'Sembunyikan' : 'Detail'}
              </button>
            </div>
            <div className="grid grid-cols-2 gap-3">
              <div>
                <p className="text-xs text-neutral-400">SDNN</p>
                <p className="font-mono text-lg font-bold text-neutral-900">{hrv.sdnn ?? '--'}<span className="text-xs text-neutral-400 ml-0.5">ms</span></p>
              </div>
              <div>
                <p className="text-xs text-neutral-400">RMSSD</p>
                <p className="font-mono text-lg font-bold text-neutral-900">{hrv.rmssd ?? '--'}<span className="text-xs text-neutral-400 ml-0.5">ms</span></p>
              </div>
              {showHrv && (
                <>
                  <div>
                    <p className="text-xs text-neutral-400">pNN50</p>
                    <p className="font-mono text-lg font-bold text-neutral-900">{hrv.pnn50 ?? '--'}<span className="text-xs text-neutral-400 ml-0.5">%</span></p>
                  </div>
                  <div>
                    <p className="text-xs text-neutral-400">Mean RR</p>
                    <p className="font-mono text-lg font-bold text-neutral-900">{hrv.meanRR ?? '--'}<span className="text-xs text-neutral-400 ml-0.5">ms</span></p>
                  </div>
                </>
              )}
            </div>
          </div>
        </div>

        {/* Charts */}
        <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white mb-4">
          <div className="flex items-center justify-between mb-6">
            <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium">
              Riwayat HR &amp; SpO2{showHrv ? ' & HRV' : ''}
            </p>
            <button
              type="button"
              onClick={() => setShowHrv((v) => !v)}
              className={`text-xs font-mono px-2 py-1 rounded border border-dashed transition-colors ${showHrv ? 'border-green-300 text-green-700 bg-green-50' : 'border-neutral-200 text-neutral-500'}`}
            >
              HRV {showHrv ? 'ON' : 'OFF'}
            </button>
          </div>
          <div className="h-72 w-full">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={history} margin={{ top: 5, right: 10, left: 0, bottom: 5 }}>
                <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#e5e5e5" />
                <XAxis dataKey="time" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} fontFamily="var(--font-geist-mono)" />
                <YAxis yAxisId="left" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} domain={['dataMin - 5', 'dataMax + 5']} />
                <YAxis yAxisId="right" orientation="right" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} domain={[88, 100]} />
                {showHrv && (
                  <YAxis yAxisId="hrv" orientation="right" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} width={30} domain={['dataMin - 5', 'dataMax + 5']} />
                )}
                <Tooltip
                  contentStyle={{
                    borderRadius: '6px',
                    border: '1px dashed #e5e5e5',
                    boxShadow: 'none',
                    fontFamily: 'var(--font-geist-mono)',
                    fontSize: '11px',
                  }}
                />
                <Legend wrapperStyle={{ paddingTop: '16px', fontSize: '11px', fontFamily: 'var(--font-geist-mono)' }} />
                <Line yAxisId="left" type="monotone" dataKey="hr" name="HR (BPM)" stroke="#0a0a0a" strokeWidth={2} dot={false} isAnimationActive={false} />
                <Line yAxisId="right" type="monotone" dataKey="spo2" name="SpO2 (%)" stroke="#737373" strokeWidth={2} strokeDasharray="4 2" dot={false} isAnimationActive={false} />
                {showHrv && (
                  <Line yAxisId="hrv" type="monotone" dataKey="rmssd" name="RMSSD (ms)" stroke="#16a34a" strokeWidth={1.5} dot={false} isAnimationActive={false} />
                )}
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

        <div className="border border-dashed border-neutral-200 rounded-lg p-6 bg-white">
          <p className="text-xs uppercase tracking-wider text-neutral-400 font-medium mb-6">
            Riwayat Status Tidur
          </p>
          <div className="h-52 w-full">
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={sleepHistory} margin={{ top: 5, right: 10, left: 0, bottom: 5 }}>
                <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#e5e5e5" />
                <XAxis dataKey="time" stroke="#a3a3a3" fontSize={11} tickLine={false} axisLine={false} fontFamily="var(--font-geist-mono)" />
                <YAxis
                  stroke="#a3a3a3"
                  fontSize={11}
                  tickLine={false}
                  axisLine={false}
                  domain={[0, 1]}
                  ticks={[0, 1]}
                  tickFormatter={(v) => (v === 1 ? 'Tidur' : 'Bangun')}
                  width={50}
                  fontFamily="var(--font-geist-mono)"
                />
                <Tooltip
                  contentStyle={{
                    borderRadius: '6px',
                    border: '1px dashed #e5e5e5',
                    boxShadow: 'none',
                    fontFamily: 'var(--font-geist-mono)',
                    fontSize: '11px',
                  }}
                  formatter={(v) => [Number(v) === 1 ? 'Tidur' : 'Bangun', 'Status']}
                />
                <Area
                  type="stepAfter"
                  dataKey="isSleeping"
                  name="Status Tidur"
                  stroke="#0a0a0a"
                  strokeWidth={1.5}
                  fill="#fafafa"
                  isAnimationActive={false}
                />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* Footer */}
        <footer className="mt-8 text-center">
          <p className="text-xs text-neutral-400 font-mono">
            Edge AI &middot; IoT Sleep Apnea Monitoring &middot; MAX30102 + ESP32-S3
          </p>
        </footer>
      </div>
    </div>
  );
}
