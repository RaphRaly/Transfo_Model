'use client';

import { useEffect, useRef } from 'react';
import Image from 'next/image';

/* ------------------------------------------------------------------ */
/*  IntersectionObserver hook for scroll-reveal animations             */
/* ------------------------------------------------------------------ */
function useRevealObserver(containerRef: React.RefObject<HTMLElement | null>) {
  useEffect(() => {
    const root = containerRef.current;
    if (!root) return;

    const targets = root.querySelectorAll('.reveal, .reveal-left, .reveal-right');

    const io = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            entry.target.classList.add('visible');
            io.unobserve(entry.target);
          }
        });
      },
      { threshold: 0.15, rootMargin: '0px 0px -40px 0px' },
    );

    targets.forEach((t) => io.observe(t));
    return () => io.disconnect();
  }, [containerRef]);
}

/* ------------------------------------------------------------------ */
/*  Thin section divider                                               */
/* ------------------------------------------------------------------ */
function Divider() {
  return (
    <div className="mx-auto max-w-xs px-6">
      <div className="h-px w-full bg-border-subtle/30" />
    </div>
  );
}

/* ------------------------------------------------------------------ */
/*  Shared spec / bullet list                                          */
/* ------------------------------------------------------------------ */
function BulletList({
  items,
  accent = 'cyan',
}: {
  items: string[];
  accent?: 'cyan' | 'orange' | 'violet';
}) {
  const dotColor = {
    cyan: 'bg-accent-cyan',
    orange: 'bg-accent-orange',
    violet: 'bg-accent-violet',
  }[accent];

  return (
    <ul className="mt-6 space-y-3">
      {items.map((item) => (
        <li key={item} className="flex items-start gap-3 text-text-muted text-sm leading-relaxed">
          <span
            className={`mt-1.5 h-2 w-2 shrink-0 rounded-full ${dotColor}`}
            aria-hidden="true"
          />
          {item}
        </li>
      ))}
    </ul>
  );
}

