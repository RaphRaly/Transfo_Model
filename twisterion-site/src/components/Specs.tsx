"use client";

import { useEffect, useRef } from "react";

/* ------------------------------------------------------------------ */
/*  Data                                                              */
/* ------------------------------------------------------------------ */

const SPECS: { label: string; value: string }[] = [
  { label: "Realtime CPU",  value: "~9% mono @ 44.1 kHz" },
  { label: "Physical CPU",  value: "~30% mono @ 44.1 kHz" },
  { label: "Latency",       value: "< 1 ms" },
  { label: "Oversampling",  value: "4\u00d7 (Physical mode)" },
  { label: "Formats",       value: "VST3, AU, AAX, Standalone" },
  { label: "Platforms",     value: "Windows, macOS" },
  { label: "Requirements",  value: "C++17 compiler, JUCE 8" },
];

const DAWS = [
  "Ableton Live",
  "Logic Pro",
  "Pro Tools",
  "Cubase",
  "Studio One",
  "FL Studio",
  "Reaper",
  "Bitwig",
  "Luna",
  "Digital Performer",
];

const STATS: { number: string; label: string; color: string }[] = [
  { number: "260+",  label: "Tests Passing",        color: "text-accent-green" },
  { number: "11",    label: "Modeled Transistors",   color: "text-accent-orange" },
  { number: "<10%",  label: "CPU Usage",             color: "text-accent-cyan" },
];

/* ------------------------------------------------------------------ */
/*  Component                                                         */
/* ------------------------------------------------------------------ */

export default function Specs() {
  const sectionRef = useRef<HTMLElement>(null);

  useEffect(() => {
    const el = sectionRef.current;
    if (!el) return;

    const observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            entry.target.classList.add("visible");
          }
        });
      },
      { threshold: 0.1, rootMargin: "0px 0px -60px 0px" }
    );

    const revealEls = el.querySelectorAll(".reveal");
    revealEls.forEach((r) => observer.observe(r));

    return () => observer.disconnect();
  }, []);

  return (
    <section
      id="specs"
      ref={sectionRef}
      className="relative px-6 max-w-7xl mx-auto"
      style={{ paddingTop: '12rem', paddingBottom: '12rem' }}
    >
      {/* ---- Header ---- */}
      <div className="mb-24 reveal">
        <p className="uppercase tracking-widest text-accent-cyan text-sm font-semibold mb-3">
          Specifications
        </p>
        <h2 className="text-4xl font-bold text-text-primary">
          Built for Production
        </h2>
      </div>

      {/* ---- Two-column layout ---- */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-16 mb-24">
        {/* LEFT: Performance table */}
        <div className="reveal rounded-xl overflow-hidden">
          <table className="w-full text-left">
            <thead>
              <tr className="bg-bg-elevated">
                <th className="px-6 py-3.5 text-xs font-semibold uppercase tracking-wider text-text-muted">
                  Spec
                </th>
                <th className="px-6 py-3.5 text-xs font-semibold uppercase tracking-wider text-text-muted">
                  Value
                </th>
              </tr>
            </thead>
            <tbody>
              {SPECS.map((spec, i) => (
                <tr
                  key={spec.label}
                  className={
                    i % 2 === 0 ? "bg-bg-surface" : "bg-bg-primary"
                  }
                >
                  <td className="px-6 py-3.5 text-sm font-medium text-text-primary whitespace-nowrap">
                    {spec.label}
                  </td>
                  <td className="px-6 py-3.5 text-sm text-text-muted">
                    {spec.value}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        {/* RIGHT: Compatible DAWs */}
        <div className="reveal" style={{ transitionDelay: "150ms" }}>
          <h3 className="text-lg font-bold text-text-primary mb-5">
            Compatible DAWs
          </h3>
          <div className="flex flex-wrap gap-3">
            {DAWS.map((daw) => (
              <span
                key={daw}
                className="inline-flex items-center bg-bg-elevated border border-border-subtle rounded-full px-4 py-2 text-sm text-text-muted transition-colors duration-200 hover:text-text-primary hover:border-border-accent"
              >
                {daw}
              </span>
            ))}
          </div>
        </div>
      </div>

      {/* ---- Stats bar ---- */}
      <div className="reveal rounded-2xl bg-bg-surface/50 border border-border-subtle/60 p-10" style={{ transitionDelay: "300ms" }}>
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-8">
          {STATS.map((stat) => (
            <div key={stat.label} className="text-center">
              <p className={`text-4xl font-bold ${stat.color}`}>
                {stat.number}
              </p>
              <p className="text-sm text-text-muted mt-1">{stat.label}</p>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}
