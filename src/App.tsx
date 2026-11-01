/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useState, useEffect, useMemo } from 'react';
import {
  Download,
  Copy,
  Check,
  FileCode,
  Terminal,
  Zap,
  Activity,
  Play,
  Pause,
  RotateCw,
  Trash2,
  Sliders,
  ShieldCheck,
  Monitor,
  Settings,
  EyeOff
} from 'lucide-react';
import { TASKMGR_C_SOURCE, MAKEFILE_SOURCE, BUILDBAT_SOURCE, SETTINGS_INI_SOURCE } from './data/cSource';

interface SimulatedProcess {
  pid: number;
  name: string;
  cpu: number;
  ramMB: number;
  diskMB: number;
  netKB: number;
  gpu: number;
  priority: string;
  cmdLine: string;
  isSystem?: boolean;
  isWindowsBase?: boolean;
}

const INITIAL_PROCESSES: SimulatedProcess[] = [
  { pid: 4, name: 'System', cpu: 0.1, ramMB: 84, diskMB: 0.2, netKB: 0.0, gpu: 0.0, priority: 'Normale', cmdLine: 'ntoskrnl.exe', isSystem: true, isWindowsBase: true },
  { pid: 820, name: 'csrss.exe', cpu: 0.2, ramMB: 12, diskMB: 0.0, netKB: 0.0, gpu: 0.0, priority: 'Temps réel', cmdLine: 'C:\\Windows\\System32\\csrss.exe ObjectDirectory=\\Windows SharedSection=1024,20480,768', isSystem: true, isWindowsBase: true },
  { pid: 1044, name: 'explorer.exe', cpu: 1.4, ramMB: 312, diskMB: 0.8, netKB: 1.2, gpu: 2.1, priority: 'Normale', cmdLine: 'C:\\Windows\\explorer.exe /factory,{75d4e0b0-1993-4503-ac29-07d561b4c151}', isWindowsBase: true },
  { pid: 3240, name: 'chrome.exe', cpu: 6.8, ramMB: 894, diskMB: 3.4, netKB: 45.8, gpu: 4.5, priority: 'Normale', cmdLine: 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe --type=renderer --field-trial-handle=1840 --lang=fr', isWindowsBase: false },
  { pid: 3812, name: 'chrome.exe (GPU)', cpu: 2.1, ramMB: 420, diskMB: 0.1, netKB: 0.0, gpu: 12.4, priority: 'Supérieure', cmdLine: 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe --type=gpu-process --enable-features=Vulkan', isWindowsBase: false },
  { pid: 4892, name: 'code.exe', cpu: 3.2, ramMB: 654, diskMB: 1.1, netKB: 3.5, gpu: 1.8, priority: 'Normale', cmdLine: 'C:\\Users\\User\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe --unity-launch .', isWindowsBase: false },
  { pid: 5612, name: 'gcc.exe', cpu: 18.5, ramMB: 245, diskMB: 14.8, netKB: 0.0, gpu: 0.0, priority: 'Haute', cmdLine: 'C:\\msys64\\mingw64\\bin\\gcc.exe -mwindows -O2 -Wall -o taskmgr.exe taskmgr.c -lcomctl32 -lpsapi -lntdll', isWindowsBase: false },
  { pid: 6104, name: 'discord.exe', cpu: 1.1, ramMB: 280, diskMB: 0.0, netKB: 18.4, gpu: 1.2, priority: 'Normale', cmdLine: 'C:\\Users\\User\\AppData\\Local\\Discord\\app-1.0.9160\\Discord.exe --enable-speech-dispatcher', isWindowsBase: false },
  { pid: 7420, name: 'spotify.exe', cpu: 1.8, ramMB: 198, diskMB: 0.4, netKB: 128.0, gpu: 0.8, priority: 'Normale', cmdLine: 'C:\\Users\\User\\AppData\\Roaming\\Spotify\\Spotify.exe --autostart --minimized', isWindowsBase: false },
  { pid: 8150, name: 'steam.exe', cpu: 0.4, ramMB: 165, diskMB: 0.0, netKB: 0.4, gpu: 0.0, priority: 'Normale', cmdLine: 'C:\\Program Files (x86)\\Steam\\steam.exe -silent', isWindowsBase: false },
  { pid: 9344, name: 'powershell.exe', cpu: 0.0, ramMB: 92, diskMB: 0.0, netKB: 0.0, gpu: 0.0, priority: 'Normale', cmdLine: 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe -NoProfile -ExecutionPolicy Bypass', isWindowsBase: true },
  { pid: 10220, name: 'taskmgr.exe', cpu: 0.8, ramMB: 18, diskMB: 0.0, netKB: 0.0, gpu: 0.5, priority: 'Haute', cmdLine: 'taskmgr.exe', isWindowsBase: false },
];

export default function App() {
  const [activeTab, setActiveTab] = useState<'source' | 'preview' | 'settings' | 'makefile' | 'buildbat' | 'guide'>('source');
  const [copiedFile, setCopiedFile] = useState<string | null>(null);

  // State du simulateur Win32 interactif
  const [processes, setProcesses] = useState<SimulatedProcess[]>(INITIAL_PROCESSES);
  const [selectedPid, setSelectedPid] = useState<number | null>(5612);
  const [isAutoRefresh, setIsAutoRefresh] = useState<boolean>(true);
  const [hideWindows, setHideWindows] = useState<boolean>(false);
  const [isMinimizedToTray, setIsMinimizedToTray] = useState<boolean>(false);
  const [trayMenu, setTrayMenu] = useState<{ x: number; y: number; type: 'cpu' | 'ram' | 'gpu' } | null>(null);
  const [cpuHistory, setCpuHistory] = useState<number[]>([12, 14, 18, 15, 22, 19, 14, 16, 25, 20, 18, 22, 15, 17]);
  const [ramHistory, setRamHistory] = useState<number[]>([58, 58, 59, 59, 60, 60, 61, 61, 62, 62, 62, 63, 63, 63]);
  const [gpuHistory, setGpuHistory] = useState<number[]>([4, 6, 8, 5, 12, 7, 4, 6, 9, 5, 8, 11, 6, 5]);
  const [excludedProcesses, setExcludedProcesses] = useState<string[]>([]);
  const [showExclusionsMenu, setShowExclusionsMenu] = useState<boolean>(false);
  const [searchFilter, setSearchFilter] = useState<string>('');
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; pid: number } | null>(null);
  const [sortCol, setSortCol] = useState<keyof SimulatedProcess>('cpu');
  const [sortAsc, setSortAsc] = useState<boolean>(false);
  const [notification, setNotification] = useState<string | null>(null);

  // Timer de simulation 1 seconde
  useEffect(() => {
    if (!isAutoRefresh) return;
    const interval = setInterval(() => {
      setProcesses((prev) => {
        const next = prev.map((p) => {
          if (p.isSystem) return p;
          const cpuDelta = (Math.random() - 0.5) * 1.5;
          const newCpu = Math.max(0.0, Math.min(99.0, Number((p.cpu + cpuDelta).toFixed(1))));
          const diskDelta = (Math.random() - 0.5) * 0.4;
          const newDisk = Math.max(0.0, Number((p.diskMB + diskDelta).toFixed(2)));
          const netDelta = (Math.random() - 0.5) * 8.0;
          const newNet = Math.max(0.0, Number((p.netKB + netDelta).toFixed(1)));
          return {
            ...p,
            cpu: newCpu,
            diskMB: newDisk,
            netKB: newNet,
          };
        });

        const cTotal = Math.min(100, Number(next.reduce((acc, p) => acc + p.cpu, 0).toFixed(1)));
        const rTotalMB = next.reduce((acc, p) => acc + p.ramMB, 0);
        const rPercent = Math.min(100, Math.round((rTotalMB / 16384) * 100));
        const gTotal = Math.min(100, Number(next.reduce((acc, p) => acc + p.gpu, 0).toFixed(1)));

        setCpuHistory((h) => [...h.slice(1), cTotal]);
        setRamHistory((h) => [...h.slice(1), rPercent]);
        setGpuHistory((h) => [...h.slice(1), gTotal]);

        return next;
      });
    }, 1000);

    return () => clearInterval(interval);
  }, [isAutoRefresh]);

  // Raccourci clavier Suppr dans la prévisualisation
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (activeTab === 'preview' && e.key === 'Delete' && selectedPid) {
        killProcessImmediately(selectedPid);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [activeTab, selectedPid]);

  // Copier un fichier dans le presse-papier
  const copyToClipboard = (text: string, fileName: string) => {
    navigator.clipboard.writeText(text);
    setCopiedFile(fileName);
    setTimeout(() => setCopiedFile(null), 2000);
  };

  // Télécharger un fichier directement
  const downloadFile = (filename: string, content: string) => {
    const blob = new Blob([content], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  };

  // Tuer un processus immédiatement sans confirmation
  const killProcessImmediately = (pid: number) => {
    const proc = processes.find((p) => p.pid === pid);
    if (!proc) return;
    if (proc.isSystem) {
      showToast(`Impossible de terminer ${proc.name} (Processus système protégé)`);
      return;
    }
    setProcesses((prev) => prev.filter((p) => p.pid !== pid));
    if (selectedPid === pid) setSelectedPid(null);
    showToast(`TerminateProcess(hProcess, 1) exécuté pour PID ${pid} (${proc.name}) sans confirmation`);
    setContextMenu(null);
  };

  // Changer la priorité d'un processus
  const changePriority = (pid: number, newPriority: string) => {
    setProcesses((prev) =>
      prev.map((p) => (p.pid === pid ? { ...p, priority: newPriority } : p))
    );
    showToast(`SetPriorityClass : Priorité de PID ${pid} changée en ${newPriority}`);
    setContextMenu(null);
  };

  const showToast = (msg: string) => {
    setNotification(msg);
    setTimeout(() => setNotification(null), 3500);
  };

  // Tri et filtrage des processus (recherche textuelle, processus Windows, exclusions par nom)
  const filteredProcesses = useMemo(() => {
    return processes
      .filter((p) => {
        if (excludedProcesses.includes(p.name)) return false;
        if (hideWindows && p.isWindowsBase) return false;
        if (!searchFilter.trim()) return true;
        const q = searchFilter.toLowerCase();
        return (
          p.name.toLowerCase().includes(q) ||
          p.pid.toString().includes(q) ||
          p.cmdLine.toLowerCase().includes(q)
        );
      })
      .sort((a, b) => {
        let valA = a[sortCol];
        let valB = b[sortCol];
        if (typeof valA === 'string') {
          return sortAsc
            ? (valA as string).localeCompare(valB as string)
            : (valB as string).localeCompare(valA as string);
        }
        return sortAsc ? (valA as number) - (valB as number) : (valB as number) - (valA as number);
      });
  }, [processes, hideWindows, excludedProcesses, searchFilter, sortCol, sortAsc]);

  const handleSort = (col: keyof SimulatedProcess) => {
    if (sortCol === col) {
      setSortAsc(!sortAsc);
    } else {
      setSortCol(col);
      setSortAsc(col === 'name' || col === 'priority' || col === 'cmdLine');
    }
  };

  // Statistiques globales calculées
  const totalCpu = useMemo(() => {
    const sum = processes.reduce((acc, p) => acc + p.cpu, 0);
    return Math.min(100.0, Number(sum.toFixed(1)));
  }, [processes]);

  const totalRamMB = useMemo(() => {
    return processes.reduce((acc, p) => acc + p.ramMB, 0);
  }, [processes]);

  const totalGpu = useMemo(() => {
    const sum = processes.reduce((acc, p) => acc + p.gpu, 0);
    return Math.min(100.0, Number(sum.toFixed(1)));
  }, [processes]);

  const ramPercent = useMemo(() => {
    return Math.min(100, Math.round((totalRamMB / 16384) * 100));
  }, [totalRamMB]);

  // Génération dynamique du contenu du fichier settings.ini
  const generatedSettingsIni = useMemo(() => {
    const colIndex = ['name', 'pid', 'cpu', 'ramMB', 'diskMB', 'netKB', 'gpu', 'priority', 'cmdLine'].indexOf(sortCol);
    let ini = `[Settings]\r\nAutoRefresh=${isAutoRefresh ? 1 : 0}\r\nHideWindowsProcesses=${hideWindows ? 1 : 0}\r\nSortColumn=${colIndex >= 0 ? colIndex : 2}\r\nSortAscending=${sortAsc ? 1 : 0}\r\n\r\n[Exclusions]\r\nCount=${excludedProcesses.length}\r\n`;
    excludedProcesses.forEach((name, i) => {
      ini += `Process${i}=${name}\r\n`;
    });
    return ini;
  }, [isAutoRefresh, hideWindows, sortCol, sortAsc, excludedProcesses]);

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col font-sans" onClick={() => { setContextMenu(null); setShowExclusionsMenu(false); }}>
      {/* Barre de navigation supérieure */}
      <header className="h-16 border-b border-slate-800 bg-slate-900/90 backdrop-blur px-6 flex items-center justify-between gap-6 shrink-0">
        <div className="flex items-center gap-3 whitespace-nowrap shrink-0">
          <div className="w-8 h-8 rounded bg-blue-600/20 border border-blue-500/40 flex items-center justify-center text-blue-400">
            <Activity className="w-4 h-4" />
          </div>
          <div className="flex flex-col">
            <span className="font-semibold text-sm tracking-tight text-slate-100">
              Windows Native Task Manager
            </span>
            <span className="text-xs text-slate-400 font-mono">
              C Win32 · MinGW-w64 · Zero Dependencies
            </span>
          </div>
        </div>

        {/* Onglets principaux */}
        <nav className="flex items-center gap-1 bg-slate-950 p-1 rounded-lg border border-slate-800">
          <button
            onClick={() => setActiveTab('source')}
            className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 flex items-center gap-1.5 ${
              activeTab === 'source'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <FileCode className="w-3.5 h-3.5" />
            taskmgr.c
          </button>
          <button
            onClick={() => setActiveTab('preview')}
            className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 flex items-center gap-1.5 ${
              activeTab === 'preview'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <Monitor className="w-3.5 h-3.5" />
            Aperçu GUI Win32
          </button>
          <button
            onClick={() => setActiveTab('settings')}
            className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 flex items-center gap-1.5 ${
              activeTab === 'settings'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <Settings className="w-3.5 h-3.5" />
            settings.ini
            {excludedProcesses.length > 0 && (
              <span className="ml-1 px-1 py-0.2 rounded-full bg-purple-500 text-[10px] text-white font-mono leading-none">
                {excludedProcesses.length}
              </span>
            )}
          </button>
          <button
            onClick={() => setActiveTab('makefile')}
            className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 flex items-center gap-1.5 ${
              activeTab === 'makefile'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <Terminal className="w-3.5 h-3.5" />
            Makefile
          </button>
          <button
            onClick={() => setActiveTab('buildbat')}
            className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 flex items-center gap-1.5 ${
              activeTab === 'buildbat'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <Zap className="w-3.5 h-3.5" />
            build.bat
          </button>
          <button
            onClick={() => setActiveTab('guide')}
            className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 flex items-center gap-1.5 ${
              activeTab === 'guide'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <ShieldCheck className="w-3.5 h-3.5" />
            Guide Compilation
          </button>
        </nav>

        {/* Boutons d'action rapides */}
        <div className="flex items-center gap-2 shrink-0">
          <button
            onClick={() => downloadFile('taskmgr.c', TASKMGR_C_SOURCE)}
            className="px-3.5 py-1.5 text-xs font-medium bg-blue-600 hover:bg-blue-500 text-white rounded transition-colors flex items-center gap-1.5 whitespace-nowrap shadow-sm cursor-pointer"
          >
            <Download className="w-3.5 h-3.5" />
            Télécharger taskmgr.c
          </button>
        </div>
      </header>

      {/* Notification Toast */}
      {notification && (
        <div className="fixed top-20 right-6 z-50 bg-slate-900 border border-blue-500/50 text-slate-200 px-4 py-2.5 rounded shadow-xl text-xs flex items-center gap-2 animate-in fade-in slide-in-from-top-2">
          <Check className="w-4 h-4 text-emerald-400 shrink-0" />
          <span className="font-mono">{notification}</span>
        </div>
      )}

      {/* Contenu principal */}
      <main className="flex-1 p-6 flex flex-col min-h-0 overflow-hidden">
        {/* VUE 1 : CODE SOURCE C */}
        {activeTab === 'source' && (
          <div className="flex-1 flex flex-col bg-slate-900 border border-slate-800 rounded-lg overflow-hidden min-h-0">
            <div className="h-12 border-b border-slate-800 bg-slate-950 px-4 flex items-center justify-between shrink-0">
              <div className="flex items-center gap-3">
                <span className="text-xs font-semibold text-slate-300 font-mono flex items-center gap-2">
                  <FileCode className="w-4 h-4 text-blue-400" />
                  taskmgr.c
                </span>
                <span className="text-xs text-slate-500 font-mono">
                  ~1000 lignes · C99/C11 · Win32 + NTAPI · settings.ini persistant
                </span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={() => copyToClipboard(TASKMGR_C_SOURCE, 'taskmgr.c')}
                  className="px-2.5 py-1 text-xs bg-slate-800 hover:bg-slate-700 text-slate-200 rounded border border-slate-700 transition-colors flex items-center gap-1.5 cursor-pointer"
                >
                  {copiedFile === 'taskmgr.c' ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  {copiedFile === 'taskmgr.c' ? 'Copié !' : 'Copier'}
                </button>
                <button
                  onClick={() => downloadFile('taskmgr.c', TASKMGR_C_SOURCE)}
                  className="px-2.5 py-1 text-xs bg-blue-600/30 hover:bg-blue-600/50 text-blue-300 border border-blue-500/40 rounded transition-colors flex items-center gap-1.5 cursor-pointer"
                >
                  <Download className="w-3.5 h-3.5" />
                  Télécharger
                </button>
              </div>
            </div>

            <div className="flex-1 overflow-auto bg-slate-950 text-slate-300 font-mono text-xs p-4 leading-relaxed select-text">
              <pre className="tab-4">
                <code>{TASKMGR_C_SOURCE}</code>
              </pre>
            </div>
          </div>
        )}

        {/* VUE 2 : APERÇU GRAPHIQUE WIN32 NATIF */}
        {activeTab === 'preview' && (
          <div className="flex-1 flex flex-col min-h-0 relative">
            {isMinimizedToTray ? (
              /* Simulation du bureau Windows lorsque l'application est réduite en Tray */
              <div
                className="flex-1 flex flex-col justify-between bg-gradient-to-br from-slate-950 via-slate-900 to-indigo-950 border border-slate-800 rounded-lg overflow-hidden p-6 shadow-2xl relative select-none"
                onClick={() => setTrayMenu(null)}
              >
                {/* Carte d'explication centrale */}
                <div className="max-w-md mx-auto my-auto bg-slate-900/90 backdrop-blur border border-slate-700 rounded-xl p-6 shadow-2xl space-y-4 text-center animate-in fade-in zoom-in-95">
                  <div className="w-16 h-16 mx-auto rounded-2xl bg-slate-950 border border-slate-700 flex items-center justify-center p-2.5 shadow-inner">
                    {/* Icône 3 couleurs animée */}
                    <div className="w-full h-full bg-[#0f172a] border border-[#1e293b] rounded p-1 flex items-end justify-between gap-1 shadow-md">
                      <div className="w-2.5 bg-slate-800 h-full rounded-sm flex items-end">
                        <div
                          style={{
                            height: `${Math.max(10, Math.min(100, totalCpu))}%`,
                            backgroundColor: totalCpu > 80 ? '#ef4444' : totalCpu > 45 ? '#f59e0b' : '#10b981'
                          }}
                          className="w-full rounded-sm transition-all duration-300"
                        ></div>
                      </div>
                      <div className="w-2.5 bg-slate-800 h-full rounded-sm flex items-end">
                        <div
                          style={{
                            height: `${Math.max(10, Math.min(100, ramPercent))}%`,
                            backgroundColor: ramPercent > 85 ? '#ef4444' : ramPercent > 65 ? '#f59e0b' : '#8b5cf6'
                          }}
                          className="w-full rounded-sm transition-all duration-300"
                        ></div>
                      </div>
                      <div className="w-2.5 bg-slate-800 h-full rounded-sm flex items-end">
                        <div
                          style={{
                            height: `${Math.max(10, Math.min(100, totalGpu))}%`,
                            backgroundColor: totalGpu > 80 ? '#ef4444' : totalGpu > 40 ? '#f59e0b' : '#06b6d4'
                          }}
                          className="w-full rounded-sm transition-all duration-300"
                        ></div>
                      </div>
                    </div>
                  </div>

                  <div>
                    <h3 className="font-semibold text-slate-100 text-sm">
                      Application réduite dans la zone de notification (System Tray)
                    </h3>
                    <p className="text-xs text-slate-400 mt-1">
                      L'icône Tray en bas à droite s'actualise chaque seconde en temps réel avec 3 couleurs :
                    </p>
                  </div>

                  <div className="grid grid-cols-3 gap-2 text-xs font-mono py-1">
                    <div
                      onDoubleClick={() => {
                        setIsMinimizedToTray(false);
                        showToast("Task Manager restauré (Double-clic)");
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        setTrayMenu({ x: e.clientX, y: e.clientY, type: 'cpu' });
                      }}
                      className="bg-slate-950 p-2.5 rounded border border-slate-800 hover:border-emerald-500/50 cursor-pointer transition-all text-center group"
                      title="Icône Tray 1 : CPU (Vert) - Double-clic pour restaurer"
                    >
                      <div className="flex items-center justify-between text-[10px] text-slate-500 uppercase pb-1 border-b border-slate-850">
                        <span>1. Tray CPU</span>
                        <span className="text-emerald-400 font-bold">{totalCpu.toFixed(1)} %</span>
                      </div>
                      <div className="h-6 bg-[#0a0f1d] border border-[#1e293b] rounded mt-1.5 p-[1px] flex items-end justify-between gap-[1px] overflow-hidden">
                        {cpuHistory.slice(-14).map((val, i) => (
                          <div key={i} className="flex-1 h-full flex items-end">
                            <div
                              style={{
                                height: `${Math.max(val > 0 ? 8 : 0, Math.min(100, val))}%`,
                                backgroundColor: val > 80 ? '#ef4444' : val > 45 ? '#f59e0b' : '#10b981'
                              }}
                              className="w-full transition-all duration-300"
                            />
                          </div>
                        ))}
                      </div>
                      <div className="text-[10px] text-slate-400 mt-1">Historique 14s (Vert)</div>
                    </div>

                    <div
                      onDoubleClick={() => {
                        setIsMinimizedToTray(false);
                        showToast("Task Manager restauré (Double-clic)");
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        setTrayMenu({ x: e.clientX, y: e.clientY, type: 'ram' });
                      }}
                      className="bg-slate-950 p-2.5 rounded border border-slate-800 hover:border-purple-500/50 cursor-pointer transition-all text-center group"
                      title="Icône Tray 2 : RAM (Violet) - Double-clic pour restaurer"
                    >
                      <div className="flex items-center justify-between text-[10px] text-slate-500 uppercase pb-1 border-b border-slate-850">
                        <span>2. Tray RAM</span>
                        <span className="text-purple-400 font-bold">{ramPercent} %</span>
                      </div>
                      <div className="h-6 bg-[#0a0f1d] border border-[#1e293b] rounded mt-1.5 p-[1px] flex items-end justify-between gap-[1px] overflow-hidden">
                        {ramHistory.slice(-14).map((val, i) => (
                          <div key={i} className="flex-1 h-full flex items-end">
                            <div
                              style={{
                                height: `${Math.max(val > 0 ? 8 : 0, Math.min(100, val))}%`,
                                backgroundColor: val > 85 ? '#ef4444' : val > 65 ? '#f59e0b' : '#8b5cf6'
                              }}
                              className="w-full transition-all duration-300"
                            />
                          </div>
                        ))}
                      </div>
                      <div className="text-[10px] text-slate-400 mt-1">Historique 14s (Violet)</div>
                    </div>

                    <div
                      onDoubleClick={() => {
                        setIsMinimizedToTray(false);
                        showToast("Task Manager restauré (Double-clic)");
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        setTrayMenu({ x: e.clientX, y: e.clientY, type: 'gpu' });
                      }}
                      className="bg-slate-950 p-2.5 rounded border border-slate-800 hover:border-cyan-500/50 cursor-pointer transition-all text-center group"
                      title="Icône Tray 3 : GPU (Cyan) - Double-clic pour restaurer"
                    >
                      <div className="flex items-center justify-between text-[10px] text-slate-500 uppercase pb-1 border-b border-slate-850">
                        <span>3. Tray GPU</span>
                        <span className="text-cyan-400 font-bold">{totalGpu.toFixed(1)} %</span>
                      </div>
                      <div className="h-6 bg-[#0a0f1d] border border-[#1e293b] rounded mt-1.5 p-[1px] flex items-end justify-between gap-[1px] overflow-hidden">
                        {gpuHistory.slice(-14).map((val, i) => (
                          <div key={i} className="flex-1 h-full flex items-end">
                            <div
                              style={{
                                height: `${Math.max(val > 0 ? 8 : 0, Math.min(100, val))}%`,
                                backgroundColor: val > 80 ? '#ef4444' : val > 40 ? '#f59e0b' : '#06b6d4'
                              }}
                              className="w-full transition-all duration-300"
                            />
                          </div>
                        ))}
                      </div>
                      <div className="text-[10px] text-slate-400 mt-1">Historique 14s (Cyan)</div>
                    </div>
                  </div>

                  <button
                    onClick={() => {
                      setIsMinimizedToTray(false);
                      showToast("Task Manager restauré");
                    }}
                    className="w-full py-2 bg-blue-600 hover:bg-blue-500 text-white rounded text-xs font-medium transition-colors shadow cursor-pointer flex items-center justify-center gap-2"
                  >
                    <span>Restaurer la fenêtre (Double-cliquez sur n'importe quel icône)</span>
                  </button>
                </div>

                {/* Simulation de la barre des tâches Windows 10/11 en bas */}
                <div className="h-12 bg-slate-900/95 border border-slate-700/80 rounded-lg px-4 flex items-center justify-between shadow-2xl">
                  <div className="flex items-center gap-3 text-xs text-slate-400 font-medium">
                    <div className="w-5 h-5 rounded bg-blue-600 flex items-center justify-center text-white text-[11px] font-bold">
                      ⊞
                    </div>
                    <span className="text-[11px] text-slate-500 hidden sm:inline">Windows Taskbar Simulation</span>
                  </div>

                  {/* Zone de notification (System Tray) avec les 3 icônes d'historique */}
                  <div className="flex items-center gap-2 bg-slate-950/80 border border-slate-800 px-3 py-1 rounded">
                    <div className="text-[11px] text-slate-400 font-mono hidden md:inline">
                      Tray Icons (3) :
                    </div>

                    {/* Icône 1 : CPU */}
                    <div
                      onDoubleClick={() => {
                        setIsMinimizedToTray(false);
                        showToast("Task Manager restauré (Double-clic CPU)");
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        setTrayMenu({ x: e.clientX, y: e.clientY, type: 'cpu' });
                      }}
                      className="flex items-center gap-1 px-1.5 py-1 rounded bg-slate-900 hover:bg-slate-800 border border-slate-700 hover:border-emerald-400 cursor-pointer transition-all shadow-inner relative group"
                      title={`Task Manager - CPU: ${totalCpu.toFixed(1)}%\nHistorique (14s) [Vert]\nDouble-cliquez pour ouvrir`}
                    >
                      <div className="w-4 h-4 bg-[#0a0f1d] border border-[#1e293b] rounded-sm p-[1px] flex items-end justify-between gap-[0.5px] overflow-hidden">
                        {cpuHistory.slice(-14).map((val, i) => (
                          <div key={i} className="flex-1 h-full flex items-end">
                            <div
                              style={{
                                height: `${Math.max(val > 0 ? 8 : 0, Math.min(100, val))}%`,
                                backgroundColor: val > 80 ? '#ef4444' : val > 45 ? '#f59e0b' : '#10b981'
                              }}
                              className="w-full transition-all duration-300"
                            />
                          </div>
                        ))}
                      </div>
                      <span className="text-[10px] font-mono text-emerald-400 font-bold">{Math.round(totalCpu)}%</span>
                    </div>

                    {/* Icône 2 : RAM */}
                    <div
                      onDoubleClick={() => {
                        setIsMinimizedToTray(false);
                        showToast("Task Manager restauré (Double-clic RAM)");
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        setTrayMenu({ x: e.clientX, y: e.clientY, type: 'ram' });
                      }}
                      className="flex items-center gap-1 px-1.5 py-1 rounded bg-slate-900 hover:bg-slate-800 border border-slate-700 hover:border-purple-400 cursor-pointer transition-all shadow-inner relative group"
                      title={`Task Manager - RAM: ${ramPercent}%\nHistorique (14s) [Violet]\nDouble-cliquez pour ouvrir`}
                    >
                      <div className="w-4 h-4 bg-[#0a0f1d] border border-[#1e293b] rounded-sm p-[1px] flex items-end justify-between gap-[0.5px] overflow-hidden">
                        {ramHistory.slice(-14).map((val, i) => (
                          <div key={i} className="flex-1 h-full flex items-end">
                            <div
                              style={{
                                height: `${Math.max(val > 0 ? 8 : 0, Math.min(100, val))}%`,
                                backgroundColor: val > 85 ? '#ef4444' : val > 65 ? '#f59e0b' : '#8b5cf6'
                              }}
                              className="w-full transition-all duration-300"
                            />
                          </div>
                        ))}
                      </div>
                      <span className="text-[10px] font-mono text-purple-400 font-bold">{ramPercent}%</span>
                    </div>

                    {/* Icône 3 : GPU */}
                    <div
                      onDoubleClick={() => {
                        setIsMinimizedToTray(false);
                        showToast("Task Manager restauré (Double-clic GPU)");
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        setTrayMenu({ x: e.clientX, y: e.clientY, type: 'gpu' });
                      }}
                      className="flex items-center gap-1 px-1.5 py-1 rounded bg-slate-900 hover:bg-slate-800 border border-slate-700 hover:border-cyan-400 cursor-pointer transition-all shadow-inner relative group"
                      title={`Task Manager - GPU: ${totalGpu.toFixed(1)}%\nHistorique (14s) [Cyan]\nDouble-cliquez pour ouvrir`}
                    >
                      <div className="w-4 h-4 bg-[#0a0f1d] border border-[#1e293b] rounded-sm p-[1px] flex items-end justify-between gap-[0.5px] overflow-hidden">
                        {gpuHistory.slice(-14).map((val, i) => (
                          <div key={i} className="flex-1 h-full flex items-end">
                            <div
                              style={{
                                height: `${Math.max(val > 0 ? 8 : 0, Math.min(100, val))}%`,
                                backgroundColor: val > 80 ? '#ef4444' : val > 40 ? '#f59e0b' : '#06b6d4'
                              }}
                              className="w-full transition-all duration-300"
                            />
                          </div>
                        ))}
                      </div>
                      <span className="text-[10px] font-mono text-cyan-400 font-bold">{Math.round(totalGpu)}%</span>
                    </div>

                    <div className="text-[11px] font-mono text-slate-300 pl-1.5 border-l border-slate-800">
                      {new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                    </div>
                  </div>
                </div>
              </div>
            ) : (
              /* Vue normale de la fenêtre du Task Manager */
              <div className="flex-1 flex flex-col bg-slate-900 border border-slate-700 rounded-lg overflow-hidden shadow-2xl min-h-0">
                {/* Barre de titre de fenêtre Windows classique */}
                <div className="h-8 bg-slate-800 border-b border-slate-700 px-3 flex items-center justify-between shrink-0 select-none">
                  <div className="flex items-center gap-2 text-xs font-medium text-slate-300">
                    <Activity className="w-3.5 h-3.5 text-blue-400" />
                    <span>Gestionnaire des tâches Windows (C Win32 Natif - SysListView32)</span>
                  </div>
                  <div className="flex items-center gap-1.5 text-xs text-slate-400">
                    <span className="w-3 h-3 rounded-full bg-slate-700 inline-block"></span>
                    <span className="w-3 h-3 rounded-full bg-slate-700 inline-block"></span>
                    {/* Bouton de fermeture qui réduit en Tray (WM_CLOSE) */}
                    <button
                      onClick={() => {
                        setIsMinimizedToTray(true);
                        showToast("Application réduite en barre des tâches (Tray Icon). Double-cliquez pour la réafficher.");
                      }}
                      title="Fermer (Réduire en Tray Icon dans la barre des tâches)"
                      className="w-3 h-3 rounded-full bg-red-600 hover:bg-red-500 inline-block cursor-pointer transition-transform hover:scale-110"
                    ></button>
                  </div>
                </div>

                {/* Barre d'outils Win32 */}
                <div className="p-3 bg-slate-850 border-b border-slate-700 flex flex-wrap items-center justify-between gap-3 shrink-0">
                  <div className="flex items-center gap-2">
                    {/* Auto-refresh toggle */}
                    <button
                      onClick={() => setIsAutoRefresh(!isAutoRefresh)}
                      className={`px-3 py-1.5 text-xs font-medium rounded border transition-colors flex items-center gap-1.5 cursor-pointer ${
                        isAutoRefresh
                          ? 'bg-blue-600/20 border-blue-500 text-blue-300 hover:bg-blue-600/30'
                          : 'bg-slate-800 border-slate-600 text-slate-400 hover:bg-slate-700'
                      }`}
                    >
                      {isAutoRefresh ? <Pause className="w-3.5 h-3.5 text-blue-400" /> : <Play className="w-3.5 h-3.5" />}
                      <span>Auto-refresh : {isAutoRefresh ? 'ACTIF (1s)' : 'PAUSE'}</span>
                    </button>

                    {/* Hide Windows toggle */}
                    <button
                      onClick={() => setHideWindows(!hideWindows)}
                      className={`px-3 py-1.5 text-xs font-medium rounded border transition-colors flex items-center gap-1.5 cursor-pointer ${
                        hideWindows
                          ? 'bg-amber-600/20 border-amber-500 text-amber-300 hover:bg-amber-600/30'
                          : 'bg-slate-800 border-slate-600 text-slate-400 hover:bg-slate-700'
                      }`}
                      title="Masque ou affiche les processus système de base Windows (IDC_BTN_HIDE_WINDOWS dans taskmgr.c)"
                    >
                      <ShieldCheck className={`w-3.5 h-3.5 ${hideWindows ? 'text-amber-400' : 'text-slate-400'}`} />
                      <span>Processus Windows : {hideWindows ? 'MASQUÉS' : 'AFFICHÉS'}</span>
                    </button>

                    {/* Bouton Gestion des exclusions */}
                    <div className="relative" onClick={(e) => e.stopPropagation()}>
                      <button
                        onClick={() => setShowExclusionsMenu(!showExclusionsMenu)}
                        className={`px-3 py-1.5 text-xs font-medium rounded border transition-colors flex items-center gap-1.5 cursor-pointer ${
                          excludedProcesses.length > 0
                            ? 'bg-purple-600/20 border-purple-500 text-purple-300 hover:bg-purple-600/30'
                            : 'bg-slate-800 border-slate-600 text-slate-400 hover:bg-slate-700'
                        }`}
                        title="Gérer les processus exclus par leur nom (enregistrés dans settings.ini)"
                      >
                        <Sliders className="w-3.5 h-3.5" />
                        <span>Exclusions ({excludedProcesses.length})</span>
                      </button>

                      {showExclusionsMenu && (
                        <div className="absolute left-0 top-full mt-1.5 w-64 bg-slate-900 border border-slate-700 shadow-2xl rounded p-2 z-50 text-xs">
                          <div className="font-semibold text-slate-300 pb-1.5 mb-1.5 border-b border-slate-800 flex justify-between items-center">
                            <span className="flex items-center gap-1.5">
                              <EyeOff className="w-3.5 h-3.5 text-purple-400" />
                              Processus masqués par nom
                            </span>
                            <button onClick={() => setShowExclusionsMenu(false)} className="text-slate-500 hover:text-slate-300 text-base leading-none">×</button>
                          </div>
                          {excludedProcesses.length === 0 ? (
                            <p className="text-slate-500 py-3 text-center italic text-[11px]">
                              Aucun processus exclu.<br />
                              Faites un clic droit sur un processus pour le masquer par son nom.
                            </p>
                          ) : (
                            <div className="space-y-1 max-h-48 overflow-auto">
                              {excludedProcesses.map((name) => (
                                <div key={name} className="flex items-center justify-between bg-slate-950 px-2 py-1.5 rounded border border-slate-800">
                                  <span className="font-mono text-slate-300 text-xs truncate">{name}</span>
                                  <button
                                    onClick={() => {
                                      setExcludedProcesses(prev => prev.filter(x => x !== name));
                                      showToast(`Processus "${name}" réaffiché`);
                                    }}
                                    className="text-[11px] text-blue-400 hover:text-blue-300 hover:underline shrink-0 ml-2 cursor-pointer"
                                  >
                                    Réafficher
                                  </button>
                                </div>
                              ))}
                              <button
                                onClick={() => {
                                  setExcludedProcesses([]);
                                  setShowExclusionsMenu(false);
                                  showToast("Toutes les exclusions ont été réinitialisées");
                                }}
                                className="w-full mt-2 py-1 text-center bg-slate-800 hover:bg-slate-700 text-amber-400 rounded text-[11px] cursor-pointer"
                              >
                                Tout réafficher (effacer exclusions)
                              </button>
                            </div>
                          )}
                        </div>
                      )}
                    </div>

                    {/* Actualiser */}
                    <button
                      onClick={() => showToast('Actualisation manuelle forcée (F5)')}
                      className="px-3 py-1.5 text-xs font-medium bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-600 rounded transition-colors flex items-center gap-1.5 cursor-pointer"
                    >
                      <RotateCw className="w-3.5 h-3.5" />
                      Actualiser (F5)
                    </button>

                    {/* Tuer immédiatement sans confirmation */}
                    <button
                      onClick={() => selectedPid && killProcessImmediately(selectedPid)}
                      disabled={!selectedPid}
                      className="px-3 py-1.5 text-xs font-medium bg-red-600 hover:bg-red-500 disabled:opacity-40 disabled:hover:bg-red-600 text-white rounded transition-colors flex items-center gap-1.5 cursor-pointer shadow-sm"
                      title="Termine le processus via TerminateProcess(hProcess, 1) sans aucune confirmation"
                    >
                      <Trash2 className="w-3.5 h-3.5" />
                      Tuer Immédiatement (Suppr)
                    </button>
                  </div>

                  {/* Réduire en Tray Icon */}
                  <div className="flex items-center gap-2">
                    <button
                      onClick={() => {
                        setIsMinimizedToTray(true);
                        showToast("Application réduite en barre des tâches (Tray Icon). Double-cliquez pour la réafficher.");
                      }}
                      className="px-2.5 py-1 text-xs font-medium bg-slate-800 hover:bg-slate-700 text-slate-300 border border-slate-700 rounded transition-colors flex items-center gap-1 cursor-pointer"
                      title="Réduit l'application dans la zone de notification (System Tray)"
                    >
                      <span>Réduire en Tray</span>
                    </button>

                    {/* Champ de recherche */}
                    <div className="relative">
                      <input
                        type="text"
                        value={searchFilter}
                        onChange={(e) => setSearchFilter(e.target.value)}
                        placeholder="Nom ou PID..."
                        className="w-44 bg-slate-950 border border-slate-700 rounded px-2.5 py-1 text-xs text-slate-200 placeholder:text-slate-500 focus:outline-none focus:border-blue-500 font-mono"
                      />
                      {searchFilter && (
                        <button
                          onClick={() => setSearchFilter('')}
                          className="absolute right-2 top-1.5 text-slate-500 hover:text-slate-300 text-xs"
                        >
                          ×
                        </button>
                      )}
                    </div>
                  </div>
                </div>

                {/* Tableau plat de processus (SysListView32 LVS_REPORT) */}
                <div className="flex-1 overflow-auto bg-slate-950 min-h-0 select-none">
                  <table className="w-full text-left border-collapse text-xs">
                    <thead className="bg-slate-900 border-b border-slate-700 sticky top-0 z-10 text-slate-300 select-none">
                      <tr>
                        <th
                          onClick={() => handleSort('name')}
                          className={`py-2 px-3 font-semibold hover:bg-slate-800 cursor-pointer w-48 whitespace-nowrap transition-colors ${
                            sortCol === 'name' ? 'text-blue-400 bg-slate-800/80 font-bold' : ''
                          }`}
                        >
                          <div className="flex items-center gap-1.5">
                            <span>Nom de l'image</span>
                            {sortCol === 'name' ? (
                              <span className="text-xs text-blue-300 bg-blue-500/30 px-1 py-0.5 rounded leading-none font-bold">
                                {sortAsc ? '▲' : '▼'}
                              </span>
                            ) : (
                              <span className="text-[10px] text-slate-600">▲▼</span>
                            )}
                          </div>
                        </th>
                        <th
                          onClick={() => handleSort('pid')}
                          className={`py-2 px-3 font-semibold text-right hover:bg-slate-800 cursor-pointer w-24 whitespace-nowrap font-mono transition-colors ${
                            sortCol === 'pid' ? 'text-blue-400 bg-slate-800/80 font-bold' : ''
                          }`}
                        >
                          <div className="flex items-center justify-end gap-1.5">
                            <span>PID</span>
                            {sortCol === 'pid' ? (
                              <span className="text-xs text-blue-300 bg-blue-500/30 px-1 py-0.5 rounded leading-none font-bold">
                                {sortAsc ? '▲' : '▼'}
                              </span>
                            ) : (
                              <span className="text-[10px] text-slate-600">▲▼</span>
                            )}
                          </div>
                        </th>
                        <th
                          onClick={() => handleSort('cpu')}
                          className={`py-2 px-3 font-semibold text-right hover:bg-slate-800 cursor-pointer w-24 whitespace-nowrap font-mono transition-colors ${
                            sortCol === 'cpu' ? 'text-blue-400 bg-slate-800/80 font-bold' : ''
                          }`}
                        >
                          <div className="flex items-center justify-end gap-1.5">
                            <span>CPU %</span>
                            {sortCol === 'cpu' ? (
                              <span className="text-xs text-blue-300 bg-blue-500/30 px-1 py-0.5 rounded leading-none font-bold">
                                {sortAsc ? '▲' : '▼'}
                              </span>
                            ) : (
                              <span className="text-[10px] text-slate-600">▲▼</span>
                            )}
                          </div>
                        </th>
                        <th
                          onClick={() => handleSort('ramMB')}
                          className={`py-2 px-3 font-semibold text-right hover:bg-slate-800 cursor-pointer w-28 whitespace-nowrap font-mono transition-colors ${
                            sortCol === 'ramMB' ? 'text-blue-400 bg-slate-800/80 font-bold' : ''
                          }`}
                        >
                          <div className="flex items-center justify-end gap-1.5">
                            <span>Mémoire</span>
                            {sortCol === 'ramMB' ? (
                              <span className="text-xs text-blue-300 bg-blue-500/30 px-1 py-0.5 rounded leading-none font-bold">
                                {sortAsc ? '▲' : '▼'}
                              </span>
                            ) : (
                              <span className="text-[10px] text-slate-600">▲▼</span>
                            )}
                          </div>
                        </th>
                        <th
                          onClick={() => handleSort('diskMB')}
                          className={`py-2 px-3 font-semibold text-right hover:bg-slate-800 cursor-pointer w-28 whitespace-nowrap font-mono transition-colors ${
                            sortCol === 'diskMB' ? 'text-blue-400 bg-slate-800/80 font-bold' : ''
                          }`}
                        >
                          <div className="flex items-center justify-end gap-1.5">
                            <span>Disque</span>
                            {sortCol === 'diskMB' ? (
                              <span className="text-xs text-blue-300 bg-blue-500/30 px-1 py-0.5 rounded leading-none font-bold">
                                {sortAsc ? '▲' : '▼'}
                              </span>
                            ) : (
                              <span className="text-[10px] text-slate-600">▲▼</span>
                            )}
                          </div>
                        </th>
                        <th
                          onClick={() => handleSort('netKB')}
                          className={`py-2 px-3 font-semibold text-right hover:bg-slate-800 cursor-pointer w-28 whitespace-nowrap font-mono transition-colors ${
                            sortCol === 'netKB' ? 'text-blue-400 bg-slate-800/80 font-bold' : ''
                          }`}
                        >
                          <div className="flex items-center justify-end gap-1.5">
                            <span>Réseau</span>
                            {sortCol === 'netKB' ? (
                              <span className="text-xs text-blue-300 bg-blue-500/30 px-1 py-0.5 rounded leading-none font-bold">
                                {sortAsc ? '▲' : '▼'}
                              </span>
                            ) : (
                              <span className="text-[10px] text-slate-600">▲▼</span>
                            )}
                          </div>
                        </th>
                        <th
                          onClick={() => handleSort('gpu')}
                          className={`py-2 px-3 font-semibold text-right hover:bg-slate-800 cursor-pointer w-24 whitespace-nowrap font-mono transition-colors ${
                            sortCol === 'gpu' ? 'text-blue-400 bg-slate-800/80 font-bold' : ''
                          }`}
                        >
                          <div className="flex items-center justify-end gap-1.5">
                            <span>GPU %</span>
                            {sortCol === 'gpu' ? (
                              <span className="text-xs text-blue-300 bg-blue-500/30 px-1 py-0.5 rounded leading-none font-bold">
                                {sortAsc ? '▲' : '▼'}
                              </span>
                            ) : (
                              <span className="text-[10px] text-slate-600">▲▼</span>
                            )}
                          </div>
                        </th>
                        <th
                          onClick={() => handleSort('priority')}
                          className={`py-2 px-3 font-semibold hover:bg-slate-800 cursor-pointer w-28 whitespace-nowrap transition-colors ${
                            sortCol === 'priority' ? 'text-blue-400 bg-slate-800/80 font-bold' : ''
                          }`}
                        >
                          <div className="flex items-center gap-1.5">
                            <span>Priorité</span>
                            {sortCol === 'priority' ? (
                              <span className="text-xs text-blue-300 bg-blue-500/30 px-1 py-0.5 rounded leading-none font-bold">
                                {sortAsc ? '▲' : '▼'}
                              </span>
                            ) : (
                              <span className="text-[10px] text-slate-600">▲▼</span>
                            )}
                          </div>
                        </th>
                        <th className="py-2 px-3 font-semibold whitespace-nowrap">
                          Ligne de commande (CLI Arguments)
                        </th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-slate-850">
                      {filteredProcesses.map((proc) => {
                        const isSelected = selectedPid === proc.pid;
                        return (
                          <tr
                            key={proc.pid}
                            onClick={() => setSelectedPid(proc.pid)}
                            onContextMenu={(e) => {
                              e.preventDefault();
                              setSelectedPid(proc.pid);
                              setContextMenu({ x: e.clientX, y: e.clientY, pid: proc.pid });
                            }}
                            className={`transition-colors cursor-pointer ${
                              isSelected
                                ? 'bg-blue-600/30 text-white font-medium'
                                : 'hover:bg-slate-900/80 text-slate-300'
                            }`}
                          >
                            <td className="py-1.5 px-3 whitespace-nowrap font-medium flex items-center gap-1.5">
                              <Activity className={`w-3.5 h-3.5 ${proc.isSystem ? 'text-amber-400' : 'text-blue-400'}`} />
                              <span>{proc.name}</span>
                              {proc.isWindowsBase && (
                                <span className="text-[10px] px-1 py-0.2 rounded bg-slate-800 text-slate-400 border border-slate-700">Win</span>
                              )}
                            </td>
                            <td className="py-1.5 px-3 whitespace-nowrap text-right font-mono tabular-nums text-slate-400">
                              {proc.pid}
                            </td>
                            <td className="py-1.5 px-3 whitespace-nowrap text-right font-mono tabular-nums">
                              <span
                                className={
                                  proc.cpu > 10
                                    ? 'text-amber-400 font-semibold'
                                    : proc.cpu > 0
                                    ? 'text-slate-200'
                                    : 'text-slate-500'
                                }
                              >
                                {proc.cpu.toFixed(1)} %
                              </span>
                            </td>
                            <td className="py-1.5 px-3 whitespace-nowrap text-right font-mono tabular-nums text-slate-300">
                              {proc.ramMB} Mo
                            </td>
                            <td className="py-1.5 px-3 whitespace-nowrap text-right font-mono tabular-nums text-slate-400">
                              {proc.diskMB.toFixed(2)} Mo/s
                            </td>
                            <td className="py-1.5 px-3 whitespace-nowrap text-right font-mono tabular-nums text-slate-400">
                              {proc.netKB.toFixed(1)} Ko/s
                            </td>
                            <td className="py-1.5 px-3 whitespace-nowrap text-right font-mono tabular-nums text-slate-400">
                              {proc.gpu > 0 ? `${proc.gpu.toFixed(1)} %` : '0.0 %'}
                            </td>
                            <td className="py-1.5 px-3 whitespace-nowrap">
                              <span className="text-xs text-slate-400">{proc.priority}</span>
                            </td>
                            <td className="py-1.5 px-3 text-slate-400 font-mono text-[11px] truncate max-w-md">
                              {proc.cmdLine}
                            </td>
                          </tr>
                        );
                      })}
                      {filteredProcesses.length === 0 && (
                        <tr>
                          <td colSpan={9} className="py-8 text-center text-slate-500">
                            {hideWindows && processes.every(p => p.isWindowsBase)
                              ? "Tous les processus sont masqués par le filtre Windows"
                              : excludedProcesses.length > 0 && processes.every(p => excludedProcesses.includes(p.name))
                              ? "Tous les processus sont actuellement masqués par les exclusions par nom"
                              : `Aucun processus ne correspond au filtre "${searchFilter}"`}
                          </td>
                        </tr>
                      )}
                    </tbody>
                  </table>
                </div>

                {/* Menu contextuel clic droit */}
                {contextMenu && (
                  <div
                    style={{ top: contextMenu.y - 40, left: contextMenu.x }}
                    className="fixed z-50 bg-slate-900 border border-slate-700 shadow-2xl rounded py-1 min-w-[220px] text-xs text-slate-200"
                    onClick={(e) => e.stopPropagation()}
                  >
                    <button
                      onClick={() => killProcessImmediately(contextMenu.pid)}
                      className="w-full text-left px-3 py-1.5 hover:bg-red-600 hover:text-white flex items-center justify-between cursor-pointer"
                    >
                      <span className="font-semibold text-red-400 hover:text-white">Tuer immédiatement</span>
                      <span className="text-[10px] opacity-70">Suppr</span>
                    </button>
                    <div className="h-px bg-slate-700 my-1"></div>
                    <div className="px-3 py-1 text-[10px] text-slate-400 uppercase font-semibold">
                      Définir la priorité
                    </div>
                    {['Temps réel', 'Haute', 'Supérieure', 'Normale', 'Inférieure', 'Basse'].map((prio) => (
                      <button
                        key={prio}
                        onClick={() => changePriority(contextMenu.pid, prio)}
                        className="w-full text-left px-4 py-1 hover:bg-blue-600 hover:text-white cursor-pointer"
                      >
                        {prio}
                      </button>
                    ))}
                    <div className="h-px bg-slate-700 my-1"></div>
                    <button
                      onClick={() => {
                        const proc = processes.find((p) => p.pid === contextMenu.pid);
                        if (proc) {
                          navigator.clipboard.writeText(proc.cmdLine);
                          showToast('Ligne de commande copiée dans le presse-papier');
                        }
                        setContextMenu(null);
                      }}
                      className="w-full text-left px-3 py-1.5 hover:bg-slate-800 cursor-pointer"
                    >
                      Copier les CLI Args
                    </button>
                    <div className="h-px bg-slate-700 my-1"></div>
                    <button
                      onClick={() => {
                        const proc = processes.find((p) => p.pid === contextMenu.pid);
                        if (proc) {
                          if (!excludedProcesses.includes(proc.name)) {
                            setExcludedProcesses(prev => [...prev, proc.name]);
                            showToast(`Processus "${proc.name}" masqué par son nom (sauvegardé dans settings.ini)`);
                          }
                        }
                        setContextMenu(null);
                      }}
                      className="w-full text-left px-3 py-1.5 hover:bg-purple-600 hover:text-white cursor-pointer flex items-center justify-between text-purple-300"
                    >
                      <span className="flex items-center gap-1.5">
                        <EyeOff className="w-3.5 h-3.5" />
                        Masquer par son nom
                      </span>
                      <span className="text-[10px] opacity-70 font-mono">settings.ini</span>
                    </button>
                  </div>
                )}

                {/* Barre d'état StatusBar Win32 */}
                <div className="h-8 bg-slate-900 border-t border-slate-700 px-4 flex items-center justify-between text-xs text-slate-400 font-mono shrink-0 select-none">
                  <div className="flex items-center gap-5">
                    <span>
                      Processus : {filteredProcesses.length} / {processes.length}
                      {hideWindows && ` (${processes.filter(p => p.isWindowsBase).length} Win)`}
                      {excludedProcesses.length > 0 && ` (${excludedProcesses.length} excl)`}
                    </span>
                    <span>CPU : {totalCpu} %</span>
                    <span>RAM : {(totalRamMB / 1024).toFixed(1)} Go</span>
                    <span className="text-blue-400 font-semibold">
                      Tri : {sortCol.toUpperCase()} {sortAsc ? '▲' : '▼'}
                    </span>
                  </div>

                  {/* Aperçu direct des 3 icônes Tray (CPU, RAM, GPU) avec historique */}
                  <div className="flex items-center gap-2">
                    <span className="text-[11px] text-slate-500 hidden sm:inline">
                      Fermer = 3 Tray Icons :
                    </span>
                    <div
                      onClick={() => {
                        setIsMinimizedToTray(true);
                        showToast("Application réduite en barre des tâches (3 Tray Icons avec historique)");
                      }}
                      className="flex items-center gap-1.5 px-2 py-0.5 rounded bg-slate-950 border border-slate-800 hover:border-blue-500 cursor-pointer"
                      title="Cliquez pour tester la réduction en System Tray (3 icônes d'historique distinctes)"
                    >
                      {/* 1. CPU */}
                      <div className="flex items-center gap-1" title={`CPU: ${totalCpu.toFixed(1)}%`}>
                        <div className="w-3.5 h-3.5 bg-[#0a0f1d] border border-[#1e293b] rounded-sm p-[1px] flex items-end justify-between gap-[0.5px]">
                          {cpuHistory.slice(-8).map((val, i) => (
                            <div key={i} className="flex-1 h-full flex items-end">
                              <div
                                style={{
                                  height: `${Math.max(val > 0 ? 10 : 0, Math.min(100, val))}%`,
                                  backgroundColor: val > 80 ? '#ef4444' : val > 45 ? '#f59e0b' : '#10b981'
                                }}
                                className="w-full"
                              />
                            </div>
                          ))}
                        </div>
                        <span className="text-[9px] text-emerald-400 font-bold">{Math.round(totalCpu)}%</span>
                      </div>

                      {/* 2. RAM */}
                      <div className="flex items-center gap-1 pl-1 border-l border-slate-800" title={`RAM: ${ramPercent}%`}>
                        <div className="w-3.5 h-3.5 bg-[#0a0f1d] border border-[#1e293b] rounded-sm p-[1px] flex items-end justify-between gap-[0.5px]">
                          {ramHistory.slice(-8).map((val, i) => (
                            <div key={i} className="flex-1 h-full flex items-end">
                              <div
                                style={{
                                  height: `${Math.max(val > 0 ? 10 : 0, Math.min(100, val))}%`,
                                  backgroundColor: val > 85 ? '#ef4444' : val > 65 ? '#f59e0b' : '#8b5cf6'
                                }}
                                className="w-full"
                              />
                            </div>
                          ))}
                        </div>
                        <span className="text-[9px] text-purple-400 font-bold">{ramPercent}%</span>
                      </div>

                      {/* 3. GPU */}
                      <div className="flex items-center gap-1 pl-1 border-l border-slate-800" title={`GPU: ${totalGpu.toFixed(1)}%`}>
                        <div className="w-3.5 h-3.5 bg-[#0a0f1d] border border-[#1e293b] rounded-sm p-[1px] flex items-end justify-between gap-[0.5px]">
                          {gpuHistory.slice(-8).map((val, i) => (
                            <div key={i} className="flex-1 h-full flex items-end">
                              <div
                                style={{
                                  height: `${Math.max(val > 0 ? 10 : 0, Math.min(100, val))}%`,
                                  backgroundColor: val > 80 ? '#ef4444' : val > 40 ? '#f59e0b' : '#06b6d4'
                                }}
                                className="w-full"
                              />
                            </div>
                          ))}
                        </div>
                        <span className="text-[9px] text-cyan-400 font-bold">{Math.round(totalGpu)}%</span>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            )}

            {/* Menu contextuel clic droit du System Tray */}
            {trayMenu && (
              <div
                style={{ top: Math.max(10, trayMenu.y - 80), left: Math.max(10, trayMenu.x - 90) }}
                className="fixed z-50 bg-slate-900 border border-slate-700 shadow-2xl rounded py-1 min-w-[210px] text-xs text-slate-200 select-none"
                onClick={(e) => e.stopPropagation()}
              >
                <div className="px-3 py-1 text-[11px] font-mono text-slate-300 font-bold border-b border-slate-800 flex items-center justify-between">
                  <span>
                    {trayMenu.type === 'cpu'
                      ? `Historique CPU : ${totalCpu.toFixed(1)} %`
                      : trayMenu.type === 'ram'
                      ? `Historique RAM : ${ramPercent} % (${(totalRamMB / 1024).toFixed(1)} Go)`
                      : `Historique GPU : ${totalGpu.toFixed(1)} %`}
                  </span>
                </div>
                <button
                  onClick={() => {
                    setIsMinimizedToTray(false);
                    setTrayMenu(null);
                    showToast("Task Manager restauré");
                  }}
                  className="w-full text-left px-3 py-1.5 hover:bg-blue-600 hover:text-white font-semibold flex items-center justify-between cursor-pointer"
                >
                  <span>Ouvrir le Task Manager</span>
                  <span className="text-[10px] text-slate-400">Défaut</span>
                </button>
                <div className="h-px bg-slate-700 my-1"></div>
                <button
                  onClick={() => {
                    setTrayMenu(null);
                    showToast("Fermeture complète de l'application");
                  }}
                  className="w-full text-left px-3 py-1.5 hover:bg-red-600 hover:text-white text-red-400 hover:text-white cursor-pointer"
                >
                  Quitter définitivement
                </button>
              </div>
            )}
          </div>
        )}

        {/* VUE 3 : SETTINGS.INI */}
        {activeTab === 'settings' && (
          <div className="flex-1 flex flex-col bg-slate-900 border border-slate-800 rounded-lg overflow-hidden min-h-0">
            <div className="h-12 border-b border-slate-800 bg-slate-950 px-4 flex items-center justify-between shrink-0">
              <div className="flex items-center gap-3">
                <span className="text-xs font-semibold text-slate-300 font-mono flex items-center gap-2">
                  <Settings className="w-4 h-4 text-purple-400" />
                  settings.ini (Fichier de configuration automatique)
                </span>
                <span className="text-xs text-slate-500 font-mono">
                  Chargé au démarrage et sauvegardé en temps réel dans le dossier de l'exécutable
                </span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={() => copyToClipboard(generatedSettingsIni, 'settings.ini')}
                  className="px-2.5 py-1 text-xs bg-slate-800 hover:bg-slate-700 text-slate-200 rounded border border-slate-700 transition-colors flex items-center gap-1.5 cursor-pointer"
                >
                  {copiedFile === 'settings.ini' ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  {copiedFile === 'settings.ini' ? 'Copié !' : 'Copier'}
                </button>
                <button
                  onClick={() => downloadFile('settings.ini', generatedSettingsIni)}
                  className="px-2.5 py-1 text-xs bg-purple-600/30 hover:bg-purple-600/50 text-purple-300 border border-purple-500/40 rounded transition-colors flex items-center gap-1.5 cursor-pointer"
                >
                  <Download className="w-3.5 h-3.5" />
                  Télécharger settings.ini
                </button>
              </div>
            </div>

            <div className="p-4 bg-slate-900 border-b border-slate-800 text-xs text-slate-400 space-y-1">
              <p>
                Ce fichier est automatiquement géré par <code className="text-slate-200 font-mono">taskmgr.exe</code> via les API Win32 <code className="text-purple-300 font-mono">GetPrivateProfileStringW</code> et <code className="text-purple-300 font-mono">WritePrivateProfileStringW</code>.
              </p>
              <p>
                Vous pouvez également l'éditer manuellement dans le Bloc-notes pour ajouter des processus à exclure par défaut.
              </p>
            </div>

            <div className="flex-1 overflow-auto bg-slate-950 text-slate-300 font-mono text-xs p-4 leading-relaxed select-text">
              <pre>
                <code>{generatedSettingsIni}</code>
              </pre>
            </div>
          </div>
        )}

        {/* VUE 4 : MAKEFILE */}
        {activeTab === 'makefile' && (
          <div className="flex-1 flex flex-col bg-slate-900 border border-slate-800 rounded-lg overflow-hidden min-h-0">
            <div className="h-12 border-b border-slate-800 bg-slate-950 px-4 flex items-center justify-between shrink-0">
              <div className="flex items-center gap-3">
                <span className="text-xs font-semibold text-slate-300 font-mono flex items-center gap-2">
                  <Terminal className="w-4 h-4 text-emerald-400" />
                  Makefile
                </span>
                <span className="text-xs text-slate-500 font-mono">
                  Pour mingw32-make ou make Linux cross-compilation
                </span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={() => copyToClipboard(MAKEFILE_SOURCE, 'Makefile')}
                  className="px-2.5 py-1 text-xs bg-slate-800 hover:bg-slate-700 text-slate-200 rounded border border-slate-700 transition-colors flex items-center gap-1.5 cursor-pointer"
                >
                  {copiedFile === 'Makefile' ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  {copiedFile === 'Makefile' ? 'Copié !' : 'Copier'}
                </button>
                <button
                  onClick={() => downloadFile('Makefile', MAKEFILE_SOURCE)}
                  className="px-2.5 py-1 text-xs bg-blue-600/30 hover:bg-blue-600/50 text-blue-300 border border-blue-500/40 rounded transition-colors flex items-center gap-1.5 cursor-pointer"
                >
                  <Download className="w-3.5 h-3.5" />
                  Télécharger
                </button>
              </div>
            </div>
            <div className="flex-1 overflow-auto bg-slate-950 text-slate-300 font-mono text-xs p-4 leading-relaxed select-text">
              <pre>
                <code>{MAKEFILE_SOURCE}</code>
              </pre>
            </div>
          </div>
        )}

        {/* VUE 5 : BUILD.BAT */}
        {activeTab === 'buildbat' && (
          <div className="flex-1 flex flex-col bg-slate-900 border border-slate-800 rounded-lg overflow-hidden min-h-0">
            <div className="h-12 border-b border-slate-800 bg-slate-950 px-4 flex items-center justify-between shrink-0">
              <div className="flex items-center gap-3">
                <span className="text-xs font-semibold text-slate-300 font-mono flex items-center gap-2">
                  <Zap className="w-4 h-4 text-amber-400" />
                  build.bat
                </span>
                <span className="text-xs text-slate-500 font-mono">
                  Script batch en un clic pour compiler et lancer taskmgr.exe sous Windows
                </span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={() => copyToClipboard(BUILDBAT_SOURCE, 'build.bat')}
                  className="px-2.5 py-1 text-xs bg-slate-800 hover:bg-slate-700 text-slate-200 rounded border border-slate-700 transition-colors flex items-center gap-1.5 cursor-pointer"
                >
                  {copiedFile === 'build.bat' ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  {copiedFile === 'build.bat' ? 'Copié !' : 'Copier'}
                </button>
                <button
                  onClick={() => downloadFile('build.bat', BUILDBAT_SOURCE)}
                  className="px-2.5 py-1 text-xs bg-blue-600/30 hover:bg-blue-600/50 text-blue-300 border border-blue-500/40 rounded transition-colors flex items-center gap-1.5 cursor-pointer"
                >
                  <Download className="w-3.5 h-3.5" />
                  Télécharger
                </button>
              </div>
            </div>
            <div className="flex-1 overflow-auto bg-slate-950 text-slate-300 font-mono text-xs p-4 leading-relaxed select-text">
              <pre>
                <code>{BUILDBAT_SOURCE}</code>
              </pre>
            </div>
          </div>
        )}

        {/* VUE 6 : GUIDE DE COMPILATION */}
        {activeTab === 'guide' && (
          <div className="flex-1 flex flex-col bg-slate-900 border border-slate-800 rounded-lg overflow-auto p-6 space-y-6">
            <div>
              <h2 className="text-lg font-semibold text-slate-100 mb-1">
                Guide de Compilation et Exécution avec MinGW-w64
              </h2>
              <p className="text-xs text-slate-400">
                Instructions détaillées pour compiler sans dépendances externes sous Windows 10/11.
              </p>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <div className="bg-slate-950 p-4 rounded border border-slate-800 space-y-2">
                <div className="text-xs font-semibold text-blue-400 font-mono">1. Commande de compilation directe</div>
                <div className="bg-slate-900 p-2.5 rounded font-mono text-xs text-slate-200 border border-slate-700 select-all overflow-x-auto">
                  gcc -mwindows -O2 -Wall -Wextra -o taskmgr.exe taskmgr.c -lcomctl32 -lpsapi -lntdll -lole32 -lshell32
                </div>
                <ul className="text-xs text-slate-400 space-y-1 pt-1 list-disc list-inside">
                  <li><code className="text-slate-300">-mwindows</code> : Supprime la fenêtre de console CMD noire.</li>
                  <li><code className="text-slate-300">-lcomctl32</code> : Contrôle de liste natif SysListView32.</li>
                  <li><code className="text-slate-300">-lpsapi</code> : GetProcessMemoryInfo pour la mémoire vive.</li>
                  <li><code className="text-slate-300">-lntdll</code> : NtQueryInformationProcess pour les arguments CLI.</li>
                </ul>
              </div>

              <div className="bg-slate-950 p-4 rounded border border-slate-800 space-y-2">
                <div className="text-xs font-semibold text-emerald-400 font-mono">2. Avec le script build.bat fourni</div>
                <p className="text-xs text-slate-400">
                  Téléchargez <code className="text-slate-200">taskmgr.c</code> et <code className="text-slate-200">build.bat</code> dans le même dossier, puis double-cliquez sur <code className="text-slate-200">build.bat</code>.
                </p>
                <div className="bg-slate-900 p-2.5 rounded font-mono text-xs text-slate-300 border border-slate-700">
                  build.bat
                </div>
              </div>
            </div>

            <div className="bg-slate-950 p-4 rounded border border-slate-800 space-y-3">
              <div className="text-xs font-semibold text-purple-400 font-mono">3. Réglages et persistance settings.ini</div>
              <p className="text-xs text-slate-300">
                Au démarrage, <code className="text-slate-200">taskmgr.exe</code> recherche automatiquement <code className="text-purple-300">settings.ini</code> dans son dossier d'exécution :
              </p>
              <ul className="text-xs text-slate-400 space-y-1 list-disc list-inside">
                <li><strong className="text-slate-200">AutoRefresh</strong> : 1 pour actif (1s), 0 pour pause.</li>
                <li><strong className="text-slate-200">HideWindowsProcesses</strong> : 1 pour masquer automatiquement les processus Windows de base.</li>
                <li><strong className="text-slate-200">SortColumn & SortAscending</strong> : Mémorise la colonne triée et son ordre.</li>
                <li><strong className="text-slate-200">Exclusions</strong> : Liste des processus masqués par leur nom (<code className="text-slate-300">Process0=discord.exe</code>, etc.).</li>
              </ul>
            </div>
          </div>
        )}
      </main>
    </div>
  );
}