/* ------------------------------------------------------------------ */
/*  Section 1 -- HysteriCore Engine                                    */
/* ------------------------------------------------------------------ */
function BHCurveVisual() {
  return (
    <div className="relative mx-auto w-full max-w-md aspect-square">
      {/* Outer glow card */}
      <div className="absolute inset-0 rounded-2xl bg-bg-surface border border-border-subtle shadow-[0_0_60px_rgba(6,182,212,0.08)] overflow-hidden">
        {/* Grid pattern */}
        <svg
          className="absolute inset-0 h-full w-full opacity-[0.07]"
          xmlns="http://www.w3.org/2000/svg"
          aria-hidden="true"
        >
          <defs>
            <pattern id="bhGrid" width="32" height="32" patternUnits="userSpaceOnUse">
              <path d="M 32 0 L 0 0 0 32" fill="none" stroke="#06B6D4" strokeWidth="0.5" />
            </pattern>
          </defs>
          <rect width="100%" height="100%" fill="url(#bhGrid)" />
        </svg>

        {/* Axis lines */}
        <div className="absolute left-1/2 top-4 bottom-4 w-px bg-border-accent opacity-40" />
        <div className="absolute top-1/2 left-4 right-4 h-px bg-border-accent opacity-40" />

        {/* Axis labels */}
        <span className="absolute top-3 left-1/2 -translate-x-1/2 text-[10px] font-medium tracking-widest text-accent-cyan/60 uppercase">
          B
        </span>
        <span className="absolute bottom-2 right-3 text-[10px] font-medium tracking-widest text-accent-cyan/60 uppercase">
          H
        </span>

        {/* B-H hysteresis loop -- stylized SVG */}
        <svg
          viewBox="0 0 200 200"
          className="absolute inset-0 h-full w-full p-8"
          xmlns="http://www.w3.org/2000/svg"
          aria-label="B-H hysteresis curve"
        >
          <defs>
            <filter id="bhGlow">
              <feGaussianBlur stdDeviation="3" result="blur" />
              <feMerge>
                <feMergeNode in="blur" />
                <feMergeNode in="SourceGraphic" />
              </feMerge>
            </filter>
            <linearGradient id="bhGrad" x1="0%" y1="0%" x2="100%" y2="100%">
              <stop offset="0%" stopColor="#06B6D4" />
              <stop offset="100%" stopColor="#A855F7" />
            </linearGradient>
          </defs>
          {/* Upper branch */}
          <path
            d="M 20,100 C 40,100 55,92 70,72 C 85,52 95,28 120,20 C 145,12 165,18 180,30 C 180,30 180,100 180,100"
            fill="none"
            stroke="url(#bhGrad)"
            strokeWidth="2"
            filter="url(#bhGlow)"
            opacity="0.9"
          />
          {/* Lower branch */}
          <path
            d="M 180,100 C 160,100 145,108 130,128 C 115,148 105,172 80,180 C 55,188 35,182 20,170 C 20,170 20,100 20,100"
            fill="none"
            stroke="url(#bhGrad)"
            strokeWidth="2"
            filter="url(#bhGlow)"
            opacity="0.9"
          />
          {/* Filled area for subtle indication */}
          <path
            d="M 20,100 C 40,100 55,92 70,72 C 85,52 95,28 120,20 C 145,12 165,18 180,30 L 180,100 C 160,100 145,108 130,128 C 115,148 105,172 80,180 C 55,188 35,182 20,170 Z"
            fill="url(#bhGrad)"
            opacity="0.04"
          />
          {/* Animated dot on the curve */}
          <circle r="3" fill="#06B6D4" opacity="0.9" filter="url(#bhGlow)">
            <animateMotion
              dur="4s"
              repeatCount="indefinite"
              path="M 20,100 C 40,100 55,92 70,72 C 85,52 95,28 120,20 C 145,12 165,18 180,30 L 180,100 C 160,100 145,108 130,128 C 115,148 105,172 80,180 C 55,188 35,182 20,170 Z"
            />
          </circle>
        </svg>

        {/* Corner label */}
        <div className="absolute bottom-4 left-4 flex items-center gap-2">
          <span className="h-1.5 w-1.5 rounded-full bg-accent-cyan animate-pulse" />
          <span className="text-[10px] font-medium tracking-wider text-accent-cyan/70 uppercase">
            Hysteresis Scope
          </span>
        </div>
      </div>
    </div>
  );
}

function SectionHysteriCore() {
  return (
    <section className="mx-auto max-w-7xl px-6" style={{ paddingTop: '10rem', paddingBottom: '10rem' }}>
      <div className="grid grid-cols-1 items-center gap-20 lg:grid-cols-2 lg:gap-32">
        {/* Text -- LEFT */}
        <div className="reveal-left">
          <p className="text-xs font-semibold uppercase tracking-[0.25em] text-accent-cyan mb-6">
            Core Technology
          </p>
          <h2 className="text-3xl font-bold text-text-primary lg:text-5xl leading-tight">
            HysteriCore Engine
          </h2>
          <p className="mt-8 max-w-lg text-base leading-[1.8] text-text-muted">
            Not a waveshaper. Not an impulse response. TWISTERION solves the actual
            Jiles-Atherton ferromagnetic equations at sample rate&nbsp;&mdash; real magnetic
            physics producing real analog behavior.
          </p>
          <BulletList
            accent="cyan"
            items={[
              'Jiles-Atherton hysteresis with Bertotti dynamic losses',
              'CPWL + ADAA antialiasing -- zero oversampling overhead',
              'Under 10% CPU in realtime mode',
            ]}
          />
        </div>

        {/* Visual -- RIGHT */}
        <div className="reveal-right">
          <BHCurveVisual />
        </div>
      </div>
    </section>
  );
}

/* ------------------------------------------------------------------ */
/*  Section 2 -- O.D.T Balanced Preamp                                 */
/* ------------------------------------------------------------------ */
function SignalFlowDiagram() {
  return (
    <div className="relative mx-auto w-full max-w-md">
      <div className="rounded-2xl border border-border-subtle bg-bg-surface p-6 shadow-[0_0_40px_rgba(249,115,22,0.06)]">
        {/* Title */}
        <p className="mb-6 text-center text-[10px] font-semibold uppercase tracking-widest text-text-dim">
          Signal Flow
        </p>

        {/* Flow row */}
        <div className="flex items-center justify-between gap-2">
          {/* T1 Input */}
          <div className="flex flex-col items-center gap-1.5">
            <div className="flex h-14 w-14 items-center justify-center rounded-lg border border-accent-orange/40 bg-accent-orange/10">
              <span className="text-xs font-bold text-accent-orange">T1</span>
            </div>
            <span className="text-[9px] font-medium uppercase tracking-wider text-text-dim">
              Input
            </span>
          </div>

          {/* Arrow */}
          <div className="flex-1 flex items-center">
            <div className="h-px flex-1 bg-gradient-to-r from-accent-orange/60 to-accent-cyan/60" />
            <svg className="h-3 w-3 -ml-px text-accent-cyan/60" viewBox="0 0 12 12" fill="currentColor">
              <path d="M 2 1 L 10 6 L 2 11 Z" />
            </svg>
          </div>

          {/* Heritage / Modern switch */}
          <div className="flex flex-col items-center gap-1.5">
            <div className="relative flex h-14 items-center gap-0 rounded-lg border border-border-accent overflow-hidden">
              <div className="flex h-full w-16 items-center justify-center bg-accent-cyan/10 border-r border-border-accent">
                <span className="text-[10px] font-bold text-accent-cyan">HRT</span>
              </div>
              <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 z-10 flex h-5 w-5 items-center justify-center rounded-full bg-bg-elevated border border-border-accent">
                <svg className="h-2.5 w-2.5 text-text-dim" viewBox="0 0 10 10" fill="none" stroke="currentColor" strokeWidth="1.5">
                  <path d="M 2 5 L 8 5 M 5 2 L 5 8" />
                </svg>
              </div>
              <div className="flex h-full w-16 items-center justify-center bg-accent-violet/10">
                <span className="text-[10px] font-bold text-accent-violet">MOD</span>
              </div>
            </div>
            <span className="text-[9px] font-medium uppercase tracking-wider text-text-dim">
              Topology
            </span>
          </div>

          {/* Arrow */}
          <div className="flex-1 flex items-center">
            <div className="h-px flex-1 bg-gradient-to-r from-accent-violet/60 to-accent-orange/60" />
            <svg className="h-3 w-3 -ml-px text-accent-orange/60" viewBox="0 0 12 12" fill="currentColor">
              <path d="M 2 1 L 10 6 L 2 11 Z" />
            </svg>
          </div>

          {/* T2 Output */}
          <div className="flex flex-col items-center gap-1.5">
            <div className="flex h-14 w-14 items-center justify-center rounded-lg border border-accent-orange/40 bg-accent-orange/10">
              <span className="text-xs font-bold text-accent-orange">T2</span>
            </div>
            <span className="text-[9px] font-medium uppercase tracking-wider text-text-dim">
              Output
            </span>
          </div>
        </div>

        {/* Bottom label */}
        <p className="mt-6 text-center text-[10px] text-text-dim">
          T1 Input &rarr; Heritage / Modern &rarr; T2 Output
        </p>
      </div>
    </div>
  );
}

function SectionODT() {
  return (
    <section className="mx-auto max-w-7xl px-6" style={{ paddingTop: '10rem', paddingBottom: '10rem' }}>
      <div className="grid grid-cols-1 items-center gap-20 lg:grid-cols-2 lg:gap-32">
        {/* Visual -- LEFT */}
        <div className="reveal-left order-2 lg:order-1">
          <SignalFlowDiagram />
        </div>

        {/* Text -- RIGHT */}
        <div className="reveal-right order-1 lg:order-2">
          <p className="text-xs font-semibold uppercase tracking-widest text-accent-orange">
            Signal Architecture
          </p>
          <h2 className="mt-4 text-3xl font-bold text-text-primary lg:text-5xl leading-tight">
            O.D.T Balanced Preamp
          </h2>
          <p className="mt-1 text-lg font-medium text-accent-orange/80">
            Original Dual Topology
          </p>
          <p className="mt-6 max-w-lg text-base leading-[1.8] text-text-muted">
            Two transformer stages. Two amplifier topologies. One seamless signal path.
            The O.D.T routes your signal through a physically modeled input transformer,
            into one of two amplifier circuits, then through an output
            transformer&nbsp;&mdash; all powered by HysteriCore.
          </p>
        </div>
      </div>
    </section>
  );
}

/* ------------------------------------------------------------------ */
/*  Section 3 -- Heritage Mode                                         */
/* ------------------------------------------------------------------ */
function CircuitVisualCyan() {
  return (
    <div className="relative mx-auto w-full max-w-md aspect-[4/3]">
      <div className="absolute inset-0 rounded-2xl bg-bg-surface border border-border-subtle overflow-hidden shadow-[0_0_40px_rgba(6,182,212,0.06)]">
        {/* Grid */}
        <svg className="absolute inset-0 h-full w-full opacity-[0.05]" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
          <defs>
            <pattern id="circGridCyan" width="24" height="24" patternUnits="userSpaceOnUse">
              <circle cx="12" cy="12" r="0.5" fill="#06B6D4" />
            </pattern>
          </defs>
          <rect width="100%" height="100%" fill="url(#circGridCyan)" />
        </svg>

        {/* Circuit schematic */}
        <svg
          viewBox="0 0 300 200"
          className="absolute inset-0 h-full w-full p-6"
          xmlns="http://www.w3.org/2000/svg"
          aria-label="Heritage mode circuit visualization"
        >
          <defs>
            <filter id="cyanGlow">
              <feGaussianBlur stdDeviation="2" result="blur" />
              <feMerge>
                <feMergeNode in="blur" />
                <feMergeNode in="SourceGraphic" />
              </feMerge>
            </filter>
          </defs>

          {/* Signal path */}
          <line x1="20" y1="100" x2="70" y2="100" stroke="#06B6D4" strokeWidth="1.5" filter="url(#cyanGlow)" opacity="0.8" />
          <line x1="110" y1="100" x2="150" y2="100" stroke="#06B6D4" strokeWidth="1.5" filter="url(#cyanGlow)" opacity="0.8" />
          <line x1="190" y1="100" x2="230" y2="100" stroke="#06B6D4" strokeWidth="1.5" filter="url(#cyanGlow)" opacity="0.8" />
          <line x1="270" y1="100" x2="290" y2="100" stroke="#06B6D4" strokeWidth="1.5" filter="url(#cyanGlow)" opacity="0.8" />

          {/* Transistor Q1 -- BC184C */}
          <circle cx="90" cy="100" r="18" fill="none" stroke="#06B6D4" strokeWidth="1" opacity="0.5" />
          <text x="90" y="104" textAnchor="middle" fill="#06B6D4" fontSize="8" fontWeight="600">Q1</text>
          <line x1="90" y1="82" x2="90" y2="60" stroke="#06B6D4" strokeWidth="1" opacity="0.4" />
          <line x1="90" y1="118" x2="90" y2="150" stroke="#06B6D4" strokeWidth="1" opacity="0.4" />

          {/* Transistor Q2 -- BC214C */}
          <circle cx="170" cy="100" r="18" fill="none" stroke="#06B6D4" strokeWidth="1" opacity="0.5" />
          <text x="170" y="104" textAnchor="middle" fill="#06B6D4" fontSize="8" fontWeight="600">Q2</text>
          <line x1="170" y1="82" x2="170" y2="60" stroke="#06B6D4" strokeWidth="1" opacity="0.4" />
          <line x1="170" y1="118" x2="170" y2="150" stroke="#06B6D4" strokeWidth="1" opacity="0.4" />

          {/* Transistor Q3 -- BD139 */}
          <circle cx="250" cy="100" r="18" fill="none" stroke="#06B6D4" strokeWidth="1" opacity="0.5" />
          <text x="250" y="104" textAnchor="middle" fill="#06B6D4" fontSize="8" fontWeight="600">Q3</text>
          <line x1="250" y1="82" x2="250" y2="60" stroke="#06B6D4" strokeWidth="1" opacity="0.4" />
          <line x1="250" y1="118" x2="250" y2="150" stroke="#06B6D4" strokeWidth="1" opacity="0.4" />

          {/* Supply rails */}
          <line x1="30" y1="40" x2="280" y2="40" stroke="#06B6D4" strokeWidth="0.5" opacity="0.2" strokeDasharray="4,4" />
          <line x1="30" y1="160" x2="280" y2="160" stroke="#06B6D4" strokeWidth="0.5" opacity="0.2" strokeDasharray="4,4" />
          <text x="285" y="43" fill="#06B6D4" fontSize="7" opacity="0.4">V+</text>
          <text x="285" y="163" fill="#06B6D4" fontSize="7" opacity="0.4">GND</text>

          {/* Feedback path */}
          <path
            d="M 270,130 L 270,175 L 70,175 L 70,130"
            fill="none"
            stroke="#06B6D4"
            strokeWidth="0.8"
            opacity="0.25"
            strokeDasharray="3,3"
          />
          <text x="170" y="185" textAnchor="middle" fill="#06B6D4" fontSize="6" opacity="0.35">AC-COUPLED NFB</text>

          {/* Input / Output labels */}
          <text x="15" y="96" fill="#06B6D4" fontSize="7" opacity="0.5">IN</text>
          <text x="283" y="96" fill="#06B6D4" fontSize="7" opacity="0.5">OUT</text>
        </svg>

        {/* Label */}
        <div className="absolute bottom-3 left-4 flex items-center gap-2">
          <span className="h-1.5 w-1.5 rounded-full bg-accent-cyan animate-pulse" />
          <span className="text-[10px] font-medium tracking-wider text-accent-cyan/70 uppercase">
            3-Stage Class-A Cascade
          </span>
        </div>
      </div>
    </div>
  );
}

function SectionHeritage() {
  return (
    <section className="mx-auto max-w-7xl px-6" style={{ paddingTop: '10rem', paddingBottom: '10rem' }}>
      <div className="grid grid-cols-1 items-center gap-20 lg:grid-cols-2 lg:gap-32">
        {/* Text -- LEFT */}
        <div className="reveal-left">
          <p className="text-xs font-semibold uppercase tracking-widest text-accent-cyan">
            Amplifier Path A
          </p>
          <h2 className="mt-4 text-3xl font-bold text-text-primary lg:text-5xl leading-tight">
            Heritage Mode
          </h2>
          <p className="mt-6 max-w-lg text-base leading-[1.8] text-text-muted">
            A 3-transistor Class-A cascade delivering the warm, thick character of
            vintage British console preamps. Soft clipping, musical compression, rich
            even harmonics.
          </p>
          <BulletList
            accent="cyan"
            items={[
              'BC184C \u2192 BC214C \u2192 BD139',
              '11-position gain (+10 to +50 dB)',
              'AC-coupled negative feedback',
            ]}
          />
        </div>

        {/* Visual -- RIGHT */}
        <div className="reveal-right">
          <CircuitVisualCyan />
        </div>
      </div>
    </section>
  );
}

/* ------------------------------------------------------------------ */
/*  Section 4 -- Modern Mode                                           */
/* ------------------------------------------------------------------ */
function CircuitVisualViolet() {
  return (
    <div className="relative mx-auto w-full max-w-md aspect-[4/3]">
      <div className="absolute inset-0 rounded-2xl bg-bg-surface border border-border-subtle overflow-hidden shadow-[0_0_40px_rgba(168,85,247,0.06)]">
        {/* Grid */}
        <svg className="absolute inset-0 h-full w-full opacity-[0.05]" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
          <defs>
            <pattern id="circGridViolet" width="24" height="24" patternUnits="userSpaceOnUse">
              <circle cx="12" cy="12" r="0.5" fill="#A855F7" />
            </pattern>
          </defs>
          <rect width="100%" height="100%" fill="url(#circGridViolet)" />
        </svg>

        {/* Circuit */}
        <svg
          viewBox="0 0 300 200"
          className="absolute inset-0 h-full w-full p-6"
          xmlns="http://www.w3.org/2000/svg"
          aria-label="Modern mode circuit visualization"
        >
          <defs>
            <filter id="violetGlow">
              <feGaussianBlur stdDeviation="2" result="blur" />
              <feMerge>
                <feMergeNode in="blur" />
                <feMergeNode in="SourceGraphic" />
              </feMerge>
            </filter>
          </defs>

          {/* Differential pair */}
          <circle cx="80" cy="90" r="14" fill="none" stroke="#A855F7" strokeWidth="1" opacity="0.5" />
          <text x="80" y="93" textAnchor="middle" fill="#A855F7" fontSize="6" fontWeight="600">D+</text>
          <circle cx="80" cy="130" r="14" fill="none" stroke="#A855F7" strokeWidth="1" opacity="0.5" />
          <text x="80" y="133" textAnchor="middle" fill="#A855F7" fontSize="6" fontWeight="600">D-</text>
          {/* Connection lines for diff pair */}
          <line x1="20" y1="90" x2="66" y2="90" stroke="#A855F7" strokeWidth="1.5" filter="url(#violetGlow)" opacity="0.8" />
          <line x1="20" y1="130" x2="66" y2="130" stroke="#A855F7" strokeWidth="1.5" filter="url(#violetGlow)" opacity="0.8" />
          <text x="50" y="115" textAnchor="middle" fill="#A855F7" fontSize="6" opacity="0.4">LM-394</text>

          {/* Cascode */}
          <rect x="120" y="80" width="40" height="50" rx="4" fill="none" stroke="#A855F7" strokeWidth="1" opacity="0.4" />
          <text x="140" y="108" textAnchor="middle" fill="#A855F7" fontSize="7" fontWeight="600">CAS</text>
          <line x1="94" y1="100" x2="120" y2="100" stroke="#A855F7" strokeWidth="1.5" filter="url(#violetGlow)" opacity="0.8" />

          {/* VAS */}
          <rect x="180" y="80" width="35" height="50" rx="4" fill="none" stroke="#A855F7" strokeWidth="1" opacity="0.4" />
          <text x="197" y="108" textAnchor="middle" fill="#A855F7" fontSize="7" fontWeight="600">VAS</text>
          <line x1="160" y1="105" x2="180" y2="105" stroke="#A855F7" strokeWidth="1.5" filter="url(#violetGlow)" opacity="0.8" />

          {/* Push-pull output */}
          <circle cx="250" cy="80" r="14" fill="none" stroke="#A855F7" strokeWidth="1" opacity="0.5" />
          <text x="250" y="83" textAnchor="middle" fill="#A855F7" fontSize="6" fontWeight="600">NPN</text>
          <circle cx="250" cy="130" r="14" fill="none" stroke="#A855F7" strokeWidth="1" opacity="0.5" />
          <text x="250" y="133" textAnchor="middle" fill="#A855F7" fontSize="6" fontWeight="600">PNP</text>
          <line x1="215" y1="105" x2="236" y2="90" stroke="#A855F7" strokeWidth="1.2" filter="url(#violetGlow)" opacity="0.7" />
          <line x1="215" y1="105" x2="236" y2="120" stroke="#A855F7" strokeWidth="1.2" filter="url(#violetGlow)" opacity="0.7" />
          {/* Output merge */}
          <line x1="264" y1="90" x2="280" y2="105" stroke="#A855F7" strokeWidth="1.2" filter="url(#violetGlow)" opacity="0.7" />
          <line x1="264" y1="120" x2="280" y2="105" stroke="#A855F7" strokeWidth="1.2" filter="url(#violetGlow)" opacity="0.7" />
          <line x1="280" y1="105" x2="295" y2="105" stroke="#A855F7" strokeWidth="1.5" filter="url(#violetGlow)" opacity="0.8" />

          {/* Supply rails */}
          <line x1="30" y1="40" x2="280" y2="40" stroke="#A855F7" strokeWidth="0.5" opacity="0.2" strokeDasharray="4,4" />
          <line x1="30" y1="170" x2="280" y2="170" stroke="#A855F7" strokeWidth="0.5" opacity="0.2" strokeDasharray="4,4" />
          <text x="285" y="43" fill="#A855F7" fontSize="7" opacity="0.4">V+</text>
          <text x="285" y="173" fill="#A855F7" fontSize="7" opacity="0.4">V-</text>

          {/* Labels */}
          <text x="10" y="88" fill="#A855F7" fontSize="7" opacity="0.5">IN+</text>
          <text x="10" y="133" fill="#A855F7" fontSize="7" opacity="0.5">IN-</text>
          <text x="283" y="102" fill="#A855F7" fontSize="7" opacity="0.5">OUT</text>
        </svg>

        {/* Label */}
        <div className="absolute bottom-3 left-4 flex items-center gap-2">
          <span className="h-1.5 w-1.5 rounded-full bg-accent-violet animate-pulse" />
          <span className="text-[10px] font-medium tracking-wider text-accent-violet/70 uppercase">
            8-Transistor Discrete Op-Amp
          </span>
        </div>
      </div>
    </div>
  );
}

function SectionModern() {
  return (
    <section className="mx-auto max-w-7xl px-6" style={{ paddingTop: '10rem', paddingBottom: '10rem' }}>
      <div className="grid grid-cols-1 items-center gap-20 lg:grid-cols-2 lg:gap-32">
        {/* Visual -- LEFT */}
        <div className="reveal-left order-2 lg:order-1">
          <CircuitVisualViolet />
        </div>

        {/* Text -- RIGHT */}
        <div className="reveal-right order-1 lg:order-2">
          <p className="text-xs font-semibold uppercase tracking-widest text-accent-violet">
            Amplifier Path B
          </p>
          <h2 className="mt-4 text-3xl font-bold text-text-primary lg:text-5xl leading-tight">
            Modern Mode
          </h2>
          <p className="mt-6 max-w-lg text-base leading-[1.8] text-text-muted">
            An 8-transistor discrete op-amp architecture. Clinical precision with
            musical transparency. High headroom, fast transients, controlled harmonic
            overtones.
          </p>
          <BulletList
            accent="violet"
            items={[
              'LM-394 differential pair',
              'Cascode + VAS stages',
              'Class-AB push-pull output',
            ]}
          />
        </div>
      </div>
    </section>
  );
}

/* ------------------------------------------------------------------ */
/*  Section 5 -- Real-Time Analysis                                    */
/* ------------------------------------------------------------------ */
function SectionAnalysis() {
  return (
    <section className="mx-auto max-w-7xl px-6" style={{ paddingTop: '10rem', paddingBottom: '10rem' }}>
      {/* Centered text */}
      <div className="reveal mx-auto max-w-2xl text-center">
        <p className="text-xs font-semibold uppercase tracking-widest text-accent-cyan">
          Visual Feedback
        </p>
        <h2 className="mt-4 text-3xl font-bold text-text-primary lg:text-5xl leading-tight">
          Watch the Physics in Real Time
        </h2>
        <p className="mt-6 text-base leading-[1.8] text-text-muted">
          The built-in B-H scope displays the actual hysteresis loop as your signal
          passes through the transformer core. See saturation, see the magnetic
          flux&nbsp;&mdash; in real time.
        </p>
      </div>

      {/* Plugin GUI screenshot */}
      <div className="reveal mt-16">
        <div className="relative mx-auto max-w-5xl overflow-hidden rounded-xl shadow-[0_8px_60px_rgba(0,0,0,0.5),0_0_80px_rgba(6,182,212,0.08)]">
          <Image
            src="/twisterion-gui.png"
            alt="TWISTERION plugin GUI showing the real-time B-H hysteresis scope and controls"
            width={1920}
            height={1080}
            className="w-full h-auto"
            sizes="(max-width: 1024px) 100vw, 80vw"
          />
        </div>
      </div>

      {/* Gradient line */}
      <div className="mt-16">
        <div className="mx-auto h-0.5 w-full max-w-5xl bg-gradient-to-r from-accent-cyan via-accent-violet to-accent-cyan opacity-60" />
      </div>
    </section>
  );
}

/* ------------------------------------------------------------------ */
/*  Main Features component                                            */
/* ------------------------------------------------------------------ */
export default function Features() {
  const ref = useRef<HTMLDivElement>(null);
  useRevealObserver(ref);

  return (
    <div id="features" ref={ref}>
      <SectionHysteriCore />
      <Divider />
      <SectionODT />
      <Divider />
      <SectionHeritage />
      <Divider />
      <SectionModern />
      <Divider />
      <SectionAnalysis />
    </div>
  );
}
